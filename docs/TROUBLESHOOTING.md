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
