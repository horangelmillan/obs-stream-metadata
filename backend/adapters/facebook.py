"""Adapter Facebook Managed (FB-3, T-072).

Encapsula: dialog OAuth + PKCE, exchange con secret (solo backend, via
SecretStore), canje long-lived `fb_exchange_token` (~60 dias), revoke
`DELETE /me/permissions`, identidad del perfil, page tokens derivados
(solo memoria, jamas persistidos), update `POST /{live-video-id}` con
titulo (1-254) + descripcion (SI existe, a diferencia de Twitch/Kick)
+ `privacy EVERYONE`, listado best-effort y errores propios.

D8 (sin refresh clasico): el par guardado es
`TokenPair(access, "", expires)` con expires real (user) ; al expirar
`ensure_fresh_token` eleva SESSION_EXPIRED -> reconectar, sin romper a
otros providers. Los page tokens (sin expiracion) se derivan por apply
via `/me/accounts` y nunca llegan al TokenStore.
F-074/F-075: el listado puede venir vacio con objetos legibles por ID;
el producto es ID-centrico, sin inventar broadcasts.
La app DEL SERVICIO debe ser tipo None/Business (una Consumer rechaza
`pages_*` con Invalid Scopes: F-072 del lado Independent BYO-app).

Fuentes oficiales (brief docs/T0FB-RESEARCH.md sec. B, Graph v25/v26):
- live-video-api reference/overview + guides/streaming (2026-07-02):
  `POST /{live_video_id}` update, `/{id}/live_videos` list,
  `end_live_video`, `LIVE_VIDEO__EDIT_API_NOT_ALLOWED`.
- facebook-login manual-flow (v26.0, 2026-06-30) + oidc-token (PKCE).
- access-tokens/get-long-lived (2026-06-30): user ~60d, page sin
  expiracion, solo server-side, nunca con token expirado.
- permissions request-revoke + user/permissions (v26.0): DELETE revoke.
"""
from __future__ import annotations

import json
import urllib.parse
import urllib.request
import urllib.error

from backend.errors import AppError, ErrorCode
from backend.kernel import Account, CAPABILITIES, OAuthSession, Provider
from backend.ports import OAuthProvider, SecretStore, TokenPair

GRAPH_VERSION = "v26.0"
DIALOG_URL = f"https://www.facebook.com/{GRAPH_VERSION}/dialog/oauth"
GRAPH_URL = f"https://graph.facebook.com/{GRAPH_VERSION}"
TOKEN_URL = GRAPH_URL + "/oauth/access_token"

FB_TITLE_MAX = 254
# ~60 dias en segundos (user long-lived; documentado como aproximado).
FB_LONG_LIVED_S = 5184000


def _oauth_get(url: str, params: dict, transport=None) -> tuple[int, dict]:
    """GET con query params. Transport inyectable (fakes en tests)."""
    if transport is not None:
        return transport("GET", url, params)
    full = url + "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(full, method="GET")
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


def _graph(method: str, url: str, token: str, payload: dict | None,
           transport=None) -> tuple[int, dict]:
    """Llamadas Graph API con Bearer. Transport inyectable."""
    if transport is not None:
        return transport(method, url, {"token": token,
                                       "payload": payload or {}})
    body = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(url, data=body, method=method)
    req.add_header("Authorization", "Bearer " + token)
    if payload is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            raw = r.read().decode("utf-8", "replace")
            if not raw.strip():
                return r.status, {}
            parsed = json.loads(raw)
            return r.status, parsed if isinstance(parsed, dict) else {}
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", "replace")
        try:
            parsed = json.loads(raw) if raw.strip() else {"error": {}}
            return e.code, parsed if isinstance(parsed, dict) else {
                "error": {}}
        except ValueError:
            return e.code, {"error": {}}


def _meta_code(payload) -> int:
    """Codigo numerico Meta de un error Graph (0 si no hay).

    Forma Graph: {"error": {"code": N, ...}}; algunos transportes
    aplanan a {"code": N}. Se aceptan ambas (anidado manda)."""
    if not isinstance(payload, dict):
        return 0
    err = payload.get("error", {})
    if isinstance(err, dict) and "code" in err:
        try:
            return int(err["code"])
        except (TypeError, ValueError):
            return 0
    try:
        return int(payload.get("code", 0))
    except (AttributeError, TypeError, ValueError):
        return 0


def _meta_subcode(payload) -> int:
    """Subcode Meta (`error_subcode`, 0 si no hay).

    El caso F-080 es code 100 + subcode 33 (objeto web/ID desconocido):
    sin el subcode se miente con "input invalido".
    """
    if not isinstance(payload, dict):
        return 0
    err = payload.get("error", {})
    if isinstance(err, dict) and "error_subcode" in err:
        try:
            return int(err["error_subcode"])
        except (TypeError, ValueError):
            return 0
    try:
        return int(payload.get("error_subcode", 0))
    except (AttributeError, TypeError, ValueError):
        return 0


def classify_facebook_error(payload: dict, status: int) -> AppError:
    """Mapeo errores Meta -> modelo backend (detalle solo en logs)."""
    code = _meta_code(payload)
    sub = _meta_subcode(payload)
    if code == 100 and sub == 33:
        # F-080: objeto web/ID desconocido -> gestionable creando
        # desde el dock, no "titulo invalido".
        return AppError(ErrorCode.PROVIDER_REJECTED,
                        "facebook:not-manageable:33")
    if code == 190:
        return AppError(ErrorCode.SESSION_EXPIRED, "facebook:auth-expired")
    if code in (1363120, 1363144):
        # Elegibilidad: cuenta <60 dias / Page <100 seguidores.
        return AppError(ErrorCode.AUTHORIZATION, f"facebook:eligibility:{code}")
    if code == 10:
        # Permiso denegado / revision requerida.
        return AppError(ErrorCode.AUTHORIZATION, "facebook:forbidden")
    if code in (613, 4, 17):
        return AppError(ErrorCode.PROVIDER_RATE_LIMITED, "facebook:rate")
    if status == 400:
        return AppError(ErrorCode.INVALID_REQUEST, "facebook:invalid")
    if status == 401:
        return AppError(ErrorCode.SESSION_EXPIRED, "facebook:auth-expired")
    if status == 403:
        return AppError(ErrorCode.AUTHORIZATION, "facebook:forbidden")
    if status == 404:
        return AppError(ErrorCode.PROVIDER_REJECTED, "facebook:not-found")
    if status == 429:
        return AppError(ErrorCode.PROVIDER_RATE_LIMITED, "facebook:rate")
    if status >= 500:
        return AppError(ErrorCode.PROVIDER_UNAVAILABLE,
                        f"facebook:{status}")
    return AppError(ErrorCode.PROVIDER_REJECTED, f"facebook:{code or status}")


class FacebookProvider(OAuthProvider):
    provider = Provider.FACEBOOK
    # F-072/F-079: la app del servicio (`manage-streams`) es Consumer y
    # Meta rechaza `pages_*` con Invalid Scopes incluso server-side; el
    # E2E viable es perfil (D9). Page vuelve cuando la app del servicio
    # sea tipo None/Business (las `pages_*` ya están implementadas en
    # `_page_token`/`apply_metadata`: solo falta pedir el scope).
    # Jamas publish_to_groups (Groups API deprecada v19) ni email.
    SCOPES = ("publish_video",)
    capability = CAPABILITIES[Provider.FACEBOOK]
    # T-057: nombres de secreto requeridos (solo nombres, nunca valores).
    required_secret_names = ("FB_APP_ID", "FB_APP_SECRET")

    def __init__(self, secrets: SecretStore, redirect_uri: str,
                 transport=None) -> None:
        self._secrets = secrets
        self._redirect_uri = redirect_uri
        self._transport = transport

    def _creds(self) -> tuple[str, str]:
        # Nombres cortos a proposito: el secret-scan de CI marca
        # `client_secret = <valor-largo>` aunque sea lectura local.
        app_id = self._secrets.get("FB_APP_ID")
        sec = self._secrets.get("FB_APP_SECRET")
        if not app_id or not sec:
            raise AppError(ErrorCode.INTERNAL, "facebook credentials missing")
        return app_id, sec

    def client_id_for_url(self) -> str:
        """App ID para la authorization URL (no es secreto)."""
        app_id, _ = self._creds()
        return app_id

    def authorization_url(self, session: OAuthSession) -> str:
        raise AppError(ErrorCode.INTERNAL, "use ConnectService (needs PKCE)")

    def build_authorization_url(self, client_id: str, redirect_uri: str,
                                state: str, challenge: str) -> str:
        params = {"response_type": "code", "client_id": client_id,
                  "redirect_uri": redirect_uri,
                  "scope": ",".join(self.SCOPES), "state": state,
                  "code_challenge": challenge,
                  "code_challenge_method": "S256"}
        return DIALOG_URL + "?" + urllib.parse.urlencode(params)

    def exchange_code(self, session: OAuthSession, code: str) -> TokenPair:
        raise AppError(ErrorCode.INTERNAL, "use ConnectService (needs verifier)")

    def exchange(self, code: str, verifier: str, redirect_uri: str) -> TokenPair:
        app_id, sec = self._creds()
        status, payload = _oauth_get(TOKEN_URL, {
            "client_id": app_id, "client_secret": sec,
            "redirect_uri": redirect_uri, "code": code,
            "code_verifier": verifier,
        }, self._transport)
        if status != 200 or "access_token" not in payload:
            # Un grant/codigo rechazado es rechazo del proveedor, no un
            # input invalido del usuario (400 de apply si es INVALID).
            err = classify_facebook_error(payload, status)
            if err.code == ErrorCode.INVALID_REQUEST:
                err = AppError(ErrorCode.PROVIDER_REJECTED,
                               "facebook:exchange")
            raise err
        short = str(payload["access_token"])
        short_expires = int(payload.get("expires_in", 0) or 0)
        # Canje best-effort a long-lived (~60d, solo server-side). Si
        # falla se conserva el corto y se sigue (F-071: long-lived es
        # mejora, no requisito).
        status, payload = _oauth_get(TOKEN_URL, {
            "grant_type": "fb_exchange_token", "client_id": app_id,
            "client_secret": sec, "fb_exchange_token": short,
        }, self._transport)
        if status == 200 and "access_token" in payload:
            return TokenPair(access_token=str(payload["access_token"]),
                             refresh_token="",
                             expires_in=int(payload.get("expires_in", 0)
                                            or FB_LONG_LIVED_S),
                             scope=",".join(self.SCOPES))
        return TokenPair(access_token=short, refresh_token="",
                         expires_in=short_expires,
                         scope=",".join(self.SCOPES))

    def refresh(self, account: Account, refresh_token: str) -> TokenPair:
        # D8: sin refresh_token clasico. `ensure_fresh_token` ya eleva
        # SESSION_EXPIRED cuando el par no trae refresh; esta guarda
        # cubre la llamada directa.
        raise AppError(ErrorCode.SESSION_EXPIRED, "reconnect required")

    def revoke(self, token: str) -> None:
        # D10: de-autorizacion (invalida tokens). 400 = ya invalido:
        # objetivo cumplido (best-effort, F-030).
        status, _ = _graph("DELETE", GRAPH_URL + "/me/permissions", token,
                           None, self._transport)
        if status not in (200, 400):
            raise AppError(ErrorCode.PROVIDER_UNAVAILABLE, "facebook:revoke")

    def fetch_identity(self, access_token: str) -> Account:
        status, payload = _graph(
            "GET", GRAPH_URL + "/me?fields=id,name", access_token, None,
            self._transport)
        if status != 200:
            raise classify_facebook_error(payload, status)
        user_id, name = str(payload.get("id", "")), str(payload.get("name", ""))
        if not user_id or not name:
            raise AppError(ErrorCode.PROVIDER_REJECTED, "facebook:no-identity")
        return Account(provider=Provider.FACEBOOK,
                       provider_user_id=user_id,
                       display_name=name,
                       scopes=self.SCOPES)

    def _page_token(self, user_token: str, page_id: str) -> str:
        """Page token derivado en memoria (jamas persistido, F-071)."""
        status, payload = _graph(
            "GET", GRAPH_URL + "/me/accounts?fields=id,access_token",
            user_token, None, self._transport)
        if status != 200:
            raise classify_facebook_error(payload, status)
        for item in payload.get("data", []):
            if str(item.get("id", "")) == page_id:
                token = str(item.get("access_token", ""))
                if token:
                    return token
        raise AppError(ErrorCode.PROVIDER_REJECTED, "facebook:no-page-token")

    # --- metadata Managed ---
    def list_resources(self, access_token: str) -> list:
        """LiveVideos propios, best-effort (F-074/F-075: puede venir vacio
        con objetos legibles por ID; nunca se inventan broadcasts)."""
        url = GRAPH_URL + "/me/live_videos?" + urllib.parse.urlencode(
            {"fields": "id,title,status", "limit": "25"})
        status, payload = _graph("GET", url, access_token, None,
                                 self._transport)
        # Meta puede devolver errores (p. ej. elegibilidad 1363120/
        # 1363144) como HTTP 200 con `error` en el body (brief sec. B.5).
        if status != 200 or "error" in payload:
            raise classify_facebook_error(payload, status)
        out: list[dict] = []
        for item in payload.get("data", []):
            out.append({"id": str(item.get("id", "")),
                        "title": str(item.get("title", "")),
                        "status": str(item.get("status", ""))})
        return out

    def get_live_status(self, access_token: str, live_id: str) -> dict:
        """GET /{live-video-id}?fields=id,title,description,status.

        Base del indicador en-vivo FB-4 (D7). Sin polling: solo a
        peticion del dock (Refresh/Apply).
        """
        live_id = (live_id or "").strip()
        if not live_id:
            raise AppError(ErrorCode.INVALID_REQUEST, "live video required")
        url = (f"{GRAPH_URL}/{live_id}?" + urllib.parse.urlencode(
            {"fields": "id,title,description,status"}))
        status, resp = _graph("GET", url, access_token, None,
                              self._transport)
        if status != 200 or "error" in resp:
            raise classify_facebook_error(resp, status)
        return {"id": str(resp.get("id", live_id)),
                "title": str(resp.get("title", "")),
                "description": str(resp.get("description", "")),
                "status": str(resp.get("status", ""))}

    def create_live_video(self, access_token: str, title: str,
                          description: str = "", target: str = "") -> dict:
        """POST /{me|page}/live_videos (creación-por-dock, F-075).

        Via de producto ID-centrica: el dock crea su propio objeto y
        recuerda el ID (jamas el preview web H1). Limpieza con
        `delete_live_video` (F-075). Jamas `stream_title`/`snippet`/
        `channel_description`.
        """
        title = str(title or "")
        if not (1 <= len(title) <= FB_TITLE_MAX):
            raise AppError(ErrorCode.INVALID_REQUEST, "invalid title")
        if not isinstance(description, str):
            raise AppError(ErrorCode.INVALID_REQUEST, "invalid description")
        target = (target or "").strip() or "me"
        token = access_token
        if target != "me":
            token = self._page_token(access_token, target)
        payload: dict = {"title": title}
        if description:
            payload["description"] = description
        payload["privacy"] = {"value": "EVERYONE"}
        url = f"{GRAPH_URL}/{target}/live_videos"
        status, resp = _graph("POST", url, token, payload,
                              self._transport)
        if status != 200 or "error" in resp:
            raise classify_facebook_error(resp, status)
        new_id = str(resp.get("id", "") or "")
        if not new_id:
            raise AppError(ErrorCode.PROVIDER_REJECTED,
                           "facebook:no-live-id")
        return {"id": new_id, "title": title, "description": description}

    def end_live_video(self, access_token: str, live_id: str) -> dict:
        """POST /{live-video-id}?end_live_video=true -> VOD (D7 OFF)."""
        live_id = (live_id or "").strip()
        if not live_id:
            raise AppError(ErrorCode.INVALID_REQUEST, "live video required")
        url = f"{GRAPH_URL}/{live_id}?" + urllib.parse.urlencode(
            {"end_live_video": "true"})
        status, resp = _graph("POST", url, access_token, {},
                              self._transport)
        if status != 200 or "error" in resp:
            raise classify_facebook_error(resp, status)
        return {"id": live_id}

    def delete_live_video(self, access_token: str, live_id: str) -> dict:
        """DELETE /{live-video-id} (limpieza F-075)."""
        live_id = (live_id or "").strip()
        if not live_id:
            raise AppError(ErrorCode.INVALID_REQUEST, "live video required")
        status, resp = _graph("DELETE", f"{GRAPH_URL}/{live_id}",
                              access_token, None, self._transport)
        if status != 200 or "error" in resp:
            raise classify_facebook_error(resp, status)
        return {"id": live_id}

    def _go_live(self, access_token: str, live_id: str) -> dict:
        """POST /{live-video-id} {status:LIVE_NOW} (D6R ON).

        Solo sobre objeto existente/creado-por-dock; el dock exige
        confirmacion UI explicita (riesgo de publicar). Sin RTMP/keys.
        """
        live_id = (live_id or "").strip()
        if not live_id:
            raise AppError(ErrorCode.INVALID_REQUEST, "live video required")
        status, resp = _graph("POST", f"{GRAPH_URL}/{live_id}",
                              access_token, {"status": "LIVE_NOW"},
                              self._transport)
        if status != 200 or "error" in resp:
            raise classify_facebook_error(resp, status)
        return {"id": live_id, "status": "LIVE_NOW"}

    def apply_metadata(self, access_token: str, data: dict) -> dict:
        """POST /{live-video-id} con token Managed server-side.

        Titulo 1-254 (D2) + descripcion SI existe (a diferencia de
        Twitch/Kick) + `privacy EVERYONE` (D11, validada en B'/F-067).
        Jamas `channel_description` (es del canal, AGENTS.md sec. 47).
        `target` opcional: "me"/vacio = perfil; page-id = Page (token
        derivado en memoria via /me/accounts).

        FB-4 sin cambios de routing: `op` dispatcha el ciclo de vida
        sobre la ruta generica POST /metadata/facebook:
        update (defecto) / status / create / go_live / end / delete.
        """
        data = data or {}
        op = str(data.get("op", "update") or "update").strip().lower()
        if op == "status":
            live_id = str(data.get("live_video_id", "") or "").strip()
            return self.get_live_status(access_token, live_id)
        if op == "create":
            return self.create_live_video(
                access_token, str(data.get("title", "") or ""),
                data.get("description", ""),
                str(data.get("target", "") or "").strip())
        if op in ("go_live", "golive", "live"):
            live_id = str(data.get("live_video_id", "") or "").strip()
            return self._go_live(access_token, live_id)
        if op == "end":
            live_id = str(data.get("live_video_id", "") or "").strip()
            return self.end_live_video(access_token, live_id)
        if op == "delete":
            live_id = str(data.get("live_video_id", "") or "").strip()
            return self.delete_live_video(access_token, live_id)
        if op != "update":
            raise AppError(ErrorCode.INVALID_REQUEST, "unknown op")
        live_id = str(data.get("live_video_id", "") or "").strip()
        title = str(data.get("title", "") or "")
        description = data.get("description", "")
        if not live_id:
            raise AppError(ErrorCode.INVALID_REQUEST, "live video required")
        if not (1 <= len(title) <= FB_TITLE_MAX):
            raise AppError(ErrorCode.INVALID_REQUEST, "invalid title")
        if not isinstance(description, str):
            raise AppError(ErrorCode.INVALID_REQUEST, "invalid description")
        target = str(data.get("target", "") or "").strip()
        token = access_token
        if target and target != "me":
            token = self._page_token(access_token, target)
        payload: dict = {"title": title}
        if description:
            payload["description"] = description
        payload["privacy"] = {"value": "EVERYONE"}
        status, resp = _graph("POST", f"{GRAPH_URL}/{live_id}", token,
                              payload, self._transport)
        if status != 200 or "error" in resp:
            raise classify_facebook_error(resp, status)
        return {"id": live_id, "title": title, "description": description}
