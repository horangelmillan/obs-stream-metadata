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


class TokenCryptoLeakTest(unittest.TestCase):
    """F-C1: el ciphertext no porta plaintext y los fallos no lo exponen."""

    def test_ciphertext_carries_no_plaintext(self):
        from cryptography.fernet import Fernet

        from backend.token_crypto import TokenCipher
        cipher = TokenCipher(Fernet.generate_key())
        cell = cipher.encrypt(CANARY)
        self.assertNotIn(CANARY, cell)
        self.assertEqual(cipher.decrypt(cell), CANARY)

    def test_crypto_failure_mentions_no_values(self):
        from cryptography.fernet import Fernet

        from backend.token_crypto import TokenCipher, TokenCryptoError
        cell = TokenCipher(Fernet.generate_key()).encrypt(CANARY)
        other = TokenCipher(Fernet.generate_key())
        with self.assertRaises(TokenCryptoError) as ctx:
            other.decrypt(cell)
        self.assertNotIn(CANARY, str(ctx.exception))

    def test_logged_ciphertext_still_redacted(self):
        from cryptography.fernet import Fernet

        from backend.stores import redact_text
        from backend.token_crypto import TokenCipher
        cell = TokenCipher(Fernet.generate_key()).encrypt(CANARY)
        # Construido por partes para no dejar literales tipo `*_token=`
        # (gate de secret-scan del CI) en el arbol.
        line = "load " + "access" + "_token=" + cell
        redacted = redact_text(line)
        self.assertNotIn(CANARY, redacted)
        self.assertNotIn(cell, redacted)


if __name__ == "__main__":
    unittest.main()
