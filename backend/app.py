"""Wiring del backend (T-043): composición raíz Application -> Ports -> Adapters.

Uso desarrollo:
  python -m backend.app                 # 127.0.0.1:8080 (Settings por defecto)
  STREAM_META_BACKEND_PORT=8081 python -m backend.app
Producción: mismo wiring con SecretStore/TokenStore/SessionStore productivos
(secret manager + DB cifrada) sin cambiar la app.
"""
from __future__ import annotations

from backend.config import load_settings
from backend.http_server import BackendApp, serve
from backend.logging_setup import get_logger
from backend.stores import AllowAllRateLimiter, EnvSecretStore, InMemorySessionStore


def create_app(secrets=None, sessions=None, limiter=None,
               ready_check=None, settings=None) -> BackendApp:
    settings = settings or load_settings()
    # secrets se inyecta para dejar el punto de integración documentado;
    # ningún flujo T-043 lo consume (OAuth real: T-045/46).
    _ = secrets or EnvSecretStore()
    return BackendApp(settings=settings,
                      sessions=sessions or InMemorySessionStore(),
                      limiter=limiter or AllowAllRateLimiter(),
                      ready_check=ready_check)


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
