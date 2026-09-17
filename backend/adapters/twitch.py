"""Adapter Twitch Managed (FASE 2: Authorization Code Grant server-side).

Encapsula: authorize `id.twitch.tv/oauth2/authorize` (response_type=code,
sin PKCE: el flujo code de Twitch no documenta code_challenge y los
parámetros no documentados se omiten a propósito), exchange/refresh con
secret (solo backend, vía SecretStore), revoke oficial, identidad vía
`GET /oauth2/validate` (wire format ya validado en el plugin Independent,
F-015: solo Bearer, sin header Client-Id) y errores propios.

Fuentes oficiales (re-verificadas 2026-09-14):
- getting-tokens-oauth: auth-code flow para apps con servidor + secret,
  devuelve access_token + refresh_token.
- refresh-tokens: grant_type=refresh_token con client_id + client_secret.
- validate-tokens: `GET https://id.twitch.tv/oauth2/validate`.
- revoke-tokens: `POST https://id.twitch.tv/oauth2/revoke`
  con client_id + token (igual que el revoke Independent del plugin).

El modo Independent (Device Flow, DIRECT_FROM_PLUGIN) queda intacto: este
adapter solo sirve al flujo Managed vía ConnectService.
"""
from __future__ import annotations

import json
import urllib.parse
import urllib.request
import urllib.error

from backend.errors import AppError, ErrorCode
from backend.kernel import Account, CAPABILITIES, OAuthSession, Provider
from backend.ports import OAuthProvider, SecretStore, TokenPair

AUTH_URL = "https://id.twitch.tv/oauth2/authorize"
TOKEN_URL = "https://id.twitch.tv/oauth2/token"
REVOKE_URL = "https://id.twitch.tv/oauth2/revoke"
VALIDATE_URL = "https://id.twitch.tv/oauth2/validate"
HELIX_CHANNELS_URL = "https://api.twitch.tv/helix/channels"


def _post_form(url: str, fields: dict, transport=None) -> tuple[int, dict]:
    """Transport inyectable: por defecto urllib; fakes en tests."""
    if transport is not None:
        return transport("POST", url, fields)
    data = urllib.parse.urlencode(fields).encode()
    req = urllib.request.Request(url, data=data, method="POST")
    req.add_header("Content-Type", "application/x-www-form-urlencoded")
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            raw = r.read().decode("utf-8", "replace")
            return r.status, json.loads(raw) if raw.strip() else {}
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", "replace")
        try:
            return e.code, json.loads(raw) if raw.strip() else {"message": "unknown"}
        except ValueError:
            return e.code, {"message": "unknown"}


def _get_json(url: str, token: str, transport=None) -> tuple[int, dict]:
    if transport is not None:
        return transport("GET", url, {"token": token})
    req = urllib.request.Request(url, method="GET")
    req.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read().decode("utf-8", "replace"))
        except ValueError:
            return e.code, {"message": "unknown"}


def _patch_json(url: str, token: str, client_id: str, payload: dict,
                transport=None) -> tuple[int, dict]:
    """PATCH Helix (Bearer + Client-Id + JSON). Transport inyectable."""
    if transport is not None:
        return transport("PATCH", url, {"token": token, "client_id": client_id,
                                        "payload": payload})
    data = json.dumps(payload).encode()
    req = urllib.request.Request(url, data=data, method="PATCH")
    req.add_header("Authorization", "Bearer " + token)
    req.add_header("Client-Id", client_id)
    req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            raw = r.read().decode("utf-8", "replace")
            return r.status, json.loads(raw) if raw.strip() else {}
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", "replace")
        try:
            return e.code, json.loads(raw) if raw.strip() else {"message": "unknown"}
        except ValueError:
            return e.code, {"message": "unknown"}


def classify_twitch_error(payload: dict, status: int) -> AppError:
    """Mapeo errores Twitch → modelo backend (detalle solo en logs).

    El token endpoint responde `{"status":400,"message":"..."}`.
    """
    message = str(payload.get("message", payload.get("error", ""))).lower()
    if "client" in message:
        # client_id/secret: siempre configuración nuestra (INTERNAL).
        return AppError(ErrorCode.INTERNAL, "twitch-oauth-config")
    if "access_denied" in message or "denied" in message:
        return AppError(ErrorCode.AUTHORIZATION, f"twitch:{message[:48]}")
    if ("grant" in message or "refresh" in message or "code" in message
            or "token" in message):
        return AppError(ErrorCode.PROVIDER_REJECTED, f"twitch:{message[:48]}")
    if status == 429:
        return AppError(ErrorCode.PROVIDER_RATE_LIMITED, "twitch:rate")
    if status >= 500:
        return AppError(ErrorCode.PROVIDER_UNAVAILABLE, f"twitch:{status}")
    return AppError(ErrorCode.PROVIDER_REJECTED, f"twitch:{message[:48] or status}")


def _scope_text(scope) -> str:
    # Twitch devuelve `scope` como LISTA en el exchange; TokenPair.scope
    # es str en el resto de adapters: normalizar sin perder información.
    if isinstance(scope, list):
        return ",".join(str(s) for s in scope)
    return str(scope or "")


class TwitchProvider(OAuthProvider):
    provider = Provider.TWITCH
    SCOPES = ("channel:manage:broadcast",)
    capability = CAPABILITIES[Provider.TWITCH]
    # Independent sigue directo desde el plugin (Device Flow, T-047).
    DIRECT_FROM_PLUGIN = True
    # FASE 2: nombres de secreto requeridos (solo nombres, nunca valores).
    required_secret_names = ("TWITCH_CLIENT_ID", "TWITCH_CLIENT_SECRET")

    def __init__(self, secrets: SecretStore | None = None,
                 redirect_uri: str = "", transport=None) -> None:
        self._secrets = secrets
        self._redirect_uri = redirect_uri
        self._transport = transport

    def _creds(self) -> tuple[str, str]:
        if self._secrets is None:
            raise AppError(ErrorCode.INTERNAL, "twitch credentials missing")
        cid = self._secrets.get("TWITCH_CLIENT_ID")
        sec = self._secrets.get("TWITCH_CLIENT_SECRET")
        if not cid or not sec:
            raise AppError(ErrorCode.INTERNAL, "twitch credentials missing")
        return cid, sec

    def client_id_for_url(self) -> str:
        """Client ID para la authorization URL (no es secreto)."""
        client_id, _ = self._creds()
        return client_id

    def authorization_url(self, session: OAuthSession) -> str:
        raise AppError(ErrorCode.INTERNAL, "twitch stays direct from plugin (T-047)")

    def build_authorization_url(self, client_id: str, redirect_uri: str,
                                state: str, challenge: str) -> str:
        # Sin PKCE: el authorization code grant de Twitch no documenta
        # code_challenge(_method); `challenge` se ignora a propósito para
        # no enviar parámetros no documentados. ConnectService genera el
        # verifier igualmente (nunca sale del backend).
        params = {"response_type": "code", "client_id": client_id,
                  "redirect_uri": redirect_uri,
                  "scope": self.SCOPES[0], "state": state}
        return AUTH_URL + "?" + urllib.parse.urlencode(params)

    def exchange_code(self, session: OAuthSession, code: str) -> TokenPair:
        raise AppError(ErrorCode.INTERNAL, "twitch stays direct from plugin (T-047)")

    def exchange(self, code: str, verifier: str, redirect_uri: str) -> TokenPair:
        cid, sec = self._creds()
        status, payload = _post_form(TOKEN_URL, {
            "grant_type": "authorization_code", "code": code,
            "client_id": cid, "client_secret": sec,
            "redirect_uri": redirect_uri,
        }, self._transport)
        if status != 200 or "access_token" not in payload:
            raise classify_twitch_error(payload, status)
        return TokenPair(access_token=payload["access_token"],
                         refresh_token=str(payload.get("refresh_token", "") or ""),
                         expires_in=int(payload.get("expires_in", 0) or 0),
                         scope=_scope_text(payload.get("scope", "")))

    def refresh(self, account: Account, refresh_token: str) -> TokenPair:
        cid, sec = self._creds()
        status, payload = _post_form(TOKEN_URL, {
            "grant_type": "refresh_token", "refresh_token": refresh_token,
            "client_id": cid, "client_secret": sec,
        }, self._transport)
        if status != 200 or "access_token" not in payload:
            raise classify_twitch_error(payload, status)
        # Conservar el anterior si el endpoint no devuelve uno nuevo.
        rt = str(payload.get("refresh_token", "") or "") or refresh_token
        return TokenPair(access_token=payload["access_token"],
                         refresh_token=rt,
                         expires_in=int(payload.get("expires_in", 0) or 0),
                         scope=_scope_text(payload.get("scope", "")))

    def revoke(self, token: str) -> None:
        # Endpoint oficial por query + client_id (igual que Independent).
        cid, _ = self._creds()
        url = (REVOKE_URL + "?" + urllib.parse.urlencode(
            {"client_id": cid, "token": token}))
        status, _ = _post_form(url, {}, self._transport)
        if status not in (200, 400):
            # 400 = token ya inválido: objetivo cumplido (best-effort).
            raise AppError(ErrorCode.PROVIDER_UNAVAILABLE, "twitch:revoke")

    def fetch_identity(self, access_token: str) -> Account:
        status, payload = _get_json(VALIDATE_URL, access_token, self._transport)
        if status != 200:
            raise classify_twitch_error(payload, status)
        user_id, login = str(payload.get("user_id", "")), str(payload.get("login", ""))
        if not user_id or not login:
            raise AppError(ErrorCode.PROVIDER_REJECTED, "twitch:no-identity")
        return Account(provider=Provider.TWITCH,
                       provider_user_id=user_id,
                       display_name=login,
                       scopes=self.SCOPES)

    # --- metadata Managed (Apply Twitch desde Cloud Run) ---
    def apply_metadata(self, access_token: str, data: dict) -> dict:
        """PATCH title vía Helix con token Managed server-side.

        Twitch no tiene descripción de stream equivalente (AGENTS.md §2):
        `description` se acepta y se ignora. Éxito = 204 sin body.
        `broadcaster_id` lo envía el plugin (es su propia identidad,
        visible en "Connected as"; Twitch lo valida contra el token).
        """
        title = str(data.get("title", ""))
        broadcaster_id = str(data.get("broadcaster_id", ""))
        if not broadcaster_id:
            raise AppError(ErrorCode.INVALID_REQUEST, "broadcaster required")
        if not (1 <= len(title) <= 140):
            raise AppError(ErrorCode.INVALID_REQUEST, "invalid title")
        cid, _ = self._creds()
        url = HELIX_CHANNELS_URL + "?" + urllib.parse.urlencode(
            {"broadcaster_id": broadcaster_id})
        status, payload = _patch_json(url, access_token, cid,
                                      {"title": title}, self._transport)
        if status in (200, 204):
            return {"broadcaster_id": broadcaster_id, "title": title}
        if status == 400:
            raise AppError(ErrorCode.INVALID_REQUEST, "twitch:invalid")
        if status == 401:
            raise AppError(ErrorCode.SESSION_EXPIRED, "twitch:auth-expired")
        if status == 403:
            raise AppError(ErrorCode.AUTHORIZATION, "twitch:forbidden")
        raise classify_twitch_error(payload, status)
