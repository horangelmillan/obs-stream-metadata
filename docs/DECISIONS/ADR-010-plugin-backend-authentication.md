# ADR-010 — Autenticación Plugin ↔ Backend (T-044)

- **Fecha:** 2026-09-09 · **Tarea:** T-044 · **Estado:** decidido e implementado (backend + cliente plugin; wiring UI en T-048).
- **Contexto:** ADR-009 exige auth sin secreto permanente en el plugin; T-043 dejó formato/TTL/sesiones/rate-limit pendientes. Trazabilidad: ADR-009 → T-043 → T-044 → T-045/T-046.
- **Amenaza:** binario público inspeccionable; robo de sesión; perfil copiado; backend abusado masivamente; replay. Máquina totalmente comprometida: fuera de alcance (declarado, no mitigable).

## 1. Alternativas evaluadas

- **A. API key permanente embebida:** descartada — viola la propiedad central (§3 del encargo).
- **B. Installation ID sin secreto:** descartada como auth (identificación ≠ autenticación); se usa solo como índice.
- **C+D. Bootstrap anónimo + secreto por instalación + sesión corta:** ADOPTADA. El secreto se entrega una vez vía TLS; comprometer una instalación no revela credencial maestra.
- **E. Device-style:** innecesario (hay backend HTTPS con callbacks).
- **F. Keypair por instalación:** descartado — propiedades equivalentes al secreto simétrico bajo compromiso de endpoint, pero exige crypto asimétrica (nueva dependencia C++/Qt); HMAC-SHA256 es stdlib en ambos lados y no es crypto inventada.
- **G. Challenge/response:** adoptado en forma liviana (firma HMAC sobre `id|timestamp|nonce` + nonces de un uso + ventana ±, sin estado de challenge pendiente).
- **H. Signed requests por llamada:** descartado para llamadas normales (coste/complejidad); firma solo en emisión/renovación; sesiones bearer cortas sobre TLS el resto.
- **I. Sesión opaca corta:** ADOPTADA frente a JWT largo (revocable, sin claims sensibles en cliente, sin gestión de claves de firma).

## 2. Protocolo

```text
POST /auth/bootstrap  {} -> 201 {installation_id, installation_secret} (única emisión)
POST /auth/session    {installation_id, timestamp, nonce, signature} -> {session_token, expires_in: 1800}
POST /auth/refresh    {session_token, +firma fresca} -> {session_token nuevo} (rota; el anterior muere)
POST /auth/revoke     {session_token} -> 200 {} (idempotente)
POST /auth/installation/revoke {installation_id, +firma} -> 200 {} (mata instalación + sesiones)
```
Firma: `HMAC-SHA256(secret, "id|timestamp|nonce")`. Nonce ≥16 chars, un solo uso, ventana 10 min; timestamp ±5 min (clock skew). TTL sesión 1800 s (decisión T-044). Públicas sin auth: `/health`, `/ready`, `/version`.

## 3. Lifecycle y almacenamiento

Instalación: UUID aleatorio (identificador) + secreto 256-bit (Secreto, DPAPI `backendInstall`, jamás UI/logs). Sesión: opaca 256-bit (Confidencial, solo memoria; re-auth al reiniciar). Expirada → refresh (firma fresca); refresh en carrera → gana la primera, la otra recibe 401 y re-autentica. Revocación: sesión o instalación completa. Copia a otro equipo = clon indistinguible (documentado, no mitigable sin cuenta de usuario — fuera de alcance). Reinstalación/borrado = bootstrap nuevo.

## 4. Rate limiting inicial (razonado, ajustable)

Bootstrap: 5/h por IP + 100/h global (anónimo = límites duros). Session/refresh/revoke: 30/min por instalación + 200/min por IP. Fixed-window en memoria (single-instance; distribuido pendiente). Respuesta 429 + backoff del cliente, sin reintento agresivo.

## 5. Mitigado / no mitigado

Mitigado: extracción de secreto global (no existe), replay (nonce+timestamp), robo de sesión (TTL corto + rotación + revocación), abuso masivo de bootstrap (límites), filtración en logs/respuestas (redacción + tests canario). No mitigado: endpoint totalmente comprometido, clonación de perfil, abuso con muchas IPs (requiere abuse-detection futuro).

## 6. Consecuencias

T-045/T-046 usan este sistema sin redefinirlo (sesión para `/connect/*` y metadata). Cliente Qt en `src/backend_auth.*` (async, sin UI); wiring en T-048. TLS obligatorio en producción; desarrollo solo loopback local.
