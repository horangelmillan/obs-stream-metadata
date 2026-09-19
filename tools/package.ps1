# tools/package.ps1 — T-067: unico punto de entrada para producir el instalador.
#
# Sabores (docs/notes/001-builds-por-comando-segun-destino.md):
#   local            backend local (default CMake, sin flag) — desarrollo
#   testing          ambiente test (-BackendUrl explicita, nunca prod)
#   commercial       produccion (-BackendUrl https://<cloud-run-url>, BYO, nada empaquetado)
#   prod-integrated  productivo puntual: como commercial + confirmacion + rastro en log
#
# Salida: dist/obs-stream-metadata-<version>-windows-x64-<flavor>.exe + .sha256
# (dist/ esta ignorada por git; publicar por GitHub Release, nunca commit).
# Sin secretos: ningun sabor escribe secretos en repo ni instalador.
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File tools/package.ps1 -Flavor local [-WhatIf]
#   powershell -ExecutionPolicy Bypass -File tools/package.ps1 -Flavor commercial -BackendUrl https://<cloud-run-url>

param(
  [ValidateSet('commercial', 'testing', 'local', 'prod-integrated')]
  [string]$Flavor = 'local',
  [string]$BackendUrl = '',
  [string]$BuildDir = 'build_x64',
  [string]$Staging = 'build_x64/staging-pkg',
  [string]$PkgDir = 'build_x64/pkg',
  [string]$DistDir = 'dist',
  [switch]$WhatIf
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
function Join-Root([string]$p) {
  if ([System.IO.Path]::IsPathRooted($p)) { return $p }
  return (Join-Path $RepoRoot $p)
}
$BuildDirAbs = Join-Root $BuildDir
$StagingAbs = Join-Root $Staging
$PkgDirAbs = Join-Root $PkgDir
$DistDirAbs = Join-Root $DistDir

function Fail([string]$msg) {
  Write-Error "package.ps1 [$Flavor]: $msg"
  exit 1
}

function Is-LoopbackUrl([string]$u) {
  return ($u -match '^http://(127\.0\.0\.1|localhost)(:[0-9]+)?(/|$)')
}

function Is-HttpsUrl([string]$u) {
  return ($u -match '^https://[^/]+')
}

# --- 1. Validaciones por sabor (nota 001) ---
if (($Flavor -eq 'commercial') -or ($Flavor -eq 'prod-integrated')) {
  if ($BackendUrl -eq '') {
    Fail 'exige -BackendUrl https://<cloud-run-url> explicita (nunca el default local)'
  }
  if (-not (Is-HttpsUrl $BackendUrl)) {
    Fail '-BackendUrl debe ser https no-loopback para este sabor'
  }
  if ($env:OBS_TWITCH_CLIENT_ID) {
    Fail 'OBS_TWITCH_CLIENT_ID esta definido en el entorno: este sabor no debe heredar IDs de pruebas'
  }
  $devEnv = Join-Path $RepoRoot 'dev.env'
  if (Test-Path -LiteralPath $devEnv) {
    $prodHits = 0
    foreach ($line in (Get-Content -LiteralPath $devEnv)) {
      $t = $line.Trim()
      if (($t -eq '') -or ($t.StartsWith('#'))) { continue }
      if ($t -match '(?i)^\s*ENV\s*=\s*production\s*$') {
        $prodHits++
      }
      elseif ($t -match 'https://') {
        if ($t -notmatch 'https://(127\.0\.0\.1|localhost)(:[0-9]+)?(/|$)') {
          $prodHits++
        }
      }
    }
    if ($prodHits -gt 0) {
      Fail "dev.env contiene $prodHits valor(es) productivo(s): este sabor exige entorno limpio"
    }
  }
}
elseif ($Flavor -eq 'testing') {
  if ($BackendUrl -eq '') {
    Fail 'exige -BackendUrl del ambiente test explicita (nunca prod, nunca el default)'
  }
}
else {
  # local: default sin flag; si se pasa URL, solo loopback.
  if (($BackendUrl -ne '') -and (-not (Is-LoopbackUrl $BackendUrl))) {
    Fail 'local solo admite loopback (http://127.0.0.1 o http://localhost)'
  }
}

# --- 2. Version/nombre desde buildspec.json (el .nsi ya usa la misma) ---
$spec = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot 'buildspec.json') | ConvertFrom-Json
$Version = $spec.version
$PluginName = $spec.name
$BaseExeName = "$PluginName-$Version-windows-x64.exe"
$DistExeName = "$PluginName-$Version-windows-x64-$Flavor.exe"

# --- 3. Los 4 comandos (docs/INSTALLER.md) ---
$configureArgs = @('-S', $RepoRoot, '-B', $BuildDirAbs, '--preset', 'windows-x64',
  '-DENABLE_FRONTEND_API=ON', '-DENABLE_QT=ON', '-DCMAKE_SYSTEM_VERSION=10.0.22621.0')
if ($BackendUrl -ne '') {
  $configureArgs += "-DSTREAM_META_BACKEND_URL=$BackendUrl"
}
$nsiFile = Join-Path $BuildDirAbs 'windows-installer.nsi'
$configureCmd = "cmake $($configureArgs -join ' ')"
$buildCmd = "cmake --build `"$BuildDirAbs`" --config RelWithDebInfo"
$installCmd = "cmake --install `"$BuildDirAbs`" --config RelWithDebInfo --prefix `"$StagingAbs`" --component obs-package"
$makensisCmd = "makensis /DPKG_BIN=`"$StagingAbs\$PluginName\obs-plugins\64bit`" " +
"/DPKG_DATA=`"$StagingAbs\$PluginName\data\obs-plugins\$PluginName`" " +
"/DOUTDIR=`"$PkgDirAbs`" `"$nsiFile`""

Write-Output "package.ps1 flavor=$Flavor version=$Version backend=[$BackendUrl]"

if ($WhatIf) {
  Write-Output '[WhatIf] configure: ' + $configureCmd
  Write-Output '[WhatIf] build:     ' + $buildCmd
  Write-Output '[WhatIf] install:   ' + $installCmd
  Write-Output '[WhatIf] makensis:  ' + $makensisCmd
  Write-Output "[WhatIf] dist:      $DistExeName + .sha256 (sin ejecutar, sin crear ficheros)"
  exit 0
}

# --- 4. Herramientas (auto-descubrimiento: PATH primero, rutas típicas después) ---
function Find-Tool([string]$name, [string[]]$candidates) {
  $cmd = Get-Command $name -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  foreach ($p in $candidates) {
    if (Test-Path -LiteralPath $p) { return $p }
  }
  return $null
}
$cmakeExe = Find-Tool 'cmake' @(
  'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
  'C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
  'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
  'C:\Program Files\CMake\bin\cmake.exe'
)
if (-not $cmakeExe) { Fail 'cmake no encontrado (ni en PATH ni en rutas típicas de VS/CMake)' }
$makensisExe = Find-Tool 'makensis' @(
  'C:\Program Files (x86)\NSIS\makensis.exe',
  'C:\Program Files\NSIS\makensis.exe'
)
if (-not $makensisExe) { Fail 'makensis no encontrado (ni en PATH ni en NSIS típico): instala NSIS 3.x' }

# --- 5. Confirmacion prod-integrated (nota 001: interactiva + rastro) ---
if ($Flavor -eq 'prod-integrated') {
  $c = Read-Host 'prod-integrated empaqueta contra PRODUCCION. Escribe SI para confirmar'
  if ($c -ne 'SI') {
    Fail 'confirmacion no recibida (se esperaba SI)'
  }
}

# --- 6. Secuencia configure -> build -> install -> makensis ---
& $cmakeExe @configureArgs
if ($LASTEXITCODE -ne 0) { Fail "cmake configure fallo (exit $LASTEXITCODE)" }
& $cmakeExe --build $BuildDirAbs --config RelWithDebInfo
if ($LASTEXITCODE -ne 0) { Fail "cmake build fallo (exit $LASTEXITCODE)" }
& $cmakeExe --install $BuildDirAbs --config RelWithDebInfo --prefix $StagingAbs --component obs-package
if ($LASTEXITCODE -ne 0) { Fail "cmake install fallo (exit $LASTEXITCODE)" }

# --- 7. Verifica payload: solo .dll + en-US.ini + qschannelbackend.dll ---
$payload = @(
  "$PluginName\obs-plugins\64bit\$PluginName.dll",
  "$PluginName\data\obs-plugins\$PluginName\locale\en-US.ini",
  "$PluginName\data\obs-plugins\$PluginName\tls\qschannelbackend.dll"
)
foreach ($rel in $payload) {
  if (-not (Test-Path -LiteralPath (Join-Path $StagingAbs $rel))) {
    Fail "payload incompleto en staging: falta $rel"
  }
}
# ponytail: -Include sin comodin en la ruta no filtra en PS 5.1 (devolvia todo);
# Where-Object por extension es el filtro fiable aqui.
$bad = Get-ChildItem -LiteralPath $StagingAbs -Recurse -File -ErrorAction SilentlyContinue |
  Where-Object { ($_.Extension -eq '.exe') -or ($_.Extension -eq '.pdb') }
if ($bad) {
  Fail ('staging con ficheros dev no permitidos: ' + (($bad | ForEach-Object { $_.Name }) -join ', '))
}

& $makensisExe "/DPKG_BIN=$StagingAbs\$PluginName\obs-plugins\64bit" "/DPKG_DATA=$StagingAbs\$PluginName\data\obs-plugins\$PluginName" "/DOUTDIR=$PkgDirAbs" $nsiFile
if ($LASTEXITCODE -ne 0) { Fail "makensis fallo (exit $LASTEXITCODE)" }

# --- 8. Copia a dist/ con sabor en el nombre + SHA256 ---
$srcExe = Join-Path $PkgDirAbs $BaseExeName
if (-not (Test-Path -LiteralPath $srcExe)) {
  Fail "makensis no produjo el esperado: $srcExe"
}
New-Item -ItemType Directory -Path $DistDirAbs -Force | Out-Null
$distExe = Join-Path $DistDirAbs $DistExeName
Copy-Item -LiteralPath $srcExe -Destination $distExe -Force
$hash = (Get-FileHash -LiteralPath $distExe -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $DistExeName" | Out-File -LiteralPath "$distExe.sha256" -Encoding ascii -NoNewline

# --- 9. Verifica hash ---
$check = (Get-FileHash -LiteralPath $distExe -Algorithm SHA256).Hash.ToLowerInvariant()
if ($check -ne $hash) {
  Fail 'hash de verificacion no coincide con el .sha256 generado'
}

if ($Flavor -eq 'prod-integrated') {
  "flavor=$Flavor user=$env:USERNAME date=$(Get-Date -Format o) reason=manual src=$DistExeName" | Out-File -LiteralPath (Join-Path $DistDirAbs 'build-log.txt') -Append -Encoding ascii
}

Write-Output "package.ps1 OK: $DistExeName"
Write-Output "sha256: $hash"
