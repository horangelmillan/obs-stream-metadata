"""HTTP/API base stdlib (T-043 §7-8): health/readiness/version + errores JSON.

Convenciones: requestId por request (resp. header X-Request-Id), cuerpos JSON
con límite, validación básica, sin stack traces ni secretos en respuestas.
Rutas no públicas exigen sesión (401); la validación real llega en T-044,
aquí el gate existe como frontera documentada.
"""
from __future__ import annotations

import json
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from backend import auth as auth_boundary
from backend.config import Settings
from backend.errors import AppError, ErrorCode
from backend.logging_setup import API_VERSION, BACKEND_NAME, BACKEND_VERSION, get_logger
from backend.ports import RateLimiter, SessionStore


class BackendApp:
    def __init__(self, settings: Settings, sessions: SessionStore,
                 limiter: RateLimiter, ready_check=None) -> None:
        self.settings = settings
        self.sessions = sessions
        self.limiter = limiter
        self._ready_check = ready_check or (lambda: (True, "ok"))
        self.started_at = time.time()
        self.log = get_logger("http", settings.log_level)

    # --- rutas públicas ---
    def health(self) -> dict:
        return {"status": "ok", "name": BACKEND_NAME, "version": BACKEND_VERSION,
                "api": API_VERSION}

    def version(self) -> dict:
        return {"name": BACKEND_NAME, "version": BACKEND_VERSION, "api": API_VERSION}

    def ready(self) -> tuple[int, dict]:
        ok, detail = self._ready_check()
        code = 200 if ok else 503
        return code, {"ready": ok, "detail": detail if ok else "not-ready"}

    def check_access(self, path: str, headers) -> None:
        """Gate de autenticación (frontera T-044): rutas no públicas → 401."""
        if auth_boundary.is_public(path):
            return
        token = auth_boundary.extract_bearer(headers.get("Authorization", ""))
        if not token or self.sessions.load_session(token) is None:
            raise AppError(ErrorCode.AUTHENTICATION, "no valid session")


class _Handler(BaseHTTPRequestHandler):
    app: BackendApp  # inyectada por serve()

    def log_message(self, *args) -> None:  # logs propios vía logger con requestId
        return

    def _send(self, status: int, payload: dict, request_id: str) -> None:
        data = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("X-Request-Id", request_id)
        self.end_headers()
        self.wfile.write(data)

    def _handle(self, method: str) -> None:
        request_id = uuid.uuid4().hex[:16]
        t0 = time.time()
        path = self.path.split("?", 1)[0].rstrip("/") or "/"
        status = 500
        try:
            if method != "GET":
                raise AppError(ErrorCode.INVALID_REQUEST, f"unsupported method {method}")
            if not self.server.app.limiter.allow("global"):
                raise AppError(ErrorCode.PROVIDER_RATE_LIMITED, "rate limited")
            self.server.app.check_access(path, self.headers)
            if path == "/health":
                payload, status = self.server.app.health(), 200
            elif path == "/ready":
                status, payload = self.server.app.ready()
            elif path == "/version":
                payload, status = self.server.app.version(), 200
            else:
                raise AppError(ErrorCode.INVALID_REQUEST, f"unknown path {path}")
            self._send(status, payload, request_id)
        except AppError as exc:
            status = exc.http_status()
            # detail interno solo al log; la respuesta lleva mensaje seguro
            self.server.app.log.warning("request error code=%s",
                                        exc.code.value,
                                        extra={"requestId": request_id})
            self._send(status, exc.public_body(request_id), request_id)
        except (ConnectionError, BrokenPipeError):
            pass
        except Exception:  # noqa: BLE001 — frontera: jamás filtrar internos
            self.server.app.log.exception("unhandled error",
                                          extra={"requestId": request_id})
            self._send(500, AppError(ErrorCode.INTERNAL).public_body(request_id),
                       request_id)
        finally:
            ms = int((time.time() - t0) * 1000)
            self.server.app.log.info("method=%s path=%s status=%s ms=%s",
                                     method, path, status, ms,
                                     extra={"requestId": request_id})

    def do_GET(self) -> None:
        self._handle("GET")

    def do_POST(self) -> None:  # reservado: validation base ya cubierta
        self._handle("POST")


def serve(app: BackendApp) -> ThreadingHTTPServer:
    server = ThreadingHTTPServer((app.settings.host, app.settings.port), _Handler)
    server.app = app
    server.daemon_threads = True
    return server
