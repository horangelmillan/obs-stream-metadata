# Monitoreo F-C4 — creación por el operador (T-065)

> Pasos ejecutados de verdad el 2026-09-19 (ver "Estado real" abajo).
> Sustituir `OPERADOR-CANAL-ID` / `OPERADOR-CHECK-ID` por los IDs reales
> que devuelva cada comando antes de crear las políticas.
>
> Notas de campo (Windows + gcloud.ps1): no existe
> `gcloud monitoring channels` en GA y el componente `alpha` no es
> instalable con el Python empaquetado → canal y uptime check van por
> REST; las comillas dentro de `--log-filter` las rompe el wrapper
> `gcloud.ps1` → la métrica también va por REST. Políticas sí van por
> `gcloud monitoring policies create` (GA, verificado).

## 1. Canal de notificación (email operador, REST)

```powershell
$access = (gcloud auth print-access-token --project=obs-stream-metadata | Select-Object -Last 1).Trim()
$body = @{type = 'email'; displayName = 'f-c4-operador'; labels = @{email_address = 'horangelmillan@gmail.com'}} | ConvertTo-Json
Invoke-RestMethod -Method Post -Uri 'https://monitoring.googleapis.com/v3/projects/obs-stream-metadata/notificationChannels' -Headers @{Authorization = ('Bearer ' + $access)} -Body $body -ContentType 'application/json'
```

Anotar el `name` devuelto → `OPERADOR-CANAL-ID` en los 4 JSON.

## 2. Métrica log-based para rechazos YouTube (consume A3, REST)

El backend ya emite la señal (`classify_broadcast_error` →
`request error code=provider_rate_limited detail=google:rate`,
contrato fijado en `backend/tests/test_youtube.py`):

```powershell
$body = @{name = 'yt-quota-rejects'; description = 'Rechazos YouTube por rate/quota (detail=google:rate)'; filter = 'resource.type="cloud_run_revision" AND resource.labels.service_name="obs-stream-metadata-service" AND textPayload:"detail=google:rate"'} | ConvertTo-Json
Invoke-RestMethod -Method Post -Uri 'https://logging.googleapis.com/v2/projects/obs-stream-metadata/metrics' -Headers @{Authorization = ('Bearer ' + $access)} -Body $body -ContentType 'application/json'
```

## 3. Políticas de alerta (A1, A2, A3 — gcloud GA)

```text
gcloud monitoring policies create \
  --policy-from-file=ops/monitoring/alert-5xx.json \
  --project=obs-stream-metadata
gcloud monitoring policies create \
  --policy-from-file=ops/monitoring/alert-p99.json \
  --project=obs-stream-metadata
gcloud monitoring policies create \
  --policy-from-file=ops/monitoring/alert-yt-quota.json \
  --project=obs-stream-metadata
```

Corrección verificada 2026-09-19: A3 exige
`resource.type="cloud_run_revision"` (con `global` la API rechaza la
política: "invalid combination of metric and monitored resource").

## 4. Uptime check + A4 (REST + gcloud GA)

```powershell
$body = @{
    displayName = 'f-c4-health'
    monitoredResource = @{type = 'uptime_url'; labels = @{
        host = 'obs-stream-metadata-service-364043334054.us-east5.run.app'
        project_id = 'obs-stream-metadata' } }
    httpCheck = @{path = '/health'; port = 443; useSsl = $true; validateSsl = $true}
    period = '300s'
    timeout = '10s'
} | ConvertTo-Json -Depth 6
Invoke-RestMethod -Method Post -Uri 'https://monitoring.googleapis.com/v3/projects/obs-stream-metadata/uptimeCheckConfigs' -Headers @{Authorization = ('Bearer ' + $access)} -Body $body -ContentType 'application/json'
```

Anotar el `check_id` (`name` tras la última `/`) →
`OPERADOR-CHECK-ID` en `uptime-health.json`, y crear A4:

```text
gcloud monitoring policies create \
  --policy-from-file=ops/monitoring/uptime-health.json \
  --project=obs-stream-metadata
```

Corrección verificada: A4 exige `resource.type="uptime_url"` en el
filtro además del `check_id`.

## 5. Cuota YouTube al 80 % (manual, Console)

APIs & Services → YouTube Data API v3 → Quotas → métrica de consultas
diarias (cupo 10 000 u/día, reset medianoche PT) → Create Alert a 80 %.
Pasos exactos dependen de la Console vigente; el operador confirma el
umbral tras 2 semanas de baseline.

## 6. Prueba de alerta (cierra la puerta F-C4, verificada 2026-09-19)

Provocar 5xx reales e inofensivos (callback con state vacío: sin
efectos, responde 502) y esperar ~6–8 min a que A1 abra incidente:

```powershell
for ($i=1; $i -le 6; $i++) { try { Invoke-WebRequest -Uri 'https://obs-stream-metadata-service-364043334054.us-east5.run.app/connect/youtube/callback' -UseBasicParsing | Select-Object StatusCode } catch { [int]$_.Exception.Response.StatusCode } }
```

Confirmar recepción en el canal (email del operador). Las líneas
`method=GET path=/connect/youtube/callback status=502` quedan en Cloud
Logging como evidencia de la señal.

## Estado real (2026-09-19, IDs no secretos)

- Canal: `.../notificationChannels/16953027976390805340`
- Métrica: `yt-quota-rejects` · Uptime check: `f-c4-health-zIZIBy7nn70`
- Políticas: A1 `.../alertPolicies/723565970077690701`,
  A2 `.../723565970077689470`, A3 `.../12789121545381253904`,
  A4 `.../14137160385012394081`
- Scheduler: `obs-stream-metadata-purge` (`us-east1`, diario
  `0 3 * * * America/New_York`, deadline 120 s, 3 reintentos)
- Prod: imagen `:master-27288a9` (`sha256:c69bbf8c…`), revisión
  `00026-4wr` al 100 %; `OPS_PURGE_TOKEN` v4 viva (v1–v3 deshabilitadas)
