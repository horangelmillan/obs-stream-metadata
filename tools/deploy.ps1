# tools/deploy.ps1 — flujo de deploy Cloud Run (T-068, cómodo agente/operador).
#
# Un solo comando construye la imagen (Cloud Build), actualiza el servicio
# (conserva env/secrets/SA/Account existentes: solo cambia la imagen) y
# verifica /health + /version + /ready. Sin secretos en el script ni en
# la imagen (configuración por entorno, secretos por Secret Manager).
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File tools/deploy.ps1 [-WhatIf] [-AllowDirty]
#   powershell -ExecutionPolicy Bypass -File tools/deploy.ps1 -Tag rev-t-068 [-AllowDirty]
#
# Rollback: gcloud run services update-traffic <svc> --to-revisions <REV>=100 --region <region>

param(
  [string]$Service = 'obs-stream-metadata-service',
  [string]$Region = 'us-east5',
  [string]$Project = 'obs-stream-metadata',
  [string]$Tag = '',
  [switch]$AllowDirty,
  [switch]$WhatIf
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot

function Fail([string]$msg) {
  Write-Error "deploy.ps1: $msg"
  exit 1
}

# --- 1. Prechequeos ---
if (-not (Get-Command gcloud -ErrorAction SilentlyContinue)) {
  Fail 'gcloud no encontrado en PATH (instala Google Cloud SDK y autentica)'
}
$dirty = git -C $RepoRoot status --porcelain
if ($dirty -and -not $AllowDirty -and -not $WhatIf) {
  Fail 'working tree con cambios: commitea o re-ejecuta con -AllowDirty'
}
$sha = (git -C $RepoRoot rev-parse --short HEAD).Trim()
if ($Tag -eq '') {
  $Tag = "master-$sha"
  if ($dirty) { $Tag += '-dirty' }
}
$Image = "us-east5-docker.pkg.dev/$Project/$Project/$Service`:$Tag"

# Cloud Build no acepta Dockerfile con otro nombre por flag: se genera un
# cloudbuild.yaml temporal (fuera del repo) que usa Dockerfile.backend.
$cbYaml = Join-Path ([System.IO.Path]::GetTempPath()) 'obsmeta-cloudbuild.yaml'
$cbContent = @"
steps:
- name: gcr.io/cloud-builders/docker
  args: ['build', '-f', 'Dockerfile.backend', '-t', '$Image', '.']
images:
- '$Image'
"@
$buildCmd = "gcloud builds submit --project=$Project --config=`"$cbYaml`" --timeout=20m `"$RepoRoot`""
$deployCmd = "gcloud run deploy $Service --project=$Project --image=$Image --region=$Region --quiet"
$base = "https://$Service-364043334054.$Region.run.app"

Write-Output "deploy.ps1 service=$Service region=$Region image=$Image"

if ($WhatIf) {
  Write-Output '[WhatIf] build:  ' + $buildCmd
  Write-Output '[WhatIf] deploy: ' + $deployCmd
  Write-Output "[WhatIf] verify: $base/health + /version + /ready (sin ejecutar)"
  exit 0
}

# --- 2. Build ---
$cbContent | Out-File -LiteralPath $cbYaml -Encoding ascii
Write-Output 'deploy.ps1: Cloud Build...'
& gcloud builds submit --project=$Project --config=$cbYaml --timeout=20m $RepoRoot
if ($LASTEXITCODE -ne 0) { Fail "Cloud Build fallo (exit $LASTEXITCODE)" }

# --- 3. Deploy (solo imagen; env/secrets/SA se conservan) ---
Write-Output 'deploy.ps1: Cloud Run deploy...'
& gcloud run deploy $Service --project=$Project --image=$Image --region=$Region --quiet
if ($LASTEXITCODE -ne 0) { Fail "run deploy fallo (exit $LASTEXITCODE)" }

# --- 4. Verificación ---
Start-Sleep -Seconds 15
foreach ($p in @('/health', '/version', '/ready')) {
  $out = curl.exe -sS -m 25 "$base$p"
  if ($LASTEXITCODE -ne 0) { Fail "verify $p sin respuesta" }
  Write-Output "verify ${p}: $out"
}
$rev = (gcloud run services describe $Service --project=$Project --region=$Region --format="value(spec.template.metadata.name)")
Write-Output "deploy.ps1 OK: revision=$rev image=$Tag"
