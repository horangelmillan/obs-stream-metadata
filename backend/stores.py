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
from backend.users import User, UserExistsError


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
        # Fase 1 (spec §3): propietario Managed del token (el token
        # pertenece al usuario, no a la instalación). Instalaciones
        # pre-adopción: sin entrada (pendiente de adopción).
        self._owners: dict[tuple[str, str], str] = {}

    def _key(self, account: Account) -> tuple[str, str]:
        return (account.provider.value, account.provider_user_id)

    def save(self, account: Account, tokens: TokenPair) -> None:
        self._data[self._key(account)] = tokens

    def load(self, account: Account) -> TokenPair | None:
        return self._data.get(self._key(account))

    def delete(self, account: Account) -> None:
        self._data.pop(self._key(account), None)
        self._owners.pop(self._key(account), None)

    def set_owner(self, provider: str, provider_user_id: str,
                  owner_user_id: str) -> bool:
        """Asigna `owner_user_id` si existe la fila (adopción)."""
        if (provider, provider_user_id) not in self._data:
            return False
        self._owners[(provider, provider_user_id)] = owner_user_id
        return True

    def owner_of(self, provider: str,
                 provider_user_id: str) -> str | None:
        return self._owners.get((provider, provider_user_id))


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

    def delete_for_user(self, user_id: str) -> int:
        """Borra las sesiones marcadas con `user_id` (Fase 5a)."""
        if not user_id:
            return 0
        doomed = [sid for sid, payload in self._data.items()
                  if payload.get("user_id") == user_id]
        for sid in doomed:
            del self._data[sid]
        return len(doomed)

    def delete_for_installation(self, installation_id: str) -> int:
        """Borra las sesiones de una instalación (F-C2). Devuelve el conteo."""
        doomed = [sid for sid, payload in self._data.items()
                  if payload.get("installation_id") == installation_id]
        for sid in doomed:
            del self._data[sid]
        return len(doomed)

    def purge_expired(self, now: float) -> dict:
        """Borra sesiones expiradas o revocadas (F-C4). Conteos disjuntos."""
        expired = revoked = 0
        for sid in [sid for sid, payload in self._data.items()
                    if payload.get("expires_at", 0) <= now
                    or payload.get("revoked")]:
            payload = self._data.pop(sid)
            if payload.get("expires_at", 0) <= now:
                expired += 1
            else:
                revoked += 1
        return {"expired": expired, "revoked": revoked}

    def set_user_id_for_installation(self, installation_id: str,
                                     user_id: str) -> int:
        """Adopción Fase 1 (spec §3): marca las sesiones de la instalación."""
        count = 0
        for payload in self._data.values():
            if payload.get("installation_id") == installation_id:
                payload["user_id"] = user_id
                count += 1
        return count


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
        # Fase 1 (spec §3): propietario Managed (adopción sin reset).
        # `create` no lo toca: re-bootstrap conserva al dueño.
        self._owners: dict[str, str] = {}

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

    def delete(self, installation_id: str) -> None:
        """Borrado total de la fila (F-C2). Idempotente."""
        self._data.pop(installation_id, None)
        self._owners.pop(installation_id, None)

    def set_user_id(self, installation_id: str, user_id: str) -> bool:
        """Adopción Fase 1 (spec §3). False si no existe (sin efectos)."""
        if installation_id not in self._data:
            return False
        self._owners[installation_id] = user_id
        return True

    def get_user_id(self, installation_id: str) -> str | None:
        return self._owners.get(installation_id)

    def list_for_user(self, user_id: str) -> list:
        """Instalaciones adoptadas por el usuario (Fase 5a). Ordenadas."""
        if not user_id:
            return []
        return sorted(iid for iid, owner in self._owners.items()
                      if owner == user_id and iid in self._data)


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

    def delete_for_installation(self, installation_id: str) -> int:
        """Borra transacciones pendientes de una instalación (F-C2)."""
        doomed = [tid for tid, entry in self._data.items()
                  if entry.get("installation_id") == installation_id]
        for tid in doomed:
            del self._data[tid]
        return len(doomed)

    def purge_expired(self, now: float) -> dict:
        """Borra transacciones expiradas o consumidas (F-C4). Disjuntos."""
        expired = consumed = 0
        for tid in [tid for tid, entry in self._data.items()
                    if entry.get("expires_at", 0) <= now
                    or entry.get("consumed")]:
            entry = self._data.pop(tid)
            if entry.get("expires_at", 0) <= now:
                expired += 1
            else:
                consumed += 1
        return {"expired": expired, "consumed": consumed}


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

    def list_referencing(self, provider: str, provider_user_id: str) -> list:
        """Instalaciones cuya conexión apunta a esta cuenta (F-C2, guarded-delete)."""
        return sorted(iid for (iid, prov), entry in self._data.items()
                      if prov == provider and entry.get("account", {}).get(
                          "provider_user_id") == provider_user_id)

    def providers_for_installation(self, installation_id: str) -> list:
        """Proveedores con conexión en la instalación (limpieza por
        usuario [D] 2026-10-26). Ordenados."""
        return sorted(provider for (iid, provider) in self._data
                      if iid == installation_id)

    def list_for_user(self, user_id: str,
                      installation_ids=()) -> list:
        """Parejas `(installation_id, provider)` del usuario (Fase 5a):
        entrada marcada con su `user_id`, o sin marca en una instalación
        suya (backfill). Ordenadas."""
        if not user_id:
            return []
        owned = set(installation_ids or ())
        return sorted(
            (iid, provider)
            for (iid, provider), entry in self._data.items()
            if entry.get("user_id") == user_id
            or (entry.get("user_id") is None and iid in owned))

    def set_user_id_for_installation(
            self, installation_id: str,
            user_id: str) -> list[tuple[str, str]]:
        """Adopción Fase 1 (spec §3): marca conexiones y devuelve las
        parejas `(provider, provider_user_id)` para asignar `owner_user_id`
        en tokens."""
        pairs = []
        for (iid, provider), entry in self._data.items():
            if iid != installation_id:
                continue
            entry["user_id"] = user_id
            provider_user_id = entry.get("account", {}).get(
                "provider_user_id")
            if provider_user_id:
                pairs.append((provider, provider_user_id))
        return sorted(pairs)


class InMemoryUserStore:
    """Development/tests: cuentas Managed en memoria (Fase 1, spec §3)."""

    DEVELOPMENT_ONLY = True

    def __init__(self) -> None:
        self._by_id: dict[str, User] = {}
        self._by_username: dict[str, User] = {}

    def create(self, user: User) -> None:
        if user.username in self._by_username:
            raise UserExistsError("username already exists")
        if user.id in self._by_id:
            raise UserExistsError("user id already exists")
        self._by_id[user.id] = user
        self._by_username[user.username] = user

    def load_by_id(self, user_id: str) -> User | None:
        return self._by_id.get(user_id)

    def load_by_username(self, username: str) -> User | None:
        return self._by_username.get(username)

    def delete(self, user_id: str) -> bool:
        """Borrado de la fila (Fase 5a). Idempotente: True si existía."""
        found = self._by_id.pop(user_id, None)
        if found is None:
            return False
        self._by_username.pop(found.username, None)
        return True


# --- Redacción para logs/respuestas (guardrail, no única defensa) ---
# Fase 1 (spec §3): `password_hash` va explícito en la alternancia porque
# el patrón `password\s*=` no casa con `password_hash=` (el hash PBKDF2
# jamás aparece en logs).
_SECRET_KEYS = ("secret", "token", "code", "verifier", "authorization",
                "cookie", "set-cookie", "api_key", "apikey", "password",
                "user_session")
_SECRET_VALUE_RE = re.compile(
    r"(secret|token|verifier|user_session|password_hash|password"
    r"|authorization|cookie|api_key)\s*=\s*\S+",
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
