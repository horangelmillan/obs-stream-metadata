# ADR-009 — Backend centralizado (YouTube + Kick) + Twitch directo (P6)

- **Fecha:** 2026-09-09 · **Tarea:** T-038 · **Estado:** decidido (esta tarea). Implementación pendiente (T-043+).
- **Relación:** resuelve la contradicción ADR-008 (BYO-app) vs UX `AGENTS.md` §36-38. Preserva T-030/T-031/T-032 como historia válida. Evidencia: T-035 (YouTube secretless FAIL), T-036 (Kick secretless FAIL + control 200 con secret), T-037 (distribución Google). Referencia de desarrollo: `docs/ARCHITECTURE-BACKEND.md`. Custodia: `docs/SECURITY.md`.

## 1. Contexto

El producto exige `Connect → Browser → Connected as` sin que el usuario cree apps ni pegue secrets (`AGENTS.md` §36-38). T-032/ADR-008 resolvió custodia para el modelo BYO-app/operador, pero ese modelo no es UX final (F-037). T-035 y T-036 refutaron secretless para nuestras configuraciones YouTube y Kick: ambos token endpoints exigen `client_secret`. T-037 concluyó que no hay permiso oficial para distribuir el secret Google en binario público open-source, y que Kick lo trata como secreto real de servidor.

## 2. Problema

¿Dónde viven los `client_secret` de YouTube y Kick en una app desktop distribuida open-source, si el plugin no puede custodiarlos y el usuario no debe aportarlos?

## 3. Alternativas evaluadas (resumen; detalle en `docs/ARCHITECTURE-BACKEND.md` §2)

- **A. Directo (`OBS → Provider`):** viable solo para Twitch (DCF público, sin secret). Imposible para YouTube/Kick con la configuración probada (T-035/T-036 FAIL).
- **B. Backend solo donde sea necesario:** Twitch directo + backend YouTube/Kick. Mínimo secreto expuesto, mínima latencia/coste añadido donde no aporta nada.
- **C. Backend centralizado para todo:** uniforme pero añade punto de fallo, latencia, coste y custodia de tokens Twitch sin ningún beneficio de secretos.
- **D. BYO-app (ADR-008):** funcional como etapa operador, **incompatible con la UX objetivo**; se conserva como modo operador/debug, no como producto.
- **E. Secreto embebido / release-privada en binario:** descartada para Kick (secreto real) y sin permiso oficial para Google en repo público (T-037 Q2/Q4).

## 4. Decisión: MODIFY

Se **acepta** la propuesta “backend centralizado + Hexagonal/Shared Kernel” con **una modificación de alcance**:

> **Backend centralizado para YouTube y Kick. Twitch permanece directo (Device Flow público).**
> El backend expone una API única con un adapter por proveedor (incluido un adapter Twitch reservado/futuro); el plugin usa el backend para YouTube/Kick y habla directo con Twitch.

Motivo: forzar Twitch por el backend añadiría latencia, coste, dependencia y custodia de tokens sin eliminar ningún secreto (no hay secret que custodiar). Seguridad vs complejidad (§17 del encargo): el backend se usa exactamente donde resuelve un problema real de secretos.

## 5. Reparto de responsabilidades

- **Plugin OBS:** UI/UX, estado visual, inicia Connect, abre navegador del sistema, recibe resultado de conexión, envía comandos de metadata (`title/desc` + plataforma) al backend (YouTube/Kick) o directo al proveedor (Twitch), muestra estados/errores normalizados, async sin bloquear UI, backoff/retry de transporte. **Jamás** recibe/guarda `client_secret` de ningún proveedor ni `refresh_token` de YouTube/Kick.
- **Backend:** custodia credenciales de aplicación (Google + Kick) en secret manager; orquesta OAuth (URLs, callbacks HTTPS propios, exchange, refresh, revoke); almacena refresh/user-tokens cifrados en reposo; ejecuta llamadas de metadata YouTube/Kick; normaliza errores al modelo §28; rate-limit/abuso/auditoría; rotación de secrets. No toca video/audio/RTMP.
- **Shared Kernel (backend):** `Account`, `Connection`, `OAuthSession`, `ProviderId`, `AccessToken`/`RefreshToken` (opacos fuera del adapter), `MetadataUpdateCommand`, `ProviderCapability` (p. ej. `description: youtube-only`), `ErrorModel` (§28), `AuthorizationState`. Sin `if twitch/if youtube` en el núcleo.
- **Adapters:** Twitch (directo en plugin hoy; adapter backend reservado), YouTube, Kick — cada uno con OAuth, scopes, token handling, API client, reglas de metadata y errores propios (F-016/F-020/F-021, F-017/F-022/F-023).

## 6. Flujo Connect (YouTube/Kick)

```text
OBS --Connect--> Backend (authorization URL) --> Browser
--> login/consent --> Provider --> callback HTTPS Backend
--> token exchange (secret en backend) --> custodia cifrada
--> connection result (session token opaco + display name) --> OBS
--> Connected as ...
```

Twitch conserva Device Flow directo (F-015). Sin uniformidad forzada donde el proveedor difiere.

## 7. Seguridad (resumen; detalle en `docs/SECURITY.md` + `docs/ARCHITECTURE-BACKEND.md` §5-7)

- **Matriz de custodia:** `client_secret` (Google/Kick): solo backend + proveedor, jamás plugin/repo/logs. `Client ID` Twitch: distribuible en plugin (público por docs Twitch). `Client ID` Google: solo vía backend (ToS). `refresh_token` YouTube/Kick: solo backend cifrado; Twitch: DPAPI local (F-029). `access_token`: tránsito TLS + memoria mínima; el plugin solo recibe tokens de sesión opacos de corta vida del backend.
- **Auth plugin→backend:** sin secreto permanente embebido (recrearía el problema). Identidad de instalación aleatoria (UUID persistido en config DPAPI) + credenciales de sesión rotables de corta vida emitidas tras OAuth completado; rate-limit por instalación + por cuenta; endpoints endurecidos con challenge/response por intento. Tensión documentada: anonimitas total invita abuso; secreto permanente invita extracción — se resuelve con identidad no-secreta + sesiones cortas + límites.
- **Tokens de usuario:** custodiados donde se usan (backend para YouTube/Kick, DPAPI local para Twitch). Revocación en Disconnect (F-030) preservada en ambos caminos.

## 8. Consecuencias

- ADR-008 sigue válido como **modo operador/debug + hardening local** (DPAPI, revoke, backoff); deja de ser la arquitectura de distribución. Los campos ID/Secret del dock migran a modo oculto de operador y desaparecen del camino feliz (T-041).
- T-033 (BYO-app) sigue válida como validación de operador; la validación del producto distribuible requiere backend (nueva matriz, no reescribir T-033).
- Nuevas dependencias: hosting + DB + secret manager + dominio/TLS + verificación OAuth Google (`youtube.force-ssl`) + cuenta Kick app del proyecto. Coste pequeña escala estimado: VPS modesto + gestionados mínimos (detalle en ARCHITECTURE-BACKEND §10, orden de magnitud, no diseño definitivo).
- Privacidad: el backend ve identidad, tokens, metadata y timestamps; minimización obligatoria (solo refresh + user-id + display + scopes + expiry persistidos; contenido de títulos solo en tránsito/logs de estado sin texto completo).
- Latencia: +1 RTT backend por operación de metadata (imperceptible frente a OAuth/red proveedor); todo async; degradación controlada si el backend cae (Twitch directo sigue; YouTube/Kick informan `Backend unreachable`, reintento con backoff, sin bloquear OBS/streaming).
- Evolución: 4ª plataforma = nuevo adapter, sin tocar el kernel.

## 9. Preguntas abiertas (no bloquean la decisión)

1. Proveedor hosting/DB/secret-manager concreto y región.
2. Formato exacto del session token (opaco vs JWT firmado) y su TTL.
3. Estrategia anti-abuso cuantitativa (cuotas por instalación/cuenta).
4. Confirmación del tier de verificación `youtube.force-ssl` en Console (T-037 Q8).
5. Si el adapter Twitch backend llegará a implementarse (paridad operativa) o el directo es permanente.
