# tools/package.ps1 — T-067: unico punto de entrada para producir el instalador.
#
# Sabores (docs/notes/001-builds-por-comando-segun-destino.md):
#   local            backend local (loopback explícito = default CMake) — desarrollo
#   testing          ambiente test (-BackendUrl explicita, nunca prod)
#   commercial       produccion (-BackendUrl https://<cloud-run-url>, BYO, nada empaquetado)
#   prod-integrated  productivo puntual: como commercial + confirmacion + rastro en log
#
# Relay local MediaMTX (ADR-016, Fase 5): incluido por defecto en TODOS los
# sabores (local/testing/commercial/prod-integrated; -NoRelay lo omite).
# Origen: descarga oficial verificada con SHA256 en tiempo de build
# (ver tools/MEDIAMTX.md); el binario NUNCA se versiona (.gitignore).
# Staging: <staging>/.../relay/{mediamtx.exe, mediamtx.yml, NOTICE-MediaMTX.txt}.
# El NSI generado se aumenta en build (seccion SecRelay + aviso MIT en
# .onInit); cmake/windows-installer.nsi.in no se toca desde este script.
#
# Salida: dist/obs-stream-metadata-<version>-windows-x64-<flavor>.exe + .sha256
# (dist/ esta ignorada por git; publicar por GitHub Release, nunca commit).
# Sin secretos: ningun sabor escribe secretos en repo ni instalador.
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File tools/package.ps1 -Flavor local [-WhatIf]
#   powershell -ExecutionPolicy Bypass -File tools/package.ps1 -Flavor commercial -BackendUrl https://<cloud-run-url>
#   powershell -ExecutionPolicy Bypass -File tools/package.ps1 -Flavor local -NoRelay [-WhatIf]

param(
  [ValidateSet('commercial', 'testing', 'local', 'prod-integrated')]
  [string]$Flavor = 'local',
  [string]$BackendUrl = '',
  [string]$BuildDir = 'build_x64',
  [string]$Staging = 'build_x64/staging-pkg',
  [string]$PkgDir = 'build_x64/pkg',
  [string]$DistDir = 'dist',
  [string]$MediaMTXVersion = 'v1.21.3',
  [switch]$NoRelay,
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
  # local: omitir el flag heredaría la caché del sabor anterior
  # (contaminación nota 001: un local tras commercial llevaría prod) →
  # forzar loopback explícito; si se pasa URL, solo loopback.
  if ($BackendUrl -eq '') {
    $BackendUrl = 'http://127.0.0.1:8080'
  }
  elseif (-not (Is-LoopbackUrl $BackendUrl)) {
    Fail 'local solo admite loopback (http://127.0.0.1 o http://localhost)'
  }
}

# --- 2. Version/nombre desde buildspec.json (el .nsi ya usa la misma) ---
$spec = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot 'buildspec.json') | ConvertFrom-Json
$Version = $spec.version
$PluginName = $spec.name
$BaseExeName = "$PluginName-$Version-windows-x64.exe"
$DistExeName = "$PluginName-$Version-windows-x64-$Flavor.exe"

# --- 2b. Relay local MediaMTX (ADR-016): version pinneada + checksum esperado ---
# Origen oficial: https://github.com/bluenviron/mediamtx/releases/download/<ver>/
# El binario NUNCA se versiona (.gitignore: mediamtx.exe, third_party/mediamtx/,
# vendor/mediamtx/); se descarga en tiempo de build y se verifica por SHA256
# contra el pin local + el checksums.sha256 del release. Detalle y
# procedimiento de rotacion de version en tools/MEDIAMTX.md.
$RelayEnabled = -not $NoRelay
$MediaMTXPins = @{
  'v1.21.3' = 'b2f5246b07cb72961df03b8d5e8cad43dc657bcf3f49a87bb031bdf22721af74'
}
if ($RelayEnabled) {
  if ($MediaMTXVersion -notmatch '^v\d+\.\d+\.\d+$') { Fail "-MediaMTXVersion debe ser vX.Y.Z (p. ej. v1.21.3)" }
  $MediaMTXExpected = $MediaMTXPins[$MediaMTXVersion]
  if (-not $MediaMTXExpected) { Fail "MediaMTX $MediaMTXVersion sin checksum pinneado: anade el SHA256 verificado del release oficial a MediaMTXPins (ver tools/MEDIAMTX.md)" }
  $MediaMTXAsset = "mediamtx_${MediaMTXVersion}_windows_amd64.zip"
  $MediaMTXBase = "https://github.com/bluenviron/mediamtx/releases/download/$MediaMTXVersion"
  $MediaMTXZipUrl = "$MediaMTXBase/$MediaMTXAsset"
  $MediaMTXSumUrl = "$MediaMTXBase/checksums.sha256"
  $MediaMTXDlDir = Join-Path $BuildDirAbs 'mediamtx-dl'
  $RelayStageDir = Join-Path $StagingAbs "$PluginName\data\obs-plugins\$PluginName\relay"
}

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
  if ($RelayEnabled) {
    Write-Output "[WhatIf] relay:      MediaMTX $MediaMTXVersion ($MediaMTXAsset)"
    Write-Output "[WhatIf] relay from: $MediaMTXZipUrl"
    Write-Output "[WhatIf] relay sha256 (pinned): $MediaMTXExpected"
    Write-Output "[WhatIf] relay stage: $RelayStageDir (mediamtx.exe + mediamtx.yml + NOTICE-MediaMTX.txt)"
    Write-Output '[WhatIf] relay nsi:   generated windows-installer.nsi + injected SecRelay (PKG_RELAY) + MIT notice in .onInit'
  } else {
    Write-Output '[WhatIf] relay:      omitted (-NoRelay)'
  }
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

# --- 6b. Relay MediaMTX: descarga verificada + staging (ADR-016, tools/MEDIAMTX.md) ---
# Cache en build dir (ignorado por git); si el zip ya esta y el hash coincide,
# no se descarga (amable sin red). Sin secretos: solo binario+config publicos.
if ($RelayEnabled) {
  New-Item -ItemType Directory -Path $MediaMTXDlDir -Force | Out-Null
  $zipPath = Join-Path $MediaMTXDlDir $MediaMTXAsset
  $sumPath = Join-Path $MediaMTXDlDir "checksums-$MediaMTXVersion.sha256"
  $needDl = $true
  if (Test-Path -LiteralPath $zipPath) {
    $cached = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($cached -eq $MediaMTXExpected) {
      $needDl = $false
      Write-Output "package.ps1 relay: cache OK ($MediaMTXAsset, sha256 coincide, sin descarga)"
    } else {
      Write-Output 'package.ps1 relay: cache con hash distinto, re-descargando'
    }
  }
  if ($needDl) {
    Invoke-WebRequest -Uri $MediaMTXZipUrl -OutFile $zipPath -UseBasicParsing
    Invoke-WebRequest -Uri $MediaMTXSumUrl -OutFile $sumPath -UseBasicParsing
  }
  $actual = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($actual -ne $MediaMTXExpected) { Fail "MediaMTX zip SHA256 no coincide: esperado $MediaMTXExpected, obtenido $actual" }
  if (Test-Path -LiteralPath $sumPath) {
    $sumLine = @(Get-Content -LiteralPath $sumPath) | Where-Object { $_ -match [regex]::Escape($MediaMTXAsset) }
    if (-not $sumLine) { Fail "checksums.sha256 del release no lista $MediaMTXAsset" }
    if ($sumLine -notmatch [regex]::Escape($MediaMTXExpected)) { Fail 'checksums.sha256 del release no coincide con el pin local: revisar tools/MEDIAMTX.md antes de continuar' }
  }
  $extDir = Join-Path $MediaMTXDlDir 'extract'
  if (Test-Path -LiteralPath $extDir) { Remove-Item -LiteralPath $extDir -Recurse -Force }
  New-Item -ItemType Directory -Path $extDir -Force | Out-Null
  Expand-Archive -LiteralPath $zipPath -DestinationPath $extDir -Force
  $exe = Get-ChildItem -LiteralPath $extDir -Recurse -File -Filter 'mediamtx.exe' | Select-Object -First 1
  if (-not $exe) { Fail 'zip verificado pero sin mediamtx.exe dentro' }
  New-Item -ItemType Directory -Path $RelayStageDir -Force | Out-Null
  Copy-Item -LiteralPath $exe.FullName -Destination (Join-Path $RelayStageDir 'mediamtx.exe') -Force
  Copy-Item -LiteralPath (Join-Path (Join-Path $RepoRoot 'tools') 'mediamtx.yml') -Destination (Join-Path $RelayStageDir 'mediamtx.yml') -Force
  Copy-Item -LiteralPath (Join-Path (Join-Path $RepoRoot 'tools') 'NOTICE-MediaMTX.txt') -Destination (Join-Path $RelayStageDir 'NOTICE-MediaMTX.txt') -Force
  Write-Output "package.ps1 relay OK: $MediaMTXAsset (sha256 verificado)"
}

# --- 7. Verifica payload: solo .dll + en-US.ini + qschannelbackend.dll [+ relay] ---
$payload = @(
  "$PluginName\obs-plugins\64bit\$PluginName.dll",
  "$PluginName\data\obs-plugins\$PluginName\locale\en-US.ini",
  "$PluginName\data\obs-plugins\$PluginName\tls\qschannelbackend.dll"
)
if ($RelayEnabled) {
  $payload += @(
    "$PluginName\data\obs-plugins\$PluginName\relay\mediamtx.exe",
    "$PluginName\data\obs-plugins\$PluginName\relay\mediamtx.yml",
    "$PluginName\data\obs-plugins\$PluginName\relay\NOTICE-MediaMTX.txt"
  )
}
foreach ($rel in $payload) {
  if (-not (Test-Path -LiteralPath (Join-Path $StagingAbs $rel))) {
    Fail "payload incompleto en staging: falta $rel"
  }
}
# ponytail: -Include sin comodin en la ruta no filtra en PS 5.1 (devolvia todo);
# Where-Object por extension es el filtro fiable aqui.
# Excepcion: relay\mediamtx.exe (binario de producto verificado, ADR-016).
$bad = Get-ChildItem -LiteralPath $StagingAbs -Recurse -File -ErrorAction SilentlyContinue |
  Where-Object { (($_.Extension -eq '.exe') -and ($_.FullName -notlike '*\relay\mediamtx.exe')) -or ($_.Extension -eq '.pdb') }
if ($bad) {
  Fail ('staging con ficheros dev no permitidos: ' + (($bad | ForEach-Object { $_.Name }) -join ', '))
}

# --- 7b. NSI con relay: copia del generado + seccion inyectada (sin tocar cmake/) ---
# El template cmake/windows-installer.nsi.in no conoce el relay (lo posee otro
# ambito); este script aumenta la copia GENERADA en build dir: seccion SecRelay
# (File desde PKG_RELAY) + aviso MIT en .onInit. El desinstalador no necesita
# cambios: el relay vive bajo data\obs-plugins\<plugin>\ (ya hace RMDir /r).
$nsiToBuild = $nsiFile
$relayMakensisArgs = @()
if ($RelayEnabled) {
  if (-not (Test-Path -LiteralPath $nsiFile)) { Fail "no existe el NSI generado: $nsiFile (cmake configure debio crearlo)" }
  $nsiText = Get-Content -Raw -LiteralPath $nsiFile
  $relaySection = @'
; --- local relay MediaMTX (ADR-016; staged+verificado por tools/package.ps1) ---
Section "Local relay MediaMTX (for fixed local URL)" SecRelay
  SectionIn RO
  SetOutPath "$INSTDIR\data\obs-plugins\@@PLUGIN@@\relay"
  File "${PKG_RELAY}\mediamtx.exe"
  File "${PKG_RELAY}\mediamtx.yml"
  File "${PKG_RELAY}\NOTICE-MediaMTX.txt"
SectionEnd
'@.Replace('@@PLUGIN@@', $PluginName)
  $pubIdx = $nsiText.IndexOf('"Publisher" "horangelmillan"')
  if ($pubIdx -lt 0) { Fail 'NSI generado sin marcador Publisher: no se puede inyectar SecRelay' }
  $secEnd = $nsiText.IndexOf('SectionEnd', $pubIdx)
  if ($secEnd -lt 0) { Fail 'NSI generado sin SectionEnd tras Publisher: no se puede inyectar SecRelay' }
  $insertAt = $secEnd + 'SectionEnd'.Length
  $nsiText = $nsiText.Substring(0, $insertAt) + "`r`n`r`n" + $relaySection + $nsiText.Substring($insertAt)
  $mitNotice = '  MessageBox MB_OK|MB_ICONINFORMATION "This installer bundles the local relay MediaMTX (MIT License, bluenviron) under data\\obs-plugins\\' + $PluginName + '\\relay, with NOTICE-MediaMTX.txt (attribution + license text). The relay provides the fixed local URL rtmp://127.0.0.1:1935/live."'
  $fnIdx = $nsiText.IndexOf('Function .onInit')
  if ($fnIdx -lt 0) { Fail 'NSI generado sin Function .onInit: no se puede inyectar el aviso MIT' }
  $fnEnd = $nsiText.IndexOf('FunctionEnd', $fnIdx)
  if ($fnEnd -lt 0) { Fail 'NSI generado sin FunctionEnd de .onInit: no se puede inyectar el aviso MIT' }
  $nsiText = $nsiText.Substring(0, $fnEnd) + $mitNotice + "`r`n" + $nsiText.Substring($fnEnd)
  $nsiToBuild = Join-Path $PkgDirAbs 'windows-installer.with-relay.nsi'
  New-Item -ItemType Directory -Path $PkgDirAbs -Force | Out-Null
  $nsiText | Out-File -LiteralPath $nsiToBuild -Encoding ascii -NoNewline
  $relayMakensisArgs = @("/DPKG_RELAY=$RelayStageDir")
  Write-Output "package.ps1 relay NSI: SecRelay + aviso MIT inyectados en $nsiToBuild"
}

New-Item -ItemType Directory -Path $PkgDirAbs -Force | Out-Null
& $makensisExe "/DPKG_BIN=$StagingAbs\$PluginName\obs-plugins\64bit" "/DPKG_DATA=$StagingAbs\$PluginName\data\obs-plugins\$PluginName" "/DOUTDIR=$PkgDirAbs" @relayMakensisArgs $nsiToBuild
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
if ($RelayEnabled) {
  "mediamtx=$MediaMTXVersion url=$MediaMTXZipUrl sha256=$MediaMTXExpected" | Out-File -LiteralPath "$distExe.relay-provenance.txt" -Encoding ascii -NoNewline
}

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
