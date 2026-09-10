# ADR-013 — Producción: SQLite stdlib + secretos por fichero + TLS builtin opcional

- Estado: aprobado (T-054, 2026-09-10)
- Sustituye a: nada (aplica T-043 §10 + ADR-009 §10 + gates T-053)
- Relación con ADR-012: ninguna modal cambia; DEV/PROD siguen siendo
  entornos, no `ConnectionMode`. El plugin no cambia.

## Contexto

El backend es stdlib-only sin dependencias, sin DB y sin secret manager
(auditoría T-054: `backend/` + `.github/` + raíz, sin Dockerfile/compose/
requirements/terraform). Producción necesita persistencia, secretos fuera
del proceso y TLS servido sin inventar infraestructura externa.

## Problema

Elegir piezas productivas mínimas, validables localmente, que pasen los
gates T-053 (`production` rechaza `DEVELOPMENT_ONLY` y HTTP) sin atar el
proyecto a un cloud, una DB gestionada o un proxy concretos (operador los
aporta; ver Dependencias externas).

## Opciones

1. **SQLite stdlib + secretos por fichero + TLS builtin opcional (elegida).**
   Cero dependencias (el backend sigue instalable con solo Python ≥3.10);
   backups = copia del fichero; permisos 0600 enforced; secretos montables
   (Docker/K8s/systemd `LoadCredential`); TLS con `ssl` stdlib o terminación
   en proxy. Límite honesto: sin cifrado aplicacional propio (el stdlib no
   trae AES y no se implementa criptografía casera) y single-instance
   (los contadores FixedWindow son por proceso).
2. Postgres/Redis gestionados + vault cloud. Operativamente superior a
   escala, pero exige elegir proveedor/región/cuentas ahora, añade drivers
   y convierte T-054 en un proyecto de infraestructura.
3. Cifrado aplicacional propio (AES casero sobre ficheros). Rechazada:
   nunca implementar criptografía propia.

## Decisión

- Persistencia productiva: `backend/prodstores.py` (SQLite, un fichero
  `meta.db`, WAL, 0600, fail-fast ante fichero legible por terceros).
- Secretos productivos: `FileSecretStore` (un fichero por secreto, mismos
  nombres que `EnvSecretStore`, sin renombres).
- TLS: builtin vía `ssl` stdlib cuando hay cert/key (TLS ≥1.2, fail-fast si
  faltan); topología recomendada: terminación en reverse-proxy con backend
  en red privada, documentada en `docs/DEPLOYMENT.md`.
- Rate limiting global productivo: límite explícito obligatorio
  (`AllowAllRateLimiter` rechazado en prod); bootstrap/auth/callback
  conservan sus FixedWindow por IP/instalación.
- Cifrado en reposo real (disco gestionado), dominio/DNS, certificado,
  host y OAuth apps PROD: dependencias externas del operador.

## Consecuencias

- `main()` con `env=production` exige `DATA_DIR + SECRET_DIR` y arranca
  sobre piezas no-dev; sin ellas falla antes de escuchar.
- Migración futura a DB gestionada = nuevos adapters contra los mismos
  ports (sin tocar kernel/app); los gates T-053 los aceptan si no son
  `DEVELOPMENT_ONLY`.
- Multi-instancia futura exige contador distribuido (documentado, no
  implementado).

## Seguridad

- Secretos: fuera de repo/env/imágenes/logs (ficheros 0600 + redacción
  existente + leak-guard).
- Tokens/sesiones: SQLite 0600 + borrado en Disconnect (semántica
  heredada de los ports); sin exponer en `/version` (solo `env`).
- Sin fallback DEV↔PROD en ninguna capa (gates + binding T-053 intactos).
