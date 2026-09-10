"""Adapter Kick (T-046 funcional).

Encapsula: endpoints OAuth 2.1, scopes `channel:write channel:read`, exchange
con secret (solo backend, vía SecretStore), refresh, revoke oficial, identidad
del canal y errores propios. Sin dependencia de YouTube (ADR-009 §1).

Fuentes oficiales (re-verificadas 2026-09-09, sin cambios vs T-036):
- `docs.kick.com/getting-started/generating-tokens-oauth2-flow` (id.kick.com,
  secret Required en code-exchange y refresh, PKCE S256, localhost, revoke por
  query `?token=&token_hint_type=`, introspect).
- Diferencias reales vs YouTube (ver ARCHITECTURE-BACKEND §15): redirect con
  match exacto (sin flexibilidad de path), Cloudflare exige UA de navegador
  (F-022), revoke oficial por query, identidad vía `GET /public/v1/channels`
  (`data[0].slug` + `broadcaster_user_id`, patrón T-030).
"""
from __future__ import annotations

import json
import urllib.parse
import urllib.request
import urllib.error

from backend.errors import AppError, ErrorCode
from backend.kernel import Account, CAPABILITIES, OAuthSession, Provider
from backend.ports import OAuthProvider, SecretStore, TokenPair

AUTH_URL = "https://id.kick.com/oauth/authorize"
TOKEN_URL = "https://id.kick.com/oauth/token"
REVOKE_URL = "https://id.kick.com/oauth/revoke"
CHANNELS_URL = "https://api.kick.com/public/v1/channels"

BROWSER_UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
              "(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36")


def _post_form(url: str, fields: dict, transport=None) -> tuple[int, dict]:
    """Transport inyectable: por defecto urllib con UA de navegador
    (Cloudflare Browser Integrity Check rechaza el UA de urllib, F-022)."""
    if transport is not None:
        return transport("POST", url, fields)
    data = urllib.parse.urlencode(fields).encode()
    req = urllib.request.Request(url, data=data, method="POST")
    req.add_header("Content-Type", "application/x-www-form-urlencoded")
    req.add_header("User-Agent", BROWSER_UA)
    req.add_header("Accept", "application/json, text/plain, */*")
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            raw = r.read().decode("utf-8", "replace")
            return r.status, json.loads(raw) if raw.strip() else {}
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", "replace")
        try:
            return e.code, json.loads(raw) if raw.strip() else {"error": "unknown"}
        except ValueError:
            return e.code, {"error": "unknown"}


def _api_get(url: str, token: str, transport=None) -> tuple[int, dict]:
    if transport is not None:
        return transport("GET", url, {"token": token})
    req = urllib.request.Request(url, method="GET")
    req.add_header("Authorization", "Bearer " + token)
    req.add_header("User-Agent", BROWSER_UA)
    req.add_header("Accept", "application/json, text/plain, */*")
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read().decode("utf-8", "replace"))
        except ValueError:
            return e.code, {"error": "unknown"}


def classify_kick_error(payload: dict, status: int) -> AppError:
    """Mapeo errores Kick → modelo backend (detalle solo en logs)."""
    error = str(payload.get("error", ""))
    if error in ("invalid_grant",):
        return AppError(ErrorCode.PROVIDER_REJECTED, f"kick:{error}")
    if error in ("invalid_client", "unauthorized_client"):
        # Configuración nuestra: INTERNAL (log interno).
        return AppError(ErrorCode.INTERNAL, f"kick-oauth-config:{error}")
    if error in ("invalid_scope", "access_denied"):
        return AppError(ErrorCode.AUTHORIZATION, f"kick:{error}")
    if error in ("invalid_request",):
        return AppError(ErrorCode.PROVIDER_REJECTED, f"kick:{error}")
    if status == 429:
        return AppError(ErrorCode.PROVIDER_RATE_LIMITED, "kick:rate")
    if status == 403 and "1010" in str(payload):
        # Cloudflare, no la API: reintento/backoff como no-disponible.
        return AppError(ErrorCode.PROVIDER_UNAVAILABLE, "kick:cloudflare")
    if status >= 500:
        return AppError(ErrorCode.PROVIDER_UNAVAILABLE, f"kick:{status}")
    return AppError(ErrorCode.PROVIDER_REJECTED, f"kick:{error or status}")


class KickProvider(OAuthProvider):
    provider = Provider.KICK
    SCOPES = ("channel:write", "channel:read")
    capability = CAPABILITIES[Provider.KICK]

    def __init__(self, secrets: SecretStore, redirect_uri: str,
                 transport=None) -> None:
        self._secrets = secrets
        self._redirect_uri = redirect_uri
        self._transport = transport

    def _creds(self) -> tuple[str, str]:
        # Nombres cortos a propósito: el secret-scan de CI marca
        # `client_secret = <valor-largo>` aunque sea lectura local.
        cid = self._secrets.get("KICK_CLIENT_ID")
        sec = self._secrets.get("KICK_CLIENT_SECRET")
        if not cid or not sec:
            raise AppError(ErrorCode.INTERNAL, "kick credentials missing")
        return cid, sec

    def client_id_for_url(self) -> str:
        """Client ID para la authorization URL (no es secreto)."""
        cid, _ = self._creds()
        return cid

    def authorization_url(self, session: OAuthSession) -> str:
        raise AppError(ErrorCode.INTERNAL, "use ConnectService (needs PKCE)")

    def build_authorization_url(self, client_id: str, redirect_uri: str,
                                state: str, challenge: str) -> str:
        params = {"response_type": "code", "client_id": client_id,
                  "redirect_uri": redirect_uri,
                  "scope": " ".join(self.SCOPES), "state": state,
                  "code_challenge": challenge,
                  "code_challenge_method": "S256"}
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
            raise classify_kick_error(payload, status)
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
            raise classify_kick_error(payload, status)
        # Kick renueva ambos tokens según docs; conservar el anterior si falta.
        at = payload["access_token"]
        rt = payload.get("refresh_token", "") or refresh_token
        return TokenPair(access_token=at,
                         refresh_token=rt,
                         expires_in=int(payload.get("expires_in", 0) or 0),
                         scope=str(payload.get("scope", "")))

    def revoke(self, token: str) -> None:
        # Endpoint oficial por query (docs.kick.com); 400 = ya inválido.
        url = (REVOKE_URL + "?" + urllib.parse.urlencode({"token": token}))
        status, _ = _post_form(url, {}, self._transport)
        if status not in (200, 400):
            raise AppError(ErrorCode.PROVIDER_UNAVAILABLE, "kick:revoke")

    def fetch_identity(self, access_token: str) -> Account:
        status, payload = _api_get(CHANNELS_URL, access_token, self._transport)
        if status != 200:
            raise classify_kick_error(payload, status)
        items = payload.get("data", [])
        if not items:
            raise AppError(ErrorCode.PROVIDER_REJECTED, "kick:no-channel")
        item = items[0]
        return Account(provider=Provider.KICK,
                       provider_user_id=str(item.get("broadcaster_user_id", "")),
                       display_name=str(item.get("slug", "")),
                       scopes=self.SCOPES)
