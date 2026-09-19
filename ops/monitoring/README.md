# Monitoreo F-C4 — creación por el operador (T-065)

> El agente no tiene credenciales GCP: estos pasos los ejecuta el
> operador con `gcloud` en el proyecto `obs-stream-metadata`.
> Sustituir `OPERADOR-CANAL-ID` / `OPERADOR-CHECK-ID` por los IDs reales
> que devuelva cada comando antes de crear las políticas.

## 1. Canal de notificación (email operador)

```text
gcloud monitoring channels create \
  --display-name="f-c4-operador" \
  --type=email \
  --channel-labels=email_address=horangelmillan@gmail.com \
  --project=obs-stream-metadata
```

Anotar el ID devuelto → `OPERADOR-CANAL-ID` en los 4 JSON.

## 2. Métrica log-based para rechazos YouTube (consume A3)

El backend ya emite la señal (`classify_broadcast_error` →
`request error code=provider_rate_limited detail=google:rate`,
contrato fijado en `backend/tests/test_youtube.py`):

```text
gcloud logging metrics create yt-quota-rejects \
  --description="Rechazos YouTube por rate/quota (detail=google:rate)" \
  --log-filter='resource.type="cloud_run_revision" AND resource.labels.service_name="obs-stream-metadata-service" AND textPayload:"detail=google:rate"' \
  --project=obs-stream-metadata
```

## 3. Políticas de alerta (A1, A2, A3)

```text
gcloud alpha monitoring policies create \
  --policy-from-file=ops/monitoring/alert-5xx.json \
  --project=obs-stream-metadata
gcloud alpha monitoring policies create \
  --policy-from-file=ops/monitoring/alert-p99.json \
  --project=obs-stream-metadata
gcloud alpha monitoring policies create \
  --policy-from-file=ops/monitoring/alert-yt-quota.json \
  --project=obs-stream-metadata
```

Alternativa sin alpha: Monitoring → Alerting → Create Policy y pegar
cada filtro/condición a mano (mismos valores que los JSON).

## 4. Uptime check + A4

1. Monitoring → Uptime checks → Create: protocolo HTTPS, host de
   `PUBLIC_URL`, path `/health`, intervalo 5 min. Anotar `check_id` →
   `OPERADOR-CHECK-ID` en `uptime-health.json`.
2. Crear la política A4 como en §3.

## 5. Cuota YouTube al 80 % (manual, Console)

APIs & Services → YouTube Data API v3 → Quotas → métrica de consultas
diarias (cupo 10 000 u/día, reset medianoche PT) → Create Alert a 80 %.
Pasos exactos dependen de la Console vigente; el operador confirma el
umbral tras 2 semanas de baseline.

## 6. Prueba de alerta (cierra la puerta F-C4)

1. Bajar temporalmente el umbral de A1 (o forzar 4×500 contra una
   revisión de prueba) y comprobar que el incidente se abre.
2. Confirmar recepción en el canal (email) y restaurar el umbral.
3. Registrar captura/log del canal en el informe de cierre.

PENDIENTE a la fecha de este plan: los pasos 1-6 los ejecuta el
operador (requieren proyecto GCP + recepción en su canal). La evidencia
local de señales (logs con `status=429` y `detail=google:rate`) está en
el informe de cierre F-C4.
