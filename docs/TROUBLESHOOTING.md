# TROUBLESHOOTING — resolución de problemas

Flujo obligatorio:

```text
PROBLEM → REPRODUCE → COLLECT EVIDENCE → IDENTIFY SCOPE → HYPOTHESIS → TEST → FIX → VALIDATE → DOCUMENT
```

- Reproducir con pasos mínimos; adjuntar comando + salida + versión.
- Una hipótesis cada vez; validar antes de afirmar.
- Si no se resuelve: pasar la tarea a `bloqueada` en BACKLOG + entrada en FINDINGS (sin "probablemente funciona").
- Casos API esperados (`AGENTS.md` §28): 401→refresh/reconexión, 403→scopes, 404→broadcast, 429→sin reintentos agresivos, 5xx→backoff limitado.

## T-012 — instalación reproducible del toolchain (2026-09-08)

Comando ejecutado (PowerShell, Windows 11 x64):

```powershell
winget install --id Microsoft.VisualStudio.2022.Community --exact --silent `
  --override "--quiet --wait --norestart \
  --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended \
  --add Microsoft.VisualStudio.Component.VC.ATL \
  --add Microsoft.VisualStudio.Component.Windows11SDK.22621 \
  --add Microsoft.VisualStudio.Component.VC.CMake.Project"
```

Notas:

- VS Community 2022 17.14 + MSVC 19.44 + ATL + SDK 10.0.22621 + CMake 3.31.6 (bundled VS, sin install aparte) + Git 2.49.0. Sin Qt manual (prohibido por ADR-005; lo da obs-deps en P1).
- CMake auto-selecciona el SDK más nuevo instalado (10.0.26100.0 aquí); si P1 exige compilar contra 22621, fijar `CMAKE_SYSTEM_VERSION=10.0.22621.0`.
- Wiki `obs-studio` Build Instructions (ed. 2026-06-20) pide VS2026/SDK26100/CMake4.2, pero es para compilar OBS-master, no el template (ver F-008). No mezclar requisitos.

## T-013 — validar el plugin en OBS 32.2.2 sin admin (2026-09-08)

Comandos (PowerShell, Windows 11 x64, CMake bundled de VS):

```powershell
$env:Path = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;' + $env:Path
cmake -S . -B build_x64 --preset windows-x64 -DENABLE_FRONTEND_API=ON -DENABLE_QT=ON -DCMAKE_SYSTEM_VERSION=10.0.22621.0
cmake --build build_x64 --config RelWithDebInfo
cmake --install build_x64 --config RelWithDebInfo --prefix '<staging>'
```

Despliegue dev (sin admin; F-010 — `%APPDATA%\obs-studio\plugins` NO se escanea en Windows):

```powershell
# <staging>\obs-stream-metadata\bin\64bit\*.dll -> $env:OBS_PLUGINS_PATH
# DATA_PATH es la BASE: OBS antepone /%module% (F-027). Copiar una vez:
# Copy-Item -Recurse '<staging>\obs-stream-metadata\data\*' '<base>\obs-stream-metadata\'
$env:OBS_PLUGINS_PATH = '<dir-con-dll>'; $env:OBS_PLUGINS_DATA_PATH = '<base-data>'
Start-Process 'C:\Program Files\obs-studio\bin\64bit\obs64.exe' -WorkingDirectory 'C:\Program Files\obs-studio\bin\64bit'
```

Reglas (F-009, F-011):

- **`-WorkingDirectory` obligatorio** = `bin\64bit` del OBS instalado; sin esto el arranque aborta con `Failed to find locale/en-US.ini`.
- **Cerrar siempre elegante** (`CloseMainWindow` + esperar salida); jamás `Stop-Process -Force` (deja `run_*` en `%APPDATA%\obs-studio\.sentinel` y el siguiente arranque pide safe-mode). Si ocurre: borrar `run_*` obsoletos y relanzar.
- Carga verificada en: módulos del proceso (`obs-stream-metadata.dll`) + log `%APPDATA%\obs-studio\logs` (`plugin loaded successfully` / `plugin unloaded`).

## T-031 — validación viva del dock MVP (operador, con sus propias apps)

> Nota ADR-012: este procedimiento es hoy la base de validación del modo
> Independiente (apps propias del usuario). El modo Administrado usa backend
> y no pide estas credenciales.

Requisito: una app registrada por el operador en cada plataforma
(Twitch: consola dev; Google: Cloud Console cliente Desktop + YouTube
Data API habilitada; Kick: portal dev con redirect
`http://localhost:3000/cb`). Los valores viven SOLO en env vars locales
de la sesión de OBS — jamás en repo, chat o capturas (F-018, F-024):

```powershell
$env:STREAM_META_TWITCH_CLIENT_ID = '<id>'
$env:STREAM_META_YOUTUBE_CLIENT_ID = '<id>'
$env:STREAM_META_YOUTUBE_CLIENT_SECRET = '<secret>'
$env:STREAM_META_KICK_CLIENT_ID = '<id>'
$env:STREAM_META_KICK_CLIENT_SECRET = '<secret>'
$env:OBS_PLUGINS_PATH = '<staging>\obs-stream-metadata\bin\64bit'
# DATA_PATH es la BASE: OBS antepone /%module% (OBSBasic.cpp:136-139, F-027),
# asi que el contenido de <staging>\obs-stream-metadata\data\ debe copiarse a
# <base>\obs-stream-metadata\ (una vez por staging):
# Copy-Item -Recurse '<staging>\obs-stream-metadata\data\*' '<base>\obs-stream-metadata\'
$env:OBS_PLUGINS_DATA_PATH = '<base>'
Start-Process 'C:\Program Files\obs-studio\bin\64bit\obs64.exe' -WorkingDirectory 'C:\Program Files\obs-studio\bin\64bit'
```

En el dock: Connect por plataforma (Twitch: device flow con user_code;
YouTube/Kick: navegador + callback local) → título de prueba →
Refresh broadcasts (YouTube) → Apply → verificar en la web de cada
plataforma. Kick: verificar **en directo** (en offline el 204 aplica
pero no es legible, F-023). Negativos: título 101 con YouTube
seleccionado (bloquea antes de red), token revocado (401 → refresh o
reconexión), plataforma sin conectar (error individual, el resto sigue).

## T-032 — alta de credenciales BYO-app + persistencia DPAPI (2026-09-09)

> Nota ADR-012: BYO-app = modalidad Independiente del producto (no solo
> operador/debug). Desde T-041 el dock tiene selector Independent/Managed y
> persiste `connection_mode` en `accounts.json` (Independent por defecto para
> ficheros antiguos). La sección T-033 siguiente sigue el mismo modelo.

Las env vars `STREAM_META_*` ya no se leen (código `env()` eliminado).
Alta una sola vez por plataforma, con tus propias apps:

1. Twitch: consola dev → app OAuth (device flow, sin secret) → Client ID.
   YouTube: Cloud Console → cliente **Desktop** + YouTube Data API
   habilitada → Client ID + Client Secret. Kick: portal dev con redirect
   `http://localhost:3000/cb` → Client ID + Client Secret.
2. En el dock, escribe cada valor en su campo (los secrets muestran
   puntos) y pulsa Connect por plataforma como en T-031.
3. Al conectar, la sesión queda cifrada (DPAPI) en
   `%APPDATA%\obs-studio\plugin_config\obs-stream-metadata\accounts.json`
   — verifica que el fichero existe y que NO contiene tus valores en
   claro (ábrelo con un editor: solo verás etiquetas y blobs base64).
4. Reinicia OBS: las cuentas deben aparecer `Connected as …` sin pedir
   nada. Disconnect revoca en el proveedor (mira `revoke <plat> http 2xx`
   en el log) y borra el registro.
5. 429/5xx en Apply: el resultado muestra `Retrying…` hasta 2 veces
   (2 s, 4 s) y luego informa; jamás reintenta en bucle.

Instalación del staging y reglas de CWD/cierre elegante: idénticas a
T-031 (arriba). Si OBS está en uso, instala en un staging nuevo; el
DLL en uso está bloqueado.

## T-033 — matriz viva §39-40 del operador (P6, pendiente)

Alta de credenciales y despliegue: idénticos a T-032 (arriba). Los
negativos bloqueados por validación local ya están cubiertos offline
(`metadata-selfcheck.exe`: título vacío/141/101, descripción 5001,
mapa §28 completo, mensajes sin secretos, payload YouTube `id+snippet`
sin `contentDetails`, backoff 2/4/6 s, revoke por proveedor). Lo que
sigue exige cuentas reales y lo ejecuta el operador; no se declara
verde desde esta sesión:

**Positivos §39 (verificar en la web de cada plataforma):**
Twitch 1-9 (conectar → identidad → título → read-back web →
reinicio → persistencia DPAPI → título de nuevo → revocar en la web
→ 401 con `Reconnect`); YouTube 1-10 (conectar → permiso live →
listar → seleccionar → título+descripción → read-back Studio →
reinicio → repetir → probar en `upcoming/ready` y en `live`);
Kick 1-9 igual que Twitch, confirmando `204` y verificando el título
**en directo** (en offline el 204 aplica pero no es legible, F-023).

**Negativos vivos §40 (un fallo no bloquea al resto de plataformas):**
Twitch con scope sin `channel:manage:broadcast` (403 `permissions`);
YouTube con broadcast inexistente (404 `no valid broadcast`), cuenta
sin live streaming habilitado y token sin permisos (403/401);
Kick con scope sin `channel:write` (403) y token expirado (401 →
refresh único → `Reconnect`); 429/5xx en Apply (`Retrying…` máx 2,
jamás en bucle). Sin polling (cuota YouTube §11.9): listar broadcasts
solo al pulsar Refresh, actualizar solo al pulsar Apply.

Reglas de seguridad (F-018, F-024): jamás tokens/codes/verifiers en
chat, capturas, repo, logs ni PR; tras cada sesión viva, comprobar
que `%APPDATA%\obs-studio\logs` no contiene secretos (solo longitudes
y códigos HTTP).

## T-048 — procedimiento Managed dev (2026-09-09)

Backend local + `managed-link-test.exe` (nunca en installer):

```powershell
# Terminal 1 — backend con credenciales dev SOLO en entorno (jamás en repo):
$env:STREAM_META_BACKEND_SECRET_GOOGLE_CLIENT_ID = '<id>'   # o KICK_*
$env:STREAM_META_BACKEND_SECRET_GOOGLE_CLIENT_SECRET = '<secret>'
$env:STREAM_META_BACKEND_PROVIDERS = 'youtube'  # o 'kick'; DEV-only
$env:STREAM_META_BACKEND_PORT = '9004'          # según redirect registrado
python -m backend.app
# Terminal 2 — cliente C++ real (Qt bin en PATH):
.\build_x64\RelWithDebInfo\managed-link-test.exe --base-url http://127.0.0.1:9004 `
  --provider youtube --store $env:TEMP\t048.json --timeout-s 300
# Abrir la URL impresa, consentir, verificar CONNECTED + DISCONNECT. El exe
# borra el store temporal al salir. Puertos: YT 9004 (127.0.0.1), Kick 3000
# (localhost); una instancia backend por puerto.
```

El dock usa `STREAM_META_BACKEND_URL` (DEV-only) o el default
`http://127.0.0.1:8080`; en producción será URL configurada (T-054).

## T-052 — checklist operador cross-mode GUI (PENDING, requiere clicks)

Automatizado ya cubre stores/backend; esto exige el dock real en OBS:

1. Independent: conectar Twitch/YouTube/Kick BYO-app → Apply OK → reiniciar OBS → `Connected as` intacto.
2. Cambiar a Managed (sin borrar nada): verificar que NO se copian credenciales (inspeccionar `accounts.json`: solo `connection_mode` cambia; blobs DPAPI intactos).
3. Managed: Connect YouTube/Kick → consentimiento → `Connected as` → reiniciar OBS → snapshot restaurado → Connect de nuevo reconecta sin navegador si el backend conserva la conexión.
4. Managed → Independent: cuentas Independent intactas, sin tokens backend en disco.
5. Disconnect en cada modo; revocar en la web del proveedor; reconectar.
6. Tras cada paso: `%APPDATA%\obs-studio\logs` sin secretos (longitudes/códigos como máximo).
