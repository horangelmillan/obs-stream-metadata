# AGENTS.md --- Stream Metadata Controller for OBS Studio

> Documento de reglas duraderas para agentes: límites de producto,
> decisiones vigentes y flujo de trabajo. No es un informe de estado:
> el estado actual vive en `docs/STATE.md` y el trabajo pendiente en
> `docs/BACKLOG.md` (canónico). La investigación previa al desarrollo
> que originó este archivo (2026-09-07) queda conservada como referencia
> técnica histórica en las secciones numeradas; donde el producto la haya
> superado, una nota de vigencia lo indica sin reescribir la historia.
>
> **Jerarquía documental (no duplicar):** `AGENTS.md` = límites y reglas
> duraderas · `docs/BACKLOG.md` = estado canónico de tareas (no cambiar
> estados sin evidencia) · `docs/STATE.md` = resumen breve reconciliado
> con el checkout y el backlog · `docs/ARCHITECTURE-BACKEND.md` + ADRs =
> arquitectura vigente e historia · `docs/DEPLOYMENT.md` = contrato de
> despliegue y estado operativo (hecho verificado vs documentado vs
> pendiente) · `docs/VALIDATION.md` = registro histórico de evidencia
> · `docs/PROJECT_CONTEXT.md` = resumen de recuperación con enlaces,
> no otro backlog · `README.md` = resumen para personas.
>
> **Producto actual (resumen; detalle en STATE/BACKLOG):** plugin Qt6 +
> backend Python con dos modalidades (Independiente BYO-app /
> Administrado vía backend) para Twitch, YouTube, Kick y Facebook
> (perfil; Page de Facebook diferida). Matriz de capacidades §3.

------------------------------------------------------------------------

## 1. Objetivo del proyecto

Construir un plugin para OBS Studio que añada un panel/dock sencillo
dentro de OBS para gestionar metadatos de transmisión.

> Nota de vigencia: el MVP descrito aquí se construyó y cerró (BACKLOG
> T-033 hecha; puerta §50 en verde). El producto actual añade una cuarta
> plataforma, Facebook (perfil; Page diferida, ver BACKLOG T-069–T-073),
> y dos modalidades, Independiente (BYO-app) y Administrado (backend
> propio, ADR-012). Las restricciones de esta sección siguen vigentes.

El MVP debe hacer solamente esto:

1.  Permitir vincular/autenticar las cuentas de las plataformas
    soportadas.
2.  Mostrar qué cuentas están vinculadas.
3.  Permitir introducir:
    -   Título.
    -   Descripción.
4.  Permitir aplicar esos valores a las plataformas habilitadas.
5.  Informar claramente el resultado de cada operación:
    -   éxito,
    -   error,
    -   cuenta no autorizada,
    -   token expirado/revocado,
    -   operación no soportada por la plataforma,
    -   recurso de YouTube no encontrado/no modificable, etc.
6.  No intentar resolver todavía:
    -   multichat,
    -   multistream,
    -   categorías,
    -   tags,
    -   presets,
    -   thumbnails,
    -   analytics,
    -   automatizaciones,
    -   scheduling,
    -   publicación de broadcasts,
    -   control de OBS,
    -   overlays.

El objetivo es **un MVP pequeño y fiable**, no competir con Aitum Stream
Suite.

------------------------------------------------------------------------

# 2. Hallazgo fundamental: título y descripción NO son capacidades equivalentes

Este punto debe considerarse una restricción funcional del proyecto.

## Twitch

La API oficial de Twitch permite modificar el **título** del canal
mediante:

`PATCH https://api.twitch.tv/helix/channels`

Requiere un User Access Token con:

`channel:manage:broadcast`

El campo relevante es:

`title`

La API también permite modificar categoría, idioma, tags y otras
propiedades, pero **esas capacidades están fuera del MVP**.

Importante:

**Twitch no ofrece un campo de descripción del stream equivalente al de
YouTube mediante `Modify Channel Information`.**

Por tanto, el campo `description` del panel no puede asumirse como
portable a Twitch.

## YouTube

YouTube sí tiene un concepto explícito de **live broadcast**,
representado por `liveBroadcast`.

El recurso contiene:

-   `snippet.title`
-   `snippet.description`
-   `snippet.categoryId`
-   `snippet.channelId`
-   `status`
-   `contentDetails`
-   etc.

La operación:

`PUT https://www.googleapis.com/youtube/v3/liveBroadcasts`

permite modificar la información del broadcast.

Por tanto:

-   título → soportado
-   descripción → soportada

Pero YouTube introduce una complejidad que Twitch no tiene:

**el plugin debe saber qué `liveBroadcast` se quiere modificar.**

No basta con conocer el canal.

El API permite consultar broadcasts del usuario mediante
`liveBroadcasts.list`, por ejemplo filtrando por:

-   `broadcastStatus=upcoming`
-   `broadcastStatus=active`
-   `mine=true`

El MVP deberá investigar y probar cuidadosamente cuál broadcast
corresponde al stream que el usuario está preparando.

## Kick

Kick actualmente tiene API pública oficial con OAuth 2.1.

El scope relevante es:

`channel:write`

La API pública expone actualización de metadata de livestream mediante:

`PATCH /public/v1/channels`

El endpoint acepta actualmente `stream_title` y `category_id`;
documentación y evidencia actual también muestran que algunos campos
adicionales pueden ser ignorados aunque la API responda `204`.

**Para el MVP debe considerarse confirmado que Kick permite actualizar
el título del livestream.**

No debe asumirse que Kick ofrece una "stream description" equivalente a
YouTube.

La respuesta/metadata del canal contiene un campo:

`channel_description`

pero eso corresponde a la descripción del canal, no debe confundirse con
una descripción del livestream.

Por tanto:

-   título → soportado
-   descripción del stream → NO asumir que está soportada

------------------------------------------------------------------------

# 3. Matriz de capacidades del MVP

  -------------------------------------------------------------------------
  Plataforma      Autenticación      Actualizar título           Actualizar
                                                             descripción de
                                                                     stream
  --------------- --------------- -------------------- --------------------
  Twitch          OAuth 2.0                         Sí       No equivalente

  YouTube         OAuth 2.0                         Sí                   Sí

  Kick            OAuth 2.1 +                       Sí    No asumir soporte
                  PKCE                                 

  Facebook        OAuth 2.0                         Sí                   Sí
  (perfil; Page                     (vía LiveVideo                (vía LiveVideo
  diferida)                          propio creado                 propio; Page
                                     por el plugin,               diferida)
                                     no el de Live
                                     Producer web)
  -------------------------------------------------------------------------

La UI del MVP debe reflejar esta realidad.

No debe existir una falsa promesa de:

> "Aplicar descripción a todas las plataformas"

cuando dos plataformas no tienen ese concepto equivalente.

Una opción simple es mostrar:

-   Título: `[input]`
-   Descripción: `[textarea]`
-   Twitch: `Título ✓ / Descripción no disponible`
-   YouTube: `Título ✓ / Descripción ✓`
-   Kick: `Título ✓ / Descripción no disponible`

------------------------------------------------------------------------

# 4. OBS Studio: tecnología oficial relevante

La documentación actual de OBS Studio describe tres grandes vías de
extensión:

-   plugins,
-   scripts,
-   WebSocket.

Para este proyecto se necesita un **plugin nativo**, porque queremos un
dock integrado dentro de la interfaz de OBS.

OBS indica que los plugins normalmente se implementan en C/C++ y se
compilan como bibliotecas dinámicas.

Fuentes oficiales:

-   https://obsproject.com/kb/developer-guide
-   https://docs.obsproject.com/plugins
-   https://docs.obsproject.com/reference-modules
-   https://docs.obsproject.com/frontends

------------------------------------------------------------------------

# 5. OBS Frontend API y docks

La API oficial de frontend proporciona específicamente:

`obs_frontend_add_dock_by_id`

y:

`obs_frontend_add_custom_qdock`

La documentación actual indica que `obs_frontend_add_dock_by_id` permite
añadir un `QWidget` como dock y hacerlo accesible desde el menú Docks.

La API también permite:

`obs_frontend_remove_dock`

La API de docks fue añadida en OBS 30.0.

Esto encaja directamente con el MVP.

El plugin no necesita crear una fuente, output, encoder o service de
libobs.

El elemento principal es una **UI de frontend**.

Referencia:

https://docs.obsproject.com/reference-frontend-api

Referencia fuente:

https://github.com/obsproject/obs-studio/blob/master/frontend/api/obs-frontend-api.h

Implementación de referencia:

https://github.com/obsproject/obs-studio/blob/master/frontend/OBSStudioAPI.cpp

------------------------------------------------------------------------

# 6. Qt

El template oficial actual de plugins de OBS contempla explícitamente:

-   `ENABLE_FRONTEND_API`
-   `ENABLE_QT`

y utiliza:

-   Qt6
-   Qt6::Core
-   Qt6::Widgets
-   AUTOMOC
-   AUTOUIC
-   AUTORCC

El template oficial actual:

https://github.com/obsproject/obs-plugintemplate

CMake actual del template:

https://github.com/obsproject/obs-plugintemplate/blob/master/CMakeLists.txt

El template declara actualmente soporte para:

-   Windows + Visual Studio 17 2022
-   macOS + Xcode 16
-   Ubuntu 24.04
-   CMake 3.28+
-   Qt6

Para el entorno objetivo del proyecto inicial, Windows es suficiente,
pero el código no debería introducir deliberadamente dependencias
específicas de Windows si no son necesarias.

------------------------------------------------------------------------

# 7. Tipo de plugin OBS necesario

No parece necesario implementar:

-   `obs_source_info`
-   `obs_output_info`
-   `obs_encoder_info`
-   `obs_service_info`

El plugin es principalmente una extensión del frontend.

La documentación de OBS distingue entre objetos de libobs y
funcionalidades del frontend.

El punto de entrada del módulo sigue siendo el sistema normal de
plugins:

-   `OBS_DECLARE_MODULE()`
-   `obs_module_load()`
-   opcionalmente `OBS_MODULE_USE_DEFAULT_LOCALE()`

Pero el trabajo principal será crear el widget Qt y registrarlo como
dock mediante la Frontend API.

------------------------------------------------------------------------

# 8. Template oficial recomendado

No empezar desde un plugin antiguo encontrado en GitHub.

Usar como referencia primaria el template oficial:

https://github.com/obsproject/obs-plugintemplate

El template actual contiene:

-   boilerplate del plugin,
-   CMake,
-   presets,
-   workflows,
-   soporte para frontend API,
-   soporte para Qt6,
-   integración de dependencias de OBS.

README:

https://github.com/obsproject/obs-plugintemplate/blob/master/README.md

CMake:

https://github.com/obsproject/obs-plugintemplate/blob/master/CMakeLists.txt

------------------------------------------------------------------------

# 9. OBS: persistencia de configuración

OBS proporciona APIs de configuración/datos.

El plugin debe investigar las APIs oficiales de:

-   `obs_data_t`
-   configuración del módulo
-   callbacks de frontend

El objetivo será persistir configuración no secreta como:

-   plataforma habilitada,
-   cuenta vinculada,
-   identificador de usuario/canal,
-   preferencias de UI,
-   última selección.

No almacenar secretos OAuth como texto plano en archivos JSON de
configuración sin estudiar primero el mecanismo seguro apropiado para el
sistema operativo.

------------------------------------------------------------------------

# 10. Twitch API

## 10.1 API utilizada

Endpoint:

`PATCH https://api.twitch.tv/helix/channels`

Referencia:

https://dev.twitch.tv/docs/api/reference/

Operación:

**Modify Channel Information**

## 10.2 Autorización

Requiere User Access Token.

Scope mínimo:

`channel:manage:broadcast`

Referencia oficial:

https://dev.twitch.tv/docs/authentication/scopes/

La documentación actual de scopes confirma:

`channel:manage:broadcast`

permite administrar la configuración de broadcast del canal, incluyendo:

-   Modify Channel Information
-   Create Stream Marker
-   Replace Stream Tags

## 10.3 Headers

Las llamadas a Twitch API requieren:

`Authorization: Bearer <access_token>`

y:

`Client-Id: <client_id>`

La documentación oficial explica que el Client ID debe corresponder al
token.

## 10.4 Identidad del broadcaster

El endpoint requiere:

`broadcaster_id`

Ese ID debe coincidir con el usuario del token OAuth.

No asumir que el username es suficiente.

El flujo de conexión debe obtener/validar la identidad del usuario
autenticado.

## 10.5 Payload mínimo del MVP

Para cambiar únicamente el título:

``` json
{
  "title": "Nuevo título"
}
```

No enviar campos adicionales innecesarios.

## 10.6 Límite de título

La documentación actual indica un límite de:

**140 caracteres**

Si se supera, Twitch puede devolver error.

El MVP debe validar localmente y también manejar el error de la API.

## 10.7 Tokens

Twitch documenta OAuth 2.0.

Los access tokens pueden expirar o ser revocados.

Twitch recomienda validar tokens de aplicaciones de terceros mediante:

`/validate`

Referencia:

https://dev.twitch.tv/docs/authentication/

La documentación actual también explica refresh tokens.

El plugin debe soportar recuperación/renovación del token sin obligar al
usuario a volver a autorizar cada vez que expire el access token.

## 10.8 Rate limits

Twitch utiliza un sistema de token bucket.

Las respuestas proporcionan:

-   `Ratelimit-Limit`
-   `Ratelimit-Remaining`
-   `Ratelimit-Reset`

Referencia:

https://dev.twitch.tv/docs/api/guide/

Para este MVP las operaciones serán extremadamente poco frecuentes, por
lo que el riesgo práctico de rate limit es bajo. Aun así, el cliente
HTTP debe manejar HTTP 429 correctamente.

------------------------------------------------------------------------

# 11. YouTube Live Streaming API

## 11.1 API utilizada

Endpoint:

`PUT https://www.googleapis.com/youtube/v3/liveBroadcasts`

Referencia oficial:

https://developers.google.com/youtube/v3/live/docs/liveBroadcasts/update

Última documentación consultada:

2026-08-18.

## 11.2 Scopes

La operación acepta al menos:

`https://www.googleapis.com/auth/youtube`

o:

`https://www.googleapis.com/auth/youtube.force-ssl`

Referencia oficial:

https://developers.google.com/youtube/v3/live/docs/liveBroadcasts/update

Para un MVP, estudiar si `youtube.force-ssl` es el scope más apropiado
para minimizar permisos.

No pedir scopes que no sean necesarios.

## 11.3 Título y descripción

El recurso `liveBroadcast` contiene:

``` json
{
  "snippet": {
    "title": "...",
    "description": "..."
  }
}
```

Ambos son modificables.

YouTube documenta actualmente:

-   título: 1--100 caracteres
-   descripción: hasta 5.000 caracteres

Los errores correspondientes incluyen:

-   `invalidTitle`
-   `invalidDescription`

## 11.4 Parámetro `part`

La operación requiere indicar qué parte del recurso se actualiza.

Para modificar título y descripción, el request debe trabajar con:

`part=snippet`

El cuerpo debe preservar/mandar los campos requeridos de la parte
enviada según el comportamiento actual de la API.

No implementar una actualización que envíe accidentalmente otras partes
(`contentDetails`, `status`, etc.) si no son necesarias.

## 11.5 Problema principal: identificar el broadcast

YouTube diferencia:

-   `liveStream`
-   `liveBroadcast`

Un `liveStream` representa el flujo técnico al que se envía el vídeo.

Un `liveBroadcast` representa el evento/video público.

El título y descripción visibles al usuario pertenecen al broadcast.

Por eso el plugin debe localizar el broadcast adecuado antes de
modificarlo.

La API permite:

`liveBroadcasts.list`

con filtros como:

-   `mine=true`
-   `broadcastStatus=upcoming`
-   `broadcastStatus=active`

Referencia:

https://developers.google.com/youtube/v3/live/docs/liveBroadcasts

Guía:

https://developers.google.com/youtube/v3/live/guides/implementation/broadcasts-and-streams

## 11.6 Broadcasts persistentes

La documentación de YouTube explica el concepto de broadcasts y streams
reutilizables/persistentes.

No asumir que existe un único broadcast universal que siempre
corresponda al stream de OBS.

La implementación debe comprobar experimentalmente el flujo real del
usuario:

1.  OBS transmite.
2.  YouTube recibe mediante la configuración existente.
3.  Existe o se selecciona un `liveBroadcast`.
4.  El plugin identifica ese broadcast.
5.  Se actualiza `snippet.title` y `snippet.description`.

## 11.7 Estado del broadcast

La API distingue estados como:

-   `created`
-   `ready`
-   `testing`
-   `live`
-   `complete`

Algunas propiedades solo pueden modificarse cuando el broadcast está en
ciertos estados.

Para título y descripción el MVP debe probar explícitamente:

-   broadcast `upcoming/ready`
-   broadcast activo/live

No asumir que todos los metadatos tienen las mismas restricciones.

## 11.8 Errores importantes

El API documenta:

-   `insufficientPermissions`
-   `liveStreamingNotEnabled`
-   `invalidTitle`
-   `invalidDescription`
-   `liveBroadcastNotFound`
-   `rateLimitExceeded`
-   `backendError`

Referencia:

https://developers.google.com/youtube/v3/live/docs/errors

El plugin debe mostrar estos errores de forma entendible.

## 11.9 Cuota

YouTube Data API utiliza cuota.

La cuota por defecto es actualmente:

**10.000 unidades/día**

Los métodos de Live Streaming API consumen cuota porque forman parte de
YouTube Data API.

Referencia:

https://developers.google.com/youtube/v3/guides/quota_and_compliance_audits

Calculadora:

https://developers.google.com/youtube/v3/determine_quota_cost

Para este MVP, las llamadas deben mantenerse mínimas.

No hacer polling continuo.

Idealmente:

-   autenticar,
-   buscar broadcasts cuando el usuario pulsa refrescar/conectar,
-   actualizar cuando el usuario pulsa aplicar.

No hacer una llamada cada vez que se escribe una letra.

------------------------------------------------------------------------

# 12. YouTube OAuth para aplicación de escritorio

YouTube dispone actualmente de documentación específica para
aplicaciones instaladas/escritorio.

Referencia:

https://developers.google.com/youtube/v3/guides/auth/installed-apps

Google documenta PKCE para aplicaciones instaladas.

El flujo general:

1.  Generar `code_verifier`.
2.  Generar `code_challenge`.
3.  Abrir navegador.
4.  Usuario inicia sesión.
5.  Usuario autoriza.
6.  Google devuelve authorization code.
7.  Intercambiar code por access token + refresh token.
8.  Guardar tokens de forma segura.
9.  Renovar access token cuando sea necesario.

Esto es particularmente adecuado para un plugin que vive en el
escritorio del usuario.

No implementar OAuth copiando ejemplos antiguos que dependan de un flujo
obsoleto.

------------------------------------------------------------------------

# 13. Twitch OAuth para aplicación local

Twitch ofrece OAuth 2.0 y múltiples flujos.

La documentación actual incluye:

-   Authorization Code Grant
-   Device Code
-   etc.

Referencia:

https://dev.twitch.tv/docs/authentication/getting-tokens-oauth

Para un plugin de escritorio, investigar específicamente el flujo que
mejor se adapte a una aplicación instalada/local.

El scope requerido para el MVP es:

`channel:manage:broadcast`

No solicitar scopes de chat, moderación, clips, analytics, etc.

------------------------------------------------------------------------

# 14. Kick API

## 14.1 Estado actual

Kick dispone de una API pública oficial y un portal de desarrolladores.

Fuentes:

https://dev.kick.com\
https://docs.kick.com

Kick usa OAuth 2.1.

El servidor OAuth oficial es:

`https://id.kick.com`

La API utiliza:

`https://api.kick.com`

## 14.2 Tipos de token

Kick documenta:

-   App Access Token
-   User Access Token

El App Access Token sirve para datos públicos.

Para modificar metadata del canal se necesita User Access Token.

## 14.3 OAuth

Kick utiliza Authorization Code + PKCE para User Access Tokens.

Endpoints oficiales documentados:

Authorization:

`GET https://id.kick.com/oauth/authorize`

Token:

`POST https://id.kick.com/oauth/token`

Refresh:

`POST https://id.kick.com/oauth/token`

Revoke:

`POST https://id.kick.com/oauth/revoke`

Referencia oficial:

https://github.com/KickEngineering/KickDevDocs/blob/main/getting-started/generating-tokens-oauth2-flow.md

## 14.4 Scopes

Para este MVP:

`channel:write`

es el scope esencial.

Para obtener/confirmar identidad y metadata puede ser necesario:

`user:read` `channel:read`

Referencia oficial:

https://github.com/KickEngineering/KickDevDocs/blob/main/scopes/scopes.md

No solicitar:

-   chat:write
-   moderation
-   rewards
-   events
-   streamkey

porque no forman parte del MVP.

## 14.5 Actualización de título

Endpoint documentado por la API pública:

`PATCH /public/v1/channels`

El campo relevante es:

`stream_title`

La evidencia actual del repositorio oficial de documentación también
muestra que el endpoint responde `204 No Content` cuando actualiza
correctamente `stream_title`.

Referencia oficial:

https://github.com/KickEngineering/KickDevDocs/issues/344

El payload mínimo debe ser equivalente a:

``` json
{
  "stream_title": "Nuevo título"
}
```

No enviar `tags`, `is_mature` u otros campos en el MVP.

## 14.6 Descripción

No confundir:

`channel_description`

con una descripción de livestream.

La API de Kick tiene descripción de canal, pero el MVP busca descripción
del stream.

No diseñar una operación que escriba `channel_description` pensando que
equivale a la descripción del VOD/live.

**Conclusión actual: Kick no debe considerarse compatible con "stream
description" en el MVP.**

La UI debe indicarlo.

## 14.7 Refresh tokens

Kick devuelve:

-   `access_token`
-   `refresh_token`
-   `expires_in`
-   `scope`

El flujo de refresh vuelve a utilizar:

`POST https://id.kick.com/oauth/token`

con:

`grant_type=refresh_token`

El plugin debe manejar expiración.

## 14.8 PKCE

Kick requiere:

-   `code_verifier`
-   `code_challenge`
-   `code_challenge_method=S256`

Debe usarse `state` para protección contra CSRF.

## 14.9 localhost

La documentación oficial de Kick advierte sobre un comportamiento con
`127.0.0.1` y recomienda usar `localhost` cuando sea posible.

Para desarrollo local, preferir:

`http://localhost/...`

en lugar de:

`http://127.0.0.1/...`

y seguir exactamente las reglas actuales de redirect URI de Kick.

------------------------------------------------------------------------

# 15. Autenticación: principio común

Aunque las plataformas son diferentes, el comportamiento del MVP debería
ser conceptualmente:

``` text
OBS
 |
 +-- Twitch OAuth
 |
 +-- YouTube OAuth
 |
 +-- Kick OAuth
 |
 +-- Estado de cuenta vinculada
```

Cada proveedor tiene:

-   client ID
-   client secret (según proveedor/flujo)
-   authorization URL
-   token endpoint
-   access token
-   refresh token cuando corresponda
-   scopes
-   identidad del usuario/canal

No compartir tokens entre proveedores.

No asumir que un access token sirve para más de una plataforma.

------------------------------------------------------------------------

# 16. Seguridad de credenciales

Regla crítica:

**Nunca colocar client secrets o refresh tokens dentro de
HTML/JavaScript que pueda inspeccionarse desde una WebView/Browser
Source.**

El plugin nativo debe ser el propietario de las credenciales sensibles.

No utilizar un Browser Source como almacén de secretos.

El Browser Source de OBS es una página CEF y puede cargar contenido
local/remoto.

Referencia:

https://obsproject.com/kb/browser-source

Para este MVP, la alternativa más segura y simple es que el código
nativo del plugin gestione OAuth/tokens y que la UI Qt solo interactúe
con esa capa.

El agente debe investigar mecanismos de almacenamiento seguro
específicos de Windows si el MVP se limita inicialmente a Windows.

No almacenar:

-   client_secret
-   access_token
-   refresh_token

en:

-   Git
-   logs
-   mensajes de error
-   JSON de configuración sin protección
-   archivos de proyecto compartibles

------------------------------------------------------------------------

# 17. No hacer OAuth dentro de una WebView si no es necesario

La UI de OBS puede mostrar un botón:

`Conectar Twitch`

y el plugin puede abrir el navegador predeterminado del sistema para el
OAuth.

Esto evita depender de una autenticación embebida frágil.

Después del callback, el plugin recupera el código/token.

Para Google, estudiar cuidadosamente el flujo de aplicación instalada y
PKCE.

Para Kick, respetar exactamente la redirect URI registrada.

Para Twitch, seguir la documentación vigente de OAuth para aplicaciones
de terceros.

------------------------------------------------------------------------

# 18. HTTP

El MVP necesita realizar HTTP HTTPS contra:

-   Twitch API
-   Google APIs
-   Kick API
-   endpoints OAuth de cada proveedor

No se necesita WebSocket para este MVP.

No se necesita RTMP.

No se necesita modificar el pipeline de vídeo/audio de OBS.

La capa HTTP debe:

-   usar HTTPS,
-   establecer timeouts,
-   comprobar códigos HTTP,
-   parsear JSON,
-   manejar respuestas vacías (`204`),
-   evitar imprimir tokens,
-   manejar errores de red,
-   manejar 401/403/404/429/5xx.

------------------------------------------------------------------------

# 19. Comportamiento esperado del botón "Aplicar"

Ejemplo conceptual:

``` text
Título:
[ Marvel Rivals con amigos ]

Descripción:
[ Jugando con amigos... ]

[X] Twitch
[X] YouTube
[X] Kick

[ Aplicar ]
```

Al pulsar:

``` text
Twitch
  -> actualizar título
  -> OK

YouTube
  -> localizar broadcast
  -> actualizar título + descripción
  -> OK

Kick
  -> actualizar stream_title
  -> OK
```

Resultado:

``` text
✓ Twitch actualizado
✓ YouTube actualizado
✓ Kick actualizado

Descripción:
  YouTube: aplicada
  Twitch: no soportada
  Kick: no soportada
```

No debe existir una operación "todo o nada".

Cada plataforma es independiente.

Si Twitch falla pero YouTube funciona:

``` text
✗ Twitch: 401 Unauthorized
✓ YouTube: actualizado
✓ Kick: actualizado
```

No revertir operaciones exitosas.

------------------------------------------------------------------------

# 20. Validaciones locales

El plugin debe validar antes de enviar:

## Twitch

Título:

-   requerido si Twitch está habilitado
-   máximo 140 caracteres

## YouTube

Título:

-   requerido
-   1--100 caracteres

Descripción:

-   máximo 5.000 caracteres

## Kick

Título:

-   requerido si Kick está habilitado
-   respetar límites reales documentados por API y validar también la
    respuesta del servidor.

No inventar límites cuando la documentación oficial no los establezca
claramente.

------------------------------------------------------------------------

# 21. YouTube: estrategia de selección de broadcast

Este es probablemente el punto técnicamente más importante del MVP.

El plugin debe poder responder:

> "¿Qué broadcast de YouTube estoy editando?"

La API permite listar broadcasts.

Candidatos:

-   próximos,
-   activos.

El MVP debe investigar durante implementación cuál de estos escenarios
coincide con el flujo del usuario:

### Caso A

El usuario crea/programa el broadcast en YouTube Studio antes de abrir
OBS.

Entonces el plugin puede:

1.  listar upcoming broadcasts,
2.  mostrar título actual,
3.  permitir seleccionar el broadcast,
4.  actualizarlo.

### Caso B

El usuario utiliza una emisión persistente/reutilizable.

Entonces hay que comprobar cómo se expone el broadcast actual mediante
la API y si se puede localizar de forma determinista.

### Caso C

El usuario empieza directamente desde OBS.

Hay que comprobar si la configuración de YouTube utilizada por OBS/Aitum
genera o selecciona automáticamente un broadcast y cómo se refleja en
`liveBroadcasts.list`.

**No asumir una solución hasta probar el flujo real.**

------------------------------------------------------------------------

# 22. YouTube y OBS no son necesariamente el mismo concepto

No confundir:

-   stream key,
-   live stream,
-   live broadcast,
-   video/VOD.

Un stream key puede identificar el destino de ingestión.

Pero el título/descripción públicos pertenecen al broadcast/video.

El plugin no debe intentar cambiar el stream key para cambiar el título.

------------------------------------------------------------------------

# 23. Relación con Aitum Multistream

El MVP NO debe depender de Aitum.

El plugin solamente modifica metadata mediante APIs de las plataformas.

Por tanto:

``` text
OBS/Aitum Multistream
       |
       | video
       v
 Twitch / YouTube / Kick

Nuestro plugin
       |
       | metadata
       +--> Twitch API
       +--> YouTube API
       +--> Kick API
```

No modificar:

-   configuración RTMP,
-   stream keys,
-   outputs,
-   servicios de OBS,
-   escenas,
-   fuentes.

Esto permite que el plugin sea independiente del sistema de multistream.

------------------------------------------------------------------------

# 24. No intentar "descubrir" las plataformas desde Aitum

El plugin no debería depender de:

-   archivos internos de Aitum,
-   procesos de Aitum,
-   APIs privadas de Aitum,
-   scraping del panel de Aitum.

Las cuentas se conectan directamente con sus APIs oficiales.

Esto hace que el MVP sea independiente y mantenible.

------------------------------------------------------------------------

# 25. Arquitectura NO prescrita

Este documento deliberadamente **no impone una arquitectura concreta**.

El agente de desarrollo debe decidir:

-   organización de clases,
-   módulos,
-   HTTP client,
-   OAuth manager,
-   storage,
-   Qt widgets,
-   threading,
-   etc.

Pero debe respetar estas restricciones:

1.  UI no debe bloquearse durante HTTP/OAuth.
2.  Tokens sensibles no deben llegar al frontend web.
3.  Las APIs deben estar encapsuladas por proveedor.
4.  Una falla de una plataforma no debe cancelar automáticamente las
    otras.
5.  Las respuestas `204 No Content` deben tratarse como éxito cuando la
    API las documente.
6.  No debe haber polling innecesario.
7.  Deben registrarse errores útiles pero sin secretos.

------------------------------------------------------------------------

# 26. Threading / UI

Las llamadas HTTP no deberían ejecutarse directamente en el hilo de UI
de Qt.

Un request lento no debe congelar OBS.

Esto es especialmente importante durante:

-   OAuth,
-   búsqueda de broadcasts de YouTube,
-   actualización de metadata,
-   refresh de tokens.

La UI debe mostrar estado:

`Actualizando...`

y bloquear temporalmente el botón si es necesario.

------------------------------------------------------------------------

# 27. Logging

Los logs del plugin deben ser útiles para diagnóstico.

Ejemplo:

``` text
[stream-metadata] Twitch update started
[stream-metadata] Twitch update succeeded
[stream-metadata] YouTube broadcast lookup started
[stream-metadata] YouTube broadcast found: <redacted-or-safe-id>
[stream-metadata] YouTube update succeeded
[stream-metadata] Kick update succeeded
```

Nunca:

``` text
refresh_token=...
access_token=...
client_secret=...
Authorization: Bearer ...
```

Los identificadores de usuario/canal pueden registrarse solamente si no
constituyen información sensible para el caso de uso, y preferiblemente
deben reducirse o enmascararse en logs de diagnóstico.

------------------------------------------------------------------------

# 28. Manejo de HTTP

Mínimo:

### 2xx

Éxito.

### 204

Éxito sin body.

Especialmente importante para Kick.

### 400

Input/API request inválido.

Mostrar mensaje útil.

### 401

Token inválido/expirado.

Intentar refresh cuando sea apropiado.

Si refresh falla:

`Cuenta requiere reconexión`.

### 403

Permisos insuficientes/scope incorrecto.

Mostrar claramente:

`La cuenta no concedió los permisos necesarios`.

### 404

Recurso no encontrado.

Muy importante para YouTube broadcast.

### 409

Conflicto/restricción de estado.

### 429

Rate limit.

Respetar headers si están disponibles y evitar reintentos agresivos.

### 5xx

Error temporal del proveedor.

Reintento limitado y con backoff.

------------------------------------------------------------------------

# 29. OAuth: revocación

El usuario debe poder desconectar una cuenta.

Desconectar significa:

-   eliminar tokens locales,
-   eliminar identidad local,
-   eliminar estado de autorización.

No hace falta implementar una pantalla compleja.

Pero debe existir al menos:

`Desconectar`

por proveedor.

Si el proveedor ofrece endpoint de revocación, el agente debe evaluar si
debe utilizarse.

------------------------------------------------------------------------

# 30. Scopes mínimos

## Twitch

`channel:manage:broadcast`

## YouTube

Uno de:

`https://www.googleapis.com/auth/youtube`

o

`https://www.googleapis.com/auth/youtube.force-ssl`

Investigar y elegir el mínimo adecuado para `liveBroadcasts.update`.

## Kick

`channel:write`

y posiblemente:

`user:read` `channel:read`

para identificación/lectura.

No añadir scopes innecesarios.

------------------------------------------------------------------------

# 31. Tecnologías que sí son necesarias

Como mínimo:

-   C/C++
-   CMake
-   OBS Plugin API
-   OBS Frontend API
-   Qt6 Widgets
-   OAuth 2.0 / OAuth 2.1
-   PKCE
-   HTTPS
-   JSON
-   HTTP client
-   almacenamiento local seguro
-   Git

No se necesita:

-   React
-   Node.js
-   Electron
-   WebSocket
-   RTMP
-   base de datos
-   servidor remoto
-   backend cloud
-   Docker

> Nota de vigencia: la lista "no se necesita backend cloud / servidor
> remoto" corresponde a la hipótesis MVP 2026-09-07 y quedó superada por
> decisión explícita: backend centralizado para el modo Administrado
> (ADR-009, ADR-012, T-043–T-046; referencia vigente
> `docs/ARCHITECTURE-BACKEND.md`). El modo Independiente sigue sin
> necesitarlo.

Node/JavaScript puede utilizarse durante desarrollo auxiliar si el
agente lo considera conveniente, pero no es requisito para el plugin.

------------------------------------------------------------------------

# 32. Browser Dock vs QWidget nativo

OBS soporta Browser Sources y CEF.

Referencia:

https://obsproject.com/kb/browser-source

Sin embargo, para este MVP se recomienda estudiar primero un
**QWidget/QDock nativo**, porque:

-   el panel es extremadamente simple,
-   no requiere HTML,
-   no necesita un servidor local,
-   no necesita exponer OAuth tokens al navegador,
-   encaja directamente con `obs_frontend_add_dock_by_id`.

Un Browser Source puede ser útil en una futura versión si se quiere una
UI web compleja, pero no es necesario para el MVP.

------------------------------------------------------------------------

# 33. Dependencias externas

Preferir las dependencias ya disponibles en el ecosistema de OBS/Qt
cuando sea posible.

No incorporar un framework enorme para una aplicación que solamente
necesita:

-   formularios,
-   OAuth,
-   HTTP,
-   JSON.

Antes de añadir una dependencia, evaluar:

-   licencia,
-   mantenimiento,
-   compatibilidad Windows,
-   compatibilidad con OBS,
-   tamaño,
-   facilidad de empaquetado,
-   soporte Qt6,
-   seguridad.

------------------------------------------------------------------------

# 34. Compatibilidad OBS

La investigación actual consultó documentación de:

**OBS Studio 32.2.2**

La API de docks utilizada:

`obs_frontend_add_dock_by_id`

existe desde OBS 30.0.

El plugin debe declarar explícitamente su versión mínima de OBS.

No intentar soportar versiones antiguas si eso complica el MVP.

Para desarrollo actual, partir del template oficial y del SDK
correspondiente a la versión objetivo.

------------------------------------------------------------------------

# 35. Build

El template oficial usa CMake moderno y Qt6.

Referencia:

https://github.com/obsproject/obs-plugintemplate

El entorno Windows actual documentado utiliza:

-   Visual Studio 17 2022
-   CMake 3.30.x
-   Qt6
-   dependencias precompiladas de OBS

No construir contra una instalación arbitraria de Qt diferente a la que
corresponda con el OBS objetivo sin verificar compatibilidad binaria.

------------------------------------------------------------------------

# 36. Instalación del plugin

El MVP debe poder instalarse como plugin de OBS.

Debe respetar la estructura de plugins de OBS.

El agente debe utilizar las herramientas/plantillas oficiales de
empaquetado en lugar de inventar una estructura.

El objetivo final del MVP debería ser:

``` text
Instalar plugin
↓
Abrir OBS
↓
Tools / Docks
↓
Stream Metadata
↓
Conectar cuentas
↓
Introducir título/descripción
↓
Aplicar
```

------------------------------------------------------------------------

# 37. UX mínima

La UI no necesita ser bonita.

Debe ser clara.

Propuesta funcional:

``` text
STREAM METADATA

Twitch
[ Conectar ]
Estado: No conectado

YouTube
[ Conectar ]
Estado: No conectado

Kick
[ Conectar ]
Estado: No conectado

--------------------------------

Título
[________________________________]

Descripción
[________________________________]
[________________________________]
[________________________________]

YouTube Broadcast
[ Seleccionar / Refrescar ]

--------------------------------

[ Aplicar cambios ]

Resultado:
Twitch   ✓
YouTube  ✓
Kick     ✓
```

La parte de YouTube puede requerir un selector de broadcast.

No añadir categorías/tags/etc. en la primera versión.

------------------------------------------------------------------------

# 38. MVP de autenticación

Al terminar la fase de conexión:

### Twitch

Debe mostrar:

`Conectado como: <usuario>`

### YouTube

Debe mostrar:

`Conectado como: <canal>`

### Kick

Debe mostrar:

`Conectado como: <usuario>`

No mostrar tokens.

------------------------------------------------------------------------

# 39. Prueba de cada proveedor

El desarrollo no debe considerarse funcional hasta probar operaciones
reales.

## Twitch

1.  Conectar cuenta.
2.  Obtener identidad.
3.  Cambiar título.
4.  Verificar título en Twitch.
5.  Reiniciar OBS.
6.  Verificar persistencia del token.
7.  Cambiar título nuevamente.
8.  Revocar autorización.
9.  Confirmar detección del token inválido.

## YouTube

1.  Conectar cuenta.
2.  Confirmar permiso de live streaming.
3.  Listar broadcasts.
4.  Seleccionar broadcast.
5.  Cambiar título.
6.  Cambiar descripción.
7.  Verificar en YouTube Studio.
8.  Reiniciar OBS.
9.  Repetir.
10. Probar broadcast en diferentes estados.

## Kick

1.  Conectar cuenta.
2.  Obtener identidad.
3.  Actualizar título.
4.  Confirmar respuesta 204.
5.  Verificar título en Kick.
6.  Reiniciar OBS.
7.  Repetir.
8.  Revocar token.
9.  Confirmar reconexión necesaria.

------------------------------------------------------------------------

# 40. Pruebas negativas obligatorias

### Twitch

-   título \> 140 caracteres
-   token inválido
-   scope incorrecto
-   broadcaster ID incorrecto

### YouTube

-   título vacío
-   título \> 100
-   descripción \> 5000
-   broadcast inexistente
-   broadcast no modificable
-   live streaming deshabilitado
-   token sin permisos
-   rate limit

### Kick

-   token expirado
-   scope sin `channel:write`
-   broadcaster inexistente
-   payload inválido
-   respuesta 204 sin body

------------------------------------------------------------------------

# 41. YouTube: no crear broadcasts como parte del MVP

El MVP no debería crear automáticamente nuevos broadcasts salvo que la
investigación experimental demuestre que es estrictamente necesario.

Crear broadcasts añade:

-   lifecycle,
-   privacidad,
-   horarios,
-   binding,
-   stream resources,
-   más cuota,
-   más errores.

Primera versión:

**modificar broadcasts existentes.**

Si después se quiere automatizar completamente la creación del evento de
YouTube, será una segunda fase.

------------------------------------------------------------------------

# 42. YouTube: no modificar el stream técnico

No modificar:

-   ingest type,
-   resolución,
-   framerate,
-   stream key,
-   CDN settings.

El MVP solo necesita:

`liveBroadcast.snippet.title`

y:

`liveBroadcast.snippet.description`

------------------------------------------------------------------------

# 43. Categorías, tags y demás

Aunque Twitch/Kick permiten algunas de estas operaciones, quedan fuera.

No implementar ahora:

-   Twitch game/category
-   Twitch tags
-   Twitch language
-   YouTube category
-   Kick category
-   Kick tags
-   mature flags

El objetivo es comprobar primero que el pipeline OAuth + metadata
funciona.

------------------------------------------------------------------------

# 44. Referencias oficiales prioritarias

## OBS

Developer Guide\
https://obsproject.com/kb/developer-guide

Plugin Guide\
https://docs.obsproject.com/plugins

Module API\
https://docs.obsproject.com/reference-modules

Frontend API\
https://docs.obsproject.com/frontends

OBS Plugin Template\
https://github.com/obsproject/obs-plugintemplate

Frontend API header\
https://github.com/obsproject/obs-studio/blob/master/frontend/api/obs-frontend-api.h

Dock implementation\
https://github.com/obsproject/obs-studio/blob/master/frontend/OBSStudioAPI.cpp

Browser Source\
https://obsproject.com/kb/browser-source

## Twitch

API Reference\
https://dev.twitch.tv/docs/api/reference/

Authentication\
https://dev.twitch.tv/docs/authentication/

Getting OAuth Tokens\
https://dev.twitch.tv/docs/authentication/getting-tokens-oauth

Scopes\
https://dev.twitch.tv/docs/authentication/scopes/

API Concepts / rate limits\
https://dev.twitch.tv/docs/api/guide/

## YouTube

Live Streaming API\
https://developers.google.com/youtube/v3/live/getting-started

LiveBroadcasts\
https://developers.google.com/youtube/v3/live/docs/liveBroadcasts

LiveBroadcasts.update\
https://developers.google.com/youtube/v3/live/docs/liveBroadcasts/update

Implementation: Broadcasts and Streams\
https://developers.google.com/youtube/v3/live/guides/implementation/broadcasts-and-streams

Errors\
https://developers.google.com/youtube/v3/live/docs/errors

Installed Apps OAuth\
https://developers.google.com/youtube/v3/guides/auth/installed-apps

Quota\
https://developers.google.com/youtube/v3/guides/quota_and_compliance_audits

Quota Calculator\
https://developers.google.com/youtube/v3/determine_quota_cost

## Kick

Developer Portal\
https://dev.kick.com

API Docs\
https://docs.kick.com

OAuth documentation\
https://github.com/KickEngineering/KickDevDocs/blob/main/getting-started/generating-tokens-oauth2-flow.md

Scopes\
https://github.com/KickEngineering/KickDevDocs/blob/main/scopes/scopes.md

Official Kick documentation issue confirming PATCH /channels behavior\
https://github.com/KickEngineering/KickDevDocs/issues/344

------------------------------------------------------------------------

# 45. Fuentes secundarias útiles, no normativas

Estas fuentes pueden ayudar a detectar cambios o ejemplos, pero no deben
sustituir la documentación oficial:

Kick API wrapper:

https://github.com/Landy-Dev/kick-api

Kick API wrapper/documentation:

https://docs.rs/crate/kick-api/latest

Estas fuentes pueden ser útiles para comparar:

-   nombres de campos,
-   modelos,
-   endpoints,
-   scopes.

Pero las decisiones finales deben basarse en Kick Dev Docs.

------------------------------------------------------------------------

# 46. Reglas para el agente de desarrollo

Antes de escribir código:

1.  Confirmar versión objetivo de OBS.
2.  Leer el template oficial de plugins.
3.  Leer Frontend API.
4.  Confirmar Qt6.
5.  Confirmar flujo OAuth actual de cada plataforma.
6.  Confirmar endpoints de actualización.
7.  Confirmar límites.
8.  Confirmar scopes.
9.  Confirmar comportamiento de tokens.
10. Confirmar el modelo de broadcast de YouTube.

No usar tutoriales viejos como autoridad si contradicen documentación
actual.

------------------------------------------------------------------------

# 47. Regla especialmente importante para APIs

No asumir que porque una plataforma tiene un campo llamado
`description`, ese campo significa "descripción del stream".

Actualmente:

-   YouTube: sí existe descripción del broadcast.
-   Twitch: no hay equivalente directo en Modify Channel Information.
-   Kick: `channel_description` es descripción de canal.
-   Facebook: sí existe descripción, pero solo sobre el `live_video`
    propio creado por el plugin (`POST /me/live_videos`); el objeto que
    muestra Live Producer web no es gestionable por API (F-065/F-066,
    D11). Page diferida (F-072/F-079).

El modelo interno del plugin debe respetar estas diferencias.

------------------------------------------------------------------------

# 48. Regla especialmente importante para YouTube

No diseñar:

``` text
YouTube account -> update title/description
```

como si fuera suficiente.

Debe existir conceptualmente:

``` text
YouTube account
        |
        +--> broadcast selection
                 |
                 +--> update snippet
```

La identificación del broadcast es parte esencial del MVP.

------------------------------------------------------------------------

# 49. Regla especialmente importante para tokens

No diseñar:

``` text
account = client_id + access_token
```

como solución permanente.

Los tokens expiran.

El modelo de conexión debe contemplar:

``` text
client identity
+
OAuth authorization
+
access token
+
refresh token cuando exista
+
expiration
+
granted scopes
+
provider user/channel identity
```

------------------------------------------------------------------------

# 50. Definición de terminado del MVP

> Nota de vigencia: puerta histórica en verde (BACKLOG T-033 hecha;
> evidencia en `docs/VALIDATION.md`). Se conserva como definición, no
> como estado: el estado vive en `docs/STATE.md`. Las casillas se marcan
> [x] únicamente con base en ese cierre documentado (T-033; matriz viva
> T-031 T1–T20 + hardening T-032 + negativos T-033), no en verificación
> en vivo de esta sesión.

El MVP está terminado cuando:

-   [x] El plugin compila con el OBS objetivo.
-   [x] OBS lo carga sin errores.
-   [x] Aparece un dock propio.
-   [x] El dock puede abrirse/cerrarse.
-   [x] Twitch puede conectarse.
-   [x] YouTube puede conectarse.
-   [x] Kick puede conectarse.
-   [x] Los tokens sobreviven a reiniciar OBS.
-   [x] Twitch puede actualizar título.
-   [x] YouTube puede actualizar título.
-   [x] YouTube puede actualizar descripción.
-   [x] Kick puede actualizar título.
-   [x] La UI explica que Twitch/Kick no tienen descripción de stream
    equivalente.
-   [x] Un error de Twitch no impide actualizar YouTube/Kick.
-   [x] Un error de YouTube no impide actualizar Twitch/Kick.
-   [x] Los tokens nunca aparecen en logs.
-   [x] HTTP 204 es manejado correctamente.
-   [x] HTTP 401 produce recuperación/reautorización adecuada.
-   [x] HTTP 429 no provoca un loop de requests.
-   [x] La UI no se congela durante requests.
-   [x] El usuario puede desconectar cada cuenta.
-   [x] Se puede verificar cada cambio en la plataforma correspondiente.

------------------------------------------------------------------------

# 51. Fuera de alcance explícito

No implementar hasta que el MVP sea estable:

-   multichat,
-   stream scheduling,
-   presets,
-   categorías,
-   tags,
-   thumbnails,
-   stream keys,
-   multistream,
-   analytics,
-   viewer counts,
-   activity feed,
-   chat bots,
-   WebSocket,
-   EventSub,
-   Kick webhooks,
-   YouTube live chat,
-   Twitch EventSub,
-   cloud sync,
-   cuentas múltiples por proveedor,
-   perfiles de usuario,
-   auto-start,
-   auto-update,
-   marketplace.

------------------------------------------------------------------------

# 52. Próximo paso recomendado para OpenCode

> Nota histórica: recomendación de 2026-09-07 ya ejecutada y superada
> (PoC T-030 PASS, cerrada; ver `docs/VALIDATION.md` y BACKLOG T-030).
> Se conserva como contexto de aquel momento, no como instrucción
> vigente: no iniciar ninguna PoC nueva a partir de esta sección.

No comenzar directamente por toda la implementación.

Primero crear una pequeña **prueba de concepto técnica** que demuestre:

1.  Un plugin mínimo de OBS.
2.  Un dock Qt6.
3.  Un botón.
4.  Una llamada HTTP HTTPS.
5.  OAuth Twitch.
6.  OAuth YouTube.
7.  OAuth Kick.
8.  Una operación real de cambio de título en cada proveedor.
9.  Una operación real de cambio de descripción en YouTube.
10. Persistencia segura de credenciales.

Una vez demostradas esas operaciones, construir la UI mínima.

La razón es simple:

**el riesgo real del proyecto no está en crear un formulario Qt; está en
las diferencias de OAuth y metadata entre las tres APIs, especialmente
YouTube.**

------------------------------------------------------------------------

# 53. Resumen ejecutivo

La idea es viable.

El plugin no necesita tocar el sistema de streaming de OBS.

El MVP puede limitarse a ser una herramienta de **metadata management**.

Las capacidades verificadas actualmente son:

``` text
Twitch
  OAuth 2.0
  channel:manage:broadcast
  PATCH /helix/channels
  title: YES
  description: NO equivalente

YouTube
  OAuth 2.0
  youtube / youtube.force-ssl
  liveBroadcasts.update
  title: YES
  description: YES
  requiere identificar broadcast

Kick
  OAuth 2.1 + PKCE
  channel:write
  PATCH /public/v1/channels
  stream_title: YES
  stream description: NO equivalente confirmado
```

La integración con OBS es apropiada para un plugin nativo Qt6 con
Frontend API y un `QWidget` registrado como dock.

El MVP puede mantenerse deliberadamente pequeño:

``` text
OBS
 |
 +-- Stream Metadata Dock
       |
       +-- Twitch OAuth/API
       +-- YouTube OAuth/API
       +-- Kick OAuth/API
       |
       +-- Title
       +-- Description
       +-- Apply
```

No existe ninguna necesidad técnica demostrada de incorporar Aitum,
Streamlabs, un servidor cloud, React, Electron, WebSocket o un backend
remoto para esta primera versión.

> Nota de vigencia: "backend remoto" quedó superado por decisión
> explícita para el modo Administrado (ADR-009/ADR-012, nota en §31).
> El resto de la frase sigue vigente: nada de Aitum/Streamlabs/React/
> Electron/WebSocket para el producto actual.

------------------------------------------------------------------------

# 54. Subagente GCP del proyecto (gcp-stream-metadata)

> Solo lectura por defecto. Sus dictámenes describen un instante
> temporal: no conviertas información temporal de GCP (revisiones, URLs,
> tráfico, cuotas) en reglas permanentes de este archivo. El estado
> operativo vive en `docs/DEPLOYMENT.md`, que distingue hechos
> verificados, documentados y pendientes.

Antes de cualquier cambio que afecte o pueda afectar a Google Cloud
(Cloud Run, Artifact Registry/Cloud Build, IAM, Secret Manager,
Cloud Scheduler, Cloud Logging/Monitoring, APIs habilitadas, cuotas,
OAuth de Google, URLs/callbacks, red, facturación o persistencia en la
nube), consultar al subagente persistente:

- Agente: `.opencode/agents/gcp-stream-metadata.md`
- Invocación: `@gcp-stream-metadata <tarea>` o herramienta `subagent`.
- Por defecto solo lectura; dictamina `compatible` /
  `compatible con condiciones` / `bloqueado` con evidencia y
  validaciones pendientes.
- Ninguna escritura productiva (deploy, tráfico, IAM, secretos,
  scheduler, cuotas, URLs) sin plan concreto y autorización explícita
  separada. Neon es externo: solo integración, no administración.

------------------------------------------------------------------------

# 55. Jerarquía documental (no duplicar estado ni capacidades)

1.  Este archivo manda en límites de producto, reglas duraderas y flujo
    de trabajo. No duplica el estado actual.
2.  `docs/BACKLOG.md` es el estado canónico de tareas y trabajo
    pendiente. No cambies estados sin evidencia (comando + salida o E2E
    del operador registrado).
3.  `docs/STATE.md` es el resumen actual breve, reconciliado con el
    checkout y el backlog. Si STATE y BACKLOG discrepan, manda BACKLOG
    y se registra la discrepancia.
4.  `docs/ARCHITECTURE-BACKEND.md` y los ADR de `docs/DECISIONS/` son la
    arquitectura vigente y las decisiones históricas. Una discrepancia
    doc-vs-código se registra en `docs/FINDINGS.md` (F-###) y se corrige
    en el doc correspondiente en el mismo PR cuando la evidencia lo
    permite (precedente: F-064).
5.  `docs/DEPLOYMENT.md` es el contrato de despliegue y el estado
    operativo. Distingue siempre: hecho verificado (comando + salida en
    esta sesión), hecho documentado (E2E/sesión previa citada) y
    pendiente de confirmación. No afirmes un dato de producción como
    verificado si no pudiste comprobarlo.
6.  `docs/VALIDATION.md` es registro histórico: se añaden entradas, no
    se reescribe el pasado. Los resultados antiguos se separan del
    estado vigente sin borrar historia.
7.  `docs/PROJECT_CONTEXT.md` es resumen de recuperación con enlaces a
    las fuentes canónicas. No debe convertirse en otro backlog ni
    duplicar el inventario de detalles.
8.  No confundas el estado de la app de Meta (modo Development, sin App
    Review: solo sirve a usuarios con rol en la app) con el despliegue
    del backend en Cloud Run (infraestructura productiva). Son dos
    hechos independientes y se documentan por separado.
9.  Autoridad de privacidad por capas (sin reescribir `PRIVACY.md` aquí):
    principios y restricciones duraderas → este archivo (`AGENTS.md`);
    tratamiento, retención, datos recogidos y comunicaciones al usuario
    → `docs/PRIVACY.md` como documentación descriptiva; implementación
    técnica de esos principios → código vigente más
    `docs/ARCHITECTURE-BACKEND.md`. Ante conflicto, el principio de
    `AGENTS.md` prevalece, el detalle descriptivo se corrige hacia el
    código/la arquitectura y el desajuste se registra en
    `docs/FINDINGS.md`. Si las fuentes no permiten una jerarquía
    coherente, se documenta el hueco como regla provisional marcada
    (`[P] provisional`) sin inventar decisiones de producto.
10. Rutas protegidas o excluidas (`staging/`, `dev.env` y análogas): no
    leer, listar, hashear ni inspeccionar su contenido. Registrar el
    estado Git inicial y final de esas rutas solo con comandos de
    lectura (p. ej. `git status --porcelain`). Distinguir siempre:
    (a) "no se ejecutaron acciones dirigidas a esta ruta" (alcance del
    procedimiento); (b) "el estado Git observado al inicio y al final
    fue el mismo" (solo `status`, [C] de observación, no de contenido);
    (c) "se comprobó que el contenido no cambió" (requiere una
    comprobación de contenido autorizada y adecuada; `git status` solo
    nunca basta). Si la ruta tiene prohibida la lectura o la huella, se
    reporta la limitación y se usa la formulación más acotada
    sostenible. Correspondencia: (a)/(b) como [C] de procedimiento u
    observación; cualquier salto a contenido sin su evidencia es [I];
    sin base, [P].
    Excepción G3 2026-10-26 [D] (autorización del usuario, opción
    "Quitar todo" para `.env`/`dev.env`): la lectura de ficheros
    `.env`/`dev.env` queda permitida para diagnosticar por nombres de
    variable (presencia/ausencia, jamás valores); el contenido con
    secretos jamás se cita en informes, logs o prompts. `staging/`
    sigue excluida de inspección de contenido salvo `status`.
    Excepción G4 2026-10-26 [D] (autorización del usuario en chat:
    documentar el desbloqueo para que no vuelva a bloquear + URL
    canónica confirmada): actualizar SOLO bases de URL en
    `.env`/`dev.env` está permitido únicamente con autorización
    explícita del usuario por tarea, con procedimiento obligatorio:
    (1) verificar antes de tocar que ambos archivos están ignorados
    (`git check-ignore -v .env dev.env` debe listarlos); (2)
    mostrar/imprimir solo líneas con URL, jamás valores de secretos;
    (3) reemplazar solo la base conservando rutas; (4) re-verificar el
    ignorado después; (5) si algún archivo no está ignorado, PARAR y
    pedir instrucciones. Regla dura del usuario (única regla de
    bloqueo duro): "NO SUBIR SECRETOS A GIT". Las protecciones de
    lectura de secretos se mantienen porque son las que la hacen
    cumplir.
11. Evidencia documental frente a verificación operativa: `GET /health`,
    `/ready` o `/version` no demuestran por sí solos a qué URL apunta un
    job de Cloud Scheduler. Afirmar el destino operativo del job exige
    evidencia del propio job obtenida por consulta de solo lectura
    autorizada; mientras no se ejecute, el destino queda como `[D]`
    (documentado) o `[P]` (pendiente) según la evidencia disponible.

------------------------------------------------------------------------

# 56. Agentes locales y Skill compartida

Definiciones en `.opencode/agents/` (descubrimiento:
`opencode debug agents` desde la raíz del checkout):

-   `harness-steward`: coherencia documental; edita solo los
    documentos de su alcance (único escritor de `docs/BACKLOG.md`;
    dueño de `.gitignore` para entradas de ignorado); sin código ni
    operaciones externas.
-   `obs-plugin`: C++/Qt6 y cliente del plugin; trabaja en `src/`;
    sin despliegues ni cuentas externas.
-   `backend-integrations`: backend Python/OAuth/pruebas; trabaja en
    `backend/`; dueño ejecutor del lab Facebook (`lab/**`, Node);
    sin infraestructura productiva ni despliegues.
-   `release-tooling`: herramientas de publicación; trabaja en
    `tools/**` (`deploy.ps1`, `package.ps1`); modifica sin ejecutar.
-   `git-integration`: integración Git atómica (add por paths
    explícitos, commit, push, PRs) del trabajo ya revisado; sin
    edición de archivos (`permission.edit` denegado total); solo
    actúa bajo autorización escrita del gate vigente (fase G1,
    2026-10-25 [D]).
-   `gcp-stream-metadata`: auditoría GCP en solo lectura; sus datos
    fechados son evidencia histórica hasta re-verificar.
-   `orquestador` (`orquestador.md`, `mode: primary`): clasifica,
    planea, obtiene autorización, delega a un especialista a la vez,
    revisa, valida y cierra; no edita archivos directamente
    (`permission.edit` denegado en su totalidad).

Tabla de propiedad (un escritor por ruta; reforma 2026-10-25):

| Ruta | Dueño escritor |
|---|---|
| `docs/BACKLOG.md` | `harness-steward` (único) |
| `.gitignore` (entradas de ignorado) | `harness-steward` |
| Resto documental (`AGENTS.md`, `README.md`, `docs/STATE.md`, `docs/PROJECT_CONTEXT.md`, `docs/DEPLOYMENT.md`, `docs/VALIDATION.md`, `docs/TOOLS.md`, `docs/WORKFLOW.md`, `docs/FINDINGS.md`) | `harness-steward` |
| `src/**` | `obs-plugin` |
| `backend/**` | `backend-integrations` |
| `lab/**` | `backend-integrations` (ejecutor lab) |
| `tools/**` | `release-tooling` (sin ejecución) |
| Integración Git (add/commit/push/PR/merge por paths, sin edición de archivos) | `git-integration` (solo bajo autorización escrita del gate vigente) |

Los límites de producto de §§23/51 no cambian con esta reforma.

Skill compartida en `.agents/skills/project-harness-governance/`:
gobierno documental (fuentes canónicas, discrepancias con evidencia,
etiquetas [C]/[D]/[I]/[P], límites). La cargan `harness-steward`
(siempre en docs) y los agentes de dominio cuando su tarea toque
documentación.

Coordinación: OpenCode principal orquesta e integra; un solo agente
escribe a la vez; ningún agente hace commit, push, PR, merge,
despliegue ni cambios externos sin autorización separada — la única
excepción es `git-integration`, que ejecuta exactamente lo autorizado
por el gate vigente sin editar archivos. Detalle de
permisos y herramientas por agente en cada definición y en
`docs/TOOLS.md`.
