# ADR-014 — PostgreSQL portable + Cloud Run/Neon

- Estado: aprobado (T-055, 2026-09-10)
- Sustituye a: parcialmente ADR-013 (SQLite deja de ser DB de producción;
  el resto —fail-fasts, file-secrets, TLS, gates— sigue vigente)
- Relación con ADR-012: ninguna modal cambia; DEV/PROD siguen entornos.

## Contexto

T-054 dejó un backend production-capable con persistencia SQLite local,
válido como endurecimiento pero no como despliegue: el contenedor Cloud
Run es stateless (sin filesystem persistente) y el producto necesita una
DB externa con ruta de crecimiento. Decisión ya tomada antes de T-055:
compute Cloud Run, PostgreSQL en Neon Free, migración futura posible a
Cloud SQL.

## Decisión

- **PostgreSQL estándar como contrato de persistencia.** La app solo habla
  SQL portable (tipos TEXT/INTEGER/BIGINT, DDL idempotente, `%s`
  placeholders); sin extensiones, sin APIs de Neon, sin SDK de proveedor.
- **Neon = proveedor inicial, no dependencia.** Configuración genérica
  `STREAM_META_BACKEND_DATABASE_URL` (conceptualmente DATABASE_URL; nunca
  `NEON_*`). Cambiar a Cloud SQL = cambiar la URL + migrar datos.
- **Migrations versionadas** (`backend/migrations/NNN_*.sql` + tabla
  `schema_migrations`, una transacción por fichero, ejecución al arrancar
  en producción). Esquema creable desde cero fuera de Neon.
- **Repositorios por port** (`backend/pgstores.py`): cambiar de motor no
  toca kernel/OAuth/auth/API. SQLite/InMemory se conservan para DEV/TEST
  (marcados `DEVELOPMENT_ONLY`; producción los rechaza por gates T-053).
- **Pool acotado configurable** (`db_pool_max`, timeouts): instancias ×
  pool ≤ max_connections del servidor (Neon Free: límite bajo; valores
  iniciales conservadores en DEPLOYMENT.md).
- **Cloud Run como compute**: `Dockerfile.backend` (slim, no-root,
  sin secretos en imagen, respeta `PORT`, HEALTHCHECK, SIGTERM graceful);
  stateless total (nada persistente en contenedor).
- **Driver**: `psycopg-binary==3.3.5` pinneado (`backend/requirements.txt`);
  primera y única dependencia externa del backend (imprescindible: el
  stdlib no trae driver PostgreSQL).

## Alternativas consideradas

- SQLite en producción (descartada: incompatible con contenedor
  stateless; sin multi-instancia; diverge de la semántica PG).
- Cliente PG propio en stdlib (descartado: reimplementar protocolo
  + SCRAM es más riesgo que una dependencia estándar auditada).
- DB agnóstica multi-motor (descartada: YAGNI; el contrato es
  PostgreSQL, no "cualquier SQL").

## Consecuencias

Positivas: despliegue real posible con coste inicial ~cero (tiers free);
escala horizontal; backups/export estándar (`pg_dump`/`pg_restore`);
migración Neon→Cloud SQL sin reescribir la app.
Negativas: DB externa obligatoria en prod (una pieza más que operar);
SQLite ya no vale en prod (los gates lo impiden); pool + conexiones a
dimensionar al crecer; dependencia operacional inicial de Neon + GCP.

## Migración futura

Ver `docs/DEPLOYMENT.md` (estrategia Neon→Cloud SQL por tamaño: dump,
replicación o volcado en caliente; cutover = cambio de DATABASE_URL +
nueva revisión; rollback = volver a la URL anterior).
