"""Guard de no-exposición: canarios sensibles jamás en respuestas ni logs."""
import io
import logging
import unittest

from backend.errors import AppError, ErrorCode
from backend.logging_setup import get_logger

CANARY = "sk-test-CANARY-9f8e7d6c5b4a"


class LeakGuardTest(unittest.TestCase):
    def test_error_detail_never_in_public_body(self):
        body = AppError(ErrorCode.PROVIDER_REJECTED,
                        f"exchange failed secret={CANARY}").public_body("r1")
        self.assertNotIn(CANARY, repr(body))

    def test_log_filter_redacts_secrets(self):
        logger = get_logger("leak-guard-test", "INFO")
        stream = io.StringIO()
        handler = logging.StreamHandler(stream)
        handler.setFormatter(logging.Formatter(
            "%(levelname)s requestId=%(requestId)s %(message)s"))
        logger.addHandler(handler)
        try:
            logger.warning("exchange failed client_secret=%s", CANARY,
                           extra={"requestId": "r1"})
        finally:
            logger.removeHandler(handler)
        output = stream.getvalue()
        self.assertNotIn(CANARY, output)
        self.assertIn("requestId=r1", output)


if __name__ == "__main__":
    unittest.main()
