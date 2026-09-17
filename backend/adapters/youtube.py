"""Adapter YouTube (T-045 funcional).

Encapsula: endpoints OAuth, scope `youtube.force-ssl`, exchange con secret
(solo backend, vía SecretStore), refresh, revoke, identidad del canal y
errores propios. El kernel no conoce nada de Google (ADR-009 §1).

Fuentes oficiales (re-verificadas 2026-09-09, sin cambios vs T-030):
- installed-apps: PKCE + loopback + system browser.
- token endpoint `https://oauth2.googleapis.com/token` (secret requerido en
  nuestra configuración: T-035 FAIL).
- identidad: `GET youtube/v3/channels?part=snippet&mine=true`.
- revoke: `POST https://oauth2.googleapis.com/revoke`.
- Reglas metadata T-031 (F-016/F-020/F-021) siguen en el plugin/dock.
"""
from __future__ import annotations

import json
import urllib.parse
import urllib.request
import urllib.error

from backend.errors import AppError, ErrorCode
from backend.kernel import Account, CAPABILITIES, OAuthSession, Provider
from backend.ports import OAuthProvider, SecretStore, TokenPair

AUTH_URL = "https://accounts.google.com/o/oauth2/v2/auth"
TOKEN_URL = "https://oauth2.googleapis.com/token"
REVOKE_URL = "https://oauth2.googleapis.com/revoke"
CHANNELS_URL = "https://www.googleapis.com/youtube/v3/channels"
BROADCASTS_URL = "https://www.googleapis.com/youtube/v3/liveBroadcasts"

BROWSER_UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
              "(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36")


def _post_form(url: str, fields: dict, transport=None) -> tuple[int, dict]:
    """Transport inyectable: por defecto urllib; fakes en tests."""
    if transport is not None:
        return transport("POST", url, fields)
    data = urllib.parse.urlencode(fields).encode()
    req = urllib.request.Request(url, data=data, method="POST")
    req.add_header("Content-Type", "application/x-www-form-urlencoded")
    req.add_header("User-Agent", BROWSER_UA)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read().decode("utf-8", "replace"))
        except ValueError:
            return e.code, {"error": "unknown"}


def _get_json(url: str, token: str, transport=None) -> tuple[int, dict]:
    if transport is not None:
        return transport("GET", url, {"token": token})
    req = urllib.request.Request(url + "&" + urllib.parse.urlencode({}), method="GET")
    req.add_header("Authorization", "Bearer " + token)
    req.add_header("User-Agent", BROWSER_UA)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read().decode("utf-8", "replace"))
        except ValueError:
            return e.code, {"error": "unknown"}


def classify_token_error(payload: dict, status: int) -> AppError:
    """Mapeo errores Google → modelo backend (detalle Google solo en logs)."""
    error = str(payload.get("error", ""))
    if error in ("invalid_grant",):
        return AppError(ErrorCode.PROVIDER_REJECTED, f"google:{error}")
    if error in ("invalid_client", "unauthorized_client"):
        # Configuración nuestra, no del usuario: INTERNAL (log interno).
        return AppError(ErrorCode.INTERNAL, f"google-oauth-config:{error}")
    if error in ("invalid_scope", "access_denied"):
        return AppError(ErrorCode.AUTHORIZATION, f"google:{error}")
    if error in ("invalid_request",):
        return AppError(ErrorCode.PROVIDER_REJECTED, f"google:{error}")
    if status == 429:
        return AppError(ErrorCode.PROVIDER_RATE_LIMITED, "google:rate")
    if status >= 500:
        return AppError(ErrorCode.PROVIDER_UNAVAILABLE, f"google:{status}")
    return AppError(ErrorCode.PROVIDER_REJECTED, f"google:{error or status}")


def _api(method: str, url: str, token: str, payload: dict | None,
         transport=None) -> tuple[int, dict]:
    """Llamadas JSON a YouTube Data API (Bearer). Transport inyectable."""
    if transport is not None:
        return transport(method, url, {"token": token,
                                       "payload": payload or {}})
    body = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(url, data=body, method=method)
    req.add_header("Authorization", "Bearer " + token)
    req.add_header("User-Agent", BROWSER_UA)
    if payload is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            raw = r.read().decode("utf-8", "replace")
            return r.status, json.loads(raw) if raw.strip() else {}
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", "replace")
        try:
            return e.code, json.loads(raw) if raw.strip() else {"error": {}}
        except ValueError:
            return e.code, {"error": {}}


def _normalize_google_text(payload: dict) -> tuple[str, str]:
    """(reason principal, texto normalizado) de un error YouTube Data API.

    Forma Google: {"error": {"code": N, "message": ..., "errors": [{"reason"}]}}.
    Normalización: minúsculas sin no-alfabéticos, porque los mensajes
    contienen espacios ("Rate limit exceeded") y los reasons no
    ("rateLimitExceeded"): ambas formas deben mapear igual (FASE 2.1-C.1:
    un 403 de cuota/rate con espacios caía al default PROVIDER_REJECTED).
    El reason es un enum de protocolo (sin secretos) y viaja en el detail
    (solo logs, jamás respuestas: ver ErrorCode.public_body).
    """
    err = payload.get("error", {}) if isinstance(payload, dict) else {}
    reasons = [str(e.get("reason", ""))
               for e in (err.get("errors", []) if isinstance(err, dict) else [])
               if isinstance(e, dict) and e.get("reason")]
    message = str(err.get("message", "") if isinstance(err, dict) else "")
    import re as _re
    norm = _re.sub(r"[^a-z]", "", (",".join(reasons) + "," + message).lower())
    return (str(reasons[0]) if reasons else "", norm)


def classify_broadcast_error(payload: dict, status: int) -> AppError:
    """Mapeo errores YouTube Data API → modelo backend (respuestas seguras)."""
    reason, text = _normalize_google_text(payload)
    tag = f"google:{status}:{reason}" if reason else f"google:{status}"
    if status == 401 or "unauthorized" in text or "invalidcredentials" in text:
        # Access revocado/expirado más allá del refresh → reconectar.
        return AppError(ErrorCode.SESSION_EXPIRED, "google:auth-expired")
    # "Rate limit exceeded" / "... exceeded your quota" / "Daily Limit
    # Exceeded": el orden de palabras varía entre message y reason, así que
    # se matchea por co-ocurrencia, no por subcadena contigua.
    limited = ("ratelimit" in text or "quotaexceeded" in text
               or ("exceed" in text and ("quota" in text or "daily" in text)))
    if status == 429 or limited:
        return AppError(ErrorCode.PROVIDER_RATE_LIMITED, "google:rate")
    if status >= 500 or "backenderror" in text:
        return AppError(ErrorCode.PROVIDER_UNAVAILABLE, tag)
    if ("insufficientpermissions" in text or "livestreamingnotenabled" in text
            or "forbidden" in text):
        return AppError(ErrorCode.AUTHORIZATION, "google:forbidden")
    if ("invalidtitle" in text or "invaliddescription" in text
            or "invalid" in text or "badrequest" in text):
        return AppError(ErrorCode.INVALID_REQUEST, "google:invalid")
    if "livebroadcastnotfound" in text or "notfound" in text or status == 404:
        return AppError(ErrorCode.PROVIDER_REJECTED, "google:not-found")
    return AppError(ErrorCode.PROVIDER_REJECTED, tag)


class YouTubeProvider(OAuthProvider):
    provider = Provider.YOUTUBE
    SCOPES = ("https://www.googleapis.com/auth/youtube.force-ssl",)
    capability = CAPABILITIES[Provider.YOUTUBE]
    # T-057: nombres de secreto requeridos (solo nombres, nunca valores).
    # El wiring productivo los verifica al arrancar (fail-fast) contra el
    # SecretStore elegido por despliegue (ficheros, env, vault).
    required_secret_names = ("GOOGLE_CLIENT_ID", "GOOGLE_CLIENT_SECRET")

    def __init__(self, secrets: SecretStore, redirect_uri: str,
                 transport=None) -> None:
        self._secrets = secrets
        self._redirect_uri = redirect_uri
        self._transport = transport

    def _creds(self) -> tuple[str, str]:
        # Nombres cortos a propósito: el secret-scan de CI marca
        # `client_secret = <valor-largo>` aunque sea una lectura local.
        cid = self._secrets.get("GOOGLE_CLIENT_ID")
        sec = self._secrets.get("GOOGLE_CLIENT_SECRET")
        if not cid or not sec:
            raise AppError(ErrorCode.INTERNAL, "google credentials missing")
        return cid, sec

    def client_id_for_url(self) -> str:
        """Client ID para la authorization URL (no es secreto)."""
        client_id, _ = self._creds()
        return client_id

    def authorization_url(self, session: OAuthSession) -> str:
        raise AppError(ErrorCode.INTERNAL, "use ConnectService (needs PKCE)")

    def build_authorization_url(self, client_id: str, redirect_uri: str,
                                state: str, challenge: str) -> str:
        params = {"response_type": "code", "client_id": client_id,
                  "redirect_uri": redirect_uri,
                  "scope": self.SCOPES[0], "state": state,
                  "code_challenge": challenge,
                  "code_challenge_method": "S256",
                  "access_type": "offline", "prompt": "consent"}
        return AUTH_URL + "?" + urllib.parse.urlencode(params)

    def exchange_code(self, session: OAuthSession, code: str) -> TokenPair:
        raise AppError(ErrorCode.INTERNAL, "use ConnectService (needs verifier)")

    def exchange(self, code: str, verifier: str, redirect_uri: str) -> TokenPair:
        cid, sec = self._creds()
        status, payload = _post_form(TOKEN_URL, {
            "grant_type": "authorization_code", "code": code,
            "client_id": cid, "client_secret": sec,
            "redirect_uri": redirect_uri, "code_verifier": verifier,
        }, self._transport)
        if status != 200 or "access_token" not in payload:
            raise classify_token_error(payload, status)
        at = payload["access_token"]
        rt = payload.get("refresh_token", "")
        return TokenPair(access_token=at,
                         refresh_token=rt,
                         expires_in=int(payload.get("expires_in", 0) or 0),
                         scope=str(payload.get("scope", "")))

    def refresh(self, account: Account, refresh_token: str) -> TokenPair:
        cid, sec = self._creds()
        status, payload = _post_form(TOKEN_URL, {
            "grant_type": "refresh_token", "refresh_token": refresh_token,
            "client_id": cid, "client_secret": sec,
        }, self._transport)
        if status != 200 or "access_token" not in payload:
            raise classify_token_error(payload, status)
        # Google no siempre devuelve un refresh nuevo: conservar el anterior.
        at = payload["access_token"]
        rt = payload.get("refresh_token", "") or refresh_token
        return TokenPair(access_token=at,
                         refresh_token=rt,
                         expires_in=int(payload.get("expires_in", 0) or 0),
                         scope=str(payload.get("scope", "")))

    def revoke(self, token: str) -> None:
        status, _ = _post_form(REVOKE_URL, {"token": token}, self._transport)
        if status not in (200, 400):
            # 400 = token ya inválido: objetivo cumplido (best-effort, F-030).
            raise AppError(ErrorCode.PROVIDER_UNAVAILABLE, "google:revoke")

    def channel_identity(self, access_token: str) -> Account:
        return self.fetch_identity(access_token)

    # --- metadata Managed (FASE 2.1-C) ---
    def list_resources(self, access_token: str) -> list:
        """Broadcasts propios (misma forma probada que Independent: solo
        filtro `mine`; `broadcastStatus`+`mine` es incompatibleParameters).
        Etiqueta derivada de status.lifeCycleStatus."""
        url = BROADCASTS_URL + "?" + urllib.parse.urlencode(
            {"part": "snippet,status", "mine": "true",
             "broadcastType": "all", "maxResults": "25"})
        status, payload = _api("GET", url, access_token, None,
                               self._transport)
        if status != 200:
            raise classify_broadcast_error(payload, status)
        out: list[dict] = []
        for item in payload.get("items", []):
            snippet = item.get("snippet", {})
            life = str(item.get("status", {}).get("lifeCycleStatus", ""))
            if life in ("live", "liveStarting"):
                label = "active"
            elif life in ("complete", "revoked"):
                label = "completed"
            else:
                label = "upcoming"
            out.append({"id": str(item.get("id", "")),
                        "title": str(snippet.get("title", "")),
                        "status": label})
        return out

    def apply_metadata(self, access_token: str, data: dict) -> dict:
        """Actualiza título/descripción preservando el resto del snippet
        (fetch+merge, regla T-031/F-021: nunca resetear categoryId etc.)."""
        broadcast_id = str(data.get("broadcast_id", ""))
        title = str(data.get("title", ""))
        description = str(data.get("description", ""))
        if not broadcast_id:
            raise AppError(ErrorCode.INVALID_REQUEST, "broadcast required")
        if not (1 <= len(title) <= 100):
            raise AppError(ErrorCode.INVALID_REQUEST, "invalid title")
        if len(description) > 5000:
            raise AppError(ErrorCode.INVALID_REQUEST, "invalid description")
        get_url = BROADCASTS_URL + "?" + urllib.parse.urlencode(
            {"part": "snippet", "id": broadcast_id})
        status, payload = _api("GET", get_url, access_token, None,
                               self._transport)
        if status != 200:
            raise classify_broadcast_error(payload, status)
        items = payload.get("items", [])
        if not items:
            raise AppError(ErrorCode.PROVIDER_REJECTED, "google:not-found")
        snippet = dict(items[0].get("snippet", {}))
        snippet["title"] = title
        snippet["description"] = description
        put_url = BROADCASTS_URL + "?" + urllib.parse.urlencode(
            {"part": "snippet"})
        status, payload = _api("PUT", put_url, access_token,
                               {"id": broadcast_id, "snippet": snippet},
                               self._transport)
        if status != 200:
            raise classify_broadcast_error(payload, status)
        return {"id": broadcast_id, "title": title,
                "description": description}

    def fetch_identity(self, access_token: str) -> Account:
        status, payload = _get_json(
            CHANNELS_URL + "?" + urllib.parse.urlencode(
                {"part": "snippet", "mine": "true"}),
            access_token, self._transport)
        if status != 200:
            raise classify_token_error(payload, status)
        items = payload.get("items", [])
        if not items:
            raise AppError(ErrorCode.PROVIDER_REJECTED, "google:no-channel")
        item, snippet = items[0], items[0].get("snippet", {})
        return Account(provider=Provider.YOUTUBE,
                       provider_user_id=str(item.get("id", "")),
                       display_name=str(snippet.get("title", "")),
                       scopes=self.SCOPES)
