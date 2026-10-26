# MediaMTX en el instalador (ADR-016, Fase 5)

El instalador incluye el relay local MediaMTX como binario + config mínima
+ aviso MIT. Este archivo documenta origen, checksum y verificación.
Dueño del script: `release-tooling` (`tools/**`).

## Origen [D]

- Upstream: https://github.com/bluenviron/mediamtx (MIT; aviso obligatorio
  si se empaqueta — F-096–F-099, ADR-016 §5).
- Release pinneado: **v1.21.3** (versión validada en lab T-078 [D];
  `lab/mediamtx-lab/fetch.py` + `STATE.md`).
- Asset Windows x64:
  `https://github.com/bluenviron/mediamtx/releases/download/v1.21.3/mediamtx_v1.21.3_windows_amd64.zip`
- Checksums del release:
  `https://github.com/bluenviron/mediamtx/releases/download/v1.21.3/checksums.sha256`

## Checksum esperado [D]

- zip `mediamtx_v1.21.3_windows_amd64.zip`:
  `b2f5246b07cb72961df03b8d5e8cad43dc657bcf3f49a87bb031bdf22721af74`
- Fuente del pin: `lab/mediamtx-lab/verify.py` (línea `expected:`) [D].
- El pin vive en `tools/package.ps1` (`$MediaMTXPins`); el script falla si
  `-MediaMTXVersion` no tiene pin.

## Cómo se verifica (automático en `tools/package.ps1`)

1. Descarga el zip + `checksums.sha256` a `<build>/mediamtx-dl/`
   (dir ignorado por git; si el zip ya está y el hash coincide, no
   re-descarga — amable sin red).
2. `Get-FileHash SHA256` del zip contra el pin → Fail si difiere.
3. La línea del asset dentro del `checksums.sha256` del release debe
   contener el mismo pin → Fail si no (detecta pins locales
   desactualizados o releases manipulados).
4. `Expand-Archive` y exige `mediamtx.exe` dentro → Fail si falta.
5. Staging: `<staging>/.../relay/{mediamtx.exe, mediamtx.yml,
   NOTICE-MediaMTX.txt}` verificado en el paso 7 del script.
6. Trazabilidad: junto al `.exe` de dist se escribe
   `<instalador>.relay-provenance.txt` (versión + URL + sha256).

Verificación manual (sin build):

```powershell
$zip = 'build_x64\mediamtx-dl\mediamtx_v1.21.3_windows_amd64.zip'
(Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
# esperado: b2f5246b07cb72961df03b8d5e8cad43dc657bcf3f49a87bb031bdf22721af74
Select-String 'mediamtx_v1.21.3_windows_amd64.zip' 'build_x64\mediamtx-dl\checksums-v1.21.3.sha256'
# o el lab: python lab/mediamtx-lab/verify.py
```

## Qué instala el NSI y dónde

- `tools/mediamtx.yml`: config mínima de producto (puertos 1935/9997,
  path `live`, lo demás apagado; idéntica a la de lab T-078 salvo
  cabecera). Instalada en `data\obs-plugins\obs-stream-metadata\relay\`.
- `tools/NOTICE-MediaMTX.txt`: atribución + texto íntegro MIT
  (`Copyright (c) 2019 aler9`; texto obtenido del LICENSE del tag
  v1.21.3 el 2026-10-26 [C]). Instalado junto al relay + aviso
  `MessageBox` en `.onInit` del instalador.
- El desinstalador no necesita cambios: el relay vive bajo
  `data\obs-plugins\<plugin>\`, que el uninstall ya elimina con `RMDir /r`.
- `cmake/windows-installer.nsi.in` **no se modifica** desde `tools/**`
  (otro ámbito): `package.ps1` aumenta la copia generada en build dir
  (`windows-installer.with-relay.nsi`: sección `SecRelay` vía
  `File ${PKG_RELAY}\...` + aviso MIT). Si el template cambia los
  marcadores (`"Publisher" "horangelmillan"`, `Function .onInit`,
  `SectionEnd`), el script falla con mensaje explícito [P de
  mantenimiento, ver Limitaciones].

## Sabores

Relay incluido por defecto en `local`, `testing`, `commercial` y
`prod-integrated`. `-NoRelay` lo omite (staging + NSI quedan como antes
de la Fase 5). Sin secretos en ningún sabor: solo binario + config
públicos.

## Rotar versión de MediaMTX

1. Descargar el asset + `checksums.sha256` del nuevo release oficial.
2. Verificar el hash por dos vías (lab `verify.py`-style + release file).
3. Añadir el pin a `$MediaMTXPins` en `tools/package.ps1` y actualizar
   el default `-MediaMTXVersion`, `tools/NOTICE-MediaMTX.txt` y este doc.
4. Probar `-WhatIf` + build local antes de cualquier commit.

## Limitaciones / pendientes [P]

- [P] Build E2E con descarga real + `makensis` aún no ejecutado en esta
  sesión (ver Informe): script listo, verificación operativa pendiente.
- [P] E2E operador: instalar, comprobar relay operativo + aviso MIT
  visible (criterio ADR-016 §6.1).
- [P] Puertos 1935/9997 ocupados, firewall Windows y ciclo de vida del
  proceso hijo: comportamiento + mensajes (ADR-016 §6.8; el yml y el
  instalador no reservan puertos ni crean reglas de firewall).
- [P] Si el template NSI cambia sus marcadores, la inyección falla a
  propósito (Fail explícito) hasta adaptar `package.ps1` §7b.
- Sin secretos: este flujo no toca credenciales, tokens ni backend.
