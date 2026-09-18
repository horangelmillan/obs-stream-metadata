"""Borrado total por instalación, F-C2 (T-064).

Orden F-030: DELETEs locales primero (con los tokens copiados en memoria
vía `ConnectService.erase_local`), revoke remoto best-effort al final: un
fallo de red jamás resucita estado y la garantía de borrado nunca depende
de ella. Sin secretos en respuestas ni logs: solo conteos por provider.
"""
from __future__ import annotations

from backend.errors import AppError


def erase_installation(installation_id: str, services: dict, sessions,
                       transactions, installations) -> dict:
    """Borra connections + tokens no compartidos + sessions + transactions
    + fila installation. Idempotente: instalación desconocida → ceros."""
    collected: dict[str, tuple] = {}
    conns = toks = 0
    for name, service in services.items():
        entry, tokens, row_deleted = service.erase_local(installation_id)
        if entry is not None:
            conns += 1
        if row_deleted:
            toks += 1
        collected[name] = (service, tokens)
    sess_n = sessions.delete_for_installation(installation_id)
    txn_n = transactions.delete_for_installation(installation_id)
    had_installation = installations.load(installation_id) is not None
    installations.delete(installation_id)
    revoked: dict[str, bool] = {}
    for name, (service, tokens) in collected.items():
        ok = True
        if tokens is not None:
            for token in (tokens.access_token, tokens.refresh_token):
                if not token:
                    continue
                try:
                    service._provider.revoke(token)
                except AppError:
                    ok = False  # best-effort: el borrado local ya ocurrió
        revoked[name] = ok
    return {"erased": {"connections": conns, "tokens": toks,
                       "sessions": sess_n, "transactions": txn_n,
                       "installation": 1 if had_installation else 0},
            "revoked": revoked}
