"""Tests error model: mapeo HTTP, mensajes seguros, sin fugas en respuestas."""
import unittest

from backend.errors import AppError, ErrorCode


class ErrorsTest(unittest.TestCase):
    def test_http_mapping(self):
        self.assertEqual(AppError(ErrorCode.INVALID_REQUEST).http_status(), 400)
        self.assertEqual(AppError(ErrorCode.AUTHENTICATION).http_status(), 401)
        self.assertEqual(AppError(ErrorCode.SESSION_EXPIRED).http_status(), 401)
        self.assertEqual(AppError(ErrorCode.AUTHORIZATION).http_status(), 403)
        self.assertEqual(AppError(ErrorCode.PROVIDER_RATE_LIMITED).http_status(), 429)
        self.assertEqual(AppError(ErrorCode.PROVIDER_REJECTED).http_status(), 502)
        self.assertEqual(AppError(ErrorCode.PROVIDER_UNAVAILABLE).http_status(), 502)
        self.assertEqual(AppError(ErrorCode.INTERNAL).http_status(), 500)

    def test_public_body_never_leaks_detail(self):
        canary = "sk-canary-C0NFlDENT1AL"
        body = AppError(ErrorCode.INTERNAL, canary).public_body("req1")
        text = repr(body)
        self.assertNotIn(canary, text)
        self.assertNotIn("Traceback", text)
        self.assertEqual(body["error"]["requestId"], "req1")
        self.assertEqual(body["error"]["code"], ErrorCode.INTERNAL.value)

    def test_safe_messages_stable(self):
        for code in ErrorCode:
            msg = AppError(code).safe_message()
            self.assertTrue(msg and msg == msg.strip())


if __name__ == "__main__":
    unittest.main()
