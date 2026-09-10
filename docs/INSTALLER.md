# Installer (T-057) — Windows NSIS, uso normal sin variables de entorno

El uso normal instalado NO requiere `OBS_PLUGINS_PATH` ni
`OBS_PLUGINS_DATA_PATH` (esas variables quedan solo para desarrollo,
ver `docs/TROUBLESHOOTING.md` T-013).

## Construir el instalador (x64)

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$env:Path = "$cmake;$env:Path"
cmake -S . -B build_x64 --preset windows-x64 -DENABLE_FRONTEND_API=ON -DENABLE_QT=ON -DCMAKE_SYSTEM_VERSION=10.0.22621.0
cmake --build build_x64 --config RelWithDebInfo
# Staging con el payload exacto (componente obs-package):
cmake --install build_x64 --config RelWithDebInfo --prefix '<staging>' --component obs-package
# El script NSIS se genera en build_x64/windows-installer.nsi (versión de buildspec.json).
makensis /DPKG_BIN='<staging>\obs-stream-metadata\obs-plugins\64bit' /DPKG_DATA='<staging>\obs-stream-metadata\data\obs-plugins\obs-stream-metadata' /DOUTDIR='<outdir>' build_x64\windows-installer.nsi
```

## Payload

```text
<staging>\obs-stream-metadata\
├── obs-plugins\64bit\obs-stream-metadata.dll
└── data\obs-plugins\obs-stream-metadata\
    ├── locale\en-US.ini
    └── tls\qschannelbackend.dll
```

Destinos (`%ProgramFiles%\obs-studio\`): `obs-plugins\64bit\`,
`data\obs-plugins\obs-stream-metadata\`. Nada más; sin secretos, sin
dev tools, sin `%APPDATA%`.

## Probar instalación

1. Cerrar OBS por completo (el instalador lo exige y aborta si está abierto).
2. Ejecutar `obs-stream-metadata-<versión>-windows-x64.exe` como administrador.
3. Abrir OBS normalmente desde Inicio (sin variables de entorno).
4. Verificar en el log `%APPDATA%\obs-studio\logs`: carga del plugin.

## Probar uninstall

1. Cerrar OBS. Panel de control o `uninstall-obs-stream-metadata.exe`
   en `%ProgramFiles%\obs-studio\`.
2. Verificar que solo desaparecen el DLL y `data\obs-plugins\obs-stream-metadata\`;
   OBS sigue intacto; abrir OBS confirma que el plugin ya no carga.

## ID de tarea

Este instalador se implementó bajo el encargo "T-057 Windows Installer";
como `T-057` ya existía en BACKLOG (contrato Secret Manager, hecha), el
seguimiento vive aquí y en el reporte de la tarea, sin alterar filas
históricas. El plugin no cambia: solo empaquetado + docs.
