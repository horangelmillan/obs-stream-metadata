-- 003_users.sql — cuentas de usuario Managed, Fase 1 (spec 2026-10-26, Enfoque A).
--
-- PostgreSQL estándar, sin extensiones ni tipos propietarios: TEXT/BIGINT
-- con PK/NOT NULL/UNIQUE/CHECK explícitos. `users` nace aquí (sin backfill:
-- las filas pre-existentes reciben su user_id/owner_user_id por adopción en
-- el momento del login/signup, Fase 2; antes de eso el esquema lo permite
-- como pendiente de adopción con columnas nulables).
-- password_hash: formato `pbkdf2-sha256$iter$salt_b64$hash_b64` (ver
-- `backend/users.py`); jamás en claro ni en logs (hereda RedactingFilter).
-- totp_secret/oauth_provider/oauth_subject: reserva de nombre v1, NULL sin
-- uso (TOTP y OAuth-login fuera de alcance v1, spec §10).
-- Idempotente: CREATE TABLE IF NOT EXISTS + ADD COLUMN IF NOT EXISTS.

CREATE TABLE IF NOT EXISTS users (
    id TEXT PRIMARY KEY CHECK (length(id) > 0),
    username TEXT NOT NULL UNIQUE CHECK (length(username) > 0),
    password_hash TEXT NOT NULL CHECK (length(password_hash) > 0),
    created_at BIGINT NOT NULL DEFAULT 0,
    updated_at BIGINT NOT NULL DEFAULT 0,
    totp_secret TEXT NULL,
    oauth_provider TEXT NULL,
    oauth_subject TEXT NULL
);

ALTER TABLE installations ADD COLUMN IF NOT EXISTS user_id TEXT NULL;
ALTER TABLE sessions ADD COLUMN IF NOT EXISTS user_id TEXT NULL;
ALTER TABLE connections ADD COLUMN IF NOT EXISTS user_id TEXT NULL;
ALTER TABLE tokens ADD COLUMN IF NOT EXISTS owner_user_id TEXT NULL;
