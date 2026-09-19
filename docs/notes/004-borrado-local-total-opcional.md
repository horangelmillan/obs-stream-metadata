# 004 — Borrado local total opcional al desinstalar

## Contexto

F-C2, 2026-09-19: el uninstaller solo retira el payload (restricción
vigente: nunca toca `%APPDATA%`). Idea del operador: opción de dejar el
computador limpio de datos residuales de la app.

## Problema observado

No hay vía de borrar los datos **locales** (fichero DPAPI
`accounts.json`: credenciales Independent, `backendInstall`, snapshots
Managed, modo). El borrado backend (`POST /privacy/erase`) no los toca
a propósito.

## Propuesta

- **Opt-in explícito, desactivado por defecto:** checkbox en el
  desinstalador + botón equivalente en el dock ("Borrar datos locales").
- El borrado local nunca sustituye al borrado backend (erase primero;
  el uninstaller ya lo recuerda).
- Confirmación con la ruta exacta del fichero; sin globs amplios ni
  borrados silenciosos. Los logs del plugin viven en el log de OBS
  (de OBS: **no tocar jamás**).

## Conflicto vigente (requiere ADR antes de implementar)

La restricción actual lo prohíbe (`docs/PRIVACY.md` §10). Esta nota es
propuesta, no decisión.

## Abierto

- ¿Checkbox en NSIS (página custom) o solo botón en el dock?
- ¿Rechazar el borrado local si hay sesiones Managed vivas sin erase
  previo? (Evita "limpio en disco, sucio en backend".)

## Estado

`idea`
