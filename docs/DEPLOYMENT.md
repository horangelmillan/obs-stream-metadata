# DEPLOYMENT — contrato de despliegue del backend (T-054, ADR-013)

Este documento es el contrato entre el código y el operador. El código no
adivina infraestructura: todo lo externo se declara aquí y falla rápido
si falta. Sin valores reales (los aporta el operador).

## Topología

```text
Internet (HTTPS 443)
  → [TLS: builtin del backend o terminación en reverse-proxy]
  → python -m backend.app (127.0.0.1 o red privada)
  → meta.db (SQLite 0600) + secret_dir (ficheros 0600)
  → providers OAuth (HTTPS saliente)
```

Recomendado: reverse-proxy delante (termina TLS público, reenvía a
`127.0.0.1:PUERTO` local) o TLS builtin con certificado del operador.
Nunca exponer el puerto Python directamente sin TLS.

## Variables requeridas en producción (`env=production`)

| Variable | Ejemplo (no real) | Notas |
|---|---|---|
| `STREAM_META_BACKEND_ENV` | `production` | Cualquier otro valor = fail-fast |
| `STREAM_META_BACKEND_PUBLIC_URL` | `https://api.example.com` | HTTPS obligatorio (gate T-053); de aquí derivan los redirects `/connect/*/callback`, que deben coincidir con los registrados en las OAuth apps |
| `STREAM_META_BACKEND_PROVIDERS` | `youtube,kick` | Los que el despliegue habilite |
| `STREAM_META_BACKEND_DATA_DIR` | `/var/lib/stream-meta` | Contendrá `meta.db` (creado 0600) |
| `STREAM_META_BACKEND_SECRET_DIR` | `/run/stream-meta/secrets` | Un fichero por secreto (ver abajo) |
| `STREAM_META_BACKEND_PORT`/`HOST` | `8080`/`127.0.0.1` | Detrás de proxy: loopback |
| `STREAM_META_BACKEND_TLS_CERTFILE/KEYFILE` | `/etc/ssl/...` | Solo para TLS builtin; omitir si termina el proxy |
| `STREAM_META_BACKEND_GLOBAL_LIMIT/WINDOW_S` | `600`/`60` | Puerta global (requerido implícito: AllowAll rechazado en prod) |

## Secretos (un fichero por nombre, contenido = valor, sin newline final exigido)

```text
$SECRET_DIR/GOOGLE_CLIENT_ID
$SECRET_DIR/GOOGLE_CLIENT_SECRET
$SECRET_DIR/KICK_CLIENT_ID
$SECRET_DIR/KICK_CLIENT_SECRET
```

Permisos 0600, propietario el usuario del servicio. Jamás en repo, env
visible, imágenes o logs (gate CI + leak-guard + redacción).

## OAuth apps PROD (operador, fuera del repo)

- Proyecto Google PROD + app Kick PROD separados de los DEV.
- Redirects registrados = `$PUBLIC_URL/connect/youtube/callback` y
  `$PUBLIC_URL/connect/kick/callback` (nota Kick: `localhost` solo vale
  en dev; en prod el host público que exija la app).
- Proyectos Google test/prod separados (heredado T-038 §10).

## Arranque / salud / parada

- Arranque: `python -m backend.app`; cualquier configuración insegura
  aborta antes de escuchar (stores dev, HTTP público, dirs ausentes,
  TLS a medias). `meta.db` se crea con schema + WAL automáticamente.
- Salud: `/health` (proceso), `/ready` (aplicación), `/version` (`env`
  incluido para verificar el entorno al que se habla).
- Parada: SIGINT/SIGTERM; SQLite es crash-safe (WAL); backup = copiar
  `meta.db` en caliente con `sqlite3 .backup` o copia con servicio parado.
- Logs: solo longitudes/códigos (redacción activa); sin secretos.

## Checklist operador (antes de declarar prod UP)

- [ ] `env=production` efectivo (`/version` → `"env": "production"`)
- [ ] HTTPS válido extremo a extremo (cert + cadena + hostname)
- [ ] Secrets solo en `$SECRET_DIR` 0600 (auditar con el patrón CI)
- [ ] `meta.db` 0600 y en disco con cifrado gestionado
- [ ] OAuth apps PROD con redirects exactos (probar connect real)
- [ ] `/ready` 200, rate-limit 429 ante abuso (no loop agresivo)
- [ ] Disconnect borra + revoca (semántica heredada T-045/46)

## Dependencias externas (no incluidas en el repo)

Host, dominio+DNS, certificado, disco con cifrado gestionado, OAuth apps
PROD y sus credenciales, proxy/monitoreo/backups gestionados. T-055
(licencias/pagos) y observabilidad productiva quedan fuera de T-054.
