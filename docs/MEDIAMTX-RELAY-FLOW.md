# Flujo propuesto con relay local MediaMTX

> Estado: propuesta `[P]`, no contrato validado ni comportamiento de
> extremo a extremo confirmado. Preparado el 2026-10-26 para orientar
> la investigación T-078. No implica autorización para modificar RTMP,
> claves, Aitum ni outputs de OBS. La fuente canónica de tareas sigue
> siendo `docs/BACKLOG.md` (T-077 intacta en investigación; T-078 nueva).

## Qué es MediaMTX

MediaMTX es un programa distribuido como ejecutable único,
open-source bajo licencia MIT, que corre en el PC del usuario como
relay local, sin servicio externo ni nube [D: mediamtx.org/docs/misc/license
consultado 2026-10-26].

## Actores

- **U** — Tú / operador.
- **P** — Plugin OBS (administra metadatos y el forward del relay).
- **R** — MediaMTX local (relay en el PC del usuario).
- **M** — Meta Graph API.
- **A** — OBS / Aitum (envía el vídeo).

## Diagrama de secuencia

```mermaid
sequenceDiagram
    autonumber
    actor U as Tú / operador
    participant P as Plugin OBS
    participant R as MediaMTX local
    participant M as Meta Graph API
    participant A as OBS / Aitum

    Note over U,R: Configuración única (una vez)
    U->>R: Instalas el relay local MediaMTX
    U->>A: Fijas un único destino RTMP: rtmp://127.0.0.1:1935/live
    U->>P: Conectas tu perfil de Facebook (OAuth publish_video)

    Note over U,M: Preparación de cada emisión
    U->>P: Defines título, descripción y audiencia
    U->>P: Pulsas Preparar transmisión
    P->>M: POST /me/live_videos con metadatos y audiencia
    M-->>P: ID del LiveVideo + ingesta RTMPS
    P->>R: Fija forward vía Control API :9997 hacia la ingesta del LiveVideo
    R-->>P: Confirma destino actualizado
    P-->>U: Muestra preparado con destino fijo + LiveVideo exacto

    Note over U,A: Envío de vídeo y vista previa
    U->>A: Inicias el envío al relay fijo (siempre la misma URL)
    A->>R: Envía RTMP al relay local
    R->>M: Reenvía a la ingesta RTMPS del LiveVideo
    M-->>A: Recibe vídeo; estado esperado: vista previa
    A-->>U: Confirma señal entrante / vista previa disponible

    Note over U,M: Publicación
    U->>P: Revisas la vista previa y pulsas Transmitir
    P->>M: Solicita GoLive sobre el mismo LiveVideo por su ID
    M-->>P: Devuelve resultado y estado del LiveVideo
    P-->>U: Confirma emisión pública o muestra el error real
```

## Mapeo a objetivos T-077 [P]

- **URL persistente real:** Aitum apunta siempre al relay fijo
  (`rtmp://127.0.0.1:1935/live`); el destino cambiante de cada
  LiveVideo (F-092) lo resuelve el relay, no el operador.
- **Metadatos validados E2E 2026-10-26:** create/read-back/update de
  título y descripción ya pasaron en lab [D: E2E lab 2026-10-26,
  `docs/VALIDATION.md` T-077]; el relay no los toca.
- **Cero ingeniería de vídeo:** el plugin no codifica ni transmuxa;
  solo fija el forward y ordena GoLive. El vídeo lo mueve MediaMTX.

## Preguntas abiertas [P]

1. Endpoint exacto de la Control API para cambiar el forward en
   caliente sin reiniciar el relay ni cortar la fuente Aitum.
2. Aviso de licencia MIT en el instalador si se empaqueta MediaMTX
   (atribución conforme a su licencia).
3. Umbrales de lab: latencia añadida, CPU en el PC del usuario y
   recovery ante caída del relay o del destino.
4. Ciclo de vida del proceso hijo + puertos 1935/9997 + firewall de
   Windows (ocupación previa, permisos, convivencia con OBS).
5. Bus-factor / salud del proyecto MediaMTX como dependencia
   (mantenimiento, releases, riesgo de abandono).

## Enlaces

- `docs/FACEBOOK-LIVE-API-FLOW.md` — flujo base LiveVideo propio.
- `docs/T0FB-RESEARCH.md` — investigación FB-0 (OAuth, endpoints,
  go-live/stop, tokens, errores).
- F-092 (persistencia NO: sin URL única entre creates), F-093
  (privacy ilegible + badge "Solo yo"), F-094 (localhost bloqueado en
  app Live + Test App como vía), F-095 (bug lab Status-comas, solo lab).
