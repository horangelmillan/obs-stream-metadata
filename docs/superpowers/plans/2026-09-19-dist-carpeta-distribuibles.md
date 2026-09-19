# T-067 Carpeta `dist/` para distribuibles Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Los instaladores `.exe` finales viven solo en `dist/` (ignorada por git) y se producen con un único `tools/package.ps1 -Flavor`, sin `.exe` trackeados y sin secretos en repo ni instalador.

**Architecture:** `build_x64/` sigue como área efímera (build + staging `obs-package` + `pkg/` de makensis); `tools/package.ps1` valida el sabor, ejecuta configure→build→install→makensis y copia el `.exe` + `.sha256` a `dist/` con el sabor en el nombre. Publicación = adjunto de GitHub Release, nunca commit del binario.

**Tech Stack:** PowerShell 5.1, CMake preset `windows-x64` (VS17 2022, SDK 10.0.22621.0), componente `obs-package`, NSIS 3.12 (`makensis`), `Get-FileHash SHA256`, `git check-ignore` / `git status` / `git ls-files` como gates.

**Spec:** `docs/notes/001-builds-por-comando-segun-destino.md` (sabores + reglas), `docs/INSTALLER.md` (payload exacto + comandos), `docs/BACKLOG.md` T-067, `AGENTS.md` §16 (secretos) y §36 (flujo instalar→aplicar), `docs/PHASES.md` P7, `docs/GIT.md` (sin binarios, Conventional Commits en español).

## Global Constraints

- Windows x64 + OBS Studio 32.2.2 objetivo (mínimo 30.0 para docks); Qt6 vía obs-deps del template.
- Payload del instalador = exactamente 3 ficheros (`obs-stream-metadata.dll` + `locale/en-US.ini` + `tls/qschannelbackend.dll`); sin secretos, sin dev tools (`managed-link-test.exe`, `metadata-selfcheck.exe` nunca entran), sin `%APPDATA%` (`docs/INSTALLER.md:24-36`).
- Ningún sabor escribe secretos en repo ni instalador; `commercial` falla si detecta `OBS_TWITCH_CLIENT_ID` seteado o `dev.env` con valores productivos (nota 001).
- `dist/` + `*.exe` jamás trackeados (publicación = GitHub Release); `.gitignore` ya ignora `build_x64/` y `*.exe`.
- Un objetivo cada vez; evidencia antes de avanzar (no abrir Task N+1 con Task N en rojo).
- Commits en español, atómicos, sin commit sin autorización del usuario; PR con template + CI verde + squash.
- Mínima modificación: solo `.gitignore`, `docs/INSTALLER.md`, nuevo `tools/package.ps1`; sin tocar `src/`, `backend/`, `CMakeLists.txt`.

---

## File Structure

- Modify: `.gitignore` — añadir `dist/` (+ `*.sha256` de distribuible) manteniendo `build_x64/` y `*.exe`.
- Modify: `docs/INSTALLER.md` — declarar `dist/` destino canónico + tabla sabor→backend/flags + comando `tools/package.ps1 -Flavor`.
- Create: `tools/package.ps1` — único punto de entrada (`-Flavor commercial|testing|local|prod-integrated`), valida entorno, corre configure/build/install/makensis, copia + hash a `dist/`.
- Modify (al cerrar): `docs/VALIDATION.md` — bloque T-067 con BUILD/PACKAGE/INSTALL/SECRET.
- Modify (al cerrar): `docs/BACKLOG.md` — T-067 `pendiente` → `en-progreso` → `hecha`.

---

## Estado actual verificado (2026-09-19, no re-verificar salvo regresión)

```text
DISTRIBUIBLE: build_x64/pkg/obs-stream-metadata-0.1.0-windows-x64.exe  (OUTDIR de makensis)
DEV-ONLY:     build_x64/RelWithDebInfo/metadata-selfcheck.exe + managed-link-test.exe (EXCLUDE_FROM_ALL)
STAGING:      build_x64/staging/obs-stream-metadata/... (componente obs-package)
IGNORADOS:    build_x64/ (.gitignore:3) + *.exe (.gitignore:28) → git ls-files | grep .exe = vacío
```

---

### Task 1: `dist/` ignorada + destino canónico en docs

**Files:**
- Modify: `.gitignore`
- Modify: `docs/INSTALLER.md`
- Test: `git check-ignore -v` + `git status --short --branch` (sin framework; el gate es el comando)

**Interfaces:**
- Consumes: `.gitignore` actual (`build_x64/`, `*.exe`).
- Produces: `dist/` ignorada; `docs/INSTALLER.md` nombra `dist/` como destino canónico.

- [ ] **Step 1: Añadir la excepción en `.gitignore`** — tras el bloque `# Build / CMake` (junto a `out/`), añadir:

```text
# Distribuibles (T-067): solo Release attachments, nunca trackeados
dist/
*.sha256
```

- [ ] **Step 2: Verificar que queda ignorada**

Run: `git check-ignore -v dist/obs-stream-metadata-0.1.0-windows-x64.exe`
Expected: PASS con `dist/` (cualquier `.exe` bajo `dist/` matchea).

- [ ] **Step 3: Declarar `dist/` en `docs/INSTALLER.md`** — añadir tras el bloque de comandos (§ Construir el instalador) un párrafo + tabla:

```markdown
## Destino canónico `dist/` (T-067)

El `.exe` final se copia a `dist/` con su `.sha256`:

| Sabor | Backend | Nombre |
|---|---|---|
| `commercial` | producción (`-DSTREAM_META_BACKEND_URL=https://<cloud-run-url>`) | `obs-stream-metadata-<versión>-windows-x64-commercial.exe` |
| `testing` | ambiente test | `...-testing.exe` |
| `local` | `http://127.0.0.1:8080` (default, sin flag) | `...-local.exe` |

Comando: `powershell -ExecutionPolicy Bypass -File tools/package.ps1 -Flavor commercial`
`dist/` está ignorada por git; la publicación es GitHub Release (nunca commit del binario).
```

- [ ] **Step 4: Verificar árbol limpio de binarios**

Run: `git status --short --branch`
Expected: PASS solo con `.gitignore` + `docs/INSTALLER.md` modificados; ningún `.exe` listado.

- [ ] **Step 5: No commit** (regla del proyecto: sin autorización del usuario no se commitea).

---

### Task 2: `tools/package.ps1 -Flavor` (cierra nota 001)

**Files:**
- Create: `tools/package.ps1`
- Test: `powershell -ExecutionPolicy Bypass -File tools/package.ps1 -Flavor local -WhatIf` (dry-run que imprime comandos sin ejecutar) + ejecución real `local` en Task 3

**Interfaces:**
- Consumes: preset `windows-x64`, `build_x64/windows-installer.nsi` generado, `makensis` en PATH, `Get-FileHash`.
- Produces: `dist/obs-stream-metadata-<versión>-windows-x64-<flavor>.exe` + `.sha256`; log del build con sabor/quién/cuándo (exigido para `prod-integrated`).

- [ ] **Step 1: Crear el script** (`tools/package.ps1`, PowerShell 5.1, sin dependencias nuevas):

```powershell
param(
  [ValidateSet('commercial','testing','local','prod-integrated')]
  [string]$Flavor = 'local',
  [string]$BuildDir = 'build_x64',
  [string]$Staging = 'build_x64/staging-pkg',
  [string]$PkgDir = 'build_x64/pkg',
  [string]$DistDir = 'dist',
  [switch]$WhatIf
)
$ErrorActionPreference = 'Stop'
# 1. Validaciones por sabor (nota 001):
#    commercial: falla si $env:OBS_TWITCH_CLIENT_ID existe o dev.env tiene valores productivos.
#    prod-integrated: pide confirmación interactiva (Read-Host 'ESCRIBE SI') y la registra en el log.
# 2. Flags: local = default (sin STREAM_META_BACKEND_URL); resto exige -D explícito o falla.
#    commercial/prod-integrated exigen https no-loopback; testing exige URL test (nunca prod).
# 3. Secuencia: cmake -S . -B $BuildDir --preset windows-x64 [...] ; cmake --build $BuildDir --config RelWithDebInfo ;
#    cmake --install $BuildDir --config RelWithDebInfo --prefix $Staging --component obs-package ;
#    makensis /DPKG_BIN="$Staging\obs-stream-metadata\obs-plugins\64bit" /DPKG_DATA="$Staging\obs-stream-metadata\data\obs-plugins\obs-stream-metadata" /DOUTDIR="$PkgDir" $BuildDir\windows-installer.nsi
# 4. Copia a dist/ con sabor en el nombre + Get-FileHash -Algorithm SHA256 → .sha256.
# 5. Verifica payload: solo .dll + en-US.ini + qschannelbackend.dll en el staging (falla si hay .pdb/.exe dev).
# 6. -WhatIf: imprime los 4 comandos sin ejecutarlos (para CI/docs).
```

- [ ] **Step 2: Dry-run**

Run: `powershell -ExecutionPolicy Bypass -File tools/package.ps1 -Flavor local -WhatIf`
Expected: PASS imprime configure/build/install/makensis sin crear ni modificar ficheros (`git status` sin cambios nuevos salvo el propio script).

- [ ] **Step 3: Regla `prod-integrated`**

Verificar en el script: `if ($Flavor -eq 'prod-integrated') { $c = Read-Host 'Confirma con SI ...'; if ($c -ne 'SI') { exit 2 } ; "flavor=$Flavor user=$env:USERNAME date=$(Get-Date -Format o) reason=manual" | Out-File "$DistDir\build-log.txt" -Append }`.
Expected: sin este bloque el Task no está done (nota 001 lo exige).

- [ ] **Step 4: No commit.**

---

### Task 3: Evidencia `local` + install/uninstall desde `dist/` (puerta de evidencia)

**Files:**
- Modify: ninguno (ejecución + captura para `docs/VALIDATION.md` en Task 4)
- Test: ejecución real del script + instalador real en OBS

**Interfaces:**
- Consumes: `tools/package.ps1` del Task 2.
- Produces: `dist/*-local.exe` + `.sha256` + log de install/uninstall.

- [ ] **Step 1: Empaquetar local**

Run: `powershell -ExecutionPolicy Bypass -File tools/package.ps1 -Flavor local`
Expected: PASS `dist/obs-stream-metadata-0.1.0-windows-x64-local.exe` + `.sha256`; `Get-FileHash` del `.exe` == contenido del `.sha256`.

- [ ] **Step 2: Instalar desde `dist/`** (procedimiento `docs/INSTALLER.md:38-43`)

1. Cerrar OBS por completo. 2. Ejecutar el `.exe` de `dist/` como administrador. 3. Abrir OBS normal (sin env-vars). 4. Verificar en `%APPDATA%\obs-studio\logs` la carga del plugin.
Expected: PASS plugin carga; payload en disco = solo `obs-plugins\64bit\obs-stream-metadata.dll` + `data\obs-plugins\obs-stream-metadata\{locale\tls}`.

- [ ] **Step 3: Desinstalar** (`docs/INSTALLER.md:45-50`)

Run: `uninstall-obs-stream-metadata.exe` con OBS cerrado.
Expected: PASS solo desaparecen DLL + `data\obs-plugins\obs-stream-metadata`; OBS abre sin el plugin.

- [ ] **Step 4: Guardar evidencia** (comando + salida + hash) para pegarla en Task 4. No subir el `.exe` ni el `.sha256` al repo.

---

### Task 4: Cierre documental + gates finales (sin binarios en repo)

**Files:**
- Modify: `docs/VALIDATION.md` (bloque T-067)
- Modify: `docs/BACKLOG.md` (T-067 → `hecha`)
- Test: `git ls-files | Select-String '\.exe$'` vacío + secret-scan del workflow + `git status`

**Interfaces:**
- Consumes: evidencia del Task 3.
- Produces: T-067 `hecha` con aceptación verificada.

- [ ] **Step 1: Bloque en `docs/VALIDATION.md`** (tras el último bloque F-C4):

```text
## T-067 carpeta dist/ + package.ps1 (2026-09-19)

BUILD:     PASS (preset windows-x64, RelWithDebInfo, 0 errores; dll + selfcheck vigentes)
PACKAGE:   PASS (tools/package.ps1 -Flavor local → dist/*-local.exe + .sha256, hash coincide)
INSTALL:   PASS (desde dist/, OBS normal: carga en log; uninstall retira solo payload)
SECRET:    PASS (patrón CI limpio; dist/ ignorada; sin dev.env/productivos en el sabor)
TRACKING:  PASS (git ls-files sin .exe; dist/ con check-ignore)
FINAL: T-067 PASS
```

- [ ] **Step 2: Gates**

Run: `git ls-files | Select-String '\.exe$'` → Expected: vacío. Run: `git check-ignore -v dist/x.exe` → Expected: `dist/`. Run: secret-scan del workflow sobre el árbol trackeado → Expected: limpio.

- [ ] **Step 3: BACKLOG** — T-067 `pendiente` → `en-progreso` al empezar Tasks 1-3, → `hecha` solo con este Task en verde.

- [ ] **Step 4: No commit.** Informe final + PR solo con autorización del usuario (rama `feat/t-067-dist-packaging`, squash, template de PR).

---

## Self-Review

- Cobertura nota 001: sabores (Task 2) ✓; validaciones por sabor incl. confirmación `prod-integrated` (Task 2 Step 3) ✓; nombre con sabor (Tasks 1-2) ✓; sin secretos en instalador (Constraints + Task 3 Step 2) ✓; toca solo flags/docs/CI futuro, no lógica (Constraints) ✓.
- Sin placeholders: cada step lleva contenido/comando/expected exacto; la URL productiva real la aporta el operador al invocar (`https://<cloud-run-url>` es flag, no valor inventado).
- Tipos/paths consistentes: `build_x64/pkg` (salida makensis) → `dist/` (copia final); staging `obs-package` intacto; `managed-link-test`/`selfcheck` excluidos en Constraints y verificados en Task 2 Step 1 (paso 5).
- Riesgo secret-scan: `*.sha256` ignorado pero trackeable por error — el gate `git ls-files` del Task 4 lo detecta; el nombre `OBS_TWITCH_CLIENT_ID` en el script es lectura de entorno (`$env:`), no asignación de valor, luego no matchea el patrón de valores.
