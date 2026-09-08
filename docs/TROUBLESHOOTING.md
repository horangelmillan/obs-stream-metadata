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
# <staging>\obs-stream-metadata\data\ -> $env:OBS_PLUGINS_DATA_PATH\obs-stream-metadata\
$env:OBS_PLUGINS_PATH = '<dir-con-dll>'; $env:OBS_PLUGINS_DATA_PATH = '<base-data>'
Start-Process 'C:\Program Files\obs-studio\bin\64bit\obs64.exe' -WorkingDirectory 'C:\Program Files\obs-studio\bin\64bit'
```

Reglas (F-009, F-011):

- **`-WorkingDirectory` obligatorio** = `bin\64bit` del OBS instalado; sin esto el arranque aborta con `Failed to find locale/en-US.ini`.
- **Cerrar siempre elegante** (`CloseMainWindow` + esperar salida); jamás `Stop-Process -Force` (deja `run_*` en `%APPDATA%\obs-studio\.sentinel` y el siguiente arranque pide safe-mode). Si ocurre: borrar `run_*` obsoletos y relanzar.
- Carga verificada en: módulos del proceso (`obs-stream-metadata.dll`) + log `%APPDATA%\obs-studio\logs` (`plugin loaded successfully` / `plugin unloaded`).
