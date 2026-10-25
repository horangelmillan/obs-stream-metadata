# Flujo propuesto de Facebook Live creado por API

> Estado: propuesta de proceso `[P]`, no contrato de comportamiento ya
> validado de extremo a extremo. Preparado el 2026-10-24 para orientar la
> siguiente investigación/PoC. No implica autorización para modificar
> RTMP, claves, Aitum ni outputs de OBS.

## Propósito y límite

Este flujo describe el objetivo del operador para transmitir desde su perfil
personal de Facebook: el plugin administra título, descripción y audiencia
del LiveVideo que crea mediante Graph API; OBS/Aitum envía el video a la
ingestión de ese mismo LiveVideo; y la emisión se publica al confirmar
Transmitir.

El proceso **no adopta el borrador creado desde Live Producer web**. Las
sondas de perfil documentadas en F-065/F-066 y F-080 no pudieron enumerar ni
leer ese objeto. La ruta representada aquí crea un objeto nuevo por API,
con un ID conocido desde el inicio (F-067).

## Diagrama de secuencia

```mermaid
sequenceDiagram
    autonumber
    actor U as Tú / operador
    participant P as Plugin OBS
    participant M as Meta Graph API
    participant A as OBS / Aitum

    Note over U,M: Conexión inicial (una vez o al reconectar)
    U->>P: Conectas tu perfil de Facebook
    P->>M: OAuth con publish_video
    M-->>P: Token e identidad del perfil
    P-->>U: Cuenta conectada

    Note over U,M: Preparación de cada emisión
    U->>P: Defines título, descripción y audiencia Público
    U->>P: Pulsas Preparar transmisión
    P->>M: POST /me/live_videos con metadatos,<br/>privacy=EVERYONE y estado de preparación
    M-->>P: ID del LiveVideo + datos de ingestión RTMPS
    P->>P: Conserva el ID para operar sobre el objeto exacto
    P-->>U: Muestra preparado y confirma los datos configurados

    Note over U,A: Envío de video y vista previa
    U->>A: Configuras el destino Facebook para esta ingestión
    U->>A: Inicias el envío de video a Facebook
    A->>M: Envía RTMPS al LiveVideo recién creado
    M-->>A: Recibe video; estado esperado: vista previa
    A-->>U: Confirma señal entrante / vista previa disponible

    Note over U,M: Publicación (paso de integración por validar)
    U->>P: Revisa la vista previa y pulsa Transmitir
    P->>M: Solicita publicar el mismo LiveVideo por su ID
    M-->>P: Devuelve resultado y estado del LiveVideo
    P-->>U: Confirma emisión pública o muestra el error real
    M-->>U: El perfil muestra la transmisión a la audiencia elegida
```

## Participación por actor

| Momento | Tú | Plugin | Meta/Facebook | OBS/Aitum |
|---|---|---|---|---|
| Conexión inicial | Autorizas el perfil | Completa OAuth y conserva la sesión según el modo | Devuelve identidad/token | No participa |
| Preparación | Defines título, descripción y Público; ordenas preparar | Crea el LiveVideo por API y conserva su ID | Crea el objeto y devuelve ID e ingestión | No participa |
| Preview | Revisas que la señal y los detalles correspondan | Mantiene asociado el ID correcto | Recibe RTMPS y mantiene la vista previa | Envía el video |
| Salida al aire | Confirmas Transmitir | Solicita publicar el objeto exacto y comprueba estado | Hace visible el live si acepta la transición | Continúa enviando video |
| Fin | Decides terminar | Solicita finalizar y verifica el resultado | Marca el live como terminado | Detiene el output cuando corresponda |

## Evidencia y estado

- **[D] Creación por API:** la referencia de Meta v26.0 documenta la
  creación en `/{user_id}/live_videos`, admite `title`, `description`,
  `privacy` y estados de preparación/publicación, y devuelve `id` y URL(s)
  de ingestión. `UNPUBLISHED` es preparación no visible y puede caducar tras
  varias horas. Fuente: <https://developers.facebook.com/docs/graph-api/reference/user/live_videos/> — v26.0 — consultado 2026-10-24 — afirma creación y campos/respuesta.
- **[D] Creación, cambios y lectura confirmados en el proyecto:** F-067
  registra que un LiveVideo propio creado por API aceptó cambios de título,
  descripción y `privacy=EVERYONE`, con lectura posterior idéntica. Esa
  prueba no envió señal de OBS/Aitum ni demuestra que una audiencia externa
  viera una emisión.
- **[D] Borrador web:** F-065/F-066 y F-080 registran lista vacía y lectura
  directa no gestionable para el ID de Live Producer probado; no se debe
  reutilizar ese ID como si fuera el ID del objeto API.
- **[D] Ingestión externa:** Aitum permite destinos con dirección de servidor
  y clave RTMP. Fuente: <https://aitum-tv.com/multistream/> — consultado
  2026-10-24 — afirma gestión de destinos de multistream.
- **[I] Unión del proceso:** usar en Aitum la ingestión que devuelve la
  creación API debería hacer que el video enviado llegue al LiveVideo
  asociado a ese ID. Debe confirmarse en la PoC y con lectura de estado.
- **[P] Publicación real:** verificar la secuencia exacta para pasar de
  `UNPUBLISHED` con señal entrante a `LIVE_NOW` y confirmar que aparece
  públicamente en el perfil. No declarar éxito solo porque un POST devuelve
  200; comprobar estado y lectura de metadatos/audiencia.
- **[P] Límite de alcance vigente:** `AGENTS.md` §23 y §51 excluye modificar
  RTMP/stream keys/Aitum/multistream. Este documento es un proceso objetivo
  para contrastar; cambiar ese límite requiere decisión y alcance aprobados
  antes de implementar.

## Criterios de la prueba controlada

1. Crear exactamente un LiveVideo desde el perfil conectado con valores de
   prueba en título, descripción y audiencia `EVERYONE`.
2. Leer el mismo ID y comprobar que Meta devuelve esos valores.
3. Enviar una señal de prueba de OBS/Aitum a la ingestión de ese ID y
   comprobar que el objeto alcanza preview/estado esperado.
4. Ejecutar la acción de publicación acordada sobre ese ID y verificar el
   estado y la visibilidad en el perfil.
5. Finalizar y limpiar el objeto de prueba según su estado, registrando
   códigos/resultados sin tokens, dirección de ingestión ni claves.
6. Si un paso no está permitido o no cambia el estado leído, detener la
   prueba y registrar el error; no seguir como si el live estuviera público.

Esta secuencia no amplía por sí sola el alcance del producto. La tarea
T-077 en `docs/BACKLOG.md` gobierna la decisión y validación pendientes.
