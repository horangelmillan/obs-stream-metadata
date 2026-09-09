"""Shared Kernel (ADR-009 §1): conceptos comunes a todos los proveedores.

Regla: el kernel no nombra direcciones, claves ni detalles de proveedor.
Sin ramificaciones por proveedor aquí; ese comportamiento vive en adapters.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum


class Provider(str, Enum):
    TWITCH = "twitch"
    YOUTUBE = "youtube"
    KICK = "kick"


class ConnectionStatus(str, Enum):
    CONNECTED = "connected"
    EXPIRED = "expired"
    REVOKED = "revoked"
    ERROR = "error"


@dataclass(frozen=True)
class ProviderCapability:
    """Capacidades de metadata por plataforma (AGENTS.md §3, F-001)."""

    provider: Provider
    title: bool = True
    stream_description: bool = False


CAPABILITIES: dict[Provider, ProviderCapability] = {
    Provider.TWITCH: ProviderCapability(Provider.TWITCH, True, False),
    Provider.YOUTUBE: ProviderCapability(Provider.YOUTUBE, True, True),
    Provider.KICK: ProviderCapability(Provider.KICK, True, False),
}


@dataclass(frozen=True)
class Account:
    provider: Provider
    provider_user_id: str
    display_name: str
    scopes: tuple[str, ...] = ()
    connected_at: str = ""  # ISO-8601 UTC


@dataclass(frozen=True)
class Connection:
    account: Account
    status: ConnectionStatus = ConnectionStatus.CONNECTED


@dataclass(frozen=True)
class OAuthSession:
    """Sesión efímera de un flujo OAuth en curso (solo existe durante el flujo)."""

    id: str
    provider: Provider
    state: str
    # Referencia opaca al material de intercambio (el valor real solo vive
    # en el adapter durante el intercambio; el kernel nunca lo toca).
    verifier_ref: str = ""
    expires_at: str = ""  # ISO-8601 UTC


@dataclass(frozen=True)
class Session:
    """Sesión plugin↔backend (valores definitivos en T-044; TTL provisional)."""

    id: str
    installation_id: str
    account: Account
    issued_at: str = ""  # ISO-8601 UTC
    expires_at: str = ""  # ISO-8601 UTC


@dataclass(frozen=True)
class MetadataUpdate:
    """Comando de metadata. El contenido (título/descripción) es solo tránsito:
    ningún store del backend debe persistirlo (privacidad, ADR-009 §9)."""

    platforms: tuple[Provider, ...]
    title: str
    description: str = ""
    broadcast_id: str = ""  # YouTube: broadcast seleccionado (F-004)


@dataclass(frozen=True)
class AuthorizationState:
    installation_id: str
    session: Session | None = None
    pending_oauth: OAuthSession | None = None


# Re-exportación de conveniencia: el kernel es el único origen del error model.
from backend.errors import ErrorCode  # noqa: E402  (evita import circular: errors no importa kernel)

__all__ = [
    "Provider",
    "ConnectionStatus",
    "ProviderCapability",
    "CAPABILITIES",
    "Account",
    "Connection",
    "OAuthSession",
    "Session",
    "MetadataUpdate",
    "AuthorizationState",
    "ErrorCode",
]
