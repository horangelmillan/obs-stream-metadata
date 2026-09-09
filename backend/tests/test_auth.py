"""Tests T-044: identidad, autenticación, sesiones, replay, rate-limit, abuso."""
import threading
import unittest

from backend.auth import (AuthService, CLOCK_SKEW_S, NONCE_WINDOW_S, SESSION_TTL_S,
                          sign_installation_secret)
from backend.errors import AppError, ErrorCode
from backend.ports import Installation
from backend.stores import (FixedWindowRateLimiter, InMemoryInstallationStore,
                            InMemorySessionStore)


class FakeClock:
    def __init__(self, now: float = 1_000_000.0) -> None:
        self.now = now

    def __call__(self) -> float:
        return self.now


def make_service(clock=None):
    clock = clock or FakeClock()
    return AuthService(InMemoryInstallationStore(), InMemorySessionStore(),
                       clock=clock), clock


def signed_params(creds, clock, nonce="n" * 32):
    ts = int(clock())
    sig = sign_installation_secret(creds["installation_secret"],
                                   creds["installation_id"], ts, nonce)
    return creds["installation_id"], ts, nonce, sig


class IdentityTest(unittest.TestCase):
    def test_bootstrap_issues_unique_credentials(self):
        svc, _ = make_service()
        a = svc.bootstrap()
        b = svc.bootstrap()
        self.assertNotEqual(a["installation_id"], b["installation_id"])
        self.assertNotEqual(a["installation_secret"], b["installation_secret"])
        # El secreto se entrega una vez: el store guarda tupla (installation, secret).
        self.assertIsNotNone(svc._installations.load(a["installation_id"]))

    def test_reinstall_yields_new_identity(self):
        svc, _ = make_service()
        first = svc.bootstrap()["installation_id"]
        svc.revoke_installation(*signed_params(
            {"installation_id": first,
             "installation_secret": svc._installations.load(first)[1]},
            FakeClock()))
        # Re-bootstrap tras revocar/borrar = identidad nueva, no recuperación.
        second = svc.bootstrap()["installation_id"]
        self.assertNotEqual(first, second)


class AuthenticationTest(unittest.TestCase):
    def test_valid_session_roundtrip(self):
        svc, clock = make_service()
        creds = svc.bootstrap()
        out = svc.create_session(*signed_params(creds, clock))
        self.assertEqual(out["expires_in"], SESSION_TTL_S)
        record = svc.validate_session(out["session_token"])
        self.assertEqual(record.installation_id, creds["installation_id"])

    def test_expired_session(self):
        svc, clock = make_service()
        creds = svc.bootstrap()
        token = svc.create_session(*signed_params(creds, clock))["session_token"]
        clock.now += SESSION_TTL_S + 1
        with self.assertRaises(AppError) as ctx:
            svc.validate_session(token)
        self.assertEqual(ctx.exception.code, ErrorCode.SESSION_EXPIRED)

    def test_revoked_session(self):
        svc, clock = make_service()
        creds = svc.bootstrap()
        token = svc.create_session(*signed_params(creds, clock))["session_token"]
        svc.revoke_session(token)
        with self.assertRaises(AppError) as ctx:
            svc.validate_session(token)
        self.assertEqual(ctx.exception.code, ErrorCode.AUTHENTICATION)
        svc.revoke_session(token)  # idempotente

    def test_invalid_token_and_bad_signature(self):
        svc, clock = make_service()
        creds = svc.bootstrap()
        with self.assertRaises(AppError):
            svc.validate_session("tok-aleatorio")
        iid, ts, nonce, _ = signed_params(creds, clock)
        with self.assertRaises(AppError):
            svc.create_session(iid, ts, nonce, "firma-invalida")

    def test_unknown_installation(self):
        svc, clock = make_service()
        with self.assertRaises(AppError):
            svc.create_session("id-inexistente", int(clock()), "n" * 32, "x" * 64)

    def test_stale_timestamp_rejected(self):
        svc, clock = make_service()
        creds = svc.bootstrap()
        iid, _, nonce, _ = signed_params(creds, clock)
        old = int(clock()) - (CLOCK_SKEW_S + NONCE_WINDOW_S + 60)
        sig = sign_installation_secret(creds["installation_secret"], iid, old, nonce)
        with self.assertRaises(AppError):
            svc.create_session(iid, old, nonce, sig)

    def test_nonce_reuse_rejected(self):
        svc, clock = make_service()
        creds = svc.bootstrap()
        params = signed_params(creds, clock)
        svc.create_session(*params)
        with self.assertRaises(AppError):
            svc.create_session(*params)  # mismo nonce → replay

    def test_refresh_rotates_and_mismatch_rejected(self):
        svc, clock = make_service()
        creds = svc.bootstrap()
        token = svc.create_session(*signed_params(creds, clock))["session_token"]
        out = svc.refresh_session(token, *signed_params(creds, clock, "m" * 32))
        self.assertNotEqual(out["session_token"], token)
        with self.assertRaises(AppError):  # anterior invalidada
            svc.validate_session(token)
        other = svc.bootstrap()
        with self.assertRaises(AppError):  # sesión de otra instalación
            svc.refresh_session(out["session_token"],
                                *signed_params(other, clock, "z" * 32))

    def test_concurrent_refresh_first_wins(self):
        svc, clock = make_service()
        creds = svc.bootstrap()
        token = svc.create_session(*signed_params(creds, clock))["session_token"]
        results, errors = [], []

        def attempt(nonce):
            try:
                results.append(svc.refresh_session(
                    token, *signed_params(creds, clock, nonce)))
            except AppError as exc:
                errors.append(exc.code)

        threads = [threading.Thread(target=attempt, args=(f"{i:032d}",))
                   for i in range(8)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(len(results), 1)
        self.assertEqual(len(errors), 7)
        svc.validate_session(results[0]["session_token"])

    def test_installation_revoke_blocks_sessions(self):
        svc, clock = make_service()
        creds = svc.bootstrap()
        token = svc.create_session(*signed_params(creds, clock))["session_token"]
        svc.revoke_installation(*signed_params(creds, clock, "r" * 32))
        with self.assertRaises(AppError):
            svc.validate_session(token)
        with self.assertRaises(AppError):
            svc.create_session(*signed_params(creds, clock, "q" * 32))


class RateLimitTest(unittest.TestCase):
    def test_limit_and_recovery(self):
        clock = FakeClock()
        limiter = FixedWindowRateLimiter(3, 60, clock=clock)
        key = "installation:x"
        self.assertTrue([limiter.allow(key) for _ in range(3)])
        self.assertFalse(limiter.allow(key))
        clock.now += 61
        self.assertTrue(limiter.allow(key))  # recuperación tras ventana

    def test_isolation_between_keys(self):
        clock = FakeClock()
        limiter = FixedWindowRateLimiter(1, 60, clock=clock)
        self.assertTrue(limiter.allow("a"))
        self.assertFalse(limiter.allow("a"))
        self.assertTrue(limiter.allow("b"))


class AbuseTest(unittest.TestCase):
    def test_brute_force_session_never_leaks(self):
        svc, _ = make_service()
        for guess in ("", "a", "0" * 64, "Bearer x", "tok\ninjection"):
            with self.assertRaises(AppError) as ctx:
                svc.validate_session(guess)
            self.assertNotIn("secret", repr(ctx.exception.public_body("r")))

    def test_installation_record_marks_revoked(self):
        store = InMemoryInstallationStore()
        store.create(Installation(id="i1"), "s")
        store.revoke("i1")
        installation, _ = store.load("i1")
        self.assertTrue(installation.revoked)


if __name__ == "__main__":
    unittest.main()
