-- 001_init.sql — esquema inicial portable (T-055, ADR-014).
--
-- PostgreSQL estándar, sin extensiones ni tipos propietarios: TEXT,
-- INTEGER, BIGINT con PK/NOT NULL/CHECK explícitos. Los dicts de la app
-- (sessions, transactions, connections) viajan como JSON en TEXT y se
-- serializan en la capa de persistencia, nunca aquí.
-- Idempotente: todo CREATE TABLE es IF NOT EXISTS.

CREATE TABLE IF NOT EXISTS installations (
    id TEXT PRIMARY KEY CHECK (length(id) > 0),
    created_at TEXT NOT NULL DEFAULT '',
    revoked INTEGER NOT NULL DEFAULT 0 CHECK (revoked IN (0, 1)),
    secret TEXT NOT NULL CHECK (length(secret) > 0)
);

CREATE TABLE IF NOT EXISTS sessions (
    id TEXT PRIMARY KEY CHECK (length(id) > 0),
    payload_json TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS transactions (
    id TEXT PRIMARY KEY CHECK (length(id) > 0),
    entry_json TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS connections (
    installation_id TEXT NOT NULL CHECK (length(installation_id) > 0),
    provider TEXT NOT NULL CHECK (length(provider) > 0),
    entry_json TEXT NOT NULL,
    PRIMARY KEY (installation_id, provider)
);
CREATE INDEX IF NOT EXISTS idx_connections_installation
    ON connections (installation_id);

CREATE TABLE IF NOT EXISTS tokens (
    provider TEXT NOT NULL CHECK (length(provider) > 0),
    provider_user_id TEXT NOT NULL CHECK (length(provider_user_id) > 0),
    access_token TEXT NOT NULL,
    refresh_token TEXT NOT NULL,
    expires_in BIGINT NOT NULL DEFAULT 0 CHECK (expires_in >= 0),
    scope TEXT NOT NULL DEFAULT '',
    PRIMARY KEY (provider, provider_user_id)
);
