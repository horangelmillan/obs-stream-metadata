"""Backfill F-C1 (T-063): cifra filas legacy de `tokens` en claro a `v1:`.

Uso (operador, nunca en CI):

    $env:STREAM_META_BACKEND_DATABASE_URL = 'postgresql://...'
    $env:STREAM_META_BACKEND_SECRET_DIR = '<dir-con-TOKEN_ENCRYPTION_KEY>'
    python tools/backfill_token_crypto.py --dry-run
    python tools/backfill_token_crypto.py --apply

Idempotente: las celdas que ya empiezan por `v1:` o estan vacias se omiten.
Solo imprime conteos por provider (jamas valores, jamas tokens).
Salida 0 = ok (o dry-run sin cambios pendientes si --apply), 2 = error de
configuracion/entorno.
"""
from __future__ import annotations

import argparse
import os
import sys


def _fail(message: str) -> int:
    print(f"backfill: {message}", file=sys.stderr)
    return 2


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Cifra tokens legacy en claro a formato v1 (F-C1).")
    parser.add_argument("--apply", action="store_true",
                        help="escribe cambios (por defecto: dry-run)")
    parser.add_argument("--batch-size", type=int, default=500)
    args = parser.parse_args(argv)

    url = os.environ.get("STREAM_META_BACKEND_DATABASE_URL", "").strip()
    if not url:
        return _fail("STREAM_META_BACKEND_DATABASE_URL ausente")
    secret_dir = os.environ.get("STREAM_META_BACKEND_SECRET_DIR", "")
    secret_dirs = os.environ.get("STREAM_META_BACKEND_SECRET_DIRS", "")
    if secret_dirs and secret_dir:
        return _fail("SECRET_DIRS y SECRET_DIR a la vez (ambiguo)")

    from backend.db import validate_database_url
    from backend.prodstores import (CompositeSecretStore, FileSecretStore,
                                    split_secret_dirs)
    from backend.token_crypto import TokenCipher, needs_upgrade
    try:
        validate_database_url(url)
        dirs = split_secret_dirs(secret_dirs) or ([secret_dir]
                                                   if secret_dir else [])
        if not dirs:
            return _fail("SECRET_DIR(S) ausente "
                         "(montaje con TOKEN_ENCRYPTION_KEY)")
        secrets = CompositeSecretStore(FileSecretStore(path) for path in dirs)
        cipher = TokenCipher.from_secret_store(secrets)
    except Exception as exc:  # noqa: BLE001 — frontera CLI: mensaje + codigo
        return _fail(f"configuracion invalida: {exc}")

    import psycopg
    converted = already = empty = 0
    per_provider: dict[str, int] = {}
    with psycopg.connect(url, connect_timeout=10) as conn:
        with conn.cursor() as cur:
            cur.execute("SELECT provider, provider_user_id, access_token, "
                        "refresh_token FROM tokens")
            rows = cur.fetchall()
        for provider, user_id, access, refresh in rows:
            need_access = needs_upgrade(access or "")
            need_refresh = needs_upgrade(refresh or "")
            if not need_access and not need_refresh:
                if not access and not refresh:
                    empty += 1
                else:
                    already += 1
                continue
            new_access = cipher.encrypt(access) if need_access else access
            new_refresh = cipher.encrypt(refresh) if need_refresh else refresh
            if args.apply:
                with conn.cursor() as cur:
                    cur.execute(
                        "UPDATE tokens SET access_token=%s, "
                        "refresh_token=%s WHERE provider=%s AND "
                        "provider_user_id=%s",
                        (new_access, new_refresh, provider, user_id))
                conn.commit()
            converted += 1
            per_provider[provider] = per_provider.get(provider, 0) + 1
    mode = "apply" if args.apply else "dry-run"
    print(f"backfill [{mode}]: filas={len(rows)} converted={converted} "
          f"already={already} empty={empty}")
    for provider in sorted(per_provider):
        print(f"backfill [{mode}]: provider={provider} "
              f"converted={per_provider[provider]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
