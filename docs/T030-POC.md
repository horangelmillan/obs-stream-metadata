# T-030 — PoCs OAuth + título por proveedor (P3)

> Estado: **PASS** — lógica offline verificada (selfcheck 28/28) +
> verificación viva de los tres proveedores (OAuth real + título real +
> read-back) completada el 2026-09-08. P3 cerrada; P4 (T-031) desbloqueada.

## 1. Qué demuestra esta PoC y qué no

Sí:

- Flujo OAuth documentado por proveedor con fuentes oficiales (2026-09-08).
- PKCE S256 + `state` generados y verificados (vector RFC 7636 en selfcheck).
- URLs de autorización, validadores de título (§20), payloads mínimos.
- Clasificación HTTP §28 (`204` = éxito, `401/403/404/429/5xx` mapeados).
- Receptor localhost mínimo (`tools/t030_oauth_callback.py`, stdlib, un uso).

No (va en P5 o requiere cuentas del operador):

- Intercambio real code→token ni llamadas HTTP vivas desde el dock (resuelto en P4/T-031 para UI; el procedimiento manual §5 sigue válido para los runners);
- storage definitivo, refresh robusto (el dock T-031 ya reintenta una vez ante 401).

## 2. Matriz por proveedor

|  | Twitch | YouTube | Kick |
|---|---|---|---|
| OAuth | 2.0 **Device Code Flow** (cliente público, sin secret en el binario) o Authorization Code (exige secret en servidor) | 2.0 **installed-app** + PKCE, cliente tipo **Desktop app** (sin secret) | 2.1 Authorization Code + PKCE S256, `state` obligatorio |
| Autorizar | `POST https://id.twitch.tv/oauth2/device` → `device_code` + `user_code` + `verification_uri` (`twitch.tv/activate`) | Navegador del sistema → `https://accounts.google.com/o/oauth2/v2/auth` | Navegador del sistema → `GET https://id.kick.com/oauth/authorize` |
| Token | `POST https://id.twitch.tv/oauth2/token` (`grant_type` device) | `POST https://oauth2.googleapis.com/token` (`authorization_code` + `code_verifier` **+ secret local**: Google lo exige incluso en Desktop, F-019) | `POST https://id.kick.com/oauth/token` (`authorization_code` + `code_verifier` **+ secret**) con cabeceras de navegador (Cloudflare 1010 bloquea urllib, F-022) |
| Scope MVP | `channel:manage:broadcast` (único) | `https://www.googleapis.com/auth/youtube.force-ssl` (único) | `channel:write` (+ `channel:read` solo si hace falta identidad) |
| Redirect | N/A en device flow (sin callback) | loopback `http://127.0.0.1:<puerto>` (listener aleatorio) | `http://localhost/...` (**no** `127.0.0.1`: bug NextJS, ver F-017) |
| Refresh | refresh de **un solo uso**, expira a 30 días inactivo; access ~4 h | refresh de larga vida (guardarlo; límites por cliente/usuario) | `POST .../oauth/token` con `grant_type` refresh |
| Validar | `GET https://id.twitch.tv/oauth2/validate` (tras cada sesión) | campo `scope` en la respuesta del token | `POST https://id.kick.com/oauth/token/introspect` (Bearer) |
| Revocar | `POST https://id.twitch.tv/oauth2/revoke` | `POST https://oauth2.googleapis.com/revoke` | `POST https://id.kick.com/oauth/revoke` |
| Título | `PATCH https://api.twitch.tv/helix/channels?broadcaster_id=<id>` + `Client-Id` + Bearer, `{"title":"..."}` (≤140). `broadcaster_id` = usuario del token | `PUT https://www.googleapis.com/youtube/v3/liveBroadcasts?part=snippet` con `{"id","snippet"}` (snippet completo del GET previo + título sustituido; **sin** `contentDetails`: da 400 `unexpectedPart`, F-021). Listar con `mine=true` solo (filtros excluyentes, F-020). Título 1–100, desc ≤5000 | `PATCH https://api.kick.com/public/v1/channels`, `{"stream_title":"..."}`. `204` = éxito (sin body). Verificar **en directo**: en offline el 204 aplica pero el título no es legible hasta el directo (F-023); read-back por doble vía (`GET channels` + `GET users/livestreams`). Límite: el que diga el servidor |
| Descripción | NO equivalente (F-001) | SÍ (`snippet.description`) | NO equivalente (`channel_description` = canal, F-003) |

Fuentes (consultadas 2026-09-08): Twitch `dev.twitch.tv/docs/authentication/getting-tokens-oauth`
(device flow público en Windows, refresh un solo uso); Google
`developers.google.com/youtube/v3/guides/auth/installed-apps` (loopback +
PKCE para desktop); Kick `KickDevDocs/getting-started/generating-tokens-oauth2-flow.md`
(OAuth 2.1, endpoints id.kick.com, workaround localhost); Google
`.../live/docs/liveBroadcasts/update` (ed. 2026-08-18, semántica PUT +
errores `invalidTitle/invalidDescription/liveBroadcastNotFound/...`).

## 3. Implicación client secret (no esconderla)

- **Twitch:** el Authorization Code exige secret → en un plugin de
  escritorio el secret embebido no es seguro. PoC usa **Device Flow**
  (cliente público, sin secret). F-015.
- **YouTube:** Google exige `client_secret` en el intercambio incluso
  para clientes Desktop (F-019). Para app instalada el secret no es
  confidencial (extensión del ID); la PoC lo pide solo local
  (`--client-secret` / env / pregunta oculta). P5 debe decidir custodia
  (app registrada por el usuario o intermediario); no embeber un secret
  compartido.
- **Kick:** el token endpoint exige `client_secret` incluso con PKCE.
  Un plugin desktop no puede custodiarlo → documentado como riesgo
  abierto: o app registrada por el usuario (secret local suyo) o
  intermediario. No embeber un secret compartido. F-017.

## 4. Storage provisional P3

Tokens solo en memoria del proceso + intercambio manual por el operador.
Nada en disco, nada en repo, nada en logs (ver `SECURITY.md`).
Storage OS-apropiado (Windows) se investiga en P5 (T-032).

## 5. Procedimiento manual reproducible (operador, una vez por proveedor)

Requisito: app registrada por el operador en cada plataforma
(Twitch: consola dev; Google: Cloud Console cliente Desktop + YouTube
Data API habilitada; Kick: portal dev con redirect `http://localhost:.../cb`).
Los valores `<...>` los pone el operador; nunca se commitean.

```text
1. Generar state (+ PKCE donde aplique) — ver provider_poc.h.
2. Twitch (preferido): `python tools/t030_live_twitch.py --client-id <ID> --title "T030 LIVE Twitch <ts>" [--restore "<original>"]`
   (device flow con salida redactada: nunca imprime tokens; los revoca al final).
   Manual alternativo: POST /oauth2/device (client_id + scope) → abrir verification_uri,
            introducir user_code → sondear token hasta obtenerlo.
   YouTube/Kick: levantar tools/t030_oauth_callback.py con el state →
            abrir la URL de autorización en el navegador → autorizar.
3. Intercambiar code (curl, una vez) → guardar tokens FUERA del repo.
4. Título de prueba:  antes "T-030 TEST <hora>" → aplicar → "T-030 PASS <hora>".
   Twitch:  PATCH /helix/channels (broadcaster_id del token).
   YouTube: GET liveBroadcasts.list(mine, active/upcoming) → elegir broadcast
            → PUT update con recurso fusionado (youTubeSnippetPayload).
   Kick:    PATCH /public/v1/channels {"stream_title":"..."} → esperar 204.
5. Verificar en la web de la plataforma que el título cambió.
6. Negativos: token truncado (401), scope vacío (403), título de 141/101
   caracteres (400 o validación local), broadcast inexistente (404),
   cancelación del consentimiento (callback limpio, sin estado corrupto).
```

## 6. Aceptación T-030 (estado real)

```text
Twitch:  OAuth PASS (device flow, login sonokigame, user 182281392, scope ok)
         title-update PASS (PATCH 204 + read-back "T030 LIVE Twitch 20260908")
         verificación externa PASS (GET read-back; web pendiente del operador)
         tokens revocados tras la prueba. Estado: PASS (2026-09-08).
YouTube: OAuth PASS (installed-app + PKCE + loopback, cuenta sonokigame)
         token PASS (access + refresh; secret Desktop local, F-019)
         broadcast PASS (list mine=true solo, F-020; elegido Y9yFOeQw83s ready)
         title-update PASS (PUT 200 solo id+snippet, F-021; "T030 LIVE YouTube test1")
         read-back PASS (coincide; revoke access 200, refresh 400 best-effort)
         Estado: PASS (2026-09-08). Restore del título original: pendiente del operador.
Kick:    OAuth PASS (2.1 + PKCE + localhost, slug sonokigame, user 128456005)
         token PASS (access+refresh 48 chars, 7200s, UA navegador por Cloudflare 1010, F-022)
         PATCH PASS (204 "T030 LIVE Kick test1")
         read-back PASS en directo (channels + users/livestreams coinciden;
         en offline el 204 aplica pero no es legible, F-023)
         revoke access+refresh 200. Estado: PASS (2026-09-08).
Lógica offline (URLs/PKCE/validadores/payloads/HTTP): PASS 28/28
P3 = PASS. P4 (T-031) desbloqueada.
```
