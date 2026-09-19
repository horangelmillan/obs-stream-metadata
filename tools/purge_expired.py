"""Purga manual F-C4 (T-065): ejecuta la purga contra DATABASE_URL.

Uso (operador; para pruebas usar DB de prueba, nunca prod directamente):

    $env:STREAM_META_BACKEND_DATABASE_URL = 'postgresql://...'
    python tools/purge_expired.py

Idempotente y re-ejecutable: solo borra sesiones expiradas/revocadas y
transacciones expiradas/consumidas (filas que el backend ya rechaza).
Solo imprime conteos por tabla/estado (jamas valores, tokens ni verifiers).
El job programado (Scheduler -> POST /ops/purge) ya cubre produccion;
este script es para ejecuciones manuales y verificacion. Salida 0 = ok,
2 = error de configuracion/entorno.
"""
from __future__ import annotations

import os
import sys

# El script se ejecuta como `python tools/purge_expired.py` (sys.path[0]
# seria tools/): anadir la raiz del repo para importar `backend`.
sys.path.insert(0, os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))


def _fail(message: str) -> int:
    print(f"purge: {message}", file=sys.stderr)
    return 2


def main(argv=None) -> int:
    del argv
    url = os.environ.get("STREAM_META_BACKEND_DATABASE_URL", "").strip()
    if not url:
        return _fail("STREAM_META_BACKEND_DATABASE_URL ausente")

    from backend import maintenance as _maintenance
    from backend.db import PgPool, validate_database_url
    from backend.pgstores import (PgOAuthTransactionStore, PgSessionStore)
    try:
        validate_database_url(url)
    except Exception as exc:  # noqa: BLE001 — frontera CLI: mensaje + codigo
        return _fail(f"configuracion invalida: {exc}")

    import psycopg
    try:
        with psycopg.connect(url, connect_timeout=10) as conn:
            with conn.cursor() as cur:
                cur.execute("SELECT version FROM schema_migrations")
                cur.fetchall()
    except Exception as exc:  # noqa: BLE001 — sin esquema no hay purga segura
        return _fail(f"esquema no disponible (despliegue incompleto?): {exc}")

    pool = PgPool(url, max_size=2, acquire_timeout_s=10)
    try:
        result = _maintenance.purge_expired(PgSessionStore(pool),
                                            PgOAuthTransactionStore(pool))
    finally:
        pool.close()
    print(f"purge: sessions expired={result['sessions']['expired']} "
          f"revoked={result['sessions']['revoked']} "
          f"transactions expired={result['transactions']['expired']} "
          f"consumed={result['transactions']['consumed']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
