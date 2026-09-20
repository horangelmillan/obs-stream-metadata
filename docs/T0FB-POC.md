# T0FB-POC — FB-1 PoC offline + sondas manuales (T-070, sin código de producto)

> Fecha: 2026-09-20 · Rama: `feat/T-070-fb-1-poc` (convención `docs/GIT.md`
> `feat/<T-id>-slug`; el encargo sugería `poc/fb-1-poct`, se usa `feat/`
> por normativa) desde `master==7c39785` (squash de `9131e64`, verificado:
> `git branch --contains 7c39785` → `* master`).
> Estado: **offline GREEN 28/28 · sondas vivas PENDIENTES del operador**
> (parcial documentado, sin escrituras en esta sesión). **No avanzar a FB-2.**
> Cero código de producto: solo `poc/f0fb/` + `tools/f0fb_live_facebook.py`
> + este doc. Sin commit (prohibido sin autorización).

## 1. Qué demuestra esta PoC y qué no

Sí:

- Validadores offline FB (título 1–254 D2, descripción SÍ a diferencia de
  Twitch/Kick, payloads `POST /{live-video-id}`, `privacy EVERYONE` D11,
  `status LIVE_NOW` D6R, `end_live_video` D7, taxonomía §28+§5, scopes sin
  `publish_to_groups`/`email`): **28/28 PASS**.
- Runner redactado estilo `tools/t030_live_*.py` con las 3 sondas del
  brief §9/H1 (list-H1, update+read-back, golive con doble llave).
- Gate de parada respetado: `golive` sin aprobación se niega por diseño;
  H1 con SIN MATCH obliga a parar y registrar (el runner lo imprime).

No (requiere operador con navegador + app `manage-streams` en
Development; el agente no puede dar consentimiento OAuth humano ni maneja
secretos):

- Exchange real code→token, `list live_videos` vivo, update vivo con
  read-back, transición `LIVE_NOW` (sonda C: bloqueada hasta aprobación
  explícita + flag `--i-understand-live` + confirmación `SI`).
- `privacy` update post-create (D11: confirmar empíricamente en sonda B).
- Enum exacto de `status` en lectura para el indicador (filtros
  documentados `LIVE, VOD, SCHEDULED_LIVE`; H pendiente).
- Page 100+ / App Review / business verification (D12/D14: fuera de FB-1).

## 2. Decisiones del brief que mandan (D1–D14, extracto operativo)

| # | Operativa en FB-1 |
|---|---|
| D1/D2 | Metadata viable título+desc; título ≤254; descripción SÍ existe |
| D3 | Perfil (`publish_video`) + Page (`pages_manage_posts/pages_read_engagement/pages_show_list`) |
| D4/D5 | Independent BYO-app + Managed adapter (FB-2/FB-3, no aquí) |
| D6R/D7 | ON `status=LIVE_NOW` sobre vídeo existente (sonda C, con aprobación); OFF `end_live_video` |
| D11 | Audiencia `privacy={"value":"EVERYONE"}` (sonda B la valida) |
| D8 | Sin refresh clásico: `190`→`SESSION_EXPIRED`→reconnect; page token sin expiración |
| D9 | E2E por perfil (60 d OK); Page real pendiente Page 100+ |
| D10 | Cero Aitum/RTMP/keys; revoke `DELETE /{user-id}/permissions` |
| D12/D14 | Managed-terceros exige Live+Advanced (coste cero); sin autónomo el techo es Independent |
| D13 | Agnóstico a ingesta (H1: el vídeo de la herramienta externa debe listar) |

Fuentes Meta 2026-09-20 (Graph v25/v26) en `docs/T0FB-RESEARCH.md` §B.

## 3. Evidencia offline (comando + salida)

```text
$ python -m unittest discover -s poc/f0fb -v
...
Ran 28 tests in 0.001s
OK
```

28 checks: título (4) + descripción sin límite inventado (3) + auth URL
sin secret (3) + payloads mínimos sin `channel_description/stream_title/snippet`
(6) + clasificación §28+§5 incl. 190/1363120/1363144/10/613 (8) + scopes
sin groups/email (4). Ficheros: `poc/f0fb/fb_validators.py`,
`poc/f0fb/test_fb_validators.py` (TDD: test rojo `ImportError` primero,
luego GREEN mínimo).

## 4. Evidencia runner (comando + salida, sin red)

```text
$ python -m py_compile poc/f0fb/fb_validators.py tools/f0fb_live_facebook.py poc/f0fb/test_fb_validators.py
compile:True
$ python tools/f0fb_live_facebook.py --help   # uso con --probe list-only|update|golive
$ python tools/f0fb_live_facebook.py --app-id X --probe golive
REFUSE golive: requiere aprobacion explicita del operador (--i-understand-live + confirmacion interactiva). Sonda C bloqueada por diseno.
```

Secretos solo memoria/entorno (`FB_APP_SECRET`/`--app-secret`/getpass),
revoke `DELETE /permissions` best-effort al final, cero tokens en salida.

## 5. Sondas vivas — procedimiento operador (pendientes, una por una)

Precondiciones: app `manage-streams` (Consumer) en Development + rol propio
o test users; `http://localhost:<puerto>/cb` registrado (dev); cuenta real
solo vía perfil D9.

```text
A. H1 list-only (solo lectura, sin riesgo):
   con la ingesta externa en vista previa →
   python tools/f0fb_live_facebook.py --app-id <ID> --probe list-only --producer-id <ID-de-URL-Live-Producer>
   Esperado: el ID aparece con status UNPUBLISHED. Si SIN MATCH → parar, FINDINGS, preguntar.
B. update en vista previa (con read-back):
   python tools/f0fb_live_facebook.py --app-id <ID> --probe update --title "FB-1 <ts>" --desc "..." [--live-video-id <ID>]
   Esperado: POST 200 + read-back idéntico + visible en Live Producer. Incluye privacy D11 si el operador lo autoriza.
C. golive (SOLO con aprobación explícita, doble llave):
   python tools/f0fb_live_facebook.py --app-id <ID> --probe golive --live-video-id <ID> --i-understand-live  → escribir SI
```

Criterio de parada: sonda en rojo detiene el ciclo; se informa evidencia +
hipótesis y se espera visto bueno. En esta sesión no se ejecutó ninguna
sonda viva (sin consentimiento humano posible): estado PARCIAL documentado.

## 6. Baselines colaterales (no regresión de producto)

- `python -m unittest discover -s backend/tests` → `Ran 216 tests OK (skipped=10)`
  (PROJECT_CONTEXT decía 215; deriva +1 test, sin fallos).
- `git status`: solo `M docs/BACKLOG.md` (T-070→en-progreso) + untracked
  `poc/f0fb/`, `tools/f0fb_live_facebook.py`, `docs/T0FB-POC.md`. Cero toques
  a `src/`, `backend/`, instalador o `dist/`.

## 7. Gate FB-1

- [x] Validadores 28/28 offline con evidencia
- [x] Runner redactado estilo T-030 con revoke
- [x] Sonda C bloqueada sin aprobación (verificado por salida)
- [x] Sonda A H1-perfil viva: NEGATIVA (preview activo, lista 0 items) → F-065
- [x] Hallazgo PKCE: exchange con secret en app nativa → 400 "configured as a desktop app"; con PKCE pasa (F-065, confirma D4)
- [x] Sonda A pata Page descartada (destino es perfil, sin Page vinculada)
- [x] Sonda B' objeto propio PASS (create+update+read-back+delete, F-067)
- [x] Runner migrado a PKCE (exchange con secret imposible en nativas; verificado compile+REFUSE+scan)
- [x] Sonda C golive PASS (objeto nuevo, en vivo real, ver §11)
- [x] BACKLOG T-070 en-progreso, sin commit
- [x] Cierre total FB-1 → completo (A negativa documentada + B' + C en verde)

## 11. Sonda C 2026-09-20 (go-live real + stop + limpieza, PASS)

```text
$ POST /me/live_videos {title:"FB-1 sonda C (prueba, se borra)"} → 200 id=28237427212586319
$ POST /<id> {status:LIVE_NOW} → 200
$ READ → status=LIVE (visible en directo en el perfil)
$ POST /<id>?end_live_video=true → 200
$ DELETE /<id> → 200 (limpieza VOD; preview del operador intacto)
```
D6R/D7 validadas empíricamente. Sin privacy explícita (exposición mínima).

## 8. Sonda A viva 2026-09-20 (perfil, veredicto)

```text
$ <auth URL v26.0 dialog/oauth scope=publish_video> → consent → callback localhost:9009 OK (state coincide)
$ exchange con client_secret → HTTP 400 {"code":1,"OAuthException":"...configured as a desktop app"}
$ exchange con PKCE (challenge S256, sin secret) → Token OK (375 chars)
$ GET /me/live_videos?fields=id,title,description,status → 200, data=[]
H1 SIN MATCH (producer-id no listado; preview externo activo confirmado por el operador)
```

Lección: en apps Nativa/Desktop el secret NO sirve para el exchange
(aunque sea BYO y correcto); PKCE es la vía. El runner `tools/f0fb_live_facebook.py`
debe migrar a PKCE antes de las sondas B/C (pendiente, sin commit).

## 9. Sonda A-direct 2026-09-20 (ID de Live Producer, veredicto)

```text
$ GET /1354287230111091?fields=id,title,description,status (token PKCE válido)
→ HTTP 400 {"code":100,"error_subcode":33,"GraphMethodException":
  "Unsupported get request. Object ... does not exist, missing permissions, or does not support this operation"}
$ edge /me/live_videos → 200, 0 items (consistente: lectura de edge no soportada + objeto web no visible)
H1-DIRECT SIN MATCH → F-066. La vista previa creada por web (paso 2/3, perfil,
sin transmitir) no es legible por API con este token.

## 10. Sonda B' 2026-09-20 (objeto propio, PASS total)

```text
$ POST /me/live_videos {title:"FB-1 sonda B", description:"objeto de prueba, se borra solo"}
→ HTTP 200 id=28237388169256890
$ GET /<id>?fields=id,title,description,status → 200 status=LIVE title='FB-1 sonda B'
$ POST /<id> {title:"FB-1 sonda B editado", description:"editado + privacy", privacy={"value":"EVERYONE"}} → 200
$ READ-BACK → 200 title/desc idénticos → UPDATE-VERIFY: MATCH (D11 validada)
$ DELETE /<id> → 200 (limpieza; preview del operador intacto)
```
F-067. Vía de producto: objetos propios (patrón broadcast persistente).
