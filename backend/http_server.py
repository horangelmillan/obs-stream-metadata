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
from backend.stores import (FixedWindowRateLimiter, InMemoryInstallationStore,
                            InMemorySessionStore, InMemoryUserStore)
from backend.users import (LOGIN_PER_IP_LIMIT, LOGIN_PER_IP_WINDOW_S,
                           LOGIN_PER_USER_LIMIT, LOGIN_PER_USER_WINDOW_S,
                           SIGNUP_PER_IP_LIMIT, SIGNUP_PER_IP_WINDOW_S,
                           USER_SESSION_MAX_TTL_S, USER_SESSION_TTL_S,
                           UserExistsError,
                           adopt_installation, check_login, hash_password,
                           new_user_id, utc_now, validate_password,
                           validate_username)

# F-C4: purga de operador (job diario + manual ocasional). 10/min sobra
# y mantiene el endpoint anti-abuso sin interferir con el Scheduler.
OPS_LIMIT = 10
OPS_WINDOW_S = 60

# Fase 3 (spec §4): auth doble Managed. Falta cualquiera de las dos
# sesiones (ausente/inválida/expirada/revocada, de cualquier lado) → 401
# normalizado idéntico: mismo código y mismo `detail` interno (el mensaje
# público ya es único por código). Sin distinguir lados.
MANAGED_AUTH_DETAIL = "managed auth required"


class BackendApp:
    def __init__(self, settings: Settings, sessions, limiter: RateLimiter,
                 ready_check=None, auth_service: AuthService | None = None,
                 limiters: dict | None = None, clock=None,
                 providers: dict | None = None,
                 provider_redirects: dict | None = None,
                 installations=None, transactions=None,
                 secrets=None, users=None, user_sessions=None,
                 connections=None, tokens=None) -> None:
        self.settings = settings
        self.sessions = sessions
        self.installations = installations
        self.transactions = transactions
        self.secrets = secrets
        self.users = users if users is not None else InMemoryUserStore()
        # Fase 2 (spec §3/§4): `user_sessions` reutiliza la misma tabla
        # de sesiones (columna user_id + payload; bearer distinto del de
        # instalación); por defecto comparte el store de instalación.
        self.user_sessions = (user_sessions if user_sessions is not None
                              else sessions)
        # Adopción (spec §3): conexiones/tokens para user_id/owner_user_id
        # sin regenerar backendInstall. None = sin providers (adopción
        # parcial: instalaciones + sesiones).
        self.connections = connections
        self.tokens = tokens
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
            # F-C4: purga de operador (Scheduler diario + manual).
            # 10/min sobra para un job diario; es solo anti-abuso.
            "ops": FixedWindowRateLimiter(OPS_LIMIT, OPS_WINDOW_S,
                                          clock=self._clock),
            # Fase 2 (spec §4): rate-limit específico de login/signup
            # (adicional al vigente): 10/10min por IP y 5/10min por
            # username en login; 10/10min por IP en signup.
            "users_signup_ip": FixedWindowRateLimiter(
                SIGNUP_PER_IP_LIMIT, SIGNUP_PER_IP_WINDOW_S,
                clock=self._clock),
            "users_login_ip": FixedWindowRateLimiter(
                LOGIN_PER_IP_LIMIT, LOGIN_PER_IP_WINDOW_S,
                clock=self._clock),
            "users_login_user": FixedWindowRateLimiter(
                LOGIN_PER_USER_LIMIT, LOGIN_PER_USER_WINDOW_S,
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
        # T-053: `env` explícito para que el operador distinga
        # development vs production. Sin secretos, sin tokens.
        return {"name": BACKEND_NAME, "version": BACKEND_VERSION,
                "api": API_VERSION, "env": self.settings.env}

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
        if path.startswith("/users/"):
            return None  # /users/*: control propio (signup/login públicos,
            # logout/me exigen X-User-Session en cada handler)
        token = auth_boundary.extract_bearer(headers.get("Authorization", ""))
        return self.auth.validate_session(token or "")

    def check_managed_access(self, headers, provider=None,
                             request_id: str = "-"):
        """Gate doble Managed (Fase 3, spec §4): sesión de instalación
        vigente (T-044) + sesión de usuario vigente (Fase 2).

        Devuelve `(SessionRecord, User)`. Cualquier ausencia/invalidez/
        expiración/revocación de cualquier lado → 401 normalizado
        idéntico (mismo código, mismo `detail`) + auditoría sin secretos.
        Con `provider`: además exige y propaga propiedad
        (connections.user_id + tokens.owner_user_id del adoptado; otro
        usuario → 401; multi-PC del mismo usuario OK)."""
        install_err: AppError | None = None
        user_err: AppError | None = None
        record = None
        user = None
        try:
            token = auth_boundary.extract_bearer(
                headers.get("Authorization", ""))
            record = self.auth.validate_session(token or "")
        except AppError as exc:
            install_err = exc
        if record is None and install_err is None:
            install_err = AppError(ErrorCode.AUTHENTICATION,
                                   MANAGED_AUTH_DETAIL)
        try:
            user = self.validate_user_session(
                headers.get("X-User-Session", ""))
        except AppError as exc:
            user_err = exc
        if record is None or user is None:
            codes = ((install_err.code.value if install_err else "ok"),
                     (user_err.code.value if user_err else "ok"))
            self.log.info("managed auth reject provider=%s codes=%s",
                          provider, codes,
                          extra={"requestId": request_id})
            raise AppError(ErrorCode.AUTHENTICATION, MANAGED_AUTH_DETAIL)
        if provider is not None:
            self._enforce_managed_owner(provider, record.installation_id,
                                         user.id, request_id=request_id)
        return record, user

    def _adopted_user_id(self, installation_id: str) -> str | None:
        """user_id adoptado de la instalación (None si no hay adopción
        o el store no lo soporta). Nunca eleva ni falla."""
        if self.installations is None or not installation_id:
            return None
        get = getattr(self.installations, "get_user_id", None)
        if not callable(get):
            return None
        try:
            return get(installation_id)
        except Exception:
            return None

    def _enforce_managed_owner(self, provider: str, installation_id: str,
                               user_id: str, request_id: str = "-") -> None:
        """Aislamiento + propagación de propiedad (Fase 3, spec §3/§4).

        La instalación solo opera connections/tokens de su propio user_id
        adoptado (otro usuario → 401 normalizado; multi-PC del mismo
        usuario OK). Backfill: connection sin `user_id` / token sin
        `owner_user_id` se fijan al llamante (el callback ya vinculó lo
        suyo en `_stamp_callback_owner`)."""
        adopted = self._adopted_user_id(installation_id)
        if adopted is not None and adopted != user_id:
            self.log.info("managed auth reject provider=%s user_id=%s "
                          "codes=%s", provider, user_id, ("adopted-mismatch",),
                          extra={"requestId": request_id})
            raise AppError(ErrorCode.AUTHENTICATION, MANAGED_AUTH_DETAIL)
        if self.connections is None:
            return
        try:
            entry = self.connections.load(installation_id, provider)
        except Exception:
            entry = None
        if entry is None:
            return  # Sin conexión: nada que aislar ni propagar.
        entry_user = entry.get("user_id")
        if entry_user is not None and entry_user != user_id:
            self.log.info("managed auth reject provider=%s user_id=%s "
                          "codes=%s", provider, user_id, ("entry-mismatch",),
                          extra={"requestId": request_id})
            raise AppError(ErrorCode.AUTHENTICATION, MANAGED_AUTH_DETAIL)
        account = entry.get("account")
        puid = (account.get("provider_user_id")
                if isinstance(account, dict) else None)
        owner = None
        if self.tokens is not None and puid:
            owner_of = getattr(self.tokens, "owner_of", None)
            if callable(owner_of):
                try:
                    owner = owner_of(provider, puid)
                except Exception:
                    owner = None
        if owner is not None and owner != user_id:
            self.log.info("managed auth reject provider=%s user_id=%s "
                          "codes=%s", provider, user_id, ("owner-mismatch",),
                          extra={"requestId": request_id})
            raise AppError(ErrorCode.AUTHENTICATION, MANAGED_AUTH_DETAIL)
        if entry_user is None:
            entry["user_id"] = user_id
            try:
                self.connections.save(installation_id, provider, entry)
            except Exception:
                pass
        if owner is None and puid and self.tokens is not None:
            set_owner = getattr(self.tokens, "set_owner", None)
            if callable(set_owner):
                try:
                    set_owner(provider, puid, user_id)
                except Exception:
                    pass

    def _stamp_callback_owner(self, provider: str,
                              installation_id: str | None, result: dict,
                              request_id: str = "-") -> None:
        """Vincula connection+tokens al adoptado (Fase 3: el callback
        sigue sin bearer — la transacción+state es la autorización — pero
        el resultado se fija al `user_id` adoptado de la instalación que
        inició la transacción). Sin adopción o sin stores = sin efectos
        (transacciones legado en vuelo)."""
        if not installation_id or self.connections is None:
            return
        user_id = self._adopted_user_id(installation_id)
        if not user_id:
            return
        try:
            entry = self.connections.load(installation_id, provider)
        except Exception:
            entry = None
        stamped = 0
        if entry is not None:
            entry["user_id"] = user_id
            try:
                self.connections.save(installation_id, provider, entry)
                stamped += 1
            except Exception:
                pass
        account = result.get("account") if isinstance(result, dict) else None
        puid = account.get("id") if isinstance(account, dict) else None
        if self.tokens is not None and puid:
            set_owner = getattr(self.tokens, "set_owner", None)
            if callable(set_owner):
                try:
                    if set_owner(provider, puid, user_id):
                        stamped += 1
                except Exception:
                    pass
        # Auditoría (spec §4): solo IDs opacos + conteos. Jamás tokens,
        # hashes, passwords ni secretos (hereda RedactingFilter).
        self.log.info("managed callback owner provider=%s user_id=%s "
                      "stamped=%s", provider, user_id, stamped,
                      extra={"requestId": request_id})

    def _limited(self, name: str, key: str) -> None:
        if not self.limiters[name].allow(key):
            raise AppError(ErrorCode.RATE_LIMITED, f"{name} limit")

    def ops_purge_token(self) -> str | None:
        """Bearer de operador para /ops/purge (F-C4). Solo el nombre viaja
        en errores: el valor jamás sale del SecretStore ni entra en logs."""
        if self.secrets is None:
            return None
        return self.secrets.get("OPS_PURGE_TOKEN")

    def ops_purge(self, bearer: str) -> dict:
        """Purga F-C4 con auth de operador. Solo conteos en salida."""
        import hmac as _hmac
        expected = self.ops_purge_token()
        if not expected:
            raise AppError(ErrorCode.INTERNAL, "purge not configured")
        if not bearer or not _hmac.compare_digest(bearer, expected):
            raise AppError(ErrorCode.AUTHENTICATION, "bad ops token")
        self._limited("ops", "ops:purge")
        if self.transactions is None:
            raise AppError(ErrorCode.INTERNAL, "purge not configured")
        from backend import maintenance as _maintenance
        result = _maintenance.purge_expired(self.sessions,
                                            self.transactions, self._clock)
        self.log.info("ops purge sessions=%s transactions=%s",
                      result["sessions"], result["transactions"],
                      extra={"requestId": "-"})
        return {"purged": result}

    # --- cuentas de usuario Managed (Fase 2, spec §3/§4) ---
    def _adopt_installation(self, user_id: str,
                            installation_id: str | None) -> dict:
        """Adopción sin reset (spec §3): asigna user_id/owner_user_id sin
        regenerar backendInstall. Idempotente; sin instalación o sin
        stores = conteos a cero, sin efectos. Reutiliza Fase 1 con
        adaptadores nulos cuando no hay providers cableados."""
        zero = {"installations": 0, "sessions": 0, "connections": 0,
                "tokens": 0}
        if not user_id or not installation_id:
            return dict(zero)
        if self.installations is None or self.sessions is None:
            return dict(zero)
        connections = self.connections
        if connections is None:
            class _NullConnections:
                def set_user_id_for_installation(self, _iid, _uid):
                    return []
            connections = _NullConnections()
        tokens = self.tokens
        if tokens is None:
            class _NullTokens:
                def set_owner(self, *_args):
                    return False
            tokens = _NullTokens()
        return adopt_installation(
            user_id, installation_id, installations=self.installations,
            sessions=self.sessions, connections=connections, tokens=tokens)

    def _purge_foreign_links(self, user_id: str,
                             installation_id: str | None) -> dict:
        """Limpieza al entrar ([D] 2026-10-26): otras cuentas fuera ANTES
        de adoptar (el entrante nunca hereda enlaces ajenos). Filas sin
        dueño y del mismo usuario: intactas. Solo conteos; sin
        instalación o sin stores = ceros, sin efectos."""
        from backend.users import purge_foreign_connections
        zero = {"cleaned": {"connections": 0, "tokens": 0},
                "revoked": {}}
        if not user_id or not installation_id:
            return zero
        if self.connections is None:
            return zero
        return purge_foreign_connections(
            user_id, installation_id, services=self.providers,
            connections=self.connections, tokens=self.tokens)
    def installation_id_for_adoption(self, headers) -> str | None:
        """Instalación llamante para adopción. Nunca eleva: bearer
        ausente/inválido = None (signup/login siguen sin adopción)."""
        try:
            token = auth_boundary.extract_bearer(
                headers.get("Authorization", ""))
            if not token:
                return None
            return self.auth.validate_session(token).installation_id
        except Exception:
            return None

    def signup_user(self, body: dict, client_ip: str = "unknown",
                    installation_id: str | None = None,
                    request_id: str = "-") -> tuple[int, dict]:
        """POST /users/signup. 201 {user_id, username}; 409 si existe;
        400 si valida mal. Jamás devuelve ni loguea el hash."""
        from backend.users import User
        self._limited("users_signup_ip", f"signup:{client_ip}")
        if not isinstance(body, dict):
            raise AppError(ErrorCode.INVALID_REQUEST, "bad body")
        username = validate_username(body.get("username"))
        password = validate_password(body.get("password"))
        now = utc_now()
        user = User(id=new_user_id(), username=username,
                    password_hash=hash_password(password),
                    created_at=now, updated_at=now)
        try:
            self.users.create(user)
        except UserExistsError:
            raise AppError(ErrorCode.USER_EXISTS,
                           "duplicate username") from None
        # [D] 2026-10-26: otras cuentas fuera ANTES de adoptar (el
        # entrante nunca hereda enlaces ajenos; NULL se adopta).
        purged = self._purge_foreign_links(user.id, installation_id)
        adopted = self._adopt_installation(user.id, installation_id)
        # Auditoría (spec §4): solo user_id opaco + conteos. Jamás
        # username (identificador de persona, spec §6), password ni hash.
        self.log.info("users signup ok user_id=%s purged=%s adopted=%s",
                      user.id, purged, adopted,
                      extra={"requestId": request_id})
        return 201, {"user_id": user.id, "username": user.username}
    def login_user(self, body: dict, client_ip: str = "unknown",
                   installation_id: str | None = None,
                   request_id: str = "-") -> tuple[int, dict]:
        """POST /users/login. 200 {user_session, expires_in} + adopción;
        401 normalizado (sin distinguir usuario/existe)."""
        import secrets as _secrets
        self._limited("users_login_ip", f"login-ip:{client_ip}")
        raw_username = body.get("username") if isinstance(body, dict) else None
        user_key = (raw_username.strip()
                    if isinstance(raw_username, str) else "?")
        self._limited("users_login_user", f"login-user:{user_key}")
        if not isinstance(body, dict):
            raise AppError(ErrorCode.INVALID_REQUEST, "bad body")
        username = validate_username(body.get("username"))
        password = body.get("password")
        if not isinstance(password, str):
            raise AppError(ErrorCode.INVALID_REQUEST, "bad password")
        found = self.users.load_by_username(username)
        user = check_login(password, found)
        if user is None:
            raise AppError(ErrorCode.AUTHENTICATION, "bad credentials")
        token = _secrets.token_urlsafe(32)
        now = self._clock()
        self.user_sessions.save_session(token, {
            "kind": "user", "user_id": user.id,
            "issued_at": now, "login_at": now,
            "expires_at": now + USER_SESSION_TTL_S,
            "revoked": False})
        # [D] 2026-10-26: otras cuentas fuera ANTES de adoptar.
        purged = self._purge_foreign_links(user.id, installation_id)
        adopted = self._adopt_installation(user.id, installation_id)
        self.log.info("users login ok user_id=%s purged=%s adopted=%s",
                      user.id, purged, adopted,
                      extra={"requestId": request_id})
        return 200, {"user_session": token,
                     "expires_in": USER_SESSION_TTL_S}

    def validate_user_session(self, token: str):
        """Sesión de usuario vigente o AppError 401/401-expired.

        Deslizante 15d con tope absoluto 30d ([D] 2026-10-26): cada
        actividad válida renueva `expires_at` a +15d sin pasar
        `login_at` + 30d; superado el tope o 15d sin actividad →
        SESSION_EXPIRED (login de nuevo). Sesiones heredadas sin
        `login_at` toman `issued_at` como origen (fail-closed ante
        payloads sin origen conocido). La renovación es best-effort:
        si el guardado falla, la sesión válida no se rechaza.
        """
        payload = (self.user_sessions.load_session(token)
                   if token else None)
        if payload is None or not payload.get("user_id"):
            raise AppError(ErrorCode.AUTHENTICATION, "no valid user session")
        if payload.get("revoked"):
            raise AppError(ErrorCode.AUTHENTICATION, "user session revoked")
        now = self._clock()
        login_at = payload.get("login_at", payload.get("issued_at", 0))
        if now >= login_at + USER_SESSION_MAX_TTL_S:
            raise AppError(ErrorCode.SESSION_EXPIRED,
                           "user session lifetime exceeded")
        if now >= payload.get("expires_at", 0):
            raise AppError(ErrorCode.SESSION_EXPIRED, "user session expired")
        renewed = min(now + USER_SESSION_TTL_S,
                      login_at + USER_SESSION_MAX_TTL_S)
        if renewed != payload.get("expires_at"):
            payload["expires_at"] = renewed
            payload["issued_at"] = now
            try:
                self.user_sessions.save_session(token, payload)
            except Exception:
                pass
        user = self.users.load_by_id(payload["user_id"])
        if user is None:
            raise AppError(ErrorCode.AUTHENTICATION, "unknown user")
        return user

    def logout_user(self, token: str,
                    installation_id: str | None = None,
                    request_id: str = "-") -> tuple[int, dict]:
        """POST /users/logout ([D] 2026-10-26): limpieza total Managed de
        la instalación llamante (TODAS sus connections: borrado protegido
        + revoke best-effort) + revoca la sesión de usuario llamante.
        Responde solo conteos. 200 idempotente (token ausente/repetido
        también es 200; repetir devuelve ceros). Sin instalación o sin
        sesión conocida: solo revoca (o ceros), sin borrar nada ajeno."""
        from backend import users as _users
        cleaned = {"connections": 0, "tokens": 0, "sessions": 0}
        revoked: dict = {}
        try:
            payload = (self.user_sessions.load_session(token)
                       if token else None)
        except Exception:
            payload = None
        user_id = (payload.get("user_id")
                   if isinstance(payload, dict) else None)
        if user_id and installation_id and self.connections is not None:
            try:
                result = _users.clean_installation_connections(
                    installation_id, services=self.providers,
                    connections=self.connections, tokens=self.tokens)
            except Exception:
                # Idempotente: un store caído aquí no debe convertir un
                # logout en 500 con el token en el log.
                result = None
            if result is not None:
                cleaned["connections"] = result["cleaned"]["connections"]
                cleaned["tokens"] = result["cleaned"]["tokens"]
                revoked = result["revoked"]
        if user_id:
            try:
                if not payload.get("revoked"):
                    payload["revoked"] = True
                    self.user_sessions.save_session(token, payload)
                    cleaned["sessions"] = 1
            except Exception:
                pass
            self.log.info("users logout ok user_id=%s cleaned=%s",
                          user_id, cleaned,
                          extra={"requestId": request_id})
        else:
            self.log.info("users logout ok revoked=0",
                          extra={"requestId": request_id})
        return 200, {"cleaned": cleaned, "revoked": revoked}

    def me_user(self, token: str) -> tuple[int, dict]:
        """GET /users/me. 200 {user_id, username} sin secretos."""
        user = self.validate_user_session(token)
        return 200, {"user_id": user.id, "username": user.username}

    def erase_user(self, user_id: str,
                   request_id: str = "-") -> dict:
        """POST /users/erase (Fase 5a): cascada por usuario con auth
        doble ya verificada por el llamante. Delega en
        `backend.users.erase_user` (contrato request/response en su
        docstring: 200 solo conteos por tabla + `revoked` por
        provider). El bearer usado cae como efecto (era sesión del
        usuario)."""
        from backend import users as _users
        return _users.erase_user(
            user_id, services=self.providers, sessions=self.sessions,
            transactions=self.transactions,
            installations=self.installations,
            connections=self.connections, tokens=self.tokens,
            users=self.users, user_sessions=self.user_sessions)

    def signup_page(self) -> str:
        """GET /users/signup: HTML mínimo stdlib (form + aviso v1)."""
        return ("<!doctype html><html lang=\"es\"><head><meta charset=\"utf-8\">"
                "<title>Crear cuenta — Stream Metadata</title></head><body>"
                "<h1>Crear cuenta</h1>"
                "<p><strong>Cuenta v1 sin email: sin verificaci\u00f3n "
                "ni recuperaci\u00f3n de cuenta.</strong> "
                "Si pierdes tu contrase\u00f1a no podremos recuperarla; "
                "gu\u00e1rdala en un lugar seguro.</p>"
                "<form id=\"signup\" method=\"post\" action=\"/users/signup\">"
                "<label>Usuario <input name=\"username\" minlength=\"3\" "
                "maxlength=\"32\" required></label><br>"
                "<label>Contrase\u00f1a <input type=\"password\" "
                "name=\"password\" minlength=\"12\" maxlength=\"128\" "
                "required></label><br>"
                "<button type=\"submit\">Crear cuenta</button></form>"
                "<script>"
                "document.getElementById('signup').addEventListener('submit',"
                "function(e){e.preventDefault();"
                "var f=e.target;"
                "fetch('/users/signup',{method:'POST',"
                "headers:{'Content-Type':'application/json'},"
                "body:JSON.stringify({username:f.username.value,"
                "password:f.password.value})})"
                ".then(function(r){return r.json().then(function(j){"
                "return {status:r.status,body:j};});})"
                ".then(function(r){document.body.insertAdjacentHTML("
                "'beforeend','<p>'+r.status+' '+"
                "JSON.stringify(r.body)+'</p>');});});"
                "</script></body></html>")


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

    def _send_html(self, status: int, html: str, request_id: str) -> None:
        # Fase 2: GET /users/signup (página mínima stdlib, sin secretos).
        data = html.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
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
        if path == "/users/signup":
            # Fase 2: alta pública rate-limitada; si el llamante trae
            # sesión de instalación vigente, la adopta (sin reset).
            return app.signup_user(
                body, client_ip=client_ip,
                installation_id=app.installation_id_for_adoption(
                    self.headers),
                request_id=request_id)
        if path == "/users/login":
            # Fase 2: rate-limit específico dentro de login_user
            # (10/10min IP + 5/10min username); 401 normalizado.
            return app.login_user(
                body, client_ip=client_ip,
                installation_id=app.installation_id_for_adoption(
                    self.headers),
                request_id=request_id)
        if path == "/users/logout":
            # Fase 2: requiere X-User-Session; 200 idempotente.
            # [D] 2026-10-26: con bearer de instalación vigente, limpia
            # además todas las connections de la instalación llamante.
            token = self.headers.get("X-User-Session", "")
            if not token:
                raise AppError(ErrorCode.AUTHENTICATION,
                               "user session required")
            return app.logout_user(
                token,
                installation_id=app.installation_id_for_adoption(
                    self.headers),
                request_id=request_id)
        if path == "/users/erase":
            # Fase 5a: borrado de CUENTA en cascada (distinto de
            # /privacy/erase por instalación). Auth doble Managed.
            record, user = app.check_managed_access(
                self.headers, request_id=request_id)
            app._limited("auth_install",
                         f"erase-user:{record.installation_id}")
            payload = app.erase_user(user.id, request_id=request_id)
            app.log.info("users erase ok erased=%s",
                         payload["erased"],
                         extra={"requestId": request_id})
            return 200, payload
        if path == "/connect/youtube":
            # Compatibilidad: la ruta genérica /connect/<provider> la cubre;
            # este alias existe para no romper clientes antiguos.
            # Fase 3: auth doble Managed (instalación + usuario).
            record, _user = app.check_managed_access(
                self.headers, provider="youtube", request_id=request_id)
            service = app._service("youtube")
            app._limited("auth_install", f"yt-connect:{record.installation_id}")
            return 200, service.start(record.installation_id,
                                      app._redirect_uri("youtube"))
        if path == "/connect/youtube/disconnect":
            record, _user = app.check_managed_access(
                self.headers, provider="youtube", request_id=request_id)
            service = app._service("youtube")
            app._limited("auth_install", f"yt-disc:{record.installation_id}")
            service.disconnect(record.installation_id)
            return 200, {"provider": "youtube", "status": "disconnected"}
        if path == "/ops/purge":
            # F-C4: purga programada (T-065). Auth propia de operador
            # (bearer OPS_PURGE_TOKEN), no sesión de instalación: el
            # Scheduler no es un plugin y no tiene installation_id.
            return 200, app.ops_purge(
                auth_boundary.extract_bearer(
                    self.headers.get("Authorization", "")) or "")
        if path == "/privacy/erase":
            # F-C2: borrado total por instalación (T-064). El bearer usado
            # queda revocado como efecto (era una session de la instalación).
            record = app.check_access(path, self.headers)
            app._limited("auth_install", f"erase:{record.installation_id}")
            if app.installations is None or app.transactions is None:
                raise AppError(ErrorCode.INTERNAL, "erase not configured")
            from backend import privacy as _privacy
            payload = _privacy.erase_installation(
                record.installation_id, app.providers, app.sessions,
                app.transactions, app.installations)
            app.log.info("privacy erase providers=%s erased=%s",
                         sorted(app.providers), payload["erased"],
                         extra={"requestId": request_id})
            return 200, payload
        parts = path.split("/")
        # FASE 2.1-C: metadata Managed (tokens server-side; el plugin
        # envía solo sesión bearer + {broadcast_id,title,description}).
        if len(parts) == 3 and parts[1] == "metadata":
            name = parts[2]
            for key in ("broadcast_id", "title", "description"):
                if key in body and not isinstance(body[key], str):
                    raise AppError(ErrorCode.INVALID_REQUEST, f"bad {key}")
            # Fase 3: auth doble Managed (instalación + usuario).
            record, _user = app.check_managed_access(
                self.headers, provider=name, request_id=request_id)
            service = app._service(name)
            app._limited("auth_install", f"{name}-meta:{record.installation_id}")
            return 200, service.apply_metadata(record.installation_id, body)
        # /connect/<provider> y /connect/<provider>/disconnect (POST).
        if len(parts) == 3 and parts[1] == "connect":
            name = parts[2]
            record, _user = app.check_managed_access(
                self.headers, provider=name, request_id=request_id)
            service = app._service(name)
            app._limited("auth_install", f"{name}-connect:{record.installation_id}")
            return 200, service.start(record.installation_id,
                                      app._redirect_uri(name))
        if len(parts) == 4 and parts[1] == "connect" and parts[3] == "disconnect":
            name = parts[2]
            record, _user = app.check_managed_access(
                self.headers, provider=name, request_id=request_id)
            service = app._service(name)
            app._limited("auth_install", f"{name}-disc:{record.installation_id}")
            service.disconnect(record.installation_id)
            return 200, {"provider": name, "status": "disconnected"}
        raise AppError(ErrorCode.INVALID_REQUEST, f"unknown path {path}")

    def _route_callback(self, query: dict, provider: str,
                          request_id: str = "-") -> tuple[int, dict]:
        import urllib.parse as _up
        decode = _up.unquote
        state = decode(query.get("state", ""))
        # Fase 3: la instalación dueña sale de la transacción (el state es
        # la autorización); se lee ANTES de consumirla en el callback.
        installation_id = None
        try:
            txns = self.server.app.transactions
            if txns is not None and state:
                found = txns.find_by_state(state)
                if found:
                    installation_id = found.get("installation_id")
        except Exception:
            installation_id = None
        service = self.server.app._service(provider)
        result = service.callback(state,
                                  decode(query.get("code", "")),
                                  decode(query.get("error", "")))
        self.server.app._stamp_callback_owner(provider, installation_id,
                                              result,
                                              request_id=request_id)
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
                    status, payload = self._route_callback(
                        query, parts[2], request_id=request_id)
                elif path == "/users/signup":
                    # Fase 2: página HTML mínima (pública). Se envía con
                    # Content-Type text/html vía marcador __html__.
                    status, payload = 200, {"__html__":
                                             self.server.app.signup_page()}
                elif path == "/users/me":
                    # Fase 2: requiere X-User-Session (no bearer).
                    status, payload = self.server.app.me_user(
                        self.headers.get("X-User-Session", ""))
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
                        # Fase 3: auth doble Managed (instalación + usuario).
                        record, _user = self.server.app.check_managed_access(
                            self.headers, provider=parts[2],
                            request_id=request_id)
                        service = self.server.app._service(parts[2])
                        payload = service.status(record.installation_id)
                        status = 200
                    elif (len(parts) == 4 and parts[1] == "metadata"
                          and parts[3] == "broadcasts"):
                        # Fase 3: auth doble Managed (instalación + usuario).
                        record, _user = self.server.app.check_managed_access(
                            self.headers, provider=parts[2],
                            request_id=request_id)
                        service = self.server.app._service(parts[2])
                        payload = service.list_resources(record.installation_id)
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
            if isinstance(payload, dict) and set(payload) == {"__html__"}:
                self._send_html(status, payload["__html__"], request_id)
            else:
                self._send(status, payload, request_id)
        except AppError as exc:
            status = exc.http_status()
            # FASE 2.1-C.1: el detail viaja al log (no a la respuesta, que
            # usa safe_message). Contiene solo enums/códigos de protocolo;
            # el RedactingFilter sigue redactando cualquier secreto.
            self.server.app.log.warning("request error code=%s detail=%s",
                                        exc.code.value, exc.detail,
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
    # T-054: TLS builtin opcional (stdlib ssl). Sin cert/key → HTTP plano
    # (dev local). Con ambos → HTTPS; ficheros ausentes = fail-fast aquí,
    # nunca handshake a medias. Topología esperada en prod: TLS directo o
    # terminación en reverse-proxy (ver docs/DEPLOYMENT.md y ADR-013).
    cert, key = app.settings.tls_certfile, app.settings.tls_keyfile
    if cert or key:
        import os as _os
        import ssl as _ssl
        from backend.prodstores import ProdstoresError
        if not (cert and key):
            raise ProdstoresError(
                "tls requires both certfile and keyfile")
        missing = [label for label, path in (("certfile", cert),
                                             ("keyfile", key))
                   if not _os.path.isfile(path)]
        if missing:
            raise ProdstoresError(
                f"tls files missing: {', '.join(missing)}")
        context = _ssl.SSLContext(_ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = _ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(cert, key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
    return server
