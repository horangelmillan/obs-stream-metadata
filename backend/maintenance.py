"""Mantenimiento programado F-C4 (T-065): purga de filas inutilizables.

Borra sesiones expiradas/revocadas y transacciones expiradas/consumidas.
Idempotente y re-ejecutable: solo toca filas que `validate_session`,
`consume` y `find_by_state` ya rechazan. Jamás toca installations,
connections ni tokens (viven hasta Disconnect/erase, F-C2).
Solo conteos en salida y logs: jamás valores, tokens ni verifiers.
"""
from __future__ import annotations

import time as _time


def purge_expired(sessions, transactions, clock=None) -> dict:
    """Ejecuta una pasada de purga. Devuelve conteos por tabla/estado."""
    now = (clock or _time.time)()
    sess = sessions.purge_expired(now)
    txns = transactions.purge_expired(now)
    return {"sessions": dict(sess), "transactions": dict(txns)}
