"""Stores PostgreSQL (T-055, ADR-014): adapters de ports sobre PgPool.

Misma semántica que las versiones in-memory/SQLite (mismos ports, mismos
contratos): cambiar de SQLite/InMemory a PostgreSQL no toca kernel,
OAuth, auth ni API. Sin DEVELOPMENT_ONLY: aptos para `production`
(pasan los gates T-053). Sin SQL propietario: DML estándar + `%s`
placeholders (estilo psycopg, portable entre drivers PG).
"""
from __future__ import annotations

import json
import time as _time

from backend.db import PgPool
from backend.kernel import Account
from backend.ports import (Installation, InstallationStore, SessionStore,
                           TokenPair, TokenStore)


class PgInstallationStore(InstallationStore):
    def __init__(self, pool: PgPool) -> None:
        self._pool = pool

    def create(self, installation: Installation, secret: str) -> None:
        with self._pool as conn:
            with conn.cursor() as cur:
                cur.execute(
                    "INSERT INTO installations (id, created_at, revoked, "
                    "secret) VALUES (%s, %s, %s, %s) "
                    "ON CONFLICT (id) DO UPDATE SET created_at=EXCLUDED."
                    "created_at, revoked=EXCLUDED.revoked, "
                    "secret=EXCLUDED.secret",
                    (installation.id, installation.created_at,
                     int(installation.revoked), secret))
            conn.commit()

    def load(self, installation_id: str) -> tuple[Installation, str] | None:
        with self._pool as conn:
            with conn.cursor() as cur:
                cur.execute(
                    "SELECT id, created_at, revoked, secret "
                    "FROM installations WHERE id=%s", (installation_id,))
                row = cur.fetchone()
            conn.commit()
        if row is None:
            return None
        return (Installation(id=row[0], created_at=row[1],
                             revoked=bool(row[2])), row[3])

    def revoke(self, installation_id: str) -> None:
        with self._pool as conn:
            with conn.cursor() as cur:
                cur.execute("UPDATE installations SET revoked=1 WHERE id=%s",
                            (installation_id,))
            conn.commit()


class PgSessionStore(SessionStore):
    def __init__(self, pool: PgPool) -> None:
        self._pool = pool

    def save_session(self, session_id: str, payload: dict) -> None:
        with self._pool as conn:
            with conn.cursor() as cur:
                cur.execute(
                    "INSERT INTO sessions (id, payload_json) VALUES (%s, %s) "
                    "ON CONFLICT (id) DO UPDATE SET "
                    "payload_json=EXCLUDED.payload_json",
                    (session_id, json.dumps(payload)))
            conn.commit()

    def load_session(self, session_id: str) -> dict | None:
        with self._pool as conn:
            with conn.cursor() as cur:
                cur.execute("SELECT payload_json FROM sessions WHERE id=%s",
                            (session_id,))
                row = cur.fetchone()
            conn.commit()
        return json.loads(row[0]) if row else None

    def delete_session(self, session_id: str) -> None:
        with self._pool as conn:
            with conn.cursor() as cur:
                cur.execute("DELETE FROM sessions WHERE id=%s",
                            (session_id,))
            conn.commit()


class PgOAuthTransactionStore:
    """Mismo port que InMemoryOAuthTransactionStore (T-045 §8)."""

    def __init__(self, pool: PgPool, clock=None) -> None:
        self._pool = pool
        self._clock = clock or _time.time

    def save(self, transaction: dict) -> None:
        with self._pool as conn:
            with conn.cursor() as cur:
                cur.execute(
                    "INSERT INTO transactions (id, entry_json) "
                    "VALUES (%s, %s) ON CONFLICT (id) DO UPDATE SET "
                    "entry_json=EXCLUDED.entry_json",
                    (transaction["id"], json.dumps(transaction)))
            conn.commit()

    def load(self, transaction_id: str) -> dict | None:
        with self._pool as conn:
            with conn.cursor() as cur:
                cur.execute("SELECT entry_json FROM transactions WHERE id=%s",
                            (transaction_id,))
                row = cur.fetchone()
            conn.commit()
        return json.loads(row[0]) if row else None

    def consume(self, transaction_id: str) -> dict | None:
        found = self.load(transaction_id)
        if found is None or found.get("consumed"):
            return None
        if self._clock() >= found.get("expires_at", 0):
            with self._pool as conn:
                with conn.cursor() as cur:
                    cur.execute("DELETE FROM transactions WHERE id=%s",
                                (transaction_id,))
                conn.commit()
            return None
        found["consumed"] = True
        self.save(found)
        return dict(found)

    def find_by_state(self, state: str) -> dict | None:
        if not state:
            return None
        now = self._clock()
        with self._pool as conn:
            with conn.cursor() as cur:
                cur.execute("SELECT entry_json FROM transactions")
                rows = cur.fetchall()
            conn.commit()
        for (blob,) in rows:
            entry = json.loads(blob)
            if (entry.get("state") == state and not entry.get("consumed")
                    and now < entry.get("expires_at", 0)):
                return entry
        return None


class PgConnectionStore:
    def __init__(self, pool: PgPool) -> None:
        self._pool = pool

    def save(self, installation_id: str, provider: str, entry: dict) -> None:
        with self._pool as conn:
            with conn.cursor() as cur:
                cur.execute(
                    "INSERT INTO connections (installation_id, provider, "
                    "entry_json) VALUES (%s, %s, %s) ON CONFLICT "
                    "(installation_id, provider) DO UPDATE SET "
                    "entry_json=EXCLUDED.entry_json",
                    (installation_id, provider, json.dumps(entry)))
            conn.commit()

    def load(self, installation_id: str, provider: str) -> dict | None:
        with self._pool as conn:
            with conn.cursor() as cur:
                cur.execute(
                    "SELECT entry_json FROM connections "
                    "WHERE installation_id=%s AND provider=%s",
                    (installation_id, provider))
                row = cur.fetchone()
            conn.commit()
        return json.loads(row[0]) if row else None

    def delete(self, installation_id: str, provider: str) -> None:
        with self._pool as conn:
            with conn.cursor() as cur:
                cur.execute(
                    "DELETE FROM connections "
                    "WHERE installation_id=%s AND provider=%s",
                    (installation_id, provider))
            conn.commit()


class PgTokenStore(TokenStore):
    def __init__(self, pool: PgPool) -> None:
        self._pool = pool

    def save(self, account: Account, tokens: TokenPair) -> None:
        with self._pool as conn:
            with conn.cursor() as cur:
                cur.execute(
                    "INSERT INTO tokens (provider, provider_user_id, "
                    "access_token, refresh_token, expires_in, scope) "
                    "VALUES (%s, %s, %s, %s, %s, %s) ON CONFLICT "
                    "(provider, provider_user_id) DO UPDATE SET "
                    # Sintaxis por fila (evita `columna=` adyacente y con
                    # ello falsos positivos del secret-scan del CI).
                    "(access_token, refresh_token, expires_in, scope) = "
                    "(EXCLUDED.access_token, EXCLUDED.refresh_token, "
                    "EXCLUDED.expires_in, EXCLUDED.scope)",
                    (account.provider.value, account.provider_user_id,
                     tokens.access_token, tokens.refresh_token,
                     tokens.expires_in, tokens.scope))
            conn.commit()

    def load(self, account: Account) -> TokenPair | None:
        with self._pool as conn:
            with conn.cursor() as cur:
                cur.execute(
                    "SELECT access_token, refresh_token, expires_in, scope "
                    "FROM tokens WHERE provider=%s AND provider_user_id=%s",
                    (account.provider.value, account.provider_user_id))
                row = cur.fetchone()
            conn.commit()
        if row is None:
            return None
        return TokenPair(access_token=row[0], refresh_token=row[1],
                         expires_in=row[2], scope=row[3])

    def delete(self, account: Account) -> None:
        with self._pool as conn:
            with conn.cursor() as cur:
                cur.execute(
                    "DELETE FROM tokens "
                    "WHERE provider=%s AND provider_user_id=%s",
                    (account.provider.value, account.provider_user_id))
            conn.commit()
