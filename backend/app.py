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
from backend.config import load_settings
from backend.http_server import BackendApp, serve
from backend.logging_setup import get_logger
from backend.oauth import ConnectService
from backend.stores import (AllowAllRateLimiter, EnvSecretStore, InMemoryConnectionStore,
                            InMemoryOAuthTransactionStore, InMemorySessionStore,
                            InMemoryTokenStore)


def _loopback_base(public_base_url: str) -> str:
    return public_base_url.rstrip("/")


def _localhost_base(public_base_url: str) -> str:
    # Kick exige `localhost`, no `127.0.0.1` (F-017); mismo puerto/path base.
    return public_base_url.rstrip("/").replace("127.0.0.1", "localhost")


def create_app(secrets=None, sessions=None, limiter=None,
               ready_check=None, settings=None, providers=None,
               enable_youtube: bool = False,
               enable_kick: bool = False) -> BackendApp:
    settings = settings or load_settings()
    secrets = secrets or EnvSecretStore()
    sessions = sessions or InMemorySessionStore()
    if providers is None:
        providers = {}
        redirects = {}
        shared = (InMemoryOAuthTransactionStore(), InMemoryConnectionStore(),
                  InMemoryTokenStore())
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
    return BackendApp(settings=settings,
                      sessions=sessions,
                      limiter=limiter or AllowAllRateLimiter(),
                      ready_check=ready_check, providers=providers,
                      provider_redirects=redirects)


def main() -> None:
    app = create_app()
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
