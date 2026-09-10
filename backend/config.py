"""Configuración: no-sensible desde entorno; secretos solo vía SecretStore.

Ningún secreto por defecto. `dev.env` del repo NUNCA se lee aquí (es material
del operador para pruebas del plugin, no del backend).
"""
from __future__ import annotations

import os
from dataclasses import dataclass

from backend.environment import PRODUCTION
from backend.environment import normalize as normalize_env


@dataclass(frozen=True)
class Settings:
    host: str = "127.0.0.1"
    port: int = 8080
    env: str = "development"  # development | production
    log_level: str = "INFO"
    public_base_url: str = "http://127.0.0.1:8080"
    request_timeout_s: int = 15
    body_limit_bytes: int = 65536
    # T-054: despliegue productivo. Vacíos = no configurados (dev no los
    # necesita; production los exige vía main()). Rutas a ficheros, nunca
    # secretos inline.
    data_dir: str = ""
    secret_dir: str = ""
    tls_certfile: str = ""
    tls_keyfile: str = ""
    global_limit: int = 600
    global_window_s: int = 60
    # T-055: PostgreSQL portable. `database_url` es LA DATABASE_URL
    # (genérica, sin nombre de proveedor: ni NEON_* ni CLOUD_SQL_*).
    # Vacía = sin PostgreSQL (dev usa in-memory; production la exige).
    database_url: str = ""
    db_pool_max: int = 10
    db_pool_timeout_s: int = 10


def load_settings(env: dict[str, str] | None = None) -> Settings:
    src = env if env is not None else os.environ
    # T-055: Cloud Run inyecta PORT; STREAM_META_BACKEND_PORT manda si existe.
    port = int(src.get("STREAM_META_BACKEND_PORT",
                       src.get("PORT", "8080")))
    # T-053: entorno explícito; valor desconocido = fail-fast aquí,
    # ya no decorativo. Ausente = development (dirección segura).
    env_name = normalize_env(src.get("STREAM_META_BACKEND_ENV"))
    # T-056: Cloud Run necesita 0.0.0.0; dev conserva loopback.
    # STREAM_META_BACKEND_HOST explícito siempre gana.
    default_host = "0.0.0.0" if env_name == PRODUCTION else "127.0.0.1"
    return Settings(
        host=src.get("STREAM_META_BACKEND_HOST", default_host),
        port=port,
        env=env_name,
        log_level=src.get("STREAM_META_BACKEND_LOG_LEVEL", "INFO"),
        public_base_url=src.get("STREAM_META_BACKEND_PUBLIC_URL",
                                f"http://127.0.0.1:{port}"),
        request_timeout_s=int(src.get("STREAM_META_BACKEND_TIMEOUT_S", "15")),
        body_limit_bytes=int(src.get("STREAM_META_BACKEND_BODY_LIMIT", "65536")),
        data_dir=src.get("STREAM_META_BACKEND_DATA_DIR", ""),
        secret_dir=src.get("STREAM_META_BACKEND_SECRET_DIR", ""),
        tls_certfile=src.get("STREAM_META_BACKEND_TLS_CERTFILE", ""),
        tls_keyfile=src.get("STREAM_META_BACKEND_TLS_KEYFILE", ""),
        global_limit=int(src.get("STREAM_META_BACKEND_GLOBAL_LIMIT", "600")),
        global_window_s=int(src.get("STREAM_META_BACKEND_GLOBAL_WINDOW_S",
                                    "60")),
        database_url=src.get("STREAM_META_BACKEND_DATABASE_URL", ""),
        db_pool_max=int(src.get("STREAM_META_BACKEND_DB_POOL_MAX", "10")),
        db_pool_timeout_s=int(src.get("STREAM_META_BACKEND_DB_POOL_TIMEOUT_S",
                                      "10")),
    )
