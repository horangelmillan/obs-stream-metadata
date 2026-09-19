# F-C4 Operativa comercial mínima Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Purga programada de sesiones/transacciones expiradas, backups Neon con restore probado y alertas Cloud Run + cuota YouTube, sin cambiar arquitectura ni coste.

**Architecture:** Nuevo `backend/maintenance.py` orquestador + métodos paritarios `purge_expired(now)` en los 6 stores (mismo patrón que F-C2 `delete_for_installation`) + endpoint `POST /ops/purge` con bearer de operador + Cloud Scheduler externo + `tools/purge_expired.py` para ejecución manual + 4 políticas de alerta como JSON versionados en `ops/monitoring/`.

**Tech Stack:** Python stdlib (sin deps nuevas), PostgreSQL portable (TEXT+JSON como hoy), pg_dump 18/pg_restore, Cloud Scheduler (job HTTP + header Bearer), Cloud Monitoring (métricas built-in + log-based), `hmac.compare_digest` para el bearer.

**Spec:** `docs/FASES-COMERCIAL.md` (F-C4), `docs/BACKLOG.md` T-065, `docs/PRIVACY.md` §8, `docs/DEPLOYMENT.md`, `docs/SECURITY.md`.

## Global Constraints

- Sin tocar OAuth/flujos/stores salvo lo estrictamente necesario (métodos `purge_expired` paritarios, patrón F-C2).
- Sin Redis, sin subir de tier Neon, sin tracking de cuota en código, sin JWT/OIDC en la app.
- Sin secretos en código/logs/respuestas; fakes `fk-*` en tests; secret-scan CI debe seguir verde (patrón: `(client_secret|refresh_token|access_token)\s*=\s*["\w.-]{8,}` más `ghp_/sk-/AIza/PEM` — el nombre `OPS_PURGE_TOKEN` leído vía `secrets.get()` no matchea; no asignar valores largos con esos nombres).
- Commits en español, sin commit sin autorización del usuario.
- TDD: test rojo primero, nunca reverse-engineer código verde.
- `VACUUM FULL` prohibido en producción (lock exclusivo); autovacuum basta a este volumen.

---

## Decisión documentada (2.3, vinculante para las tasks)

### Qué se purga

- `sessions`: filas con `expires_at <= now` (TTL 1800 s) O `revoked=true`. Ambas están inutilizables por `AuthService.validate_session` (`backend/auth.py:134-146`: revoked → 401, expirada → 401 SESSION_EXPIRED, nada las reanima). Seguro borrarlas.
- `transactions`: filas con `expires_at <= now` (TTL 600 s, `backend/oauth.py:20`) O `consumed=true`. `consume()`/`find_by_state()` ya rechazan consumidas/expiradas (`backend/stores.py:164-181`). Seguro borrarlas.
- NO se purga: `installations`, `connections`, `tokens` (persisten hasta Disconnect/erase por diseño F-C2 y `PRIVACY.md` §8; purgarlas rompería cuentas conectadas y la semántica de `erase` compartido). Nonces: memoria con poda oportunista (`backend/auth.py:110-118`), nada que hacer.

### Cadencia y mecanismo

- Diaria `0 3 * * *` (zona a fijar por el operador, p. ej. `America/New_York` junto a `us-east5`), `attempt_deadline=120s`, reintentos acotados (`max-retry-attempts=3`, backoff por defecto). Volumen esperado: decenas de filas/día; la purga tarda ms.
- Mecanismo: `POST /ops/purge` (nuevo) invocado por Cloud Scheduler con header `Authorization: Bearer <OPS-token>`. Job idempotente y re-ejecutable (re-ejecutar devuelve ceros).
- Auth del endpoint: bearer de operador `OPS_PURGE_TOKEN` leído de `SecretStore` (fichero Secret Manager, mismo patrón que `GOOGLE_CLIENT_ID`). Sin secreto configurado → `500 INTERNAL` con detail `purge not configured` solo en logs (fail-closed, nunca bypass en dev; el modelo de errores no tiene 501). Comparación con `hmac.compare_digest`. Rate-limit dedicado `ops: 10/min` (FixedWindow en memoria, coherente con max 1 instancia).
- Alternativas descartadas (con razón):
  - Purga en arranque: con `min 0` los arranques son impredecibles (cold starts) y el arranque ya hace migrations; no es "programada" ni observable.
  - OIDC/validación JWT en la app: stdlib no tiene RSA/JWKS; implementarlo bien exige fetch+cache de certs Google + validación aud/exp/iss = superficie de seguridad nueva e injustificada para un job diario. Reevaluar solo si el servicio pasa a "Require authentication".
  - Limiter multi-instancia/Redis: diferido explícito en FASES-COMERCIAL; F-C4 no lo autoriza. La purga no necesita coordinación distribuida (idempotente; con max 1 instancia no hay carreras).
  - Contador de cuota YouTube en el backend: exigiría almacenamiento nuevo + sincronización con la Console; fuera de alcance. Se alerta vía Console (80 % del cupo) + log-based metric sobre rechazos.

### Retención efectiva resultante (actualizar `PRIVACY.md` §8)

- Sessions: acceso 30 min; borrado físico en purga diaria → retención efectiva ≤ 30 min + 24 h (~25 h). Revocadas se borran en la siguiente purga aunque no hayan expirado.
- Transactions (incl. `code_verifier`): acceso 600 s + un solo uso; borrado físico en purga diaria → retención efectiva ≤ ~24 h tras expirar/consumirse.
- Connections/tokens/installations: sin cambios (hasta Disconnect/erase).
- Consistente con `PRIVACY.md`: la frase "no background eraser is implemented" se sustituye por la purga diaria descrita; el resto de compromisos (borrado por instalación, contacto, logs sin IDs) intactos.

### Qué se alerta (umbrales iniciales, el operador los ajusta tras 2 semanas de baseline)

- A1 5xx Cloud Run: `run.googleapis.com/request_count` con `response_code_class=5xx`, conteo > 3 en 5 min (servicio de tráfico casi cero: cualquier 5xx sostenido es anómalo). Duración 300 s.
- A2 latencia p99: `run.googleapis.com/request_latencies` `ALIGN_PERCENTILE_99` > 5000 ms durante 300 s (timeout del servicio 300 s; 5 s ya es degradación para OAuth interactivo).
- A3 rechazos YouTube/cuota: log-based metric `yt-quota-rejects` sobre `textPayload:"detail=google:rate"` (el backend ya emite `request error code=provider_rate_limited detail=google:rate` vía `classify_broadcast_error`, `backend/adapters/youtube.py:144-147`); alerta si > 0 en 5 min. Complemento manual: alerta al 80 % del cupo diario (10 000 u) en API Console → Quotas (pasos en DEPLOYMENT, sin API).
- A4 uptime `/health`: uptime check 5 min (falla si el contenedor no responde). `/ready` hoy es `(True,"ok")` por defecto también en prod (no verifica DB): se documenta como limitación conocida; la A1/A4 la cubren indirectamente. NO se cambia el wiring en F-C4.
- Canales: email del operador (`horangelmillan@gmail.com`, ya contacto privacy) como mínimo; Slack/PagerDuty opcional.

### Qué queda manual

- Backups: `pg_dump -Fc` manual antes de cada cambio de esquema + snapshot manual Neon (Free: 1 manual, history 6 h/1 GB — RPO honesto: último dump o 6 h; RTO: restore manual, minutos-horas; sin SLA en Free). Sin scheduled snapshots (requieren Launch/Scale: subir de tier = decisión del operador, no de F-C4).
- Creación del job Scheduler + políticas de alerta en GCP: las ejecuta el operador con los comandos/JSON de este plan (el agente no tiene credenciales GCP). La "alerta recibida" queda como pendiente explícito del operador; el agente aporta la prueba local de señales.
- `VACUUM`: nada (autovacuum por defecto absorbe decenas de filas/día; `pg_restore` reconstruye índices sin bloat). Solo si un día hay deletes masivos: `VACUUM (VERBOSE)` manual, jamás `VACUUM FULL` en caliente.

### Fuentes externas (consultadas 2026-09-19)

- Scheduler→Cloud Run + OIDC: https://docs.cloud.google.com/run/docs/triggering/using-scheduler ; auth HTTP targets: https://docs.cloud.google.com/scheduler/docs/http-target-auth ; CLI: https://docs.cloud.google.com/sdk/gcloud/reference/scheduler/jobs/create/http
- Monitoring Cloud Run (request_count/request_latencies, SLO burn-rate): https://docs.cloud.google.cn/run/docs/monitoring ; alertas ejemplo Terraform 5xx+p99: https://cloudwebschool.com/docs/gcp/monitoring-and-observability/creating-alerts/ (2026-03-09) ; log-based metrics+alerting: https://cloud.google.com/logging/docs/alerting/monitoring-logs
- Neon pricing (Free: 6 h history/1 GB, 1 snapshot manual, 0.5 GB, sin scheduled): https://neon.com/pricing ; history window: https://neon.com/docs/introduction/history-window ; pg_dump (conexión directa, NO pooled): https://neon.com/docs/manage/backup-pg-dump ; estrategias: https://neon.com/docs/manage/backups ; snapshots Backup&Restore: https://neon.com/docs/guides/backup-restore
- YouTube quota (10 000 u/día default, reset medianoche PT, costes por método): https://developers.google.com/youtube/v3/determine_quota_cost ; update: https://developers.google.com/youtube/v3/live/docs/liveBroadcasts/update ; list: https://developers.google.com/youtube/v3/live/docs/liveBroadcasts/list ; errores (rateLimitExceeded/quotaExceeded): https://developers.google.com/youtube/v3/live/docs/errors
- Postgres VACUUM/bloat/autovacuum: https://www.postgresql.org/docs/19/routine-vacuuming.html ; tuning 2026: https://minervadb.com/postgresql-18-vacuum-tuning/ (2026-08-28), https://postgresqlhtx.com/postgres-vacuum-tuning-stop-table-bloat-from-slowing-queries/ (2026-09-08) ; pg_dump lógico sin bloat: https://essencesolusoft.com/why-is-pg_dump-smaller-than-database-size-postgresql-index-bloat-explained/ (2026-08-13)

---

## File Structure

- Modify: `backend/ports.py` — añade `purge_expired(now) -> dict` a `SessionStore` y `OAuthTransactionStore` (contrato + docstring, sin cambiar métodos existentes).
- Modify: `backend/stores.py` — implementa `purge_expired` en `InMemorySessionStore`, `InMemoryOAuthTransactionStore`.
- Modify: `backend/prodstores.py` — implementa `purge_expired` en `SqliteSessionStore`, `SqliteOAuthTransactionStore`.
- Modify: `backend/pgstores.py` — implementa `purge_expired` en `PgSessionStore`, `PgOAuthTransactionStore` (SELECT + filtro Python como `delete_for_installation`, `payload_json`/`entry_json` son TEXT).
- Create: `backend/maintenance.py` — `purge_expired(sessions, transactions, clock=None) -> {"sessions": {...}, "transactions": {...}}` (solo conteos en salida, jamás valores).
- Create: `backend/tests/test_maintenance.py` — TDD InMemory + Sqlite (+PG con skip si no hay servidor, patrón `test_pg.py:fresh_db`).
- Modify: `backend/http_server.py` — `POST /ops/purge`: bearer operador + rate-limit `ops` + log de conteos + `501` si no configurado. `BackendApp.__init__` acepta `secrets=None` y expone `ops_purge_token()`; `limiters` suma `"ops": FixedWindowRateLimiter(10, 60)`.
- Modify: `backend/app.py` — `create_app` pasa `secrets` al `BackendApp` (una línea + wiring, sin tocar providers/auth).
- Create: `backend/tests/test_ops_purge.py` — endpoint: 401 sin bearer, 401 bearer erróneo, 501 sin secreto, 200 con conteos, rate-limit 429.
- Create: `tools/purge_expired.py` — CLI operador contra `DATABASE_URL` (patrón `tools/backfill_token_crypto.py`: exit 0/2, solo conteos).
- Create: `ops/monitoring/alert-5xx.json`, `alert-p99.json`, `alert-yt-quota.json`, `uptime-health.json` + `ops/monitoring/README.md` (comandos `gcloud` de canal + creación, filtros exactos).
- Modify: `docs/DEPLOYMENT.md` — § purga programada (comandos Scheduler exactos) + § backup/restore probado paso a paso + § alertas exactas.
- Modify: `docs/PRIVACY.md` §8 — retención con purga diaria (sustituye "no background eraser").
- Modify: `docs/BACKLOG.md` — T-065 en-progreso → hecha al cerrar (sin commit sin autorización).

---

### Task 1: `purge_expired` en ports + stores InMemory (TDD)

**Files:**
- Modify: `backend/ports.py` (añadir 2 métodos abstractos con docstring)
- Modify: `backend/stores.py` (2 implementaciones)
- Test: `backend/tests/test_maintenance.py` (nuevo, clases InMemory)

**Interfaces:**
- Consumes: `SessionRecord` dicts con `expires_at: float`, `revoked: bool`; transaction dicts con `expires_at: float`, `consumed: bool`.
- Produces: `purge_expired(now: float) -> {"expired": int, "revoked": int}` (sessions) y `-> {"expired": int, "consumed": int}` (transactions). Claves disjuntas: `expired` = `expires_at <= now` (gane o no el otro flag); `revoked`/`consumed` = flag activo con `expires_at > now`.

- [ ] **Step 1: Write the failing test** (`backend/tests/test_maintenance.py`):

```python
"""Purga F-C4 (T-065): solo filas inutilizables; vigentes intactas."""
import unittest

from backend.stores import InMemoryOAuthTransactionStore, InMemorySessionStore


class InMemorySessionPurgeTest(unittest.TestCase):
    def test_purge_expired_and_revoked_keeps_live(self):
        store = InMemorySessionStore()
        store.save_session("live", {"installation_id": "i",
                                    "expires_at": 2000.0, "revoked": False})
        store.save_session("old", {"installation_id": "i",
                                   "expires_at": 1000.0, "revoked": False})
        store.save_session("gone", {"installation_id": "i",
                                    "expires_at": 2000.0, "revoked": True})
        store.save_session("both", {"installation_id": "i",
                                    "expires_at": 1000.0, "revoked": True})
        out = store.purge_expired(1500.0)
        self.assertEqual(out, {"expired": 2, "revoked": 1})
        self.assertIsNotNone(store.load_session("live"))
        self.assertIsNone(store.load_session("old"))
        # Re-ejecutable: segunda pasada devuelve ceros.
        self.assertEqual(store.purge_expired(1500.0),
                         {"expired": 0, "revoked": 0})


class InMemoryTransactionPurgeTest(unittest.TestCase):
    def test_purge_expired_and_consumed_keeps_live(self):
        store = InMemoryOAuthTransactionStore()
        store.save({"id": "live", "installation_id": "i",
                    "expires_at": 2000.0, "consumed": False})
        store.save({"id": "old", "installation_id": "i",
                    "expires_at": 1000.0, "consumed": False})
        store.save({"id": "used", "installation_id": "i",
                    "expires_at": 2000.0, "consumed": True})
        out = store.purge_expired(1500.0)
        self.assertEqual(out, {"expired": 1, "consumed": 1})
        self.assertIsNotNone(store.load("live"))
        self.assertEqual(store.purge_expired(1500.0),
                         {"expired": 0, "consumed": 0})


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest backend.tests.test_maintenance -v`
Expected: FAIL with `AttributeError: ... has no attribute 'purge_expired'`

- [ ] **Step 3: Write minimal implementation** — en `backend/ports.py`, tras `delete_for_installation` de `SessionStore`:

```python
    @abstractmethod
    def purge_expired(self, now: float) -> dict:
        """Borra sesiones inutilizables (expiradas o revocadas).

        Devuelve {"expired": n, "revoked": n} disjuntos (expirada gana).
        Idempotente y re-ejecutable."""
```

y tras `delete_for_installation` de `OAuthTransactionStore`:

```python
    @abstractmethod
    def purge_expired(self, now: float) -> dict:
        """Borra transacciones inutilizables (expiradas o consumidas).

        Devuelve {"expired": n, "consumed": n} disjuntos (expirada gana).
        Idempotente y re-ejecutable."""
```

En `backend/stores.py`, en `InMemorySessionStore` tras `delete_for_installation`:

```python
    def purge_expired(self, now: float) -> dict:
        """Borra sesiones expiradas o revocadas (F-C4). Conteos disjuntos."""
        expired = revoked = 0
        for sid in [sid for sid, payload in self._data.items()
                    if payload.get("expires_at", 0) <= now
                    or payload.get("revoked")]:
            payload = self._data.pop(sid)
            if payload.get("expires_at", 0) <= now:
                expired += 1
            else:
                revoked += 1
        return {"expired": expired, "revoked": revoked}
```

En `InMemoryOAuthTransactionStore` tras `delete_for_installation`:

```python
    def purge_expired(self, now: float) -> dict:
        """Borra transacciones expiradas o consumidas (F-C4)."""
        expired = consumed = 0
        for tid in [tid for tid, entry in self._data.items()
                    if entry.get("expires_at", 0) <= now
                    or entry.get("consumed")]:
            entry = self._data.pop(tid)
            if entry.get("expires_at", 0) <= now:
                expired += 1
            else:
                consumed += 1
        return {"expired": expired, "consumed": consumed}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest backend.tests.test_maintenance -v`
Expected: PASS (4 tests). Luego suite completa sigue verde: `python -m unittest discover -s backend/tests` (198+4 OK).

- [ ] **Step 5: No commit** (regla F-C4: sin commit sin autorización; los cambios quedan en working tree).

---

### Task 2: `purge_expired` en Sqlite + PG (paridad F-C2)

**Files:**
- Modify: `backend/prodstores.py` (`SqliteSessionStore`, `SqliteOAuthTransactionStore`)
- Modify: `backend/pgstores.py` (`PgSessionStore`, `PgOAuthTransactionStore`)
- Test: `backend/tests/test_maintenance.py` (añadir clases Sqlite + PG con skip sin servidor)

**Interfaces:**
- Consumes: misma semántica que Task 1; en Sqlite/PG el payload vive en `payload_json`/`entry_json` TEXT (parseo Python, igual que `delete_for_installation`).
- Produces: mismos dicts que Task 1.

- [ ] **Step 1: Write the failing test** — añadir a `backend/tests/test_maintenance.py`:

```python
class SqlitePurgeTest(unittest.TestCase):
    def test_sqlite_purge_parity(self):
        import os
        import shutil
        import tempfile
        from backend.prodstores import (SqliteOAuthTransactionStore,
                                        SqliteSessionStore)
        tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmp, True)
        db = os.path.join(tmp, "meta.db")
        sess = SqliteSessionStore(db)
        try:
            sess.save_session("live", {"installation_id": "i",
                                       "expires_at": 2000.0})
            sess.save_session("old", {"installation_id": "i",
                                      "expires_at": 1000.0})
            sess.save_session("gone", {"installation_id": "i",
                                       "expires_at": 2000.0,
                                       "revoked": True})
            self.assertEqual(sess.purge_expired(1500.0),
                             {"expired": 1, "revoked": 1})
            self.assertIsNotNone(sess.load_session("live"))
        finally:
            sess.close()
        txns = SqliteOAuthTransactionStore(db)
        try:
            txns.save({"id": "live", "installation_id": "i",
                       "expires_at": 2000.0, "consumed": False})
            txns.save({"id": "used", "installation_id": "i",
                       "expires_at": 2000.0, "consumed": True})
            self.assertEqual(txns.purge_expired(1500.0),
                             {"expired": 0, "consumed": 1})
        finally:
            txns.close()


class PgPurgeTest(unittest.TestCase):
    def test_pg_purge_parity(self):
        import os
        if not os.environ.get("STREAM_META_TEST_DATABASE_URL"):
            self.skipTest("sin PostgreSQL (diseno: skip local)")
        from backend.tests.test_pg import fresh_db
        from backend.pgstores import (PgOAuthTransactionStore,
                                      PgSessionStore)
        _, pool = fresh_db(self)
        sess, txns = PgSessionStore(pool), PgOAuthTransactionStore(pool)
        sess.save_session("old", {"installation_id": "i",
                                  "expires_at": 1000.0})
        sess.save_session("live", {"installation_id": "i",
                                   "expires_at": 9999999999.0})
        self.assertEqual(sess.purge_expired(1500.0),
                         {"expired": 1, "revoked": 0})
        self.assertIsNotNone(sess.load_session("live"))
        txns.save({"id": "old-t", "installation_id": "i",
                   "expires_at": 1000.0, "consumed": False})
        self.assertEqual(txns.purge_expired(1500.0),
                         {"expired": 1, "consumed": 0})
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest backend.tests.test_maintenance -v`
Expected: FAIL `AttributeError: purge_expired` en Sqlite (PG hace skip local sin env).

- [ ] **Step 3: Write minimal implementation** — `SqliteSessionStore` tras `delete_for_installation`:

```python
    def purge_expired(self, now: float) -> dict:
        """Borra sesiones expiradas o revocadas (F-C4). Conteos disjuntos."""
        with self._lock:
            rows = self._conn.execute(
                "SELECT id, payload_json FROM sessions").fetchall()
            expired = revoked = 0
            for sid, blob in rows:
                payload = json.loads(blob)
                if payload.get("expires_at", 0) <= now:
                    doomed, expired = True, expired + 1
                elif payload.get("revoked"):
                    doomed, revoked = True, revoked + 1
                else:
                    doomed = False
                if doomed:
                    self._conn.execute("DELETE FROM sessions WHERE id=?",
                                       (sid,))
            self._conn.commit()
            return {"expired": expired, "revoked": revoked}
```

`SqliteOAuthTransactionStore` análogo sobre `transactions`/`entry_json` con claves `expired`/`consumed` (`consumed` = `entry.get("consumed")` con `expires_at > now`). `PgSessionStore`/`PgOAuthTransactionStore` idénticos pero con `%s` placeholders y `with self._pool as conn:` (patrón `delete_for_installation` en `backend/pgstores.py:96-108` y `:168-181`).

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest backend.tests.test_maintenance -v` → PASS; `python -m unittest discover -s backend/tests` verde. PG real se verifica en CI (servicio `postgres:18`, como `test_purge_methods_f_c2`).

- [ ] **Step 5: No commit.**

---

### Task 3: `backend/maintenance.py` orquestador + tests

**Files:**
- Create: `backend/maintenance.py`
- Test: `backend/tests/test_maintenance.py` (clase `PurgeOrchestratorTest` con `FakeClock`)

**Interfaces:**
- Consumes: `sessions.purge_expired(now)`, `transactions.purge_expired(now)`.
- Produces: `purge_expired(sessions, transactions, clock=None) -> {"sessions": {"expired","revoked"}, "transactions": {"expired","consumed"}}`.

- [ ] **Step 1: Write the failing test**:

```python
class PurgeOrchestratorTest(unittest.TestCase):
    def test_orchestrator_counts(self):
        from backend import maintenance as _m
        from backend.tests.test_youtube import FakeClock
        clock = FakeClock(start=1500.0)
        sess = InMemorySessionStore()
        sess.save_session("old", {"installation_id": "i",
                                  "expires_at": 1000.0})
        txns = InMemoryOAuthTransactionStore()
        txns.save({"id": "used", "installation_id": "i",
                   "expires_at": 2000.0, "consumed": True})
        out = _m.purge_expired(sess, txns, clock=clock.time)
        self.assertEqual(out, {"sessions": {"expired": 1, "revoked": 0},
                               "transactions": {"expired": 0,
                                                "consumed": 1}})
```

(`FakeClock` existe en `backend/tests/test_youtube.py` con `.time`; verificar atributo antes de correr: si se llama distinto, usar `clock=lambda: 1500.0`.)

- [ ] **Step 2: Run** → FAIL `ModuleNotFoundError: backend.maintenance`.
- [ ] **Step 3: Create `backend/maintenance.py`**:

```python
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
```

- [ ] **Step 4: Run** → PASS + suite verde.
- [ ] **Step 5: No commit.**

---

### Task 4: Endpoint `POST /ops/purge` con auth de operador (TDD)

**Files:**
- Modify: `backend/http_server.py` (`BackendApp.__init__`: param `secrets=None`, método `ops_purge_token()`, limiter `"ops"`, ruta en `_route_post`)
- Modify: `backend/app.py` (`create_app` pasa `secrets=secrets` al `BackendApp`)
- Test: `backend/tests/test_ops_purge.py` (nuevo)

**Interfaces:**
- Consumes: `app.secrets.get("OPS_PURGE_TOKEN")` (nombre documentado, jamás valor en logs); `auth_boundary.extract_bearer`.
- Produces: `200 {"purged": {...}}` | `401` bearer erróneo | `429` rate-limit | `501` sin secreto configurado.

- [ ] **Step 1: Write the failing test** (`backend/tests/test_ops_purge.py`):

```python
"""Endpoint F-C4 POST /ops/purge (T-065): auth operador + conteos."""
import unittest

from backend.config import Settings
from backend.http_server import BackendApp
from backend.stores import (AllowAllRateLimiter, InMemoryInstallationStore,
                            InMemoryOAuthTransactionStore,
                            InMemorySessionStore)


class _Secrets:
    def __init__(self, token=None):
        self._token = token

    def get(self, name):
        return self._token if name == "OPS_PURGE_TOKEN" else None


def _app(token="fk-ops-9z"):
    settings = Settings()
    sessions = InMemorySessionStore()
    sessions.save_session("old", {"installation_id": "i",
                                  "expires_at": 1.0})
    sessions.save_session("live", {"installation_id": "i",
                                   "expires_at": 9999999999.0})
    txns = InMemoryOAuthTransactionStore()
    txns.save({"id": "used", "installation_id": "i",
               "expires_at": 9999999999.0, "consumed": True})
    return BackendApp(settings=settings, sessions=sessions,
                      limiter=AllowAllRateLimiter(),
                      transactions=txns, secrets=_Secrets(token))


class OpsPurgeTest(unittest.TestCase):
    def test_unauthorized_without_bearer(self):
        from backend.errors import ErrorCode
        app = _app()
        with self.assertRaises(Exception) as ctx:
            app.ops_purge("")
        self.assertEqual(ctx.exception.code, ErrorCode.AUTHENTICATION)

    def test_wrong_bearer_rejected(self):
        from backend.errors import ErrorCode
        app = _app()
        with self.assertRaises(Exception) as ctx:
            app.ops_purge("fk-ops-wrong")
        self.assertEqual(ctx.exception.code, ErrorCode.AUTHENTICATION)

    def test_unconfigured_returns_501(self):
        from backend.errors import ErrorCode
        app = _app(token=None)
        with self.assertRaises(Exception) as ctx:
            app.ops_purge("fk-ops-9z")
        self.assertEqual(ctx.exception.code, ErrorCode.INTERNAL)

    def test_purge_counts_and_keeps_live(self):
        app = _app()
        out = app.ops_purge("fk-ops-9z")
        self.assertEqual(out["purged"]["sessions"], {"expired": 1,
                                                    "revoked": 0})
        self.assertEqual(out["purged"]["transactions"], {"expired": 0,
                                                        "consumed": 1})
        self.assertIsNotNone(app.sessions.load_session("live"))
        # Re-ejecutable: ceros.
        out2 = app.ops_purge("fk-ops-9z")
        self.assertEqual(out2["purged"]["sessions"], {"expired": 0,
                                                     "revoked": 0})


if __name__ == "__main__":
    unittest.main()
```

NOTA: el test llama a un método `BackendApp.ops_purge(bearer)` (lógica pura, sin HTTP) para no montar sockets; la ruta `_route_post` delega en él. Además un test HTTP de integración `POST /ops/purge` con header Bearer + un test de rate-limit `ops` (11 llamadas rápidas → 429) sobre el `BackendApp` real con `FixedWindowRateLimiter(10, 60)` en `limiters["ops"]`.

- [ ] **Step 2: Run** → FAIL `TypeError: unexpected keyword secrets` / `AttributeError: ops_purge`.
- [ ] **Step 3: Implementación mínima** en `backend/http_server.py`:
  - `__init__`: param `secrets=None`, `self.secrets = secrets`; en `self.limiters` añadir `"ops": FixedWindowRateLimiter(10, 60)`.
  - Nuevo método (junto a `_limited`):

```python
    def ops_purge_token(self) -> str | None:
        if self.secrets is None:
            return None
        return self.secrets.get("OPS_PURGE_TOKEN")

    def ops_purge(self, bearer: str) -> dict:
        """Purga F-C4 con auth de operador. Solo conteos en salida."""
        import hmac as _hmac
        expected = self.ops_purge_token()
        if not expected:
            raise AppError(ErrorCode.INTERNAL, "purge not configured")
        if not bearer or not _hmac.compare_digest(bearer, expected):
            raise AppError(ErrorCode.AUTHENTICATION, "bad ops token")
        self._limited("ops", "ops:purge")
        from backend import maintenance as _maintenance
        result = _maintenance.purge_expired(self.sessions,
                                            self.transactions, self._clock)
        self.log.info("ops purge sessions=%s transactions=%s",
                      result["sessions"], result["transactions"],
                      extra={"requestId": "-"})
        return {"purged": result}
```

  - En `_route_post`, antes de `parts = path.split("/")`:

```python
        if path == "/ops/purge":
            record = app.ops_purge(
                auth_boundary.extract_bearer(
                    self.headers.get("Authorization", "")) or "")
            return 200, record
```

  - En `backend/app.py::create_app`, al construir `BackendApp`: añadir `secrets=secrets`.
  - Verificar que `create_app` con `providers is not None` (tests) no rompe: `transactions` puede ser None en ese camino → `ops_purge` con `transactions=None` fallaría; los tests de Task 4 pasan `transactions` explícito. En producción `main()` siempre cablea transactions. Añadir guarda: si `self.transactions is None` → `INTERNAL purge not configured` (misma rama que erase en `_route_post:228`).

- [ ] **Step 4: Run** → PASS + suite verde + secret-scan local (`grep -rEI` del workflow sobre el árbol, excluyendo `.git`).
- [ ] **Step 5: No commit.**

---

### Task 5: `tools/purge_expired.py` + ejecución real del job (puerta de evidencia)

**Files:**
- Create: `tools/purge_expired.py` (patrón `tools/backfill_token_crypto.py`: env `STREAM_META_BACKEND_DATABASE_URL`, exit 0/2, solo conteos)
- Test: manual (no unitario: es CLI de operador, como backfill que tampoco tiene test).

- [ ] **Step 1: Crear el script** (sin test previo: CLI operador, patrón backfill):

```python
"""Purga manual F-C4 (T-065): ejecuta la purga contra DATABASE_URL.

Uso (operador; para pruebas usar DB de prueba, nunca prod directamente):

    $env:STREAM_META_BACKEND_DATABASE_URL = 'postgresql://...'
    python tools/purge_expired.py

Idempotente y re-ejecutable (solo borra sesiones expiradas/revocadas y
transacciones expiradas/consumidas). Solo imprime conteos. Salida 0 = ok,
2 = error de configuracion/entorno.
"""
```

Implementación: `validate_database_url`, `PgPool`, `PgSessionStore`, `PgOAuthTransactionStore`, `maintenance.purge_expired`, `print("purge: sessions=... transactions=...")`, pool.close().

- [ ] **Step 2: Evidencia real del job** (puerta F-C4):
  1. `docker run --name pg-fc4 -e POSTGRES_PASSWORD=fk-ci-9z -p 5544:5432 -d postgres:18` (puerto local, password efímera).
  2. Crear DB `purge_check`, aplicar migrations (`run_migrations` vía `python -c`), insertar filas expiradas/vigentes/revocadas/consumidas (SQL directo).
  3. `pg_dump -Fc` previo (reutiliza Task 6) + correr `python tools/purge_expired.py` → capturar salida con conteos.
  4. Verificar en DB: vigentes intactas, resto borrado; re-ejecutar → ceros.
  5. `docker rm -f pg-fc4` al terminar (higiene; documentar).
  6. Guardar comando+salida para el informe (pegar en `docs/notes/` o en el informe final; no subir dumps al repo).

- [ ] **Step 3: Probar también el endpoint HTTP local**: levantar `python -m backend.app` en dev con stores en memoria + secreto de mentira vía env... El endpoint lee de `SecretStore` (env en dev: `STREAM_META_BACKEND_SECRET_OPS_PURGE_TOKEN` con `EnvSecretStore` prefix `STREAM_META_BACKEND_SECRET_`). Sembrar una sesión expirada es incómodo en memoria sin API; basta con evidenciar `501` sin secreto y `401` con bearer erróneo + `200` con ceros tras configurar el secreto. Capturar salida.

---

### Task 6: Backup + restore probado (puerta de evidencia) + docs

- [ ] **Step 1: Restore verificado en DB de prueba** (docker `postgres:18`, pg_dump 18 local):
  1. DB `bak_src`: migrations + filas de prueba en las 5 tablas + `SELECT count(*)` por tabla (antes).
  2. `pg_dump -Fc -v -d "postgresql://postgres:fk-ci-9z@127.0.0.1:5544/bak_src" -f C:\Users\Horan\AppData\Local\Temp\opencode\fc4-test.dump` (fuera del repo; URL directa sin pooler, como exige Neon).
  3. `DROP DATABASE`/`CREATE DATABASE bak_dst` + `pg_restore -v -d ... bak_dst` + conteos por tabla (después) + lectura representativa (`SELECT id FROM sessions`, una `connections`).
  4. Comparar conteos antes/después (iguales) → evidencia.
  5. Limpieza: borrar dump + `docker rm -f`.
- [ ] **Step 2: Documentar en `docs/DEPLOYMENT.md`** (§ backup F-C4): comandos exactos operador contra Neon (obtener URL directa sin pooling del dashboard, `pg_dump -Fc`, almacenar fuera de la instancia, `pg_restore -d NUEVA_DB`, verificar conteos + `/connect/*/status`), RPO/RTO honestos Free (RPO: último dump o 6 h history; RTO: manual), nota pooler (`-pooler`/6432 NO vale para dump), nota snapshots (1 manual en Free; programados requieren plan de pago = decisión operador).

---

### Task 7: Alertas exactas + prueba local de señales (puerta de evidencia)

**Files:**
- Create: `ops/monitoring/alert-5xx.json`, `alert-p99.json`, `alert-yt-quota.json`, `uptime-health.json`, `ops/monitoring/README.md` (canal email + `gcloud` exactos + pasos Console para cuota 80 %).

- [ ] **Step 1: Escribir los 4 JSON** (filtros exactos):
  - A1: `run.googleapis.com/request_count` + `response_code_class=5xx` + `service_name=obs-stream-metadata-service`, count > 3 / 300 s alineación 60 s.
  - A2: `run.googleapis.com/request_latencies` p99 > 5000 ms / 300 s.
  - A3: métrica log-based `yt-quota-rejects` (filtro `resource.type="cloud_run_revision" AND resource.labels.service_name="obs-stream-metadata-service" AND textPayload:"detail=google:rate"`) + alerta count > 0 / 300 s. Comando de creación de la métrica en el README.
  - A4: uptime check `https://obs-stream-metadata-service-.../health` cada 5 min (URL real la confirma el operador; el JSON lleva placeholder documentado `PUBLIC_URL` — única excepción permitida: es config de operador, no código).
- [ ] **Step 2: Prueba local de señales** (evidencia de que las políticas dispararían):
  1. Backend local dev: provocar un 500 (p. ej. `POST /privacy/erase` sin wiring → `INTERNAL`) y un `429` (reventar `bootstrap_ip` con 6 bootstraps) y un `detail=google:rate` (unitario contra `classify_broadcast_error` con payload quotaExceeded → ya cubierto por tests existentes; mostrar línea de log `request error code=provider_rate_limited detail=google:rate` generada por un request real al endpoint de metadata con transport fake? mínimo: mostrar las líneas `method=... status=500` y `status=429` del log local + el test de classify).
  2. Capturar logs → adjuntar al informe como "señales que consumen A1/A3".
- [ ] **Step 3: Pendiente operador explícito** (no ejecutable por el agente): crear canal + políticas con los comandos del README, provocar alerta de prueba (p. ej. forzar 4×500 o bajar umbral temporalmente), confirmar recepción en el canal. Registrar como PENDIENTE en el informe final (la puerta "alerta recibida" la cierra el operador).

---

### Task 8: Docs + gates finales (puerta de evidencia completa)

- [ ] **Step 1: `docs/DEPLOYMENT.md`**: § F-C4 purga programada (crear secreto `OPS_PURGE_TOKEN` + montar en `SECRET_DIRS` + comando `gcloud scheduler jobs create http` exacto con `--headers="Authorization=Bearer ..."`, schedule, deadline, retries) + § backup/restore probado + § alertas (tabla A1-A4 + links README).
- [ ] **Step 2: `docs/PRIVACY.md` §8**: sustituir "no background eraser is implemented" por purga diaria + retención efectiva (~25 h sessions, ~24 h transactions); resto intacto.
- [ ] **Step 3: `docs/BACKLOG.md`**: T-065 `pendiente` → `en-progreso` al empezar Tasks 1-7, → `hecha` con aceptación verificada al cerrar.
- [ ] **Step 4: Gates**: `python -m unittest discover -s backend/tests` verde; `build_x64\RelWithDebInfo\metadata-selfcheck.exe` OK (sin recompilar: cambios solo Python); secret-scan del workflow en local (los dos `grep`); `git status` limpio salvo cambios F-C4 + 6 untracked ajenos intactos.
- [ ] **Step 5: No commit.** Informe final según §6 del encargo.

---

## Self-Review

- Cobertura F-C4: purga (Tasks 1-5) ✓; backups+restore (Task 6) ✓; alertas 5xx+cuota (Task 7) ✓; revisión limiter multi-usuario (decisión: se mantiene max 1 instancia + FixedWindow; Redis diferido, sin cambio) ✓.
- Sin placeholders: todos los steps llevan código/comandos exactos; la única edición diferida al operador (URLs GCP, recepción de alerta) está marcada PENDIENTE explícito, no como TODO interno.
- Tipos: `purge_expired(now: float) -> dict` idéntico en ports y las 6 implementaciones; `maintenance.purge_expired` devuelve `{"sessions","transactions"}` y el endpoint lo envuelve en `{"purged": ...}` igual que `erase` devuelve `{"erased": ...}` (simetría F-C2).
- Riesgo secre-scan: `OPS_PURGE_TOKEN` solo aparece como string en `secrets.get("OPS_PURGE_TOKEN")` y en docs/comandos operador (el scan marca asignaciones con valor, no lecturas ni nombres en docs salvo `client_secret|refresh_token|access_token = valor`; verificar en Task 4 Step 4 con los grep exactos del workflow).
