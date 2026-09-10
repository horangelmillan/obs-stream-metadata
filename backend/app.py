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


def _migrations_dir() -> str:
    import os as _os
    return _os.path.join(_os.path.dirname(_os.path.abspath(__file__)),
                         "migrations")


def _production_wiring(settings):
    """Construye stores productivos PostgreSQL (T-055, ADR-014).

    Fail-fast si falta configuración. Ejecuta migrations al arrancar para
    que el deployment nunca corra contra un esquema viejo. Sin SQLite en
    producción (§11 T-055: el contenedor es stateless, sin filesystem
    persistente); sin nombres de proveedor en configuración (DATABASE_URL
    genérica).
    """
    from backend.db import PgPool, run_migrations
    from backend.pgstores import (PgConnectionStore, PgInstallationStore,
                                  PgOAuthTransactionStore, PgSessionStore,
                                  PgTokenStore)
    from backend.prodstores import (CompositeSecretStore, FileSecretStore,
                                    ProdstoresError, split_secret_dirs)
    from backend.stores import FixedWindowRateLimiter
    # T-058: uno o varios directorios (Cloud Run: un secreto por mount).
    # Ambos a la vez = ambiguo = fail-fast; ninguno = fail-fast.
    dirs = split_secret_dirs(settings.secret_dirs)
    if dirs and settings.secret_dir:
        raise ProdstoresError(
            "ambiguous secret config: set SECRET_DIRS or SECRET_DIR, "
            "not both")
    if not dirs:
        if not settings.secret_dir:
            raise ProdstoresError(
                "production requires STREAM_META_BACKEND_SECRET_DIR")
        dirs = [settings.secret_dir]
    secrets = CompositeSecretStore(FileSecretStore(path) for path in dirs)
    if not settings.database_url:
        raise ProdstoresError(
            "production requires STREAM_META_BACKEND_DATABASE_URL")
    # T-057: los secretos de providers habilitados deben existir ANTES de
    # escuchar (nombres en el error, nunca valores): evita descubrir un
    # mount incompleto en el primer OAuth real.
    import os as _os
    wanted = {p.strip().lower()
              for p in _os.environ.get("STREAM_META_BACKEND_PROVIDERS", "")
              .split(",") if p.strip()}
    required: dict[str, tuple[str, ...]] = {
        "youtube": YouTubeProvider.required_secret_names,
        "kick": KickProvider.required_secret_names,
    }
    missing = [f"{provider}/{name}"
               for provider in sorted(wanted)
               if provider in required
               for name in required[provider]
               if not secrets.get(name)]
    if missing:
        raise ProdstoresError(
            "production provider secrets missing: " + ", ".join(missing))
    pool = PgPool(settings.database_url, max_size=settings.db_pool_max,
                  acquire_timeout_s=settings.db_pool_timeout_s)
    run_migrations(pool, _migrations_dir())
    return {
        "secrets": secrets,
        "sessions": PgSessionStore(pool),
        "installations": PgInstallationStore(pool),
        "transactions": PgOAuthTransactionStore(pool),
        "connections": PgConnectionStore(pool),
        "tokens": PgTokenStore(pool),
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
    # T-055: graceful shutdown (Cloud Run envía SIGTERM con gracia de ~10s).
    # shutdown() debe correr fuera del hilo de serve_forever: el handler
    # solo despacha un hilo que la ejecuta (in-flight termina, no se
    # aceptan nuevas conexiones).
    import signal as _signal
    import threading as _threading

    def _stop(*_args) -> None:
        _threading.Thread(target=server.shutdown, daemon=True).start()

    try:
        _signal.signal(_signal.SIGTERM, _stop)
    except (OSError, ValueError):
        pass  # plataforma sin SIGTERM (p. ej. Windows): CTRL+C sigue válido
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
