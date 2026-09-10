# ARCHITECTURE-BACKEND — referencia de desarrollo (ADR-009, T-038)

> Decisión: **MODIFY** — backend centralizado para YouTube + Kick, Twitch directo.
> Trazabilidad: `AGENTS.md` → T-030 → T-032/ADR-008 → T-035 → T-036 → T-037 → T-038/ADR-009.

```text
                    ┌─────────────────────────┐
                    │       OBS PLUGIN        │
                    │ UI · UX · estado visual  │
                    │ API client (backend +    │
                    │   Twitch directo)        │
                    │ Metadata commands        │
                    └────────────┬────────────┘
                                 │ HTTPS (TLS)
                                 ▼
                    ┌─────────────────────────┐
                    │   CENTRAL BACKEND API   │
                    │     Shared Kernel        │
                    │ Accounts · OAuthSession  │
                    │ MetadataCommand ·        │
                    │ Capability · ErrorModel  │
                    └────────────┬────────────┘
                  ┌──────────────┼──────────────┐
                  ▼              ▼              ▼
           Twitch Adapter  YouTube Adapter  Kick Adapter
           (reservado;      │              │
            directo hoy)    ▼              ▼
                         Google           Kick
                           API             API
```

## 1. Límites del Shared Kernel vs adapters

- **Kernel:** `Account{provider, providerUserId, displayName, scopes, connectedAt}`,
  `Connection{account, status}`, `OAuthSession{id, provider, state, verifierRef, expiresAt}`,
  `ProviderId`, `AccessToken`/`RefreshToken` (opacos fuera del adapter),
  `MetadataUpdateCommand{platforms, title, description?, broadcastId?}`,
  `ProviderCapability{title, streamDescription}`, `ErrorModel` (taxonomía `AGENTS.md` §28),
  `AuthorizationState`. Regla: el kernel no nombra endpoints, secretos ni quirks de proveedor.
- **Adapters:** OAuth, scopes, token handling, API client, reglas de metadata, errores propios:
  YouTube (GET previo + `PUT id+snippet`, `mine=true` + filtro local, F-016/F-020/F-021),
  Kick (redirect exacto `localhost`, UA navegador, 204, read-back en directo, F-017/F-022/F-023),
  Twitch reservado (DCF + `/validate`, F-015).

## 2. Matriz de decisión (resumen)

| Criterio | Directo | Backend selectivo | Backend central | BYO-app |
|---|---|---|---|---|
| UX objetivo | Solo Twitch | **Sí** | Sí | No |
| Seguridad secretos | Falla YT/Kick | **Resuelve donde hay secreto** | Resuelve todo | Desplaza el secreto al usuario |
| Open-source | Falla YT/Kick | **OK (nada sensible en repo)** | OK | OK pero inusable como producto |
| Latencia/coste/complejidad | Mínimos | **Mínimo añadido útil** | Sobrecoste en Twitch sin beneficio | Nulos, a costa de UX |
| Privacidad/disponibilidad | Mejor | **Un backend que minimizar/robustecer** | Más superficie | Mejor, pero sin producto |
| Escalabilidad | Baja | **Alta (adapter nuevo)** | Alta | Nula |

Decisión: **backend selectivo con API centralizada** (= propuesta C con alcance B): un solo backend, adapters por proveedor, Twitch directo hoy.

## 3. Responsabilidades

- **Plugin:** UI, estado visual, Connect, abrir navegador, recibir resultado, comandos de metadata,
  errores normalizados, HTTPS+timeouts+retry/backoff de transporte (F-031), async Qt (F-025),
  DPAPI solo para sesión Twitch + identidad de instalación. Nunca: `client_secret` de ningún
  proveedor, `refresh_token` YouTube/Kick, secretos de backend.
- **Backend:** credenciales de app (secret manager), orquestación OAuth completa (URL, callback HTTPS,
  exchange, refresh, revoke), custodia cifrada de user-tokens, llamadas metadata YouTube/Kick,
  normalización de errores, rate-limit/abuso/auditoría, rotación, aislamiento por proveedor/cuenta.

## 4. OAuth / Connect (YouTube/Kick)

`OBS --Connect--> Backend /connect/{provider} --> {authorizationUrl, sessionId}`
`--> Browser --> Provider --> callback HTTPS backend --> exchange --> custodia`
`--> {sessionToken opaco corto, displayName} --> OBS --> Connected as ...`
Twitch: DCF directo sin cambios (user_code + `twitch.tv/activate`, polling, `/validate`).

## 5. Matriz de secretos (justificada)

| Credencial | OBS | Backend | Provider |
|---|---|---|---|
| `Client ID` Twitch | Sí (público por docs Twitch) | No necesario | Sí |
| `Client ID` Google | No (ToS; lo aporta el backend) | Sí (config, no en repo) | Sí |
| `Client ID` Kick | No | Sí | Sí |
| `Client Secret` Google/Kick | **Jamás** | Sí, solo en secret manager | Sí |
| `refresh_token` YT/Kick | **Jamás** | Sí, cifrado en reposo | Sí |
| `refresh_token` Twitch | Sí, DPAPI (F-029) | No | Sí |
| `access_token` | Memoria mínima en tránsito TLS | Memoria mínima / bóveda | Sí |
| Credencial plugin↔backend | Identidad instalación (no-secreta) + sesión corta rotable | Emite/valida sesiones | N/A |

## 6. Auth plugin↔backend (tensión explícita)

Sin secreto permanente embebido (recrearía el problema BYO-app/embebido). Modelo:
UUID de instalación generado en primer arranque (persistido en config DPAPI, no-secreto)
+ `sessionToken` opaco de corta vida emitido tras OAuth válido + challenge/response por intento
sensible + rate-limit por instalación y por cuenta. Anonimato total = abuso; secreto
permanente = extracción: el punto medio es identidad no-secreta + sesiones cortas + límites.

## 7. Tokens de usuario

Custodia donde se usan: backend (YT/Kick, cifrado en reposo, rotación, revoke en Disconnect
preservando F-030) y DPAPI local (Twitch). Tránsito siempre TLS; logs solo longitudes/códigos
(heredado T-032). Sin `refresh_token` YT/Kick en el plugin ni en disco del cliente.

## 8. Rendimiento y disponibilidad

Por operación de metadata: +1 RTT plugin→backend (ms, imperceptible frente a OAuth y API del
proveedor); todo async; timeouts + backoff acotado (F-031). Si el backend cae: YouTube/Kick
informan `Backend unreachable` con reintento, Twitch directo sigue operativo, OBS/streaming
nunca se bloquean. Si el proveedor cae: se propaga su error normalizado (§28).

## 9. Privacidad (minimización)

Backend persiste: `providerUserId`, `displayName`, `scopes`, `refresh_token` cifrado,
`expiry`, `installationId`. No persiste: texto de títulos/descripciones (solo tránsito;
logs con longitudes/códigos). OAuth: `state/verifier` solo durante la sesión. Borrado total
al Disconnect + revoke en proveedor.

## 10. Operación (orden de magnitud, no diseño definitivo)

Un servicio HTTPS pequeño + DB gestionada mínima + secret manager + dominio/TLS +
verificación OAuth Google. Coste pequeña escala: un VPS modesto o tier básico cloud
(decenas USD/mes como techo inicial típico, a confirmar con proveedor/región).
Backups de DB, monitoreo 401/403/429/5xx por adapter, rotación de secrets sin downtime,
proyectos Google test/prod separados.

## 11. Evolución

4ª plataforma = nuevo adapter contra el mismo kernel + scopes/capabilities declaradas;
el plugin añade una fila de Connect sin cambios de protocolo. El adapter Twitch backend
queda reservado para paridad futura sin rediseño.

## 12. Estado de implementación (T-043 PASS, 2026-09-09)

Fundación stdlib-only en `backend/`: `kernel.py`, `ports.py`, `errors.py`, `config.py`,
`stores.py` (dev-only marcados), `auth.py` (frontera T-044), `http_server.py` + `app.py`
(`GET /health|/ready|/version`), `adapters/{youtube,kick,twitch}.py` (estructura sin red),
27 tests (`backend/tests/`) + smoke real. Sin llamadas a proveedores ni OAuth productivo.
Stack producción (runtime/DB) no fijado: la fundación no lo condiciona.

## 13. Auth Plugin↔Backend (T-044 PASS, ADR-010)

Bootstrap anónimo rate-limitado → secreto por instalación (DPAPI `backendInstall`)
→ sesiones opacas 30 min (memoria) → refresh con firma HMAC + nonce un solo uso →
revocación por sesión o instalación. Endpoints: `POST /auth/bootstrap|/session|/refresh|/revoke|/installation/revoke`.
Cliente Qt async en `src/backend_auth.*` (sin UI; wiring en T-048). Keypair asimétrico
descartado (HMAC-SHA256 stdlib, propiedades equivalentes). Públicas: `/health|/ready|/version`.

## 14. YouTube Adapter (T-045 PASS, ADR-011)

`POST /connect/youtube` (sesión T-044) → transacción `{state, PKCE, TTL 600 s}` →
`{transaction_id, authorization_url}` → browser → `GET /connect/youtube/callback`
(validación state/expiración/binding, single-use) → exchange con secret →
`TokenStore` → identidad `channels.mine` → `Connection`. `GET .../status`,
`POST .../disconnect` (borrado + revoke best-effort). Refresh single-flight,
margen 120 s. Al plugin: `{provider, status, account{id, displayName}}` —
nunca tokens/secret/verifier. Live PASS contra Google 2026-09-09.

## 15. Kick Adapter (T-046 PASS, sin ADR nuevo)

Mismo patrón T-045 sobre `ConnectService` generalizado (multi-provider vía port
`fetch_identity`; kernel intacto). Diferencias encapsuladas en el adapter:

| Aspecto | YouTube | Kick |
|---|---|---|
| Authorization | `accounts.google.com/o/oauth2/v2/auth` | `id.kick.com/oauth/authorize` |
| Client Secret | requerido (nuestra config, T-035) | requerido (T-036) |
| PKCE | S256 + `access_type=offline` | S256 |
| State | un solo uso, TTL 600 s | un solo uso, TTL 600 s |
| Redirect | loopback `127.0.0.1`, flexible con path (Observado) | `localhost`, match exacto incl. path (F-017; URI nueva debió registrarse) |
| Scopes | `youtube.force-ssl` | `channel:write channel:read` |
| Token exchange | `oauth2.googleapis.com/token` | `id.kick.com/oauth/token` (+UA navegador, F-022) |
| Refresh | conserva anterior si falta; `invalid_grant`→re-conectar | renueva ambos según docs; mismo fallback |
| Revoke | form `{token}` | query `?token=&token_hint_type=` (oficial), best-effort |
| Identity | `channels?part=snippet&mine=true` → id + title | `GET /public/v1/channels` → `broadcaster_user_id` + `slug` |
| Errors | `invalid_grant`→rejected… (ADR-011) | `invalid_grant`→rejected; `1010`/403→unavailable; resto análogo |

Rutas: `POST /connect/kick`, `GET .../callback`, `GET .../status`,
`POST .../disconnect` (provider erróneo → `invalid_request`). Rate-limit T-044
reutilizado. Live PASS contra Kick 2026-09-09 (connect→callback→exchange→
identidad→disconnect+revoke).

## 16. Dos modalidades (ADR-012, 2026-09-09)

Este backend es la **infraestructura del modo Administrado**, no un requisito
del producto: el modo Independiente opera directo contra las APIs con
credenciales del usuario (DPAPI) y no necesita este backend. El contrato
plugin↔backend (`/auth/*`, sesiones T-044, `/connect/*`) es estable y explícito
para que el wiring Managed (T-048) no redefina seguridad. Ver matriz
KEEP/MODIFY/DEPRECATE/NEW y plan en el informe de auditoría; referencia de
producto: `obs-stream-metadata-arquitectura-dos-modalidades.md`.

## 17. Apéndice de auditoría dos-modalidades (2026-09-09, sin código)

### Reutilización verificada (§7 dirección; todo localizado, nada duplicado)

`metadata.*`, Device Flow Twitch (`metadata_dock.cpp:579-582,1048`),
OAuth YouTube/Kick directo + PKCE/state/callbacks (`:708-801`),
QNAM async (`:204-205`, `backend_auth.*`), refresh/revoke/backoff,
error mapping, DPAPI/`secure_store.*`/`accounts.json` (`:409`),
`backend_auth.*` (sin cablear), kernel/ports/sessions/bootstrap/transactions,
adapters + TokenStore/ConnectionStore, selfchecks 68/68, runners `tools/` +
lives T-045/T-046.

### Proveedores: técnica vs política vs modalidad (§8)

| Aspecto | Twitch | YouTube | Kick |
|---|---|---|---|
| Capacidad | título PATCH | título+desc `liveBroadcasts` | título PATCH 204 |
| Credenciales | Client ID público, sin secret (DCF) | ID+secret (T-035) | ID+secret (T-036) |
| Distribución secret | N/A | prohibida en binario público (T-037) | secreto real servidor |
| Redirect | N/A (device) | loopback flexible con path (Observado) | `localhost`, match exacto (F-017) |
| Independiente | directo siempre | app propia + verificación Google (OPEN) | app propia, registrar URI exacta |
| Administrado | directo (sin backend por simetría) | backend + app prod | backend + app prod |

### Seguridad por modalidad (§9; problema → propuesta, sin auto-fix)

Independiente: IDs/secrets solo DPAPI local, nunca al backend; riesgo = perfil
copiado/máquina comprometida (no mitigable, documentar). Administrado: secrets
solo secret-manager, tokens cifrados, sesiones T-044, TLS, rate-limit; riesgo =
abuso/escala Superficie (mitigar con cuotas T-051, rotación, monitoreo).
Común: nada en Git/logs/UI, browser del sistema, sin WebView ni embebidos.

### Backend por componente (§10)

`kernel/ports/errors/config/logging` KEEP AS BASE; `auth/sessions/bootstrap/
transactions/adapters/stores` KEEP AS BASE (Managed); stores memoria/Env =
PROTOTYPE (prod: secret-manager + DB cifrada, T-054); `AllowAllRateLimiter`
sustituido en T-044 (FixedWindow; distribuido = INCOMPLETE); nada a DEPRECATE.

> Nota 2026-09-10 (T-054, sin reescribir la auditoría): la dirección
> "secret-manager + DB cifrada" se concretó como SQLite stdlib 0600 +
> secretos por fichero + TLS builtin opcional (ver §19 y ADR-013); el
> gate `AllowAll` en producción se añadió en T-054.

### Correspondencia modal (T-049, sin código aquí)

`meta::ConnectionMode` vive solo en C++ (`metadata.h`). Este backend no lo
duplica: su `Connection` existe únicamente en contexto Managed, e Independent
jamás lo toca. T-041 persistirá el modo en cliente; el backend no necesita
cambios para ello.

### UX actual vs modal (§11)

Dock hoy: 5 campos universales (tw/yt/kk ID + yt/kk secret), Connect/Disconnect
por proveedor, `Connected as`, selector broadcast YT, Apply secuencial,
resultados independientes. Común siempre: metadata/Apply/estados/errores.
Varía: selector de modalidad (NEW) + campos contextuales (Twitch: solo ID;
YT/Kick: ID+secret) + wiring Managed (T-048). Sin duplicar la UI.

## 18. Matriz Provider × Mode (T-050, vigente 2026-09-09)

Estados: implementado = código existe; validado = evidencia viva citada;
pendiente = representable sin implementación; no hay casos "no soportado".

| Provider | Independent | Managed | Evidencia |
|---|---|---|---|
| Twitch | implementado + validado (DCF directo, DPAPI, T-031 live) | pendiente: sin servicio backend; dock informa "direct only", cero red | T-031; `managedSupported()==false` |
| YouTube | implementado + validado (OAuth directo, T-031 live) | implementado + validado (T-045 + T-048; live 2026-09-09 revalidado) | T-031; T-045; live este turno |
| Kick | implementado + validado (OAuth directo, T-031 live) | implementado + validado (T-046 + T-048; live 2026-09-09 revalidado) | T-031; T-036; T-046; live este turno |

Modelo/UI/routing/OAuth/storage/backend por celda: Independent = directo +
DPAPI + navegador; Managed (YT/Kick) = `backend_auth` + ConnectService +
SecretStore/TokenStore backend. Esta tabla es LA matriz vigente; no duplicarla.

## 19. Entorno explícito DEV/PROD (T-053, vigente 2026-09-10)

`ConnectionMode != environment` (ADR-012): DEV/PROD es identidad del
despliegue backend, no modalidad de conexión. No existe infraestructura
productiva todavía; esta sección define la separación para que el futuro
despliegue (T-054) no pueda cruzarse con desarrollo por accidente.

- Backend: `STREAM_META_BACKEND_ENV` → `Settings.env` (`backend/config.py`
  + `backend/environment.py`). Valor desconocido = fail-fast en arranque;
  ausente = `development` (dirección segura). `production` exige stores sin
  marca `DEVELOPMENT_ONLY` y `public_base_url` HTTPS (`create_app` gates);
  mismatch = excepción, nunca fallback ni autocorrección. Binding
  secreto↔entorno = el store elegido por despliegue (`EnvSecretStore` es
  DEVELOPMENT_ONLY, inutilizable en producción); sin renombres de variables.
- Observabilidad: `/version` expone `env` (sin secretos) para que el
  operador verifique contra qué entorno habla.
- Plugin: `secure::Data.backendBaseUrl` liga `backendInstall` al backend
  que la emitió; `loadInstallation` fail-closed ante mismatch (→
  `StorageError` → re-bootstrap automático existente, sin reutilizar el
  secreto en otro backend, sin fallback).
