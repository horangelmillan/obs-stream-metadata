"""Stores: implementaciones base + utilidades de redacción.

- EnvSecretStore: SOLO desarrollo (variables de entorno del proceso backend).
  Producción usará secret manager/vault (T-043 §10); la app no cambia.
- InMemoryTokenStore / InMemorySessionStore: SOLO desarrollo/tests.
  Producción: DB + cifrado en reposo (pendiente, ver ARCHITECTURE-BACKEND §7/§10).
- AllowAllRateLimiter: punto de integración documentado, sin política real
  (estrategia exacta pendiente, T-044+).
"""
from __future__ import annotations

import os
import re

from backend.kernel import Account
from backend.ports import RateLimiter, SecretStore, SessionStore, TokenPair, TokenStore


class EnvSecretStore(SecretStore):
    """Development-only: lee secretos del entorno del proceso backend."""

    DEVELOPMENT_ONLY = True

    def __init__(self, prefix: str = "STREAM_META_BACKEND_SECRET_",
                 env: dict[str, str] | None = None) -> None:
        self._prefix = prefix
        self._env = env if env is not None else os.environ

    def get(self, name: str) -> str | None:
        return self._env.get(self._prefix + name)


class InMemoryTokenStore(TokenStore):
    """Development/tests: sin cifrado. NO usar en producción."""

    DEVELOPMENT_ONLY = True

    def __init__(self) -> None:
        self._data: dict[tuple[str, str], TokenPair] = {}

    def _key(self, account: Account) -> tuple[str, str]:
        return (account.provider.value, account.provider_user_id)

    def save(self, account: Account, tokens: TokenPair) -> None:
        self._data[self._key(account)] = tokens

    def load(self, account: Account) -> TokenPair | None:
        return self._data.get(self._key(account))

    def delete(self, account: Account) -> None:
        self._data.pop(self._key(account), None)


class InMemorySessionStore(SessionStore):
    """Development/tests. Producción: store persistente + TTL (T-044)."""

    DEVELOPMENT_ONLY = True

    def __init__(self) -> None:
        self._data: dict[str, dict] = {}

    def save_session(self, session_id: str, payload: dict) -> None:
        self._data[session_id] = dict(payload)

    def load_session(self, session_id: str) -> dict | None:
        found = self._data.get(session_id)
        return dict(found) if found is not None else None

    def delete_session(self, session_id: str) -> None:
        self._data.pop(session_id, None)


class AllowAllRateLimiter(RateLimiter):
    """Sin política real. Punto de integración para T-044+ (ver §20 del encargo)."""

    def allow(self, key: str) -> bool:
        return True


# --- Redacción para logs/respuestas (guardrail, no única defensa) ---
_SECRET_KEYS = ("secret", "token", "code", "verifier", "authorization",
                "cookie", "set-cookie", "api_key", "apikey", "password")
_SECRET_VALUE_RE = re.compile(
    r"(secret|token|verifier|password|authorization|cookie|api_key)\s*=\s*\S+",
    re.IGNORECASE)


def redact_mapping(mapping: dict) -> dict:
    """Copia con valores sensibles sustituidos por longitud (nunca el valor)."""
    redacted = {}
    for key, value in mapping.items():
        lowered = str(key).lower()
        if any(s in lowered for s in _SECRET_KEYS) and isinstance(value, str) and value:
            redacted[key] = f"[REDACTED len={len(value)}]"
        else:
            redacted[key] = value
    return redacted


def redact_text(text: str) -> str:
    return _SECRET_VALUE_RE.sub(r"\1=[REDACTED]", text)
