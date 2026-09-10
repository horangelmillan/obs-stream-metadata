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
        transactions = InMemoryOAuthTransactionStore()
        connections = InMemoryConnectionStore()
        tokens = InMemoryTokenStore()
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
    assert_production_ready(env=settings.env,
                            public_base_url=settings.public_base_url,
                            stores=gated)
    auth_service = auth_service or AuthService(installations, sessions)
    return BackendApp(settings=settings,
                      sessions=sessions,
                      limiter=limiter or AllowAllRateLimiter(),
                      ready_check=ready_check, auth_service=auth_service,
                      providers=providers,
                      provider_redirects=redirects)


def main() -> None:
    import os as _os
    # DEV-only: lista separada por comas para levantar providers en local
    # (p. ej. "youtube,kick"). Producción lo decide el despliegue (T-054).
    wanted = {p.strip().lower()
              for p in _os.environ.get("STREAM_META_BACKEND_PROVIDERS", "")
              .split(",") if p.strip()}
    app = create_app(enable_youtube="youtube" in wanted,
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
