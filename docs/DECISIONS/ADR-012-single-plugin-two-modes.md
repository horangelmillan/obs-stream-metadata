# ADR-012 — Plugin único con dos modalidades: Independiente y Administrado (maestro)

- **Fecha:** 2026-09-09 · **Tarea:** auditoría integral dos-modalidades · **Estado:** decidido como dirección de producto; implementación incremental pendiente (T-049+).
- **Referencia de producto:** `obs-stream-metadata-arquitectura-dos-modalidades.md` (raíz del repo, propuesta 2026-09-09; este ADR la adopta formalmente tras auditar el repositorio).
- **Regla:** no rehacer. Reutilizar, adaptar y documentar. Nada existente se elimina en esta tarea.

## 1. Contexto

T-035/T-036 demostraron que YouTube y Kick exigen `client_secret` en nuestra configuración; T-037 que no hay permiso oficial para distribuirlo en binario público open-source; T-038/ADR-009 resolvió backend centralizado como vía de distribución; T-032/ADR-008 resolvió BYO-app como etapa operador. La contradicción restante: ADR-009 asumía UN modelo de distribución (backend del servicio) y declaraba BYO-app “no UX final”, lo que dejaba fuera a usuarios que prefieren operar sus propias apps sin pagar infraestructura.

## 2. Decisión

Un solo plugin `obs-stream-metadata` con dos modalidades que comparten núcleo funcional (conectar, identidad, título/descripción, broadcast YouTube, Apply, refresh, revoke, errores):

1. **Independiente / Gratis:** el usuario aporta App Identity (registra sus apps, introduce credenciales donde el proveedor las exija). Sin backend del servicio. Cuota/políticas/verificación a su cargo.
2. **Administrado / Suscripción:** el servicio aporta App Identity e infraestructura (backend T-043/44/45/46). El usuario solo autoriza en navegador. Suscripción financia hosting/mantenimiento/seguridad/soporte.

Twitch funciona directo en ambas inicialmente (DCF público; sin backend por simetría).

## 3. Qué cambia respecto a decisiones anteriores

- **ADR-008 (BYO-app):** de “única arquitectura del producto” a “modalidad Independiente del producto”. Se conserva todo (DPAPI, secure store, revoke, backoff, seguridad). Cambia documentalmente: 5 campos universales → configuración contextual por proveedor (Twitch: solo Client ID; YouTube/Kick: ID+secret donde corresponda). `STREAM_META_*` no resucitan.
- **ADR-009 (backend centralizado):** de “arquitectura de distribución” a “infraestructura de la modalidad Administrada”. El backend existente pasa a ser prototipo/base Managed. Sin backend para Independiente.
- **Afirmaciones superadas (ver §5):** “BYO-app no es UX final” (STATE) y “UX sin IDs/secrets visibles” como único camino (T-041/T-048 originales) quedan acotadas al modo Administrado.

## 4. Límites de código (sin implementar en esta tarea)

`Metadata Core` común; `Independent Auth` (directo por proveedor, reutiliza `metadata_dock.*` + `secure_store.*`) y `Managed Auth` (wiring de `backend_auth.*` + connect-vía-backend) como fuentes de autenticación seleccionables por `ConnectionMode`. Prohibido `TwitchIndependent/TwitchManaged/...` salvo necesidad real. Tokens/credenciales jamás cruzan de modalidad; cambiar de modalidad pide confirmación y no copia secrets.

## 5. Contradicciones resueltas (trazabilidad §33 del documento de dirección)

1. `BYO-app no es UX final` (STATE 2026-09-09) → existía porque solo se contemplaba distribución-servicio → ahora BYO-app = UX del modo Independiente; se conserva y refina.
2. `Backend para YouTube+Kick` (ADR-009) como obligatorio → existía por UX única sin credenciales → ahora obligatorio solo en Administrado; en Independiente el usuario aporta credenciales.
3. `Migración fuera de BYO-app` (T-041/T-048) como eliminación → existía por la misma UX única → ahora es UX modal: selector + contextualización + wiring Managed.
4. `Secret embebido prohibido` y `release-privada no conforme` (T-037/F-040) → se mantienen íntegras en ambas modalidades.

## 6. Abierto (OPEN, §31 del documento; no inventar)

Nombre/precio del servicio, proveedor de pagos, hosting/region/DB/secret-manager definitivos, sistema de cuentas/licencias, Twitch administrado o no, política exacta Google para Independiente, verificación OAuth, retención, límite de dispositivos, planes. Además: validación de producción, privacy policy, identidad DEV/PROD separada.

## 7. Consecuencias

BACKLOG: T-041/T-048 reenmarcadas a UX modal; nuevas T-049–T-057. Validación futura en tres matrices (Independent/Managed/Cross-mode). Packaging solo tras gate. Backend T-043–T-046 intacto como base Managed; dock actual como embrión Independiente.
