# Spec 3 — Servicios del sistema que faltan

Fecha: 2026-10-14. Estado: **borrador para aprobación**. Roadmap: `docs/tsuki/ROADMAP.md`, sección "Spec 3".

Marcas: **[V]** comprobado en el repo o en los logs; **[H]** hipótesis a confirmar con el sitio de
llamada del juego.

## 1. Objetivo

Que las 23 APIs de plataforma que el juego importa y el runtime no resuelve tengan un comportamiento
**explícito, documentado y registrado en el log**, sin fingir que existen funciones que esta consola
offline no tiene (PSN, capturas, Share Play, streaming).

## 2. Hechos comprobados

- **[V]** Los NID de la tabla se calcularon con `nid()` de `scripts/prepare.py` y coinciden con las
  entradas de `out/check-imports.log`.
- **[V]** **Tres de las 23 ya tienen código** y solo les falta su línea en `src/import_names.inc`
  (`runtime_lookup`, `src/runtime.c:269`, no las encuentra sin ella): `sceSaveDataInitialize3`
  (`runtime_savedata.c:336`, usa `save_initialize`), `sceSaveDataMount2` (`:337`, `save_mount2`),
  `sceMsgDialogGetResult` (`runtime_services.c:646`, `msg_result`). Con esto el trabajo real baja a
  20 APIs.
- **[V]** `sceSysmoduleLoadModule`, `...IsLoaded` y `...UnloadModule` ya existen en
  `src/runtime_content.c:81-83` (`module_load`, `module_loaded`, `module_unload`).
- **[V]** Constantes de error de SaveData ya definidas en `runtime_savedata.c:22-29`:
  `PARAMETER 0x809F0000`, `NOT_INITIALIZED 0x809F0001`, `NOT_MOUNTED 0x809F0004`, `EXISTS 0x809F0007`,
  `NOT_FOUND 0x809F0008`, `INTERNAL 0x809F000B`. Error de parámetro de SystemService:
  `SYSTEM_PARAMETER 0x80A10003` (`runtime_services.c:27`).
- **[V]** `sceSystemServiceParamGetInt` (`runtime_services.c:75`): id 1 = idioma, 2 = formato de fecha,
  3 = reloj de 24 horas, 4 = huso horario; `GOW3_LANGUAGE` cambia el idioma (por defecto 1 = inglés).
- **[V]** El runtime ya tiene 39 entradas "sin efecto" del tipo `{nombre, ok_void}`.
- **[V]** Existe `tests/test_services.c` (87 líneas, se compila sin el resto del runtime con
  `-I. -Isrc`, `build.sh:111`).

## 3. Tabla de APIs

| NID | Nombre | Biblioteca | Estado | Comportamiento |
|---|---|---|---|---|
| `TywrFKCoLGY` | `sceSaveDataInitialize3` | SaveData | código existe | solo `import_names.inc` |
| `0z45PIH+SNI` | `sceSaveDataMount2` | SaveData | código existe | solo `import_names.inc` |
| `VwadwBBBJ80` | `sceSaveDataUmountWithBackup` | SaveData | falta | igual que `sceSaveDataUmount` (`save_umount`); no crea copia |
| `RQOqDbk3bSU` | `sceSaveDataCheckBackupData` | SaveData | falta | `NOT_FOUND` (0x809F0008): no hay copia de seguridad |
| `lU9YRFsgwSU` | `sceSaveDataRestoreBackupData` | SaveData | falta | `NOT_FOUND` |
| `2xxUtuC-RzE` | `sceScreenShotEnable` | ScreenShot | falta | 0, sin efecto |
| `tIYf0W5VTi8` | `sceScreenShotDisable` | ScreenShot | falta | 0, sin efecto |
| `73WQ4Jj0nJI` | `sceScreenShotSetOverlayImageWithOrigin` | ScreenShot | falta | 0, sin efecto |
| `isruqthpYcw` | `sceSharePlayInitialize` | SharePlay | falta | 0, sin efecto |
| `UaLjloJinow` | `sceSharePlayTerminate` | SharePlay | falta | 0, sin efecto |
| `co2NCj--pnc` | `sceSharePlaySetProhibition` | SharePlay | falta | 0, sin efecto |
| `kvYEw2lBndk` | `sceGameLiveStreamingInitialize` | GameLiveStreaming | falta | 0, sin efecto |
| `9yK6Fk8mKOQ` | `sceGameLiveStreamingTerminate` | GameLiveStreaming | falta | 0, sin efecto |
| `dWM80AX39o4` | `sceGameLiveStreamingEnableLiveStreaming` | GameLiveStreaming | falta | 0 y retransmisión "no activa" **[H]** |
| `Fc8qxlKINYQ` | `sceVideoRecordingSetInfo` | VideoRecording | falta | 0, sin efecto |
| `hHrGoGoNf+s` | `sceSysmoduleLoadModuleInternalWithArg` | Sysmodule | falta | como `module_load` (acepta el mismo identificador interno; los argumentos extra se registran) **[H]** |
| `4fU5yvOkVG4` | `sceSysmoduleGetModuleInfoForUnwind` | Sysmodule | falta | error explícito (ver 4.2) |
| `SsC-m-S9JTA` | `sceSystemServiceParamGetString` | SystemService | falta | ver 4.1 |
| `9kPCz7Or+1Y` | `sceSystemServiceReenableMusicPlayer` | SystemService | falta | 0, sin efecto (`sceSystemServiceDisableMusicPlayer` ya existe) |
| `lUoqwTQu4Go` | `sceUserServiceGetUserColor` | UserService | falta | ver 4.1 |
| `Lr8ovHH9l6A` | `sceMsgDialogGetResult` | MsgDialog | código existe | solo `import_names.inc` |
| `fJgP+wqifno` | **sin nombre** | DiscMap | falta | se identifica por el sitio de llamada (4.3) |
| `NWtTN10cJzE` | `sceLibcHeapGetTraceInfo` | LibcInternalExt | hecha | ver 4.3 |

Por ser APIs que el juego solo llama si ocurre algo concreto, se trabaja con el mismo criterio que la
Spec 2: un import sin implementar solo detiene el juego al llamarlo. Se implementa lo que aparece
como `STOP:` y se cierra el resto antes de dar la spec por terminada (para dejar `--check-imports`
en cero).

## 4. Diseño

### 4.1 Comportamiento "sin efecto"

Una API "sin efecto" devuelve 0, escribe **una línea de log la primera vez que se llama**
(`Service: <nombre> accepted, no effect`) y cuenta sus llamadas para `runtime_report`. No se añade
ninguna sin esa línea. Si el juego usa el valor devuelto para decidir algo (por ejemplo,
`EnableLiveStreaming` consulta un estado), la API deja de ser "sin efecto" y se implementa de
verdad.

- `sceSystemServiceParamGetString(id, buf, size)` **[H]**: se registran el `id` y el tamaño; para ids
  desconocidos devuelve `SYSTEM_PARAMETER` (0x80A10003), igual que `ParamGetInt`. Cuando el sitio de
  llamada muestre qué id pide, se añade ese valor (por ejemplo, nombre del sistema) con un texto
  fijo.
- `sceUserServiceGetUserColor(user, *color)` **[H]**: color fijo del usuario 1. Solo para el usuario
  que el runtime ya reporta (`user_initial`); cualquier otro id devuelve el error de usuario.

### 4.2 Errores explícitos en lugar de datos falsos

`sceSysmoduleGetModuleInfoForUnwind` y su gemela de la Spec 2 (`sceKernelGetModuleInfoForUnwind`)
alimentan el desenrollado de excepciones de C++. Devolver datos inventados corrompe el desenrollado.
Se devuelve un error y se registra; si el juego lanza excepciones, aparecerá como un fallo claro y se
implementará con los datos reales de `probe.c`.

### 4.3 Las que se resuelven con el sitio de llamada

- `fJgP+wqifno` (DiscMap): no coincide con ningún nombre probado. El dump trae `sce_discmap.plt`, lo
  que indica uso real de DiscMap. Mismo método que la Spec 2 (paso 5.7): sitio de llamada, primer
  argumento y uso del valor devuelto.
- `sceLibcHeapGetTraceInfo`: la usa la libc del sistema para trazas del montón; se devuelve un error
  explícito de "no disponible" salvo que el sitio de llamada muestre otro uso.
  Verificado (2026-10-17, libc `+0x2c6da` y `+0x2c211`): **no es solo para trazas**. libc pasa una
  copia de `{u64 size=0x20; u32 1; u32 trace; u64 *id_mask; void **mstates}` e ignora el valor
  devuelto, pero su `malloc` usa siempre los dos punteros: la init hace `*id_mask = 0`, cada mspace
  toma el bit libre más bajo de `*id_mask` (`lock cmpxchg`, id = bit + 1) y se guarda en
  `mstates[id]`, y `free` comprueba el dueño de cada bloque con `mstates[bits 48..54 de su cabecera]`.
  Con la estructura intacta (punteros nulos) libc falla en `+0x2c79f`. Implementada en
  `src/runtime_kernel.c`: devuelve 0, `trace = 0` (trazas desactivadas) y apunta a una máscara y una
  tabla de 128 entradas del runtime (en la consola las comparten todas las libc del proceso; aquí hay
  una). `EINVAL` si la estructura es nula o `size < 0x20`.

### 4.4 SaveData

`UmountWithBackup`, `CheckBackupData` y `RestoreBackupData` no crean ni consultan copias: el runtime
no las implementa. `CheckBackupData` y `RestoreBackupData` devuelven `NOT_FOUND` (el juego recibe
"no hay copia"), no éxito. Si el juego muestra una pantalla de "restaurar copia", la usa el camino de
error del propio juego.

## 5. Requisitos

- El sistema nunca debe fingir que PSN está conectada.
- Cuando el juego abra un diálogo del sistema, el sistema debe concluirlo con el resultado documentado
  y registrarlo en el log.
- Una API "sin efecto" que el juego use para tomar decisiones se convierte en una implementación real.

## 6. Pruebas

Ampliar `tests/test_services.c` (que ya prueba los diálogos, PlayGo y otros) con:

1. Cada API "sin efecto" devuelve 0 y la segunda llamada no vuelve a registrar la línea de log.
2. `CheckBackupData` y `RestoreBackupData` devuelven `0x809F0008` con un `OrbisSaveDataMountPoint` válido.
3. `UmountWithBackup` desmonta un montaje abierto con `Mount2` y devuelve `NOT_FOUND` si ya estaba desmontado.
4. `ParamGetString` con un id desconocido devuelve `0x80A10003`.
5. `GetUserColor` con usuario 1 devuelve el valor fijo; con otro usuario, error.
6. `sceMsgDialogGetResult` después de `sceMsgDialogOpen` devuelve el resultado del diálogo del overlay.

Como el test compila `src/runtime_services.c` por separado, las nuevas líneas de `import_names.inc` se
comprueban con un test de tabla: cada nombre de la sección 3 está en `import_names.inc` y su NID
coincide con `nid()` (script de comprobación en `tests/test_import_names.py`, nuevo).

## 7. Archivos

`src/runtime_services.c`, `src/runtime_savedata.c`, `src/runtime_content.c` (Sysmodule),
`src/import_names.inc` (23 entradas, 22 con nombre), `tests/test_services.c`,
`tests/test_import_names.py` (nuevo).

## 8. Criterios de aceptación

- [ ] `--check-imports` no lista ninguna de las 23 (junto con las Specs 2 y 4).
- [ ] `out/services-test` cubre las APIs nuevas y pasa.
- [ ] `tests/test_import_names.py` confirma NID y nombre de cada una.
- [ ] En un arranque real, cada API "sin efecto" que se llame deja su línea de log.
- [ ] Ninguna API de PSN/red devuelve éxito.

## 9. Preguntas abiertas

- ¿El juego usa el resultado de `ParamGetString` para mostrar algo (nombre de consola, etc.)? Se
  sabrá en la primera ejecución.
- ¿Hay un camino de "copia de seguridad" que el juego tome al guardar? Si es así, `UmountWithBackup`
  pasa de "desmontar" a necesitar una copia real (el repositorio ya tiene `scripts/save_backup.py`,
  que no es del runtime).
