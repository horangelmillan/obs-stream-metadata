# 002 — Lanzador local con dev.env

- Estado: idea
- Contexto: hoy arrancar OBS en local exige abrir PowerShell, exportar
  5+ variables (`STREAM_META_*`) una por una y lanzar `obs64.exe` con el
  `WorkingDirectory` correcto. Lento y propenso a olvidar variables.

## Propuesta

Un script (p. ej. `tools/launch-obs-dev.ps1`) que:

1. Lea `dev.env` del repo (ignorado por git, solo local).
2. Exporte cada `CLAVE=valor` a la sesión.
3. Fije `OBS_PLUGINS_PATH` / `OBS_PLUGINS_DATA_PATH` al staging de
   desarrollo.
4. Lance `obs64.exe` con el `WorkingDirectory` correcto.
5. Valide al arrancar que las variables requeridas existen y avise de
   las que falten (sin imprimir valores).

Comportamiento:

- Si `dev.env` no existe, arranca igual pero avisa (modo sin
  credenciales precargadas; el dock las pide a mano).
- Nunca imprime valores, solo nombres de variables presentes/ausentes.
- Solo desarrollo: el instalador comercial no incluye ni usa este
  script ni `dev.env`.

## Relación

- Toca: nuevo `tools/launch-obs-dev.ps1` + mención en `docs/INSTALLER.md`.
- No toca: plugin, backend, instalador, CI.
