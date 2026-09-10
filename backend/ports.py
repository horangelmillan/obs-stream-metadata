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

    @abstractmethod
    def fetch_identity(self, access_token: str) -> Account:
        """Identidad mínima para `Connected as` (Account del kernel)."""


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


@dataclass(frozen=True)
class Installation:
    """Identidad de instalación: identificador (no-secreto) + secreto por
    instalación (solo backend y plugin propietario, revocado/aislado)."""

    id: str
    created_at: str = ""  # ISO-8601 UTC
    revoked: bool = False


class InstallationStore(ABC):
    """Registro de instalaciones + secreto por instalación (cifrado en
    producción; memoria dev en stores.py)."""

    @abstractmethod
    def create(self, installation: Installation, secret: str) -> None: ...

    @abstractmethod
    def load(self, installation_id: str) -> tuple[Installation, str] | None:
        """Devuelve (installation, secret) o None. Solo la capa auth la usa."""

    @abstractmethod
    def revoke(self, installation_id: str) -> None: ...


class OAuthTransactionStore(ABC):
    """Transacciones OAuth de un solo uso (T-045 §8).

    Entrada: dict con {id, provider, installation_id, state, code_verifier,
    created_at, expires_at, consumed}. El verifier solo lo lee el adapter
    durante el exchange; jamás sale en respuestas ni logs."""

    @abstractmethod
    def save(self, transaction: dict) -> None: ...

    @abstractmethod
    def load(self, transaction_id: str) -> dict | None: ...

    @abstractmethod
    def consume(self, transaction_id: str) -> dict | None:
        """Marca consumida y devuelve la entrada, o None si no existe,
        expiró o ya fue consumida (anti-replay)."""

    @abstractmethod
    def find_by_state(self, state: str) -> dict | None:
        """Localiza transacción vigente por state (callback). None si no hay
        coincidencia válida (state inválido, expirada o consumida)."""


class ConnectionStore(ABC):
    """Conexiones proveedor por instalación (T-045 §16).

    Entrada: dict con {provider, installation_id, account:{provider_user_id,
    display_name, scopes}, tokens:{access_token, refresh_token, expires_in,
    obtained_at}}. Contenido de metadata (títulos) nunca persistido."""

    @abstractmethod
    def save(self, installation_id: str, provider: str, entry: dict) -> None: ...

    @abstractmethod
    def load(self, installation_id: str, provider: str) -> dict | None: ...

    @abstractmethod
    def delete(self, installation_id: str, provider: str) -> None: ...
