"""Wiring del backend (T-043): composición raíz Application -> Ports -> Adapters.

Uso desarrollo:
  python -m backend.app                 # 127.0.0.1:8080 (Settings por defecto)
  STREAM_META_BACKEND_PORT=8081 python -m backend.app
Producción: mismo wiring con SecretStore/TokenStore/SessionStore productivos
(secret manager + DB cifrada) sin cambiar la app.
"""
from __future__ import annotations

from backend.adapters.kick import KickProvider
from backend.adapters.youtube import YouTubeProvider
from backend.auth import AuthService
from backend.config import load_settings
from backend.environment import assert_production_ready
from backend.http_server import BackendApp, serve
from backend.logging_setup import get_logger
from backend.oauth import ConnectService
from backend.stores import (AllowAllRateLimiter, EnvSecretStore, InMemoryConnectionStore,
                            InMemoryInstallationStore, InMemoryOAuthTransactionStore,
                            InMemorySessionStore, InMemoryTokenStore)


def _loopback_base(public_base_url: str) -> str:
    return public_base_url.rstrip("/")


def _localhost_base(public_base_url: str) -> str:
    # Kick exige `localhost`, no `127.0.0.1` (F-017); mismo puerto/path base.
    return public_base_url.rstrip("/").replace("127.0.0.1", "localhost")


def create_app(secrets=None, sessions=None, limiter=None,
               ready_check=None, settings=None, providers=None,
               installations=None, auth_service=None,
               transactions=None, connections=None, tokens=None,
               enable_youtube: bool = False,
               enable_kick: bool = False) -> BackendApp:
    settings = settings or load_settings()
    secrets = secrets or EnvSecretStore()
    sessions = sessions or InMemorySessionStore()
    installations = installations or InMemoryInstallationStore()
    gated: dict[str, object] = {
        "secrets": secrets,
        "sessions": sessions,
        "installations": installations,
    }
    if providers is None:
        providers = {}
        redirects = {}
        transactions = transactions or InMemoryOAuthTransactionStore()
        connections = connections or InMemoryConnectionStore()
        tokens = tokens or InMemoryTokenStore()
        gated["transactions"] = transactions
        gated["connections"] = connections
        gated["tokens"] = tokens
        shared = (transactions, connections, tokens)
        if enable_youtube:
            redirect = _loopback_base(settings.public_base_url) + \
                "/connect/youtube/callback"
            providers["youtube"] = ConnectService(
                YouTubeProvider(secrets, redirect), *shared)
            redirects["youtube"] = redirect
        if enable_kick:
            redirect = _localhost_base(settings.public_base_url) + \
                "/connect/kick/callback"
            providers["kick"] = ConnectService(
                KickProvider(secrets, redirect), *shared)
            redirects["kick"] = redirect
    else:
        redirects = {name: "" for name in providers}
    # T-053: entorno explícito. Producción con piezas de grado-dev o con
    # redirects no-HTTPS falla aquí, nunca en silencio ni con fallback.
    # T-054: el gate incluye el limiter global (AllowAll prohibido en prod).
    limiter = limiter or AllowAllRateLimiter()
    assert_production_ready(env=settings.env,
                            public_base_url=settings.public_base_url,
                            stores=gated, limiter=limiter)
    auth_service = auth_service or AuthService(installations, sessions)
    return BackendApp(settings=settings,
                      sessions=sessions,
                      limiter=limiter,
                      ready_check=ready_check, auth_service=auth_service,
                      providers=providers,
                      provider_redirects=redirects)


def _production_wiring(settings):
    """Construye stores productivos (T-054). Fail-fast si falta configuración.

    Sin adivinanzas: data_dir + secret_dir son obligatorios; el SQLite vive
    en data_dir/meta.db (0600) y los secretos se leen de secret_dir (un
    fichero por secreto). El limiter global usa límites explícitos.
    """
    from backend.prodstores import (FileSecretStore, ProdstoresError,
                                    SqliteConnectionStore,
                                    SqliteInstallationStore,
                                    SqliteOAuthTransactionStore,
                                    SqliteSessionStore, SqliteTokenStore)
    from backend.stores import FixedWindowRateLimiter
    missing = [name for name, value in
               (("STREAM_META_BACKEND_DATA_DIR", settings.data_dir),
                ("STREAM_META_BACKEND_SECRET_DIR", settings.secret_dir))
               if not value]
    if missing:
        raise ProdstoresError(
            f"production requires: {', '.join(missing)}")
    db = settings.data_dir.rstrip("/\\") + "/meta.db"
    return {
        "secrets": FileSecretStore(settings.secret_dir),
        "sessions": SqliteSessionStore(db),
        "installations": SqliteInstallationStore(db),
        "transactions": SqliteOAuthTransactionStore(db),
        "connections": SqliteConnectionStore(db),
        "tokens": SqliteTokenStore(db),
        "limiter": FixedWindowRateLimiter(settings.global_limit,
                                          settings.global_window_s),
    }


def main() -> None:
    import os as _os
    from backend.environment import PRODUCTION
    settings = load_settings()
    # DEV-only: lista separada por comas para levantar providers en local
    # (p. ej. "youtube,kick"). Producción lo decide el despliegue (T-054).
    wanted = {p.strip().lower()
              for p in _os.environ.get("STREAM_META_BACKEND_PROVIDERS", "")
              .split(",") if p.strip()}
    if settings.env == PRODUCTION:
        wiring = _production_wiring(settings)
        app = create_app(settings=settings,
                         secrets=wiring["secrets"],
                         sessions=wiring["sessions"],
                         installations=wiring["installations"],
                         transactions=wiring["transactions"],
                         connections=wiring["connections"],
                         tokens=wiring["tokens"],
                         limiter=wiring["limiter"],
                         enable_youtube="youtube" in wanted,
                         enable_kick="kick" in wanted)
    else:
        app = create_app(settings=settings,
                         enable_youtube="youtube" in wanted,
                         enable_kick="kick" in wanted)
    log = get_logger("main", app.settings.log_level)
    server = serve(app)
    log.info("listening host=%s port=%s env=%s",
             app.settings.host, app.settings.port, app.settings.env,
             extra={"requestId": "-"})
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
