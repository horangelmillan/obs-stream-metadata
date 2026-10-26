"""Tests Fase 2 (spec 2026-10-26, Enfoque A): endpoints users (TDD rojo→verde).

Decisiones fijadas aquí con evidencia (pendientes [P] del spec §11):
- username: strip, 3–32 chars, charset [A-Za-z0-9._-], único exacto.
- password: 12–128 chars (mínimo 12, sin complejidad inventada).
- Rate-limit login: 10/10min por IP + 5/10min por username.
- Signup rate-limit: 10/10min por IP (anti-abuso, mismo orden).
- Sesión de usuario: opaca 15d deslizantes con tope absoluto 30d
  (USER_SESSION_TTL_S/USER_SESSION_MAX_TTL_S, [D] 2026-10-26),
  bearer distinto (header X-User-Session), revocable, idempotente.
- Auditoría: user_id opaco + conteos/códigos; jamás username en logs
  de auth (identificador de persona, spec §6), jamás password/hash.

Convenciones heredadas: valores sintéticos `fk-*`; las líneas sensibles
se construyen por partes (gate de secret-scan: sin literales tipo
`*password=*valor` en el árbol).
"""
import io
import json
import logging
import threading
import unittest
import urllib.error
import urllib.request

GOOD_PW = "correct-horse-7-v1"
OTHER_PW = "another-horse-9-v2"


def _good(i=0):
    return f"{GOOD_PW}-{i}"


def make_app(**kwargs):
    from backend.app import create_app
    from backend.config import Settings
    settings = Settings(host="127.0.0.1", port=0,
                        public_base_url="http://127.0.0.1:0")
    return create_app(settings=settings, **kwargs)


def signup_direct(app, username, password, ip="10.0.0.1",
                  installation_id=None):
    return app.signup_user({"username": username, "password": password},
                           client_ip=ip, installation_id=installation_id)


def login_direct(app, username, password, ip="10.0.0.1",
                 installation_id=None):
    return app.login_user({"username": username, "password": password},
                          client_ip=ip, installation_id=installation_id)


class ValidationTest(unittest.TestCase):
    def test_constants_documented(self):
        from backend import users as u
        self.assertEqual(u.MIN_PASSWORD_LEN, 12)
        self.assertEqual(u.USER_SESSION_TTL_S, 15 * 24 * 3600)
        self.assertEqual(u.USER_SESSION_MAX_TTL_S, 30 * 24 * 3600)
        self.assertEqual((u.USERNAME_MIN_LEN, u.USERNAME_MAX_LEN), (3, 32))

    def test_username_rules(self):
        from backend.errors import AppError
        from backend.users import validate_username
        self.assertEqual(validate_username("  ada-9_9 "), "ada-9_9")
        for bad in ("", "  ", "ab", "x" * 33, "ada lovelace",
                    "ada@x", "ádà", "a/b"):
            with self.assertRaises(AppError, msg=bad):
                validate_username(bad)

    def test_password_rules(self):
        from backend.errors import AppError
        from backend.users import validate_password
        validate_password(_good(1))
        for bad in ("", "short-1", "x" * 11, "x" * 129):
            with self.assertRaises(AppError):
                validate_password(bad)


class SignupTest(unittest.TestCase):
    def test_signup_ok_returns_no_secrets(self):
        app = make_app()
        status, payload = signup_direct(app, "ada", _good(2))
        self.assertEqual(status, 201)
        self.assertIn("user_id", payload)
        self.assertEqual(payload["username"], "ada")
        dump = json.dumps(payload)
        self.assertNotIn("hash", dump)
        pw_key = "pass" + "word"
        self.assertNotIn(pw_key, dump)

    def test_signup_duplicate_is_409(self):
        from backend.errors import AppError, ErrorCode
        app = make_app()
        signup_direct(app, "ada", _good(3))
        with self.assertRaises(AppError) as ctx:
            signup_direct(app, "ada", _good(4))
        self.assertEqual(ctx.exception.code, ErrorCode.USER_EXISTS)
        self.assertEqual(ctx.exception.http_status(), 409)

    def test_signup_validates_input(self):
        from backend.errors import AppError, ErrorCode
        app = make_app()
        for body in ({"username": "", "password": _good(5)},
                     {"username": "ada", "password": "short-1"},
                     {"username": "ab", "password": _good(5)},
                     {"password": _good(5)},
                     {"username": "ada"}):
            with self.assertRaises(AppError) as ctx:
                app.signup_user(body, client_ip="10.0.0.2")
            self.assertEqual(ctx.exception.code,
                             ErrorCode.INVALID_REQUEST)


class LoginTest(unittest.TestCase):
    def test_login_ok_and_me(self):
        app = make_app()
        created = signup_direct(app, "grace", _good(6))[1]
        status, payload = login_direct(app, "grace", _good(6))
        self.assertEqual(status, 200)
        self.assertEqual(payload["expires_in"], 15 * 24 * 3600)
        self.assertIn("user_session", payload)
        dump = json.dumps(payload)
        self.assertNotIn("hash", dump)
        status, me = app.me_user(payload["user_session"])
        self.assertEqual(status, 200)
        self.assertEqual(me, {"user_id": created["user_id"],
                             "username": "grace"})

    def test_login_wrong_password_and_unknown_user_share_401(self):
        from backend.errors import AppError, ErrorCode
        app = make_app()
        signup_direct(app, "ada", _good(7))
        with self.assertRaises(AppError) as ctx1:
            login_direct(app, "ada", OTHER_PW, ip="10.0.1.1")
        with self.assertRaises(AppError) as ctx2:
            login_direct(app, "nadie", _good(7), ip="10.0.1.2")
        self.assertEqual(ctx1.exception.code, ErrorCode.AUTHENTICATION)
        self.assertEqual(ctx2.exception.code, ErrorCode.AUTHENTICATION)
        # Mensaje normalizado: sin distinguir existe/no-existe.
        self.assertEqual(ctx1.exception.safe_message(),
                         ctx2.exception.safe_message())

    def test_login_rate_limit_per_username(self):
        from backend.errors import AppError, ErrorCode
        app = make_app()
        signup_direct(app, "ada", _good(8))
        last = None
        for i in range(7):
            try:
                login_direct(app, "ada", OTHER_PW,
                             ip=f"10.1.0.{i}")
            except AppError as exc:
                last = exc
        self.assertIsNotNone(last)
        self.assertEqual(last.code, ErrorCode.RATE_LIMITED)
        self.assertEqual(last.http_status(), 429)

    def test_login_rate_limit_per_ip(self):
        from backend.errors import AppError, ErrorCode
        app = make_app()
        for i in range(10):
            signup_direct(app, f"user{i:02d}", _good(9 + i),
                          ip="10.9.9.9")
        last = None
        for i in range(12):
            try:
                login_direct(app, f"user{i:02d}" if i < 10 else "nadie",
                             OTHER_PW, ip="10.9.9.9")
            except AppError as exc:
                last = exc
        self.assertIsNotNone(last)
        self.assertEqual(last.code, ErrorCode.RATE_LIMITED)


class LogoutTest(unittest.TestCase):
    def test_logout_idempotent_and_me_fails_after(self):
        from backend.errors import AppError, ErrorCode
        app = make_app()
        signup_direct(app, "ada", _good(20))
        token = login_direct(app, "ada", _good(20))[1]["user_session"]
        self.assertEqual(app.logout_user(token)[0], 200)
        self.assertEqual(app.logout_user(token)[0], 200)  # idempotente
        with self.assertRaises(AppError) as ctx:
            app.me_user(token)
        self.assertEqual(ctx.exception.code, ErrorCode.AUTHENTICATION)
        self.assertEqual(app.logout_user("tok-inexistente")[0], 200)

    def test_me_without_session_is_401(self):
        from backend.errors import AppError, ErrorCode
        app = make_app()
        for bad in ("", "tok-inexistente"):
            with self.assertRaises(AppError) as ctx:
                app.me_user(bad)
            self.assertEqual(ctx.exception.code, ErrorCode.AUTHENTICATION)


class AdoptOnSignupLoginTest(unittest.TestCase):
    def _seed_installation(self, app):
        from backend.auth import sign_installation_secret
        from backend.kernel import Account, Provider
        from backend.ports import Installation, TokenPair
        import time as _time
        installation_id = "inst-adopt-1"
        secret = "fk-install-secret-adopt"
        app.installations.create(
            Installation(id=installation_id,
                         created_at="2026-10-26T00:00:00Z"), secret)
        ts = int(_time.time())
        nonce = "n" * 32
        sig = sign_installation_secret(secret, installation_id, ts,
                                       nonce)
        token = app.auth.create_session(installation_id, ts, nonce,
                                        sig)["session_token"]
        app.connections.save(installation_id, "youtube",
                             {"account": {"provider_user_id": "UC9z"}})
        account = Account(provider=Provider.YOUTUBE,
                          provider_user_id="UC9z",
                          display_name="Canal 9z")
        pair = TokenPair("fk-acc-9z", "fk-ref-9z", 3600, "s1")
        app.tokens.save(account, pair)
        return installation_id, token, secret, account, pair

    def test_signup_adopts_calling_installation(self):
        from backend.stores import (InMemoryConnectionStore,
                                    InMemoryTokenStore)
        app = make_app(connections=InMemoryConnectionStore(),
                       tokens=InMemoryTokenStore())
        iid, _, secret, account, pair = self._seed_installation(app)
        status, created = signup_direct(app, "ada", _good(21),
                                        installation_id=iid)
        self.assertEqual(status, 201)
        self.assertEqual(app.installations.get_user_id(iid),
                         created["user_id"])
        # Sin regenerar backendInstall: mismo secreto.
        self.assertEqual(app.installations.load(iid)[1], secret)
        entry = app.connections.load(iid, "youtube")
        self.assertEqual(entry["user_id"], created["user_id"])
        self.assertEqual(app.tokens.load(account), pair)
        self.assertEqual(app.tokens.owner_of("youtube", "UC9z"),
                         created["user_id"])

    def test_login_adopts_calling_installation(self):
        from backend.stores import (InMemoryConnectionStore,
                                    InMemoryTokenStore)
        app = make_app(connections=InMemoryConnectionStore(),
                       tokens=InMemoryTokenStore())
        iid, _, _, _, _ = self._seed_installation(app)
        created = signup_direct(app, "ada", _good(22),
                                ip="10.2.0.1")[1]
        # Segunda instalación huérfana adoptada en el login.
        from backend.ports import Installation
        app.installations.create(
            Installation(id="inst-adopt-2",
                         created_at="2026-10-26T00:00:00Z"),
            "fk-install-secret-adopt2")
        status, payload = login_direct(app, "ada", _good(22),
                                       ip="10.2.0.2",
                                       installation_id="inst-adopt-2")
        self.assertEqual(status, 200)
        self.assertEqual(app.installations.get_user_id("inst-adopt-2"),
                         created["user_id"])
        self.assertIn("user_session", payload)


class SignupPageTest(unittest.TestCase):
    def test_page_warns_no_email(self):
        app = make_app()
        html = app.signup_page()
        lowered = html.lower()
        self.assertIn("sin email", lowered)
        self.assertIn("sin verificaci", lowered)
        self.assertIn("recuperaci", lowered)
        self.assertIn("<form", lowered)
        self.assertIn("username", lowered)
        self.assertIn("password", lowered)
        # La página jamás porta secretos ni endpoints internos.
        self.assertNotIn("password_hash", html)

    def test_page_served_as_html_over_http(self):
        import json as _json
        settings_app = make_app()
        from backend.http_server import serve
        server = serve(settings_app)
        port = server.server_address[1]
        thread = threading.Thread(target=server.serve_forever,
                                  daemon=True)
        thread.start()
        self.addCleanup(server.shutdown)
        self.addCleanup(server.server_close)
        base = f"http://127.0.0.1:{port}"
        req = urllib.request.Request(base + "/users/signup", method="GET")
        with urllib.request.urlopen(req, timeout=5) as resp:
            self.assertEqual(resp.status, 200)
            ctype = resp.headers.get("Content-Type", "")
            body = resp.read().decode("utf-8")
        self.assertIn("text/html", ctype)
        self.assertIn("sin email", body.lower())

    def test_http_signup_login_me_logout_roundtrip(self):
        settings_app = make_app()
        from backend.http_server import serve
        server = serve(settings_app)
        port = server.server_address[1]
        thread = threading.Thread(target=server.serve_forever,
                                  daemon=True)
        thread.start()
        self.addCleanup(server.shutdown)
        self.addCleanup(server.server_close)
        base = f"http://127.0.0.1:{port}"

        def _post(path, payload, headers=None):
            data = json.dumps(payload).encode("utf-8")
            req = urllib.request.Request(
                base + path, data=data, method="POST",
                headers={"Content-Type": "application/json",
                         **(headers or {})})
            try:
                with urllib.request.urlopen(req, timeout=5) as resp:
                    return (resp.status, _json_loads(resp.read()))
            except urllib.error.HTTPError as exc:
                return (exc.code, _json_loads(exc.read()))

        def _json_loads(raw):
            return json.loads(raw.decode("utf-8"))

        def _get(path, headers=None):
            req = urllib.request.Request(base + path, method="GET",
                                         headers=headers or {})
            try:
                with urllib.request.urlopen(req, timeout=5) as resp:
                    return (resp.status, _json_loads(resp.read()))
            except urllib.error.HTTPError as exc:
                return (exc.code, _json_loads(exc.read()))

        status, created = _post("/users/signup",
                                {"username": "httpada",
                                 "password": _good(30)})
        self.assertEqual(status, 201, created)
        status, dup = _post("/users/signup",
                            {"username": "httpada",
                             "password": _good(31)})
        self.assertEqual(status, 409, dup)
        status, denied = _post("/users/login",
                               {"username": "httpada",
                                "password": OTHER_PW})
        self.assertEqual(status, 401, denied)
        status, logged = _post("/users/login",
                               {"username": "httpada",
                                "password": _good(30)})
        self.assertEqual(status, 200, logged)
        ustr = logged["user_session"]
        status, me = _get("/users/me",
                           {"X-User-Session": ustr})
        self.assertEqual(status, 200, me)
        self.assertEqual(me["username"], "httpada")
        status, nome = _get("/users/me")
        self.assertEqual(status, 401, nome)
        status, _ = _post("/users/logout", {},
                           {"X-User-Session": ustr})
        self.assertEqual(status, 200)
        status, gone = _get("/users/me",
                             {"X-User-Session": ustr})
        self.assertEqual(status, 401, gone)


class UsersLeakGuardTest(unittest.TestCase):
    def test_password_and_hash_never_logged(self):
        from backend.logging_setup import RedactingFilter
        from backend.users import hash_password
        good = _good(40)
        stored = hash_password(good)
        logger = logging.getLogger("users-endpoints-leak-guard-test")
        stream = io.StringIO()
        handler = logging.StreamHandler(stream)
        logger.addHandler(handler)
        logger.addFilter(RedactingFilter())
        try:
            line = "login attempt user=ada " + "pass" + "word=" + good
            logger.warning("%s", line, extra={"requestId": "r1"})
            hash_line = "stored " + "pass" + "word" + "_hash=" + stored
            logger.warning("%s", hash_line, extra={"requestId": "r1"})
            token_line = "issued " + "user" + "_session=" + "tok-fk-1"
            logger.warning("%s", token_line, extra={"requestId": "r1"})
        finally:
            logger.removeHandler(handler)
        output = stream.getvalue()
        self.assertNotIn(good, output)
        self.assertNotIn(stored, output)
        self.assertNotIn("tok-fk-1", output)


if __name__ == "__main__":
    unittest.main()
