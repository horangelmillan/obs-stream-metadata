"""Adapter Kick (estructura T-043; funcional en T-046).

Reglas ya demostradas (F-017/F-022/F-023): secret siempre en backend,
redirect exacto, UA de navegador ante Cloudflare, 204 = éxito, read-back
solo fiable en directo.
"""
from __future__ import annotations

from backend.errors import AppError, ErrorCode
from backend.kernel import CAPABILITIES, Account, OAuthSession, Provider
from backend.ports import OAuthProvider, TokenPair


class KickProvider(OAuthProvider):
    provider = Provider.KICK
    SCOPES = ("channel:write", "channel:read")
    capability = CAPABILITIES[Provider.KICK]

    def authorization_url(self, session: OAuthSession) -> str:
        raise AppError(ErrorCode.INTERNAL, "kick adapter not implemented (T-046)")

    def exchange_code(self, session: OAuthSession, code: str) -> TokenPair:
        raise AppError(ErrorCode.INTERNAL, "kick adapter not implemented (T-046)")

    def refresh(self, account: Account, refresh_token: str) -> TokenPair:
        raise AppError(ErrorCode.INTERNAL, "kick adapter not implemented (T-046)")

    def revoke(self, token: str) -> None:
        raise AppError(ErrorCode.INTERNAL, "kick adapter not implemented (T-046)")
