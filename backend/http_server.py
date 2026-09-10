"""HTTP/API stdlib (T-043 base + T-044 auth): health/ready/version + /auth/*.

Convenciones: requestId por request (header X-Request-Id), cuerpos JSON con
límite, validación, errores seguros sin secretos ni trazas. Rutas no públicas
exigen sesión vigente (401); /auth/* rate-limitado por IP/instalación.
Contrato T-044 (ver ADR-010):
  POST /auth/bootstrap            {} -> 201 {installation_id, installation_secret}
  POST /auth/session              {installation_id, timestamp, nonce, signature}
                                  -> 200 {session_token, expires_in}
  POST /auth/refresh              {session_token, ...firma} -> 200 (rotada)
  POST /auth/revoke               {session_token} -> 200 {}
  POST /auth/installation/revoke  {installation_id, ...firma} -> 200 {}
"""
from __future__ import annotations

import json
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from backend import auth as auth_boundary
from backend.auth import (AUTH_PER_INSTALL_LIMIT, AUTH_PER_INSTALL_WINDOW_S,
                          AUTH_PER_IP_LIMIT, AUTH_PER_IP_WINDOW_S,
                          BOOTSTRAP_GLOBAL_LIMIT, BOOTSTRAP_GLOBAL_WINDOW_S,
                          BOOTSTRAP_PER_IP_LIMIT, BOOTSTRAP_PER_IP_WINDOW_S,
                          AuthService)
from backend.config import Settings
from backend.errors import AppError, ErrorCode
from backend.logging_setup import API_VERSION, BACKEND_NAME, BACKEND_VERSION, get_logger
from backend.ports import RateLimiter
from backend.stores import FixedWindowRateLimiter, InMemoryInstallationStore


class BackendApp:
    def __init__(self, settings: Settings, sessions, limiter: RateLimiter,
                 ready_check=None, auth_service: AuthService | None = None,
                 limiters: dict | None = None, clock=None,
                 providers: dict | None = None,
                 provider_redirects: dict | None = None) -> None:
        self.settings = settings
        self.sessions = sessions
        self.limiter = limiter
        self._clock = clock or time.time
        self.auth = auth_service or AuthService(InMemoryInstallationStore(),
                                               sessions, clock=self._clock)
        self.limiters = limiters or {
            "bootstrap_ip": FixedWindowRateLimiter(BOOTSTRAP_PER_IP_LIMIT,
                                                   BOOTSTRAP_PER_IP_WINDOW_S,
                                                   clock=self._clock),
            "bootstrap_global": FixedWindowRateLimiter(BOOTSTRAP_GLOBAL_LIMIT,
                                                       BOOTSTRAP_GLOBAL_WINDOW_S,
                                                       clock=self._clock),
            "auth_install": FixedWindowRateLimiter(AUTH_PER_INSTALL_LIMIT,
                                                   AUTH_PER_INSTALL_WINDOW_S,
                                                   clock=self._clock),
            "auth_ip": FixedWindowRateLimiter(AUTH_PER_IP_LIMIT,
                                              AUTH_PER_IP_WINDOW_S,
                                              clock=self._clock),
        }
        self._ready_check = ready_check or (lambda: (True, "ok"))
        self.started_at = time.time()
        self.log = get_logger("http", settings.log_level)
        # Registro provider → ConnectService (T-045 YouTube, T-046 Kick).
        self.providers: dict = providers or {}
        self.provider_redirects: dict = provider_redirects or {}

    def _service(self, name: str):
        try:
            return self.providers[name]
        except KeyError:
            raise AppError(ErrorCode.INVALID_REQUEST,
                           f"unknown provider {name}") from None

    def _redirect_uri(self, name: str) -> str:
        try:
            return self.provider_redirects[name]
        except KeyError:
            raise AppError(ErrorCode.INTERNAL,
                           f"{name} not configured") from None

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

    def check_access(self, path: str, headers):
        """Gate: rutas no públicas exigen sesión vigente (401/401-expired).
        Devuelve el SessionRecord validado, o None en rutas públicas y
        /auth/* (control propio por endpoint)."""
        from backend.auth import SessionRecord
        if auth_boundary.is_public(path):
            return None
        if path.startswith("/auth/"):
            return None  # /auth/* tiene su propio control por endpoint
        token = auth_boundary.extract_bearer(headers.get("Authorization", ""))
        return self.auth.validate_session(token or "")

    def _limited(self, name: str, key: str) -> None:
        if not self.limiters[name].allow(key):
            raise AppError(ErrorCode.RATE_LIMITED, f"{name} limit")


def _json_body(handler: BaseHTTPRequestHandler, limit: int) -> dict:
    length = handler.headers.get("Content-Length")
    if length is None:
        raise AppError(ErrorCode.INVALID_REQUEST, "length required")
    try:
        size = int(length)
    except ValueError:
        raise AppError(ErrorCode.INVALID_REQUEST, "bad content-length")
    if size <= 0 or size > limit:
        raise AppError(ErrorCode.INVALID_REQUEST, "bad body size")
    try:
        body = json.loads(handler.rfile.read(size).decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        raise AppError(ErrorCode.INVALID_REQUEST, "body must be JSON") from None
    if not isinstance(body, dict):
        raise AppError(ErrorCode.INVALID_REQUEST, "body must be a JSON object")
    return body


def _need(body: dict, *names: str) -> None:
    missing = [n for n in names if not isinstance(body.get(n), str) or not body[n]]
    if missing:
        raise AppError(ErrorCode.INVALID_REQUEST, "missing fields")


def _need_int(body: dict, name: str) -> int:
    value = body.get(name)
    if isinstance(value, bool) or not isinstance(value, int):
        raise AppError(ErrorCode.INVALID_REQUEST, f"bad {name}")
    return value


def _parse_query(target: str) -> dict:
    parts = target.split("?", 1)
    if len(parts) < 2:
        return {}
    out: dict[str, str] = {}
    for pair in parts[1].split("&"):
        if "=" in pair:
            key, _, value = pair.partition("=")
            out[key] = value
    return out


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

    def _route_post(self, path: str, body: dict, request_id: str,
                    client_ip: str, query: dict) -> tuple[int, dict]:
        app = self.server.app
        if path == "/auth/bootstrap":
            app._limited("bootstrap_ip", f"bootstrap:{client_ip}")
            app._limited("bootstrap_global", "bootstrap:global")
            return 201, app.auth.bootstrap()
        if path == "/auth/session":
            _need(body, "installation_id", "nonce", "signature")
            ts = _need_int(body, "timestamp")
            app._limited("auth_install", f"session:{body['installation_id']}")
            app._limited("auth_ip", f"ip:{client_ip}")
            return 200, app.auth.create_session(body["installation_id"], ts,
                                                body["nonce"], body["signature"])
        if path == "/auth/refresh":
            _need(body, "session_token", "installation_id", "nonce", "signature")
            ts = _need_int(body, "timestamp")
            app._limited("auth_install", f"refresh:{body['installation_id']}")
            app._limited("auth_ip", f"ip:{client_ip}")
            return 200, app.auth.refresh_session(body["session_token"],
                                                 body["installation_id"], ts,
                                                 body["nonce"], body["signature"])
        if path == "/auth/revoke":
            _need(body, "session_token")
            app.auth.revoke_session(body["session_token"])
            return 200, {}
        if path == "/auth/installation/revoke":
            _need(body, "installation_id", "nonce", "signature")
            ts = _need_int(body, "timestamp")
            app._limited("auth_install", f"revoke:{body['installation_id']}")
            app.auth.revoke_installation(body["installation_id"], ts,
                                         body["nonce"], body["signature"])
            return 200, {}
        if path == "/connect/youtube":
            record = app.check_access(path, self.headers)
            if app.youtube is None:
                raise AppError(ErrorCode.INTERNAL, "youtube not configured")
            app._limited("auth_install", f"yt-connect:{record.installation_id}")
            return 200, app.youtube.start(record.installation_id,
                                          app.youtube_redirect_uri())
        if path == "/connect/youtube/disconnect":
            record = app.check_access(path, self.headers)
            if app.youtube is None:
                raise AppError(ErrorCode.INTERNAL, "youtube not configured")
            app._limited("auth_install", f"yt-disc:{record.installation_id}")
            app.youtube.disconnect(record.installation_id)
            return 200, {"provider": "youtube", "status": "disconnected"}
        parts = path.split("/")
        # /connect/<provider> y /connect/<provider>/disconnect (POST).
        if len(parts) == 3 and parts[1] == "connect":
            name = parts[2]
            record = app.check_access(path, self.headers)
            service = app._service(name)
            app._limited("auth_install", f"{name}-connect:{record.installation_id}")
            return 200, service.start(record.installation_id,
                                      app._redirect_uri(name))
        if len(parts) == 4 and parts[1] == "connect" and parts[3] == "disconnect":
            name = parts[2]
            record = app.check_access(path, self.headers)
            service = app._service(name)
            app._limited("auth_install", f"{name}-disc:{record.installation_id}")
            service.disconnect(record.installation_id)
            return 200, {"provider": name, "status": "disconnected"}
        raise AppError(ErrorCode.INVALID_REQUEST, f"unknown path {path}")

    def _route_callback(self, query: dict, provider: str) -> tuple[int, dict]:
        import urllib.parse as _up
        service = self.server.app._service(provider)
        decode = _up.unquote
        result = service.callback(decode(query.get("state", "")),
                                  decode(query.get("code", "")),
                                  decode(query.get("error", "")))
        return 200, result

    def _handle(self, method: str) -> None:
        request_id = uuid.uuid4().hex[:16]
        t0 = time.time()
        path = self.path.split("?", 1)[0].rstrip("/") or "/"
        client_ip = self.client_address[0] if self.client_address else "unknown"
        status = 500
        try:
            if not self.server.app.limiter.allow("global"):
                raise AppError(ErrorCode.RATE_LIMITED, "global limit")
            if method == "GET":
                query = _parse_query(self.path)
                parts = path.split("/")
                # /connect/<provider>/callback: sin bearer (navegador); la
                # transacción+state son la autorización; rate-limit por IP.
                if (len(parts) == 4 and parts[1] == "connect"
                        and parts[3] == "callback"):
                    self.server.app._limited("auth_ip", f"cb:{client_ip}")
                    status, payload = self._route_callback(query, parts[2])
                else:
                    record = self.server.app.check_access(path, self.headers)
                    if path == "/health":
                        payload, status = self.server.app.health(), 200
                    elif path == "/ready":
                        status, payload = self.server.app.ready()
                    elif path == "/version":
                        payload, status = self.server.app.version(), 200
                    elif (len(parts) == 4 and parts[1] == "connect"
                          and parts[3] == "status"):
                        service = self.server.app._service(parts[2])
                        payload = service.status(record.installation_id)
                        status = 200
                    else:
                        raise AppError(ErrorCode.INVALID_REQUEST,
                                       f"unknown path {path}")
            elif method == "POST":
                body = _json_body(self, self.server.app.settings.body_limit_bytes)
                status, payload = self._route_post(path, body, request_id,
                                                   client_ip, _parse_query(""))
            else:
                raise AppError(ErrorCode.INVALID_REQUEST, f"unsupported method {method}")
            self._send(status, payload, request_id)
        except AppError as exc:
            status = exc.http_status()
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

    def do_POST(self) -> None:
        self._handle("POST")


def serve(app: BackendApp) -> ThreadingHTTPServer:
    server = ThreadingHTTPServer((app.settings.host, app.settings.port), _Handler)
    server.app = app
    server.daemon_threads = True
    return server
