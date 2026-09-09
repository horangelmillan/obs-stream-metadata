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
