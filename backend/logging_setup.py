"""Logging con redacción obligatoria (T-043 §17).

Seguro: requestId, endpoint, status, duración, provider, código de error.
Jamás: secrets, tokens, codes, verifiers, cookies sensibles, credenciales.
"""
from __future__ import annotations

import logging

from backend.stores import redact_text

API_VERSION = "v1"
BACKEND_NAME = "obs-stream-metadata-backend"
BACKEND_VERSION = "0.1.0"


class RedactingFilter(logging.Filter):
    def filter(self, record: logging.LogRecord) -> bool:
        try:
            record.msg = redact_text(str(record.getMessage()))
            record.args = ()
        except Exception:
            record.msg = "[log-redaction-failed]"
            record.args = ()
        return True


def get_logger(name: str, level: str = "INFO") -> logging.Logger:
    logger = logging.getLogger(f"backend.{name}")
    if not logger.handlers:
        handler = logging.StreamHandler()
        handler.setFormatter(logging.Formatter(
            "%(asctime)s %(levelname)s %(name)s requestId=%(requestId)s %(message)s",
            defaults={"requestId": "-"}))
        logger.addHandler(handler)
    logger.setLevel(level.upper())
    for handler in logger.handlers:
        handler.addFilter(RedactingFilter())
    logger.addFilter(RedactingFilter())
    logger.propagate = False
    return logger
