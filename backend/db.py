"""PostgreSQL portable (T-055, ADR-014).

Neon es el proveedor inicial; PostgreSQL es el contrato. Este módulo solo
habla PostgreSQL estándar vía libpq (psycopg): sin SDK de Neon, sin APIs
propietarias, sin nombres de proveedor en configuración o código.

Contiene:
- validación/redacción de DATABASE_URL (genérica, sin `NEON_*`);
- pool acotado y configurable (Cloud Run multi-instancia);
- runner de migrations versionadas (`backend/migrations/NNN_*.sql`).

La URL completa (con password) jamás se loguea: `redacted_url()` la
enmascara y `stores.redact_text` cubre el patrón `://usuario:pass@`.
"""
from __future__ import annotations

import os
import queue
import threading
from urllib.parse import urlparse

import psycopg

VALID_SCHEMES = ("postgresql", "postgres")


class DatabaseError(Exception):
    """Configuración o estado de base de datos inválido (fail-fast)."""


def validate_database_url(url: str | None) -> str:
    """Valida DATABASE_URL o falla rápido. Devuelve la URL intacta.

    Exige esquema postgresql://, host y base de datos. No distingue
    proveedores: Neon, Cloud SQL o local son la misma forma.
    """
    if not url or not str(url).strip():
        raise DatabaseError("database_url missing: set "
                            "STREAM_META_BACKEND_DATABASE_URL")
    value = str(url).strip()
    try:
        parts = urlparse(value)
    except ValueError as exc:
        raise DatabaseError(f"database_url malformed: {exc}") from exc
    if parts.scheme.lower() not in VALID_SCHEMES:
        raise DatabaseError(
            f"database_url scheme must be postgresql:// (got "
            f"{parts.scheme!r})")
    if not parts.hostname:
        raise DatabaseError("database_url without host")
    if not parts.path or parts.path.strip("/") == "":
        raise DatabaseError("database_url without database name")
    return value


def redacted_url(url: str) -> str:
    """`postgresql://usuario:***@host/db` (para logs/errores)."""
    try:
        parts = urlparse(url)
    except ValueError:
        return "postgresql://<malformed>"
    if parts.password:
        netloc = f"{parts.username or ''}:***@{parts.hostname or ''}"
        if parts.port:
            netloc += f":{parts.port}"
        return parts._replace(netloc=netloc).geturl()
    return url


def _pool_opener(url: str, connect_timeout_s: int):
    def _open():
        # psycopg acepta la URL tal cual (parámetros libpq estándar como
        # ?sslmode= pasan intactos → portable entre proveedores).
        return psycopg.connect(url, connect_timeout=connect_timeout_s)
    return _open


class PgPool:
    """Pool acotado de conexiones PostgreSQL (stdlib threading+queue).

    - `max_size` y timeout configurables (Cloud Run: instancias × pool ≤
      max_connections del servidor; ver docs/DEPLOYMENT.md).
    - Las conexiones rotas se descartan (ping con `SELECT 1` al reciclar).
    - `close()` para apagado limpio (Windows/tests).
    """

    def __init__(self, url: str, *, max_size: int = 10,
                 acquire_timeout_s: int = 10,
                 connect_timeout_s: int = 10) -> None:
        if max_size < 1:
            raise DatabaseError("db_pool_max must be >= 1")
        self._url = validate_database_url(url)
        self._max_size = max_size
        self._acquire_timeout_s = acquire_timeout_s
        self._opener = _pool_opener(self._url, connect_timeout_s)
        self._free: queue.Queue = queue.Queue()
        self._created = 0
        self._guard = threading.Lock()

    @property
    def dsn_redacted(self) -> str:
        return redacted_url(self._url)

    def acquire(self):
        try:
            conn = self._free.get_nowait()
        except queue.Empty:
            conn = None
        if conn is not None:
            try:
                conn.execute("SELECT 1")
                return conn
            except Exception:
                try:
                    conn.close()
                except Exception:
                    pass
                conn = None
        with self._guard:
            if conn is None and self._created < self._max_size:
                conn = self._opener()
                self._created += 1
        if conn is not None:
            return conn
        try:
            return self._free.get(timeout=self._acquire_timeout_s)
        except queue.Empty:
            raise DatabaseError(
                f"postgres pool exhausted ({self._max_size} connections)")

    def release(self, conn) -> None:
        try:
            if conn.closed:
                with self._guard:
                    self._created -= 1
                return
            conn.rollback()
            self._free.put_nowait(conn)
        except Exception:
            try:
                conn.close()
            except Exception:
                pass
            with self._guard:
                self._created -= 1

    def __enter__(self):
        conn = self.acquire()
        self._released = conn
        return conn

    def __exit__(self, *exc):
        conn, self._released = self._released, None
        if conn is not None:
            if exc[0] is not None:
                try:
                    conn.rollback()
                except Exception:
                    pass
            self.release(conn)
        return False

    def close(self) -> None:
        while True:
            try:
                conn = self._free.get_nowait()
            except queue.Empty:
                return
            try:
                conn.close()
            except Exception:
                pass


MIGRATIONS_TABLE = """
CREATE TABLE IF NOT EXISTS schema_migrations (
    version TEXT PRIMARY KEY,
    applied_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
"""


def discover_migrations(directory: str) -> list[tuple[str, str]]:
    """`[(version, path)]` ordenadas. `NNN_nombre.sql`, NNN numérico."""
    try:
        names = sorted(os.listdir(directory))
    except OSError as exc:
        raise DatabaseError(
            f"migrations directory unreadable: {directory!r}") from exc
    found = []
    for name in names:
        if not name.endswith(".sql"):
            continue
        stem = name[:-4]
        version, _, _ = stem.partition("_")
        if not version.isdigit() or not stem[len(version):].startswith("_"):
            raise DatabaseError(
                f"bad migration name (expected NNN_name.sql): {name!r}")
        found.append((version, os.path.join(directory, name)))
    versions = [version for version, _ in found]
    if len(set(versions)) != len(versions):
        raise DatabaseError("duplicate migration versions")
    return found


def applied_versions(conn) -> set[str]:
    with conn.cursor() as cur:
        cur.execute(MIGRATIONS_TABLE)
        cur.execute("SELECT version FROM schema_migrations")
        return {row[0] for row in cur.fetchall()}


def run_migrations(pool: PgPool, directory: str) -> list[str]:
    """Aplica pendientes en orden (una transacción por fichero).

    Devuelve las versiones aplicadas en esta ejecución. Idempotente.
    """
    pending = discover_migrations(directory)
    applied: list[str] = []
    with pool as conn:
        done = applied_versions(conn)
        conn.commit()
    for version, path in pending:
        if version in done:
            continue
        with open(path, "r", encoding="utf-8") as handle:
            sql = handle.read()
        if not sql.strip():
            raise DatabaseError(f"empty migration: {path!r}")
        with pool as conn:
            with conn.cursor() as cur:
                cur.execute(sql)
                cur.execute(
                    "INSERT INTO schema_migrations (version) VALUES (%s)",
                    (version,))
            conn.commit()
        applied.append(version)
    return applied
