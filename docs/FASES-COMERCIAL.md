# FASES COMERCIALES — hardening pre-distribución (2026-09-17)

> Lista de objetivos para ejecutar **de uno en uno**: cada fase termina solo
> con evidencia real verificada; sin pasar a la siguiente sin visto bueno.
> Contexto: revisión de seguridad 2026-09-17 (tokens en claro en Neon,
> instalador sin firma, borrado incompleto). Sin certificaciones
> (no GDPR/SOC2/ISO claims).

## F-C1 — Cifrado de tokens en reposo (EMPEZAR AQUÍ)

Estado actual: `PgTokenStore` guarda `access_token`/`refresh_token` en
texto plano (verificado en `backend/pgstores.py` + `migrations/001_init.sql`).

- AEAD (Fernet, lib `cryptography`) con clave en Secret Manager (fichero,
  mismo patrón que el resto de secretos; jamás en repo/logs).
- Migración `002_*`: columnas/formato versionado `v1:…`, doble lectura
  (legacy en claro + cifrado) para no romper sesiones vivas, backfill.
- Rotación documentada (nueva clave → re-cifrado).

PASS solo si: tests roundtrip + clave errónea falla; dump con solo
ciphertext; deploy lee filas legacy y nuevas; sonda prod Apply OK.

## F-C2 — Borrado real de datos + privacidad operativa

Estado actual: desinstalar no borra filas del backend; `PRIVACY.md` sin
contacto designado.

- Endpoint de borrado total por instalación (connections + tokens +
  sessions de esa instalación) + botón "borrar mis datos" + nota en el
  uninstaller (desconectar/borrar antes de desinstalar).
- `PRIVACY.md`: contacto `horangelmillan@gmail.com`, qué se guarda,
  cuánto dura, cómo borrarlo.

PASS solo si: conectar → borrar → conteos a cero en DB + reconexión
exige OAuth nuevo.

## F-C4 — Operativa comercial mínima

- Purga programada de sesiones/transacciones expiradas (Scheduler +
  conteos en logs).
- Backups Neon con prueba de restore documentada.
- Alertas Cloud Run 5xx + cuota YouTube; revisión de límites/rate-limit
  para multi-usuario (hoy limiter en memoria, max 1 instancia).

PASS solo si: job ejecutado con conteos + restore verificado + alerta
de prueba disparada.

## F-C3 — Firma del distribuible (APARCADA hasta tracción)

Sin ~$220–350/año no hay firma pública (OV-IV Sectigo/Comodo a persona
física; EV innecesario: sin drivers y sin SmartScreen instantáneo desde
2024). Mientras tanto: SmartScreen/"Unknown Publisher" documentado como
limitación conocida del distribuible (no pedir a usuarios trucos raros,
solo aceptar el aviso). Firma con timestamp para que lo firmado siga
válido tras caducar. Requiere compra + validación de identidad por el
operador (1–7 días); la integración `signtool` es la parte pequeña.

## Diferido explícito (no MVP comercial)

Pentest/auditoría externa, builds reproducibles/SLSA, limiter
multi-instancia (Redis), HA/multi-región.

## Regla de ejecución

Un objetivo cada vez. Evidencia antes de avanzar. Sin refactors fuera
de cada objetivo. Commits en español, PR con template, CI verde, squash.
