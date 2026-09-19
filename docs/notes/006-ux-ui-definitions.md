# Stream Manager — Definiciones UX/UI

> Documento vivo de definiciones, hallazgos y decisiones para la evolución UX/UI del dock de Stream Manager.
>
> **Estado:** Diseño conceptual en progreso — sin implementación iniciada.
>
> **Regla de trabajo:** este documento debe actualizarse a medida que aparezcan nuevos hallazgos técnicos, decisiones UX, restricciones o definiciones.

---

## 1. Objetivo

Rediseñar la interfaz del dock de Stream Manager para hacerla más clara, intuitiva y compacta, **sin perder ninguna funcionalidad existente ni introducir complejidad técnica innecesaria**.

El objetivo principal es pasar de una interfaz donde muchos controles permanecen visibles simultáneamente a una interfaz basada en **progressive disclosure**: mostrar inicialmente solo la información esencial y revelar la configuración específica de una plataforma cuando el usuario la abre.

Este diseño debe respetar la arquitectura y los contratos existentes del proyecto. La implementación real debe basarse en inspección del repositorio y evidencia de OpenCode, no en suposiciones visuales.

---

## 2. Evidencia técnica de partida

La auditoría del repositorio confirmó:

- El dock utiliza **Qt6 nativo con `QWidget`**.
- No utiliza `.ui`, QML, Browser Source ni WebView.
- `MetadataDock` concentra actualmente los widgets y buena parte del estado de la interfaz.
- El layout actual es principalmente un `QVBoxLayout` dentro de un `QScrollArea`.
- Los controles de plataforma actuales (`Twitch`, `YouTube`, `Kick`) incluyen checkboxes que representan la **selección para Apply**, no el estado de conexión.
- El estado de conexión es independiente de esa selección.
- Actualmente existe lógica separada para `Account` y `ManagedConn`.
- YouTube tiene necesidades específicas, especialmente la selección de broadcast.
- Apply procesa las plataformas seleccionadas de forma independiente y secuencial.
- Los resultados por plataforma existen internamente, pero no se consideran necesarios como una sección permanente de la interfaz general.

**Fuente:** auditoría de OpenCode del repositorio.

---

# 3. Modelo conceptual de la nueva UX

La interfaz se organizará alrededor de tres conceptos principales:

### 3.1. Connection Mode

Define **cómo se conectan las cuentas**:

- `Independent`
- `Managed`

El selector de modo continúa existiendo porque es una decisión global del usuario.

### 3.2. Platform Cards

Cada plataforma se representa como una única entidad visual:

- Twitch
- YouTube
- Kick

La misma card representa simultáneamente:

- estado de conexión;
- configuración de la plataforma;
- acciones de conexión/desconexión;
- selección para Apply.

No se crearán secciones separadas de "Accounts" y "Destinations".

### 3.3. Shared Content

El contenido común a las plataformas permanece fuera de las cards:

- Title
- Description

El título es compartido entre proveedores.

La descripción es específica de YouTube, aunque conceptualmente pertenece al bloque de contenido común. La lógica existente debe determinar cuándo aplica.

---

# 4. Concepto visual de Platform Cards

La idea aprobada es utilizar **cards/burbujas cuadradas con bordes redondeados**.

## 4.1. Card cerrada

Cuando una plataforma no está abierta para configuración, se representa como una burbuja compacta:

```text
┌───────────────┐
│               │
│     LOGO      │
│               │
└───────────────┘
```

La card cerrada debe ocupar poco espacio y permitir reconocer rápidamente cada plataforma.

---

## 4.2. Estado de conexión mediante color

El color de la burbuja es un indicador visual del estado general de la plataforma.

Conceptualmente:

| Estado | Representación |
|---|---|
| No conectada/configurada | Gris / desaturada |
| Conectada | Paleta/color asociado a la plataforma |
| Advertencia | Borde amarillo |
| Error | Borde rojo |
| Otros estados relevantes | Definir posteriormente según los estados reales disponibles |

**Importante:** el color expresa estado de conexión/salud de la plataforma; **no representa la selección para Apply**.

Los estados reales existentes incluyen conceptos como `Not connected`, `Connecting…`, `Connected as X`, `Error` y `Needs reconnection`. La representación visual concreta de cada uno se definirá posteriormente.

---

# 5. Interacción Hover + Apply

## 5.1. El hover no desconecta

El comportamiento definido es:

> Al pasar el puntero sobre una card cerrada, aparece temporalmente un control de check para controlar la participación de esa plataforma en Apply.

**No debe desconectar, conectar ni cambiar el estado de autenticación.**

---

## 5.2. Check de Apply

El check representa exclusivamente:

> **"¿Esta plataforma debe recibir los cambios cuando el usuario pulse Apply?"**

Esto conserva exactamente la semántica actual de los checkboxes:

- checked → participa en Apply;
- unchecked → no participa en Apply.

La selección debe persistir visualmente aunque el control temporal desaparezca al salir el puntero.

Conceptualmente:

```text
Sin hover

┌───────────┐
│           │
│   LOGO    │
│           │
└───────────┘


Hover

┌───────────┐
│       [✓] │  ← control temporal de Apply
│           │
│   LOGO    │
│           │
└───────────┘


Cursor fuera

┌───────────┐
│           │
│   LOGO    │
│           │
└───────────┘
```

El hecho de que el check desaparezca visualmente **no significa que la selección se pierda**.

---

## 5.3. Acciones distintas

Quedan establecidas dos interacciones independientes:

- **Click en la zona principal de la card:** abre/cierra la configuración.
- **Click en el check que aparece durante hover:** cambia la selección de Apply.

Nunca deben confundirse:

```text
Card principal → configuración
Check de hover → Apply
```

---

# 6. Expansión de la card

Cuando el usuario selecciona una plataforma, la burbuja se transforma visualmente en una card expandida.

No se requiere inicialmente una animación compleja.

La aproximación preferida es utilizar los mecanismos nativos de Qt:

- widgets;
- layouts;
- mostrar/ocultar contenido;
- recalcular el layout.

El objetivo es que **visualmente parezca una expansión**, pero manteniendo una implementación sencilla y robusta.

No se considera necesario introducir de entrada:

- QML;
- WebView;
- animaciones complejas;
- custom painting innecesario;
- sistemas de estado paralelos complejos.

---

# 7. Card no conectada

Una card abierta y no conectada debe mostrar únicamente lo necesario para iniciar la conexión.

Conceptualmente:

```text
╭─────────────────────────────╮
│ YouTube                  × │
│                             │
│          LOGO               │
│                             │
│       No conectado          │
│                             │
│       [ CONECTAR ]          │
╰─────────────────────────────╯
```

El botón `CONECTAR` inicia el flujo de autenticación correspondiente a la plataforma y al modo de conexión seleccionado.

---

# 8. Card conectada

Una card conectada debe revelar la configuración específica de esa plataforma.

Ejemplo conceptual para YouTube:

```text
╭─────────────────────────────╮
│ YouTube                  × │
│                             │
│          LOGO               │
│                             │
│   ✓ Connected as usuario    │
│                             │
│ Broadcast                   │
│ [ Mi transmisión        ▼ ] │
│                             │
│ [ Refresh ]                 │
│                             │
│ [ Desconectar ]             │
╰─────────────────────────────╯
```

El contenido exacto de Twitch, YouTube y Kick se definirá después de revisar qué controles existentes pueden reutilizarse y qué diferencias reales tiene cada proveedor.

---

# 9. Cierre de la card

La card expandida tendrá un control de cierre.

Al cerrarla:

- desaparece la configuración detallada;
- vuelve a la forma compacta;
- conserva visualmente su estado de conexión;
- conserva la selección de Apply.

Ejemplo:

```text
Abierta
┌──────────────────────────────┐
│ YouTube                   ×  │
│ ...                          │
└──────────────────────────────┘

        ↓ cerrar

┌───────────────┐
│     LOGO      │
└───────────────┘
```

Si está conectada, la burbuja mantiene el color de plataforma.

---

# 10. Errores y advertencias

No se desea una sección permanente de `RESULTS` en la interfaz general.

La decisión es:

> **El estado problemático debe representarse de forma implícita en la card y el detalle debe aparecer al abrir esa plataforma.**

Por ejemplo:

```text
YouTube normal

╭───────────╮
│   LOGO    │
╰───────────╯


YouTube con advertencia

╭───────────╮
│   LOGO    │
╰───────────╯
    borde
   amarillo


YouTube con error

╭───────────╮
│   LOGO    │
╰───────────╯
    borde
     rojo
```

Esto proporciona:

1. reconocimiento inmediato de que algo requiere atención;
2. una razón para abrir la plataforma;
3. detalle contextual dentro de la card;
4. una interfaz general mucho más limpia.

---

# 11. Información de resultados

### Decisión: eliminar `RESULTS` de la interfaz general

No se desea una zona permanente como:

```text
RESULTS

Twitch   ✓
YouTube  ✓
Kick     —
```

Los resultados, advertencias, errores y confirmaciones deberán integrarse contextualmente dentro de la plataforma correspondiente cuando sea necesario.

Esto es coherente con que Apply procesa cada plataforma de forma independiente.

La representación exacta de:

- éxito;
- error;
- warning;
- retry;
- reconexión;

se definirá posteriormente dentro de cada card.

---

# 12. Estructura conceptual general

La interfaz general queda planteada así:

```text
CONNECTION MODE

[ Independent ▼ ]


PLATFORMS

┌────────┐  ┌────────┐  ┌────────┐
│ Twitch │  │YouTube │  │  Kick  │
│  LOGO  │  │  LOGO  │  │  LOGO  │
└────────┘  └────────┘  └────────┘


CONTENT

Title
[......................................]

Description
[......................................]


                 [ APPLY ]
```

No se desea una sección permanente de resultados.

Las plataformas comunican su estado mediante sus propias cards.

---

# 13. Principios UX establecidos

## 13.1. Progressive disclosure

No mostrar permanentemente controles que solo son relevantes después de seleccionar una plataforma.

## 13.2. Una plataforma = una entidad visual

No duplicar Twitch/YouTube/Kick en distintas secciones.

## 13.3. Estado de conexión ≠ selección de Apply

Son conceptos distintos y deben seguir siendo visualmente distintos.

## 13.4. Hover para acciones secundarias

El control de Apply puede aparecer durante hover para mantener las cards limpias.

## 13.5. Estado visible sin texto permanente

Color y borde permiten comunicar rápidamente el estado sin llenar el dock de mensajes.

## 13.6. Detalle bajo demanda

Los detalles de error, warning, confirmación y configuración aparecen al abrir la plataforma.

## 13.7. Reutilizar la lógica existente

La nueva UX debe representar los estados y acciones reales del sistema siempre que sea posible, en lugar de crear una segunda lógica de negocio.

## 13.8. Evitar complejidad innecesaria

La primera implementación debe favorecer widgets Qt6 y layouts existentes frente a nuevas tecnologías o sistemas de UI complejos.

---

# 14. Decisiones NO tomadas todavía

Estas cuestiones quedan deliberadamente abiertas para la siguiente fase:

### 14.1. Anatomía exacta de cada card

Determinar qué elementos aparecen en:

- Not connected;
- Connecting;
- Connected;
- Warning;
- Error;
- Needs reconnection.

### 14.2. Diferencias por proveedor

Definir exactamente qué contiene:

- Twitch;
- YouTube;
- Kick.

### 14.3. Tratamiento del título y descripción

Definir posición, jerarquía visual, validación y comportamiento contextual.

### 14.4. Diseño exacto del indicador de estado

Definir:

- tonos;
- grosor del borde;
- iconografía;
- prioridad entre color de plataforma y borde de warning/error.

### 14.5. Hover

Definir comportamiento exacto del hover y su implementación Qt, manteniendo la regla conceptual ya aprobada.

### 14.6. Aplicación durante Apply

La implementación actual deja editables varios controles mientras Apply está en progreso. Debe decidirse posteriormente si esto se conserva o si se mejora como parte de la UX.

### 14.7. Managed

Existe un hallazgo técnico importante pendiente de resolver:

`ManagedConn` actualmente no alimenta el flujo de Apply del dock de la misma manera que `Account`.

Esto **no debe ocultarse mediante el rediseño visual**. Debe investigarse y resolverse/decidirse explícitamente antes de considerar completa la UX para Managed.

---

# 15. Hallazgo técnico pendiente: Managed → Apply

La auditoría detectó que:

- `Managed` utiliza `ManagedConn`;
- Apply actualmente consume `Account`;
- el dock no está utilizando `ManagedConn` para alimentar Apply.

Por tanto, existe una posible inconsistencia funcional entre el modelo de conexión Managed y la aplicación de cambios.

**Estado:** pendiente de investigación/decisión.

**Regla:** no asumir una solución ni modificar esta lógica como parte del rediseño UX sin una inspección específica del repositorio y una decisión explícita.

---

# 16. Flujo de trabajo para continuar

Este documento es una **especificación viva**, no un prompt de implementación.

El proceso acordado es:

1. ChatGPT formula una hipótesis, decisión o pregunta UX.
2. Cuando sea necesario verificar implementación real, se solicita a OpenCode una inspección concreta.
3. OpenCode devuelve evidencia del repositorio.
4. Se separa:
   - HECHO;
   - INFERENCIA;
   - UNKNOWN/PENDIENTE.
5. Se toma la decisión UX correspondiente.
6. Se actualiza este documento.
7. Solo cuando el diseño y las dependencias estén suficientemente definidos se prepara un prompt de implementación.

**No iniciar código basándose únicamente en este documento si todavía existen UNKNOWNs que puedan cambiar la arquitectura.**

---

# 17. Estado actual del diseño

### Aprobado

- [x] Modelo B: una card por plataforma.
- [x] Cards/burbujas cuadradas con bordes redondeados.
- [x] Estado de conexión comunicado mediante color.
- [x] Card gris cuando no está conectada/configurada.
- [x] Card con color de plataforma cuando está conectada.
- [x] Hover muestra temporalmente el control ✓ de Apply.
- [x] El ✓ controla únicamente la participación en Apply.
- [x] El ✓ no conecta ni desconecta.
- [x] El ✓ desaparece al salir del hover, pero la selección permanece.
- [x] Click en la card abre/cierra configuración.
- [x] La card expandida contiene configuración específica de la plataforma.
- [x] No mostrar `RESULTS` permanentemente en la interfaz general.
- [x] Errores y warnings deben reflejarse visualmente en la card.
- [x] El detalle del error/warning se muestra al abrir la plataforma.
- [x] Mantener el diseño simple mediante Qt6/QWidget/layouts.
- [x] No duplicar plataformas en secciones de cuentas/destinos.

### Pendiente

- [ ] Estados visuales completos, especialmente el mapeo definitivo de warning → amarillo.
- [ ] Logos oficiales y paletas exactas por plataforma.
- [ ] Contenido final específico de Twitch.
- [ ] Contenido final específico de YouTube.
- [ ] Contenido final específico de Kick.
- [ ] Comportamiento hover fino dentro de `QScrollArea`/OBS.
- [ ] Decidir si se mantiene exactamente la habilitación actual de controles durante Apply.
- [ ] Tratamiento funcional de `Managed/Apply` (fuera del rediseño UI-only).
- [ ] Validación visual/manual mediante OBS.
- [ ] Prompt de implementación.

---

## 18. Resultado del checkpoint técnico

El checkpoint UX-UI de viabilidad confirmó que la UX definida es **implementable principalmente mediante reorganización de la UI existente**.

### Veredicto

- Viable con la arquitectura actual.
- Aproximadamente **80–85% UI-only**.
- El resto corresponde principalmente a integración pequeña para estados/feedback visual.
- No existe bloqueo funcional para el flujo Independent.
- No es necesario reescribir OAuth, Apply, broadcast, validación, persistencia ni threading para conseguir la UX Independent.
- Existe un único `SCOPE RISK` real: **Managed/Apply**.

### Estructura confirmada

La estructura de implementación mínima prevista es:

```text
MODE
  ↓
PLATFORM CARDS
  ├─ Twitch
  ├─ YouTube
  └─ Kick
  ↓
CONTENT
  ├─ Title
  └─ Description
  ↓
APPLY
```

Cada plataforma tendrá un `QFrame`/contenedor de card y un sub-widget de detalle reutilizando los widgets existentes.

### Reutilización confirmada

Los siguientes elementos pueden reubicarse sin reescribir su lógica funcional:

- checks de Apply;
- status;
- Connect/Disconnect;
- credenciales Independent;
- `devicePrompt_` de Twitch;
- `broadcastCombo_` y Refresh de YouTube;
- Title/Description;
- Apply;
- lógica OAuth;
- lógica de broadcast;
- lógica de Apply.

La recomendación es mover/reparentar los widgets existentes en lugar de crear modelos paralelos.

### Apply

Se confirma que la semántica actual puede mantenerse intacta:

```text
hover → mostrar check
click check → cambiar isChecked()
salir del hover → ocultar check
cerrar card → no modificar isChecked()
Apply → continúa leyendo isChecked()
```

No se debe modificar `onApply`, `startApplyNext` ni `validate` para conseguir este comportamiento.

### Estados visuales

Existe correspondencia directa para:

- `Not connected` → estado gris;
- `Connected as X` → color de plataforma;
- `Error: ...` / `Needs reconnection` → borde rojo;
- éxito → feedback dentro del detalle de la plataforma.

El mapeo de `Retrying…`/`Updating…` a warning amarillo queda pendiente de decisión UX.

### Results

Queda confirmado que no se necesita una zona global de `RESULTS`.

Los labels/resultados actuales deben reutilizarse o redirigirse al detalle de la plataforma correspondiente, sin eliminar la lógica que genera esos mensajes.

### YouTube

Se confirma que `broadcastCombo_` puede trasladarse al detalle de YouTube sin sustituir su almacenamiento actual (`currentData`) ni cambiar la lógica de Refresh/Apply.

Debe preservarse el comportamiento existente de `clear()` durante `wipeLocal(YT)` y al cargar nuevamente la lista.

### Managed

El problema `ManagedConn → Apply` sigue presente en `192ba40`.

No forma parte del rediseño UI-only.

Debe mantenerse documentado como `SCOPE RISK — Managed/Apply` y no corregirse accidentalmente durante la reorganización visual.

### Riesgos de implementación confirmados

1. No perder el vínculo de `broadcastCombo_`/`currentData`.
2. Redirigir correctamente `setStatus`/`setResult` a los detalles de cada card.
3. Mantener la semántica de los checks de Apply.
4. Mantener `modeFromCombo`/`refreshModeUi` coherentes.
5. Evitar flicker del hover dentro de `QScrollArea`.
6. No mezclar la implementación visual con la corrección funcional de Managed/Apply.

---

## 19. Próximo paso

Ya no necesitamos seguir diseñando la UX general durante mucho tiempo.

El siguiente paso es preparar una **implementación mínima y controlada**, pero antes debe cerrarse únicamente lo imprescindible:

1. definición visual mínima de las tres cards;
2. mapeo final de warning amarillo;
3. confirmación de qué elementos exactos aparecen en cada card;
4. prompt de implementación para OpenCode.

La implementación debe comenzar priorizando **Independent** y evitando convertir `Managed/Apply` en una tarea de alcance mayor.

---

## 20. Historial de decisiones

| Fecha | Decisión / avance |
|---|---|
| 2026-09-10 | Se selecciona el modelo UX **B**: una card por plataforma. |
| 2026-09-10 | Se define concepto de burbujas cuadradas con bordes redondeados. |
| 2026-09-10 | Se define color como indicador visual del estado de conexión. |
| 2026-09-10 | Se define hover con check temporal para controlar Apply. |
| 2026-09-10 | Se confirma que el check representa exclusivamente la selección para Apply. |
| 2026-09-10 | Se elimina conceptualmente la sección global `RESULTS`. |
| 2026-09-10 | Se define que errores/warnings se indiquen en la card mediante borde y se detallen al abrirla. |
| 2026-09-10 | Se establece Qt6/QWidget/layout como aproximación técnica preferida para evitar complejidad innecesaria. |
| 2026-09-10 | Se confirma que solo una Platform Card estará expandida simultáneamente. |
| 2026-09-10 | Checkpoint técnico confirma viabilidad principalmente UI-only (~80–85%). |
| 2026-09-10 | Se confirma que Independent no tiene bloqueo funcional para el rediseño. |
| 2026-09-10 | `Managed/Apply` queda formalmente aislado como `SCOPE RISK`, fuera del rediseño visual. |
| 2026-09-10 | Se confirma que `RESULTS` puede eliminarse de la vista global redirigiendo feedback al detalle de cada card. |

---

