"""Adapter YouTube (estructura T-043; funcional en T-045).

Reglas del proveedor ya demostradas (F-016/F-020/F-021) para no perderlas:
GET previo + PUT {id, snippet} con part=snippet; listar mine=true y filtrar
en local; título 1–100, descripción ≤5000.
"""
from __future__ import annotations

from backend.errors import AppError, ErrorCode
from backend.kernel import CAPABILITIES, OAuthSession, Provider
from backend.ports import OAuthProvider, TokenPair
from backend.kernel import Account


class YouTubeProvider(OAuthProvider):
    provider = Provider.YOUTUBE
    SCOPES = ("https://www.googleapis.com/auth/youtube.force-ssl",)
    capability = CAPABILITIES[Provider.YOUTUBE]

    def authorization_url(self, session: OAuthSession) -> str:
        raise AppError(ErrorCode.INTERNAL, "youtube adapter not implemented (T-045)")

    def exchange_code(self, session: OAuthSession, code: str) -> TokenPair:
        raise AppError(ErrorCode.INTERNAL, "youtube adapter not implemented (T-045)")

    def refresh(self, account: Account, refresh_token: str) -> TokenPair:
        raise AppError(ErrorCode.INTERNAL, "youtube adapter not implemented (T-045)")

    def revoke(self, token: str) -> None:
        raise AppError(ErrorCode.INTERNAL, "youtube adapter not implemented (T-045)")
