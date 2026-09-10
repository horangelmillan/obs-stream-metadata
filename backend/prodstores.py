"""Stores productivos (T-054): SQLite stdlib + secretos por fichero.

Qué garantiza este módulo (y qué NO):
- Persistencia real: installations/sessions/transactions/connections/tokens
  sobreviven al reinicio (los InMemory* de stores.py no).
- Protección en reposo a nivel plataforma: fichero SQLite y directorio de
  secretos con permisos estrictos (0600, rechazo fail-fast si el fichero
  ya existe con permisos amplios en POSIX). El cifrado gestionado del
  disco es dependencia externa documentada (ver docs/DEPLOYMENT.md y
  ADR-013): este módulo NO implementa criptografía propia.
- Secretos fuera del entorno del proceso: `FileSecretStore` lee un fichero
  por secreto (convención de mounts de secretos: Docker/K8s/systemd
  `LoadCredential`). Nada de dev.env, nada hardcodeado.
- Sin marca DEVELOPMENT_ONLY: los gates T-053 aceptan estos stores en
  `production` (y siguen funcionando en `development`).

Nombres de secreto idénticos a EnvSecretStore (GOOGLE_CLIENT_ID, ...):
sin renombres, sin migración de variables.
"""
from __future__ import annotations

import json
import os
import sqlite3
import stat
import threading

from backend.kernel import Account, Provider
from backend.ports import (Installation, InstallationStore, SecretStore, SessionStore,
                           TokenPair, TokenStore)

SCHEMA = """
CREATE TABLE IF NOT EXISTS installations (
    id TEXT PRIMARY KEY,
    created_at TEXT NOT NULL DEFAULT '',
    revoked INTEGER NOT NULL DEFAULT 0,
    secret TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS sessions (
    id TEXT PRIMARY KEY,
    payload_json TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS transactions (
    id TEXT PRIMARY KEY,
    entry_json TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS connections (
    installation_id TEXT NOT NULL,
    provider TEXT NOT NULL,
    entry_json TEXT NOT NULL,
    PRIMARY KEY (installation_id, provider)
);
CREATE TABLE IF NOT EXISTS tokens (
    provider TEXT NOT NULL,
    provider_user_id TEXT NOT NULL,
    access_token TEXT NOT NULL,
    refresh_token TEXT NOT NULL,
    expires_in INTEGER NOT NULL DEFAULT 0,
    scope TEXT NOT NULL DEFAULT '',
    PRIMARY KEY (provider, provider_user_id)
);
"""


class ProdstoresError(Exception):
    """Fallo fail-fast de almacenamiento productivo (permisos, IO, schema)."""


def ensure_private_file(path: str) -> None:
    """Exige fichero privado (0600). Crea padres si faltan.

    POSIX: si el fichero existe con permisos de grupo/otros → error
    (negarse a custodiar secretos/tokens sobre un fichero legible por
    terceros). No-POSIX: best-effort (las ACL son del operador/SO).
    """
    parent = os.path.dirname(os.path.abspath(path))
    os.makedirs(parent, exist_ok=True)
    if os.name != "posix":
        return
    try:
        mode = stat.S_IMODE(os.stat(path).st_mode)
    except FileNotFoundError:
        return
    if mode & 0o077:
        raise ProdstoresError(
            f"refusing to use world/group-readable store file: {path} "
            f"(mode {oct(mode)}; expected 0o600)")


def _connect(path: str) -> sqlite3.Connection:
    ensure_private_file(path)
    # 0600 desde la creación (O_CREAT exclusivo del modo).
    fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o600)
    os.close(fd)
    if os.name == "posix":
        os.chmod(path, 0o600)
    conn = sqlite3.connect(path, check_same_thread=False)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.executescript(SCHEMA)
    conn.commit()
    return conn


class SqliteStore:
    """Base: conexión SQLite compartida + lock (ThreadingHTTPServer).

    T-055: DEV/TEST únicamente. SQLite nunca es producción (contenedor
    stateless sin filesystem persistente): estas clases llevan
    DEVELOPMENT_ONLY y los gates T-053 las rechazan con env=production.
    """

    DEVELOPMENT_ONLY = True

    def __init__(self, path: str) -> None:
        self._path = path
        self._lock = threading.Lock()
        self._conn = _connect(path)

    @property
    def path(self) -> str:
        return self._path

    def close(self) -> None:
        """Cierra la conexión (tests/apagado limpio en Windows)."""
        with self._lock:
            self._conn.close()


class SqliteInstallationStore(SqliteStore, InstallationStore):
    def create(self, installation: Installation, secret: str) -> None:
        with self._lock:
            self._conn.execute(
                "INSERT OR REPLACE INTO installations "
                "(id, created_at, revoked, secret) VALUES (?,?,?,?)",
                (installation.id, installation.created_at,
                 int(installation.revoked), secret))
            self._conn.commit()

    def load(self, installation_id: str) -> tuple[Installation, str] | None:
        with self._lock:
            row = self._conn.execute(
                "SELECT id, created_at, revoked, secret FROM installations "
                "WHERE id=?", (installation_id,)).fetchone()
        if row is None:
            return None
        return (Installation(id=row[0], created_at=row[1],
                             revoked=bool(row[2])), row[3])

    def revoke(self, installation_id: str) -> None:
        with self._lock:
            self._conn.execute(
                "UPDATE installations SET revoked=1 WHERE id=?",
                (installation_id,))
            self._conn.commit()


class SqliteSessionStore(SqliteStore, SessionStore):
    def save_session(self, session_id: str, payload: dict) -> None:
        with self._lock:
            self._conn.execute(
                "INSERT OR REPLACE INTO sessions (id, payload_json) "
                "VALUES (?,?)", (session_id, json.dumps(payload)))
            self._conn.commit()

    def load_session(self, session_id: str) -> dict | None:
        with self._lock:
            row = self._conn.execute(
                "SELECT payload_json FROM sessions WHERE id=?",
                (session_id,)).fetchone()
        return json.loads(row[0]) if row else None

    def delete_session(self, session_id: str) -> None:
        with self._lock:
            self._conn.execute("DELETE FROM sessions WHERE id=?",
                               (session_id,))
            self._conn.commit()


class SqliteOAuthTransactionStore(SqliteStore):
    """Mismo port que InMemoryOAuthTransactionStore (T-045 §8)."""

    def save(self, transaction: dict) -> None:
        with self._lock:
            self._conn.execute(
                "INSERT OR REPLACE INTO transactions (id, entry_json) "
                "VALUES (?,?)", (transaction["id"], json.dumps(transaction)))
            self._conn.commit()

    def load(self, transaction_id: str) -> dict | None:
        with self._lock:
            row = self._conn.execute(
                "SELECT entry_json FROM transactions WHERE id=?",
                (transaction_id,)).fetchone()
        return json.loads(row[0]) if row else None

    def _write(self, entry: dict) -> None:
        with self._lock:
            self._conn.execute(
                "UPDATE transactions SET entry_json=? WHERE id=?",
                (json.dumps(entry), entry["id"]))
            self._conn.commit()

    def consume(self, transaction_id: str) -> dict | None:
        import time as _time
        found = self.load(transaction_id)
        if found is None or found.get("consumed"):
            return None
        if _time.time() >= found.get("expires_at", 0):
            with self._lock:
                self._conn.execute("DELETE FROM transactions WHERE id=?",
                                   (transaction_id,))
                self._conn.commit()
            return None
        found["consumed"] = True
        self._write(found)
        return dict(found)

    def find_by_state(self, state: str) -> dict | None:
        import time as _time
        if not state:
            return None
        now = _time.time()
        with self._lock:
            rows = self._conn.execute(
                "SELECT entry_json FROM transactions").fetchall()
        for (blob,) in rows:
            entry = json.loads(blob)
            if (entry.get("state") == state and not entry.get("consumed")
                    and now < entry.get("expires_at", 0)):
                return entry
        return None


class SqliteConnectionStore(SqliteStore):
    def save(self, installation_id: str, provider: str, entry: dict) -> None:
        with self._lock:
            self._conn.execute(
                "INSERT OR REPLACE INTO connections "
                "(installation_id, provider, entry_json) VALUES (?,?,?)",
                (installation_id, provider, json.dumps(entry)))
            self._conn.commit()

    def load(self, installation_id: str, provider: str) -> dict | None:
        with self._lock:
            row = self._conn.execute(
                "SELECT entry_json FROM connections "
                "WHERE installation_id=? AND provider=?",
                (installation_id, provider)).fetchone()
        return json.loads(row[0]) if row else None

    def delete(self, installation_id: str, provider: str) -> None:
        with self._lock:
            self._conn.execute(
                "DELETE FROM connections "
                "WHERE installation_id=? AND provider=?",
                (installation_id, provider))
            self._conn.commit()


def _pair_to_row(account: Account, tokens: TokenPair) -> tuple:
    return (account.provider.value, account.provider_user_id,
            tokens.access_token, tokens.refresh_token,
            tokens.expires_in, tokens.scope)


class SqliteTokenStore(SqliteStore, TokenStore):
    def save(self, account: Account, tokens: TokenPair) -> None:
        with self._lock:
            self._conn.execute(
                "INSERT OR REPLACE INTO tokens (provider, provider_user_id,"
                " access_token, refresh_token, expires_in, scope) "
                "VALUES (?,?,?,?,?,?)", _pair_to_row(account, tokens))
            self._conn.commit()

    def load(self, account: Account) -> TokenPair | None:
        with self._lock:
            row = self._conn.execute(
                "SELECT access_token, refresh_token, expires_in, scope "
                "FROM tokens WHERE provider=? AND provider_user_id=?",
                (account.provider.value, account.provider_user_id)).fetchone()
        if row is None:
            return None
        return TokenPair(access_token=row[0], refresh_token=row[1],
                         expires_in=row[2], scope=row[3])

    def delete(self, account: Account) -> None:
        with self._lock:
            self._conn.execute(
                "DELETE FROM tokens WHERE provider=? AND provider_user_id=?",
                (account.provider.value, account.provider_user_id))
            self._conn.commit()


class FileSecretStore(SecretStore):
    """Productivo: un fichero por secreto (nombre = nombre del secreto).

    Compatible con mounts de secretos (Docker/K8s/systemd LoadCredential,
    Cloud Run secret volumes) y con despliegues que escriben ficheros
    0600. Fail-fast si el directorio no existe; fichero ausente = secreto
    ausente (None), como EnvSecretStore. Sin DEVELOPMENT_ONLY: apto para
    production.
    """

    def __init__(self, directory: str) -> None:
        if not directory or not os.path.isdir(directory):
            raise ProdstoresError(
                f"secret directory missing: {directory!r}")
        self._directory = directory

    def get(self, name: str) -> str | None:
        if not name or "/" in name or "\\" in name or name.startswith("."):
            return None
        path = os.path.join(self._directory, name)
        try:
            with open(path, "r", encoding="utf-8") as handle:
                value = handle.read().strip()
        except OSError:
            return None
        return value or None


def split_secret_dirs(raw: str) -> list[str]:
    """Parte DIRS por os.pathsep, limpia y descarta vacíos (orden estable)."""
    return [part.strip() for part in (raw or "").split(os.pathsep)
            if part.strip()]


class CompositeSecretStore(SecretStore):
    """T-058: agrega N FileSecretStore en orden determinista.

    Motivo: Cloud Run permite un secreto por directorio; producción
    consume N mounts sin cambiar el contrato file-based. `get()` devuelve
    el primer valor existente según el orden dado; ausente en todos =
    None (semántica SecretStore). Vacío = fail-fast (misconfiguración).
    Sin DEVELOPMENT_ONLY: apto para production. Jamás loguea valores
    (esta clase no loguea nada).
    """

    def __init__(self, stores) -> None:
        stores = tuple(stores)
        if not stores:
            raise ProdstoresError("composite secret store needs ≥1 store")
        self._stores = stores

    def get(self, name: str) -> str | None:
        for store in self._stores:
            value = store.get(name)
            if value:
                return value
        return None
