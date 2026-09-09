"""Ports: contratos núcleo↔infraestructura (ADR-009 §1, T-043 §6).

Application -> Port -> Adapter. Añadir una plataforma = nuevo adapter que
implemente OAuthProvider (+ metadata en T-045/46), sin tocar el kernel.
"""
from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass

from backend.kernel import Account, OAuthSession, Provider


@dataclass(frozen=True)
class TokenPair:
    """Par de tokens opaco: solo el adapter y el TokenStore los manipulan."""

    access_token: str
    refresh_token: str
    expires_in: int = 0
    scope: str = ""


class OAuthProvider(ABC):
    """Frontera OAuth por proveedor (exchange/refresh/revoke reales en T-045/46)."""

    provider: Provider

    @abstractmethod
    def authorization_url(self, session: OAuthSession) -> str: ...

    @abstractmethod
    def exchange_code(self, session: OAuthSession, code: str) -> TokenPair: ...

    @abstractmethod
    def refresh(self, account: Account, refresh_token: str) -> TokenPair: ...

    @abstractmethod
    def revoke(self, token: str) -> None: ...


class TokenStore(ABC):
    """Custodia de user-tokens (cifrado en reposo en producción; ver stores.py)."""

    @abstractmethod
    def save(self, account: Account, tokens: TokenPair) -> None: ...

    @abstractmethod
    def load(self, account: Account) -> TokenPair | None: ...

    @abstractmethod
    def delete(self, account: Account) -> None: ...


class SessionStore(ABC):
    """Sesiones plugin↔backend (formato/TTL definitivos en T-044)."""

    @abstractmethod
    def save_session(self, session_id: str, payload: dict) -> None: ...

    @abstractmethod
    def load_session(self, session_id: str) -> dict | None: ...

    @abstractmethod
    def delete_session(self, session_id: str) -> None: ...


class SecretStore(ABC):
    """Credenciales de aplicación (app client_id/secret, signing keys).
    El código de aplicación nunca lee el entorno directamente."""

    @abstractmethod
    def get(self, name: str) -> str | None: ...


class RateLimiter(ABC):
    """Punto de integración anti-abuso (estrategia exacta pendiente, T-044+)."""

    @abstractmethod
    def allow(self, key: str) -> bool:
        """True si la request puede continuar; False si debe responder 429."""
