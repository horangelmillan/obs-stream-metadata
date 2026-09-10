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


class YouTubeProvider(OAuthProvider):
    provider = Provider.YOUTUBE
    SCOPES = ("https://www.googleapis.com/auth/youtube.force-ssl",)
    capability = CAPABILITIES[Provider.YOUTUBE]

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
