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
| `TywrFKCoLGY` | `sceSaveDataInitialize3` | SaveData | **hecha** (`2a43953`) | solo `import_names.inc`; el juego la llama con `NULL` (eboot `+0xed2eb`) |
| `0z45PIH+SNI` | `sceSaveDataMount2` | SaveData | **hecha** (`7b14f96`) | solo `import_names.inc`; eboot `+0x121716`, modo 1 (solo lectura); sin partida, `NOT_FOUND` |
| `VwadwBBBJ80` | `sceSaveDataUmountWithBackup` | SaveData | **hecha** (`991fc5d`) | igual que `sceSaveDataUmount` (`save_umount`); no crea copia (ver 4.4) |
| `RQOqDbk3bSU` | `sceSaveDataCheckBackupData` | SaveData | falta | `NOT_FOUND` (0x809F0008): no hay copia de seguridad |
| `lU9YRFsgwSU` | `sceSaveDataRestoreBackupData` | SaveData | falta | `NOT_FOUND` |
| `2xxUtuC-RzE` | `sceScreenShotEnable` | ScreenShot | **hecha** (`61ec55c`) | 0, sin efecto (política de captura, 4.1) |
| `tIYf0W5VTi8` | `sceScreenShotDisable` | ScreenShot | **hecha** (`61ec55c`) | 0, sin efecto (política de captura, 4.1) |
| `73WQ4Jj0nJI` | `sceScreenShotSetOverlayImageWithOrigin` | ScreenShot | **hecha** (`61ec55c`) | 0, sin efecto; `("/app0/image/corp.png", 0, 0, 3)` |
| `isruqthpYcw` | `sceSharePlayInitialize` | SharePlay | **hecha** (`1f701d3`) | 0, sin efecto; `(NULL, 0x1800)` en eboot `+0xed32e`, resultado ignorado |
| `UaLjloJinow` | `sceSharePlayTerminate` | SharePlay | falta | 0, sin efecto |
| `co2NCj--pnc` | `sceSharePlaySetProhibition` | SharePlay | **hecha** (`61ec55c`) | 0, sin efecto; `(0)` o `(2)` |
| `kvYEw2lBndk` | `sceGameLiveStreamingInitialize` | GameLiveStreaming | **hecha** (`93a9b2b`) | 0, sin efecto; `(0x4000)` en eboot `+0xed338`, resultado ignorado |
| `9yK6Fk8mKOQ` | `sceGameLiveStreamingTerminate` | GameLiveStreaming | falta | 0, sin efecto |
| `dWM80AX39o4` | `sceGameLiveStreamingEnableLiveStreaming` | GameLiveStreaming | **hecha** (`61ec55c`) | 0, sin efecto: es un setter `(bool)`, no una consulta (**[V]**, 4.1) |
| `Fc8qxlKINYQ` | `sceVideoRecordingSetInfo` | VideoRecording | **hecha** (`61ec55c`) | 0, sin efecto; `(0xd, &prohibido, 4)` |
| `hHrGoGoNf+s` | `sceSysmoduleLoadModuleInternalWithArg` | Sysmodule | **hecha** (`673559f`) | como `module_load`; los argumentos se registran (**[V]**, ver 4.3) |
| `4fU5yvOkVG4` | `sceSysmoduleGetModuleInfoForUnwind` | Sysmodule | falta | error explícito (ver 4.2) |
| `SsC-m-S9JTA` | `sceSystemServiceParamGetString` | SystemService | **hecha** (`6e40c0e`) | ver 4.1 |
| `9kPCz7Or+1Y` | `sceSystemServiceReenableMusicPlayer` | SystemService | falta | 0, sin efecto (`sceSystemServiceDisableMusicPlayer` ya existe) |
| `lUoqwTQu4Go` | `sceUserServiceGetUserColor` | UserService | falta | ver 4.1 |
| `Lr8ovHH9l6A` | `sceMsgDialogGetResult` | MsgDialog | código existe | solo `import_names.inc` |
| `fJgP+wqifno` | `sceDiscMap_7C980FFB0AA27E7A` (sin nombre real) | DiscMap | **hecha** (`d80dea5`) | `NO_BITMAP_INFO`, como su gemela (4.3) |
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

- `sceSystemServiceParamGetString(id, buf, size)` **[V]** (2026-10-17, fase 2A, iteración 1): el
  juego la llama una vez al arrancar (eboot `+0x129991`), justo después de `ParamGetInt(1)` (idioma),
  con `(6, obj+4, 0x41)`: el id 6 es el nombre del sistema, en un buffer de 65 bytes, y el valor
  devuelto se ignora. Se devuelve el texto fijo `"PS4"`. Cada llamada registra el `id` y el tamaño;
  un id desconocido, un buffer nulo o uno demasiado corto devuelven `SYSTEM_PARAMETER` (0x80A10003).
  Ojo: `ParamGetInt` no hace lo mismo con un id desconocido (da `STOP:`, `runtime_services.c`); aquí
  se sigue esta spec.
- Mecanismo común (`runtime_services.c`, desde la iteración 5): una tabla de nombres y contadores y
  `no_effect(api)`, que devuelve 0 y escribe `Service: <nombre> accepted, no effect` solo en la
  primera llamada; `runtime_services_report()` saca los contadores en `runtime_report`. Añadir otra
  API "sin efecto" es una entrada en el `enum`, su nombre y su función. Hay ocho (2026-10-17, hasta la
  iteración 9 de la fase 2A): `sceSharePlayInitialize`, `sceGameLiveStreamingInitialize` y las seis
  de la política de captura.
- **Política de captura del juego [V]** (eboot `+0xee550`, iteración 9). Con modo 0, que es el del
  arranque, llama a `sceScreenShotDisable`, a
  `sceScreenShotSetOverlayImageWithOrigin("/app0/image/corp.png", 0, 0, 3)` y a `sceScreenShotEnable`.
  Con otro modo, solo a `Disable`. Después, en cualquier modo, llama a
  `sceGameLiveStreamingEnableLiveStreaming(modo == 0)`, `sceSharePlaySetProhibition(0 o 2)` y
  `sceVideoRecordingSetInfo(0xd, &prohibido, 4)`. Ignora todos los resultados.
  `EnableLiveStreaming` es un setter, no una consulta: la **[H]** de la tabla queda resuelta como
  "sin efecto".
- `sceUserServiceGetUserColor(user, *color)` **[H]**: color fijo del usuario 1. Solo para el usuario
  que el runtime ya reporta (`user_initial`); cualquier otro id devuelve el error de usuario.

### 4.2 Errores explícitos en lugar de datos falsos

`sceSysmoduleGetModuleInfoForUnwind` y su gemela de la Spec 2 (`sceKernelGetModuleInfoForUnwind`)
alimentan el desenrollado de excepciones de C++. Devolver datos inventados corrompe el desenrollado.
Se devuelve un error y se registra; si el juego lanza excepciones, aparecerá como un fallo claro y se
implementará con los datos reales de `probe.c`.

### 4.3 Las que se resuelven con el sitio de llamada

- `sceSysmoduleLoadModuleInternalWithArg` **[V]** (2026-10-17, fase 2A, iteración 2): la llama
  `libSceFios2.prx` (`+0x394a5`) con `(0xd7, argc=0, argv=NULL, 0, &res)`. Si devuelve `0x80020064`,
  Fios2 toma el error de `res`; si su init posterior falla, descarga el módulo con la
  `sceSysmoduleUnloadModule` **pública**, así que usa los mismos ids y el mismo contador. 0xd7 es
  DiscMap: después Fios2 llama a `sceDiscMapIsRequestOnHDD`. Implementada en `runtime_content.c` sobre
  `module_load`, con `*res = 0` (un módulo del host no tiene función de inicio). Un id de más de 16
  bits (internos `0x8000xxxx`) da `STOP:`, porque aún no se ha visto ninguno.

- `fJgP+wqifno` (DiscMap) **[V]** (2026-10-17, fase 2A, iteración 8): no tiene nombre real. Sus
  bytes en base64 son `7C980FFB0AA27E7A`, así que se llama `sceDiscMap_7C980FFB0AA27E7A`, con el
  mismo convenio que su gemela `ioKMruft1ek` (`sceDiscMap_8A828CAEE7EDD5E9`), que ya existía.
  `libSceFios2.prx` la llama en `+0x3411a` con `(ruta, 0, n, &ubicación, &lba, &tamaño)`, tras poner
  a cero las tres salidas de 64 bits, para conocer la ubicación LBA del archivo en el paquete
  (`"FIOS2 WARNING: An unexpected value of %ld was returned for the LBA package location"` si la
  ubicación es ≥ 2). Con un resultado distinto de 0, usa su camino de reserva.
  - Respuesta: `NO_BITMAP_INFO` (`0x81100004`) sin tocar las salidas. Es lo que ya hacían su gemela
    y `sceDiscMapIsRequestOnHDD`, y lo que responde la librería real mientras no tiene bitmap (análisis
    del PR #2686 de shadPS4).
  - shadPS4 upstream devuelve OK con las salidas a cero para esta función. Aquí eso sería una
    ubicación inventada.
  - En la ejecución real, Fios2 sigue y el juego carga datos.
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
no las implementa. `UmountWithBackup` **[V]** (2026-10-17, fase 2A, iteración 10): el juego la llama
en eboot `+0x1214e2` sobre la partida `savedata000` que acaba de crear en el primer arranque (después
de escribirla y de `SaveIcon`) e ignora el resultado. Desmonta con `save_umount`, escribe una línea
de log en la primera llamada y `runtime_report` cuenta los desmontajes sin copia. `CheckBackupData` y `RestoreBackupData` devuelven `NOT_FOUND` (el juego recibe
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
- [ ] En un arranque real, cada API "sin efecto" que se llame deja su línea de log (2026-10-17: se
  cumple para las ocho llamadas hasta ahora, `out/run-2a-10b.log`).
- [ ] Ninguna API de PSN/red devuelve éxito.

## 9. Preguntas abiertas

- ¿El juego usa el resultado de `ParamGetString` para mostrar algo (nombre de consola, etc.)? Se
  sabrá en la primera ejecución.
- ¿Hay un camino de "copia de seguridad" que el juego tome al guardar? Si es así, `UmountWithBackup`
  pasa de "desmontar" a necesitar una copia real (el repositorio ya tiene `scripts/save_backup.py`,
  que no es del runtime).
