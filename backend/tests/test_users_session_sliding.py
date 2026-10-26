"""Tests sesión Managed deslizante 15d + tope absoluto 30d ([D] 2026-10-26).

Contrato:
- login emite `expires_at = login + 15d` y fija `login_at` (origen absoluto).
- cada actividad válida (`/users/me`, gate Managed) renueva a +15d sin
  pasar `login_at + 30d`.
- 15d sin actividad → SESSION_EXPIRED (401-expired); login de nuevo.
- 30d desde el login aunque haya actividad → SESSION_EXPIRED; el tope
  jamás se estira con renovaciones; login de nuevo emite otro ciclo.
- sesiones heredadas sin `login_at` toman `issued_at` como origen.

Reloj inyectado vía `app._clock` (mismo atributo que usa BackendApp;
sin tocar producción ni secretos; sintéticos `fk-*`).
"""
import unittest

DAY = 24 * 3600

TTL = 15 * DAY
MAX_TTL = 30 * DAY

GOOD_PW = "sliding-horse-1-v1"


class FakeClock:
    def __init__(self, now=1_000_000.0):
        self.now = float(now)

    def __call__(self):
        return self.now


def make_app(clock):
    from backend.app import create_app
    from backend.config import Settings
    settings = Settings(host="127.0.0.1", port=0,
                        public_base_url="http://127.0.0.1:0")
    app = create_app(settings=settings)
    app._clock = clock
    return app


def _signup_login(app, username="ada"):
    app.signup_user({"username": username, "password": GOOD_PW},
                    client_ip="10.0.0.1")
    status, payload = app.login_user(
        {"username": username, "password": GOOD_PW},
        client_ip="10.0.0.1")
    assert status == 200
    return payload["user_session"]


class SlidingSessionTest(unittest.TestCase):
    def test_login_sets_15d_expiry_and_absolute_origin(self):
        clock = FakeClock()
        app = make_app(clock)
        token = _signup_login(app)
        payload = app.user_sessions.load_session(token)
        self.assertEqual(payload["expires_at"], clock.now + TTL)
        self.assertEqual(payload["login_at"], clock.now)

    def test_activity_renews_sliding_window(self):
        from backend.errors import AppError
        clock = FakeClock()
        app = make_app(clock)
        token = _signup_login(app)
        original_expiry = clock.now + TTL
        # 5d después sigue válida y la expiración avanza (deslizante).
        clock.now += 5 * DAY
        status, _ = app.me_user(token)
        self.assertEqual(status, 200)
        renewed = app.user_sessions.load_session(token)["expires_at"]
        self.assertEqual(renewed, clock.now + TTL)
        self.assertGreater(renewed, original_expiry)
        # Sin la renovación habría caído en el día 15+1 desde el login.
        clock.now += 10 * DAY + 1  # día 15+1 desde el login original
        try:
            app.me_user(token)
        except AppError as exc:
            self.fail(f"sesión renovada no debió expirar: {exc}")
        # Pero 15d sin actividad desde la última sí expiran.
        clock.now += TTL + 1
        from backend.errors import ErrorCode
        with self.assertRaises(AppError) as ctx:
            app.me_user(token)
        self.assertEqual(ctx.exception.code, ErrorCode.SESSION_EXPIRED)

    def test_inactivity_expiry_requires_login(self):
        from backend.errors import AppError, ErrorCode
        clock = FakeClock()
        app = make_app(clock)
        token = _signup_login(app)
        clock.now += TTL + 1
        with self.assertRaises(AppError) as ctx:
            app.me_user(token)
        self.assertEqual(ctx.exception.code, ErrorCode.SESSION_EXPIRED)
        self.assertEqual(ctx.exception.http_status(), 401)

    def test_absolute_cap_survives_continuous_activity(self):
        from backend.errors import AppError, ErrorCode
        clock = FakeClock()
        app = make_app(clock)
        token = _signup_login(app)
        login_at = clock.now
        # Actividad diaria durante 29 días: cada día renueva sin error.
        for _ in range(29):
            clock.now += DAY
            status, _ = app.me_user(token)
            self.assertEqual(status, 200)
        # Día 30 desde el login: tope absoluto, aunque hubo actividad.
        clock.now = login_at + MAX_TTL
        with self.assertRaises(AppError) as ctx:
            app.me_user(token)
        self.assertEqual(ctx.exception.code, ErrorCode.SESSION_EXPIRED)
        # Login de nuevo abre otro ciclo completo de 30 días.
        clock.now += 1
        status, payload = app.login_user(
            {"username": "ada", "password": GOOD_PW},
            client_ip="10.0.0.1")
        self.assertEqual(status, 200)
        fresh = app.user_sessions.load_session(
            payload["user_session"])
        self.assertEqual(fresh["login_at"], clock.now)
        self.assertEqual(fresh["expires_at"], clock.now + TTL)

    def test_renewal_never_passes_absolute_cap(self):
        clock = FakeClock()
        app = make_app(clock)
        token = _signup_login(app)
        login_at = clock.now
        # Actividad diaria hasta el día 29 (la sesión sigue viva).
        for _ in range(29):
            clock.now += DAY
            status, _ = app.me_user(token)
            self.assertEqual(status, 200)
        # Día 29: renovar daría +15d, pero se recorta al tope.
        self.assertEqual(status, 200)
        payload = app.user_sessions.load_session(token)
        self.assertEqual(payload["expires_at"], login_at + MAX_TTL)
        self.assertLess(payload["expires_at"], clock.now + TTL)

    def test_legacy_payload_without_login_at_uses_issued_at(self):
        from backend.errors import AppError, ErrorCode
        clock = FakeClock()
        app = make_app(clock)
        token = _signup_login(app)
        payload = app.user_sessions.load_session(token)
        del payload["login_at"]  # simula sesión anterior al cambio
        app.user_sessions.save_session(token, payload)
        # El origen absoluto es issued_at: a los 30d expira igual.
        clock.now = payload["issued_at"] + MAX_TTL
        with self.assertRaises(AppError) as ctx:
            app.me_user(token)
        self.assertEqual(ctx.exception.code, ErrorCode.SESSION_EXPIRED)

    def test_renewal_failure_does_not_reject_valid_session(self):
        clock = FakeClock()
        app = make_app(clock)
        token = _signup_login(app)

        def _boom(_sid, _payload):
            raise OSError("store caído")

        app.user_sessions.save_session = _boom
        clock.now += DAY
        status, me = app.me_user(token)  # válida: no se rechaza
        self.assertEqual(status, 200)
        self.assertEqual(me["username"], "ada")


if __name__ == "__main__":
    unittest.main()
