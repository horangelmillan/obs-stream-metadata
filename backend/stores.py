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
from backend.ports import (Installation, InstallationStore, RateLimiter, SecretStore,
                           SessionStore, TokenPair, TokenStore)


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
    """Sin política real. Punto de integración para T-044+ (ver §20 del encargo).

    T-054: prohibido como puerta global en producción (gate en
    environment.assert_production_ready vía ALLOW_ALL).
    """

    ALLOW_ALL = True

    def allow(self, key: str) -> bool:
        return True


class FixedWindowRateLimiter(RateLimiter):
    """Ventana fija en memoria (single-instance). Producción distribuida:
    mismo port contra store compartido (pendiente, ver ADR-010)."""

    def __init__(self, limit: int, window_s: int, clock=None) -> None:
        self._limit = limit
        self._window = window_s
        self._clock = clock or __import__("time").time
        self._hits: dict[str, list[float]] = {}

    def allow(self, key: str) -> bool:
        now = self._clock()
        cutoff = now - self._window
        hits = [t for t in self._hits.get(key, []) if t > cutoff]
        if len(hits) >= self._limit:
            self._hits[key] = hits
            return False
        hits.append(now)
        self._hits[key] = hits
        return True


class InMemoryInstallationStore(InstallationStore):
    """Development/tests: secreto en memoria clara. Producción: cifrado en
    reposo (el secreto solo lo usa la capa auth para verificar HMAC)."""

    DEVELOPMENT_ONLY = True

    def __init__(self) -> None:
        self._data: dict[str, tuple[Installation, str]] = {}

    def create(self, installation: Installation, secret: str) -> None:
        self._data[installation.id] = (installation, secret)

    def load(self, installation_id: str) -> tuple[Installation, str] | None:
        return self._data.get(installation_id)

    def revoke(self, installation_id: str) -> None:
        found = self._data.get(installation_id)
        if found is not None:
            installation, secret = found
            self._data[installation_id] = (
                Installation(id=installation.id,
                             created_at=installation.created_at,
                             revoked=True), secret)


class InMemoryOAuthTransactionStore:
    """Development/tests: verifiers en memoria clara, un solo uso con TTL.
    Producción: mismo port contra store cifrado (pendiente)."""

    DEVELOPMENT_ONLY = True

    def __init__(self, clock=None) -> None:
        import time as _time
        self._clock = clock or _time.time
        self._data: dict[str, dict] = {}

    def save(self, transaction: dict) -> None:
        self._data[transaction["id"]] = dict(transaction)

    def load(self, transaction_id: str) -> dict | None:
        found = self._data.get(transaction_id)
        return dict(found) if found is not None else None

    def consume(self, transaction_id: str) -> dict | None:
        found = self._data.get(transaction_id)
        if found is None or found.get("consumed"):
            return None
        if self._clock() >= found.get("expires_at", 0):
            self._data.pop(transaction_id, None)
            return None
        found["consumed"] = True
        return dict(found)

    def find_by_state(self, state: str) -> dict | None:
        if not state:
            return None
        for entry in self._data.values():
            if (entry.get("state") == state and not entry.get("consumed")
                    and self._clock() < entry.get("expires_at", 0)):
                return dict(entry)
        return None


class InMemoryConnectionStore:
    """Development/tests: tokens en memoria clara. Producción: DB + cifrado
    en reposo (pendiente, ver ARCHITECTURE-BACKEND §7/§10)."""

    DEVELOPMENT_ONLY = True

    def __init__(self) -> None:
        self._data: dict[tuple[str, str], dict] = {}

    def save(self, installation_id: str, provider: str, entry: dict) -> None:
        self._data[(installation_id, provider)] = dict(entry)

    def load(self, installation_id: str, provider: str) -> dict | None:
        found = self._data.get((installation_id, provider))
        return dict(found) if found is not None else None

    def delete(self, installation_id: str, provider: str) -> None:
        self._data.pop((installation_id, provider), None)


# --- Redacción para logs/respuestas (guardrail, no única defensa) ---
_SECRET_KEYS = ("secret", "token", "code", "verifier", "authorization",
                "cookie", "set-cookie", "api_key", "apikey", "password")
_SECRET_VALUE_RE = re.compile(
    r"(secret|token|verifier|password|authorization|cookie|api_key)\s*=\s*\S+",
    re.IGNORECASE)
# T-055: passwords embebidos en URLs (DATABASE_URL `://usuario:pass@host`).
_URL_PASSWORD_RE = re.compile(r"(://[^/:@\s]+:)[^@\s]+(@)")


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
    redacted = _SECRET_VALUE_RE.sub(r"\1=[REDACTED]", text)
    return _URL_PASSWORD_RE.sub(r"\1***\2", redacted)
