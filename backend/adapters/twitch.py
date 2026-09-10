"""Adapter Twitch (reservado).

Twitch continúa directo desde el plugin vía Device Flow público (F-015/F-033):
este módulo existe para que una futura paridad backend no rediseñe el kernel.
Sin implementación en T-043 (ver T-047).
"""
from __future__ import annotations

from backend.errors import AppError, ErrorCode
from backend.kernel import CAPABILITIES, Account, OAuthSession, Provider
from backend.ports import OAuthProvider, TokenPair


class TwitchProvider(OAuthProvider):
    provider = Provider.TWITCH
    SCOPES = ("channel:manage:broadcast",)
    capability = CAPABILITIES[Provider.TWITCH]
    DIRECT_FROM_PLUGIN = True

    def authorization_url(self, session: OAuthSession) -> str:
        raise AppError(ErrorCode.INTERNAL, "twitch stays direct from plugin (T-047)")

    def exchange_code(self, session: OAuthSession, code: str) -> TokenPair:
        raise AppError(ErrorCode.INTERNAL, "twitch stays direct from plugin (T-047)")

    def refresh(self, account: Account, refresh_token: str) -> TokenPair:
        raise AppError(ErrorCode.INTERNAL, "twitch stays direct from plugin (T-047)")

    def revoke(self, token: str) -> None:
        raise AppError(ErrorCode.INTERNAL, "twitch stays direct from plugin (T-047)")

    def fetch_identity(self, access_token: str) -> Account:
        raise AppError(ErrorCode.INTERNAL, "twitch stays direct from plugin (T-047)")
