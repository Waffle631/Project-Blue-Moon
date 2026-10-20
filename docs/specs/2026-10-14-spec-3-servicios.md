# Spec 3 — Servicios del sistema que faltan

Fecha: 2026-10-14. Estado: **cerrada** el 2026-10-19. El plan de implementación tenía 7 tareas, y
están todas hechas (`f4a5d0a`, `4e09b95`, `45ba36b`, `82c7d78`, `2ced05c`, `5fcc578` y el cierre). Los
resultados están en la sección 6, y los criterios, en la 8.
Roadmap: `docs/tsuki/ROADMAP.md`, sección "Spec 3".

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
  20 APIs. *(Corregido el 2026-10-19: eran 8, porque la Spec 2 ya había hecho 15; ver el hecho
  siguiente.)*
- **[V]** (2026-10-19, `tests/test_import_names.py`) De las 23, **15 ya tienen su línea** en
  `import_names.inc` y faltan 8: `sceMsgDialogGetResult`, que ya tiene código, y las 7 que siguen como
  "falta" en la tabla. `--check-imports` da 19 faltantes: esas 8, 8 de Audiodec (Spec 4) y las 3 trampas
  de libkernel de la Spec 2.
- **[V]** (2026-10-19, `4e09b95`) Ya están las 23 líneas. `--check-imports` da 18:
  `sceMsgDialogGetResult` ya resuelve. Las otras 7 siguen en la lista, pero ahora con su nombre en vez de
  "name not resolved".
- **[V]** (2026-10-19, `45ba36b`) `sceSharePlayTerminate`, `sceGameLiveStreamingTerminate` y
  `sceSystemServiceReenableMusicPlayer` ya son "sin efecto". `--check-imports` da 15: 8 de Audiodec, las 3
  trampas de kernel y 4 de esta spec (`GetUserColor`, `Check`/`RestoreBackupData` y
  `sceSysmoduleGetModuleInfoForUnwind`). Sitios de llamada: los dos Terminate están en la ruta de cierre
  del juego (eboot `+0xed3f7`) y `ReenableMusicPlayer` solo en un envoltorio al que nada llama (ver 4.1).
- **[V]** (2026-10-19, `82c7d78`) `sceUserServiceGetUserColor` ya resuelve. `--check-imports` da 14. De esta
  spec quedan `sceSaveDataCheckBackupData`, `sceSaveDataRestoreBackupData` y
  `sceSysmoduleGetModuleInfoForUnwind`.
- **[V]** (2026-10-19, `2ced05c`) `sceSaveDataCheckBackupData` y `sceSaveDataRestoreBackupData` ya
  resuelven. `--check-imports` da 12. De esta spec solo queda `sceSysmoduleGetModuleInfoForUnwind`.
- **[V]** (2026-10-19, `5fcc578`) `sceSysmoduleGetModuleInfoForUnwind` ya resuelve. `--check-imports` da
  11: los 8 de Audiodec (Spec 4) y las 3 trampas de libkernel (Spec 2). Ya no queda ninguna de las 23.
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
| `RQOqDbk3bSU` | `sceSaveDataCheckBackupData` | SaveData | **hecha** (`2ced05c`) | `NOT_FOUND` (0x809F0008): no hay copia de seguridad; eboot `+0x121895`, solo tras una carga rota (ver 4.4) |
| `lU9YRFsgwSU` | `sceSaveDataRestoreBackupData` | SaveData | **hecha** (`2ced05c`) | `NOT_FOUND`; eboot `+0x121bb2`, `+0x1224fd` y `+0x1257b8` |
| `2xxUtuC-RzE` | `sceScreenShotEnable` | ScreenShot | **hecha** (`61ec55c`) | 0, sin efecto (política de captura, 4.1) |
| `tIYf0W5VTi8` | `sceScreenShotDisable` | ScreenShot | **hecha** (`61ec55c`) | 0, sin efecto (política de captura, 4.1) |
| `73WQ4Jj0nJI` | `sceScreenShotSetOverlayImageWithOrigin` | ScreenShot | **hecha** (`61ec55c`) | 0, sin efecto; `("/app0/image/corp.png", 0, 0, 3)` |
| `isruqthpYcw` | `sceSharePlayInitialize` | SharePlay | **hecha** (`1f701d3`) | 0, sin efecto; `(NULL, 0x1800)` en eboot `+0xed32e`, resultado ignorado |
| `UaLjloJinow` | `sceSharePlayTerminate` | SharePlay | **hecha** (`45ba36b`) | 0, sin efecto; ruta de cierre, eboot `+0xed406`, resultado ignorado |
| `co2NCj--pnc` | `sceSharePlaySetProhibition` | SharePlay | **hecha** (`61ec55c`) | 0, sin efecto; `(0)` o `(2)` |
| `kvYEw2lBndk` | `sceGameLiveStreamingInitialize` | GameLiveStreaming | **hecha** (`93a9b2b`) | 0, sin efecto; `(0x4000)` en eboot `+0xed338`, resultado ignorado |
| `9yK6Fk8mKOQ` | `sceGameLiveStreamingTerminate` | GameLiveStreaming | **hecha** (`45ba36b`) | 0, sin efecto; ruta de cierre, eboot `+0xed40b`, resultado ignorado |
| `dWM80AX39o4` | `sceGameLiveStreamingEnableLiveStreaming` | GameLiveStreaming | **hecha** (`61ec55c`) | 0, sin efecto: es un setter `(bool)`, no una consulta (**[V]**, 4.1) |
| `Fc8qxlKINYQ` | `sceVideoRecordingSetInfo` | VideoRecording | **hecha** (`61ec55c`) | 0, sin efecto; `(0xd, &prohibido, 4)` |
| `hHrGoGoNf+s` | `sceSysmoduleLoadModuleInternalWithArg` | Sysmodule | **hecha** (`673559f`) | como `module_load`; los argumentos se registran (**[V]**, ver 4.3) |
| `4fU5yvOkVG4` | `sceSysmoduleGetModuleInfoForUnwind` | Sysmodule | **hecha** (`5fcc578`) | error explícito `0x805a1000`; la llama `libc.prx` (`+0xb852c`), no el eboot (ver 4.2) |
| `SsC-m-S9JTA` | `sceSystemServiceParamGetString` | SystemService | **hecha** (`6e40c0e`) | ver 4.1 |
| `9kPCz7Or+1Y` | `sceSystemServiceReenableMusicPlayer` | SystemService | **hecha** (`45ba36b`) | 0, sin efecto; solo la usa un envoltorio al que nada llama (4.1) |
| `lUoqwTQu4Go` | `sceUserServiceGetUserColor` | UserService | **hecha** (`82c7d78`) | azul (0) para el usuario 1; solo la usa un getter al que nada llama (4.1) |
| `Lr8ovHH9l6A` | `sceMsgDialogGetResult` | MsgDialog | **hecha** (`4e09b95`) | solo `import_names.inc`; ya probada en `tests/test_services.c:45-54` |
| `fJgP+wqifno` | `sceDiscMap_7C980FFB0AA27E7A` (sin nombre real) | DiscMap | **hecha** (`d80dea5`) | `NO_BITMAP_INFO`, como su gemela (4.3) |
| `NWtTN10cJzE` | `sceLibcHeapGetTraceInfo` | LibcInternalExt | hecha | ver 4.3 |

Por ser APIs que el juego solo llama si ocurre algo concreto, se trabaja con el mismo criterio que la
Spec 2: un import sin implementar solo detiene el juego al llamarlo. Se implementa lo que aparece
como `STOP:` y se cierra el resto antes de dar la spec por terminada (para dejar `--check-imports`
en cero). *(Corregido el 2026-10-19: en cero de las 23 de esta spec. El total no llega a 0 porque las
3 trampas de la Spec 2 son decididas y Audiodec es de la Spec 4.)*

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
  de la política de captura. Desde el 2026-10-19 (`45ba36b`) hay once: se suman
  `sceSharePlayTerminate`, `sceGameLiveStreamingTerminate` y `sceSystemServiceReenableMusicPlayer`.
- **Ruta de cierre del juego [V]** (eboot `+0xed3f7`, 2026-10-19): llama, en este orden, a
  `sceSaveDataTerminate`, `sceSaveDataDialogTerminate`, `scePlayGoTerminate`, `sceSharePlayTerminate` y
  `sceGameLiveStreamingTerminate`, e ignora todos los resultados.
- **`sceSystemServiceReenableMusicPlayer` [V]** (2026-10-19): solo la importa un envoltorio en eboot
  `+0x12a8e0` (`flag ? Reenable : DisableMusicPlayer`, con salto final). Nada del eboot lo llama, salta a
  él, lo carga con `lea` ni lo guarda como puntero (la misma búsqueda sí encuentra punteros a las funciones
  vecinas), así que lo más probable es que no se llame nunca. `sceSystemServiceDisableMusicPlayer` sigue
  devolviendo 0 sin log: no es de esta spec.
- **Política de captura del juego [V]** (eboot `+0xee550`, iteración 9). Con modo 0, que es el del
  arranque, llama a `sceScreenShotDisable`, a
  `sceScreenShotSetOverlayImageWithOrigin("/app0/image/corp.png", 0, 0, 3)` y a `sceScreenShotEnable`.
  Con otro modo, solo a `Disable`. Después, en cualquier modo, llama a
  `sceGameLiveStreamingEnableLiveStreaming(modo == 0)`, `sceSharePlaySetProhibition(0 o 2)` y
  `sceVideoRecordingSetInfo(0xd, &prohibido, 4)`. Ignora todos los resultados.
  `EnableLiveStreaming` es un setter, no una consulta: la **[H]** de la tabla queda resuelta como
  "sin efecto".
- `sceUserServiceGetUserColor(user, *color)` **[V]** (2026-10-19, `82c7d78`): color fijo del usuario 1,
  azul (`USER_COLOR_BLUE` = 0, el del primer usuario de una consola nueva). Solo para el usuario que el
  runtime ya reporta (`user_initial`). Cualquier otro id, o un puntero nulo, devuelve
  `USER_INVALID_ARGUMENT` (0x80960005) y no escribe nada.
  Sitio de llamada: solo la importa un getter en eboot `+0x11e750`. El getter lee el usuario de su
  objeto (`+0x4d8`) y, si es negativo, devuelve -1. Si no, devuelve el color sin mirar el resultado, así
  que una llamada fallida le daría basura de la pila. Nada del eboot llama, salta a ese getter ni apunta a
  él; su vecino `+0x11e7a0` sí está en una vtable. El valor no decide nada visible y el color fijo basta.

### 4.2 Errores explícitos en lugar de datos falsos

`sceSysmoduleGetModuleInfoForUnwind` y su gemela de la Spec 2 (`sceKernelGetModuleInfoForUnwind`)
alimentan el desenrollado de excepciones de C++. Devolver datos inventados corrompe el desenrollado.
Se devuelve un error y se registra; si el juego lanza excepciones, aparecerá como un fallo claro y se
implementará con los datos reales de `probe.c`.

**[V]** (2026-10-19, Tarea 6) El eboot no importa `sceSysmoduleGetModuleInfoForUnwind`; la importa
`libc.prx`. Tiene una sola llamada, en `libc.prx +0xb852c`, dentro del desenrollador (`+0xb84e0`):

- Lee el pc del cursor de desenrollado y pide el módulo que lo contiene, con flags 1 y una salida de
  `0x130` bytes (el tamaño va en su primer campo).
- Con 0 copia la información. Con cualquier otro valor devuelve "sin datos de desenrollado para este pc"
  y no lee la salida.
- La gemela de kernel (`sceKernelGetModuleInfoForUnwind`, trampa de la Spec 2) tiene una llamada con la
  misma forma en `libc.prx +0xb8191`.

La función devuelve `0x805a1000` (`INVALID_ID`, el "no existe ese módulo" de Sysmodule, con el nombre
`UNWIND_UNAVAILABLE`) sin escribir en la salida. Escribe una línea de log en la primera llamada, y
`runtime_content_report` cuenta las consultas.

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

**[V]** (2026-10-19, Tarea 5) Sitios de llamada de las dos:

- `CheckBackupData` tiene una sola llamada, en eboot `+0x121895`, dentro de la rutina de carga
  (`+0x121650`). La rutina monta la partida con `Mount2` y la lee. Solo pregunta por la copia si la
  lectura falla con `0x809F000F` (partida rota) o `0x80020060`, y solo pasa al estado de "restaurar" si
  recibe 0. Con `0x809F0008` o `0x80020002` de la carga va al estado "sin partida".
- `RestoreBackupData` tiene tres llamadas: `+0x121bb2`, `+0x1224fd` y `+0x1257b8`. Las tres guardan el
  resultado como último error del objeto de partida y toman cualquier valor distinto de 0 como fallo.
- Las cuatro llamadas pasan una estructura de 72 bytes a cero con el usuario, el título a `NULL` y el
  nombre de directorio de la partida (el mismo que acaba de montar).

Además de `NOT_FOUND`, devuelven `NOT_INITIALIZED` antes de `Initialize3` y `PARAMETER` sin
estructura o sin un nombre de directorio válido, como `sceSaveDataDelete`. Cada una escribe una línea
de log en su primera llamada, y `runtime_savedata_report` cuenta las consultas y las restauraciones.

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

*(Corregido el 2026-10-19: los puntos 2 y 3 están en `tests/test_runtime.c`, porque
`runtime_savedata.c` no se compila en `services-test`. `GetModuleInfoForUnwind` está en
`tests/test_content.c`.)*

Como el test compila `src/runtime_services.c` por separado, las nuevas líneas de `import_names.inc` se
comprueban con un test de tabla: cada nombre de la sección 3 está en `import_names.inc` y su NID
coincide con `nid()` (script de comprobación en `tests/test_import_names.py`, nuevo).

**Resultado (2026-10-19, commit `f4a5d0a`).** `tests/test_import_names.py` tiene la lista de las 23
`(nombre, biblioteca, NID)` de la sección 3 y dos tests:

- `test_nid_matches_name`: el NID de cada nombre coincide con `nid()`. La excepción es
  `sceDiscMap_7C980FFB0AA27E7A`, que no es un nombre real (`nid()` da `qYaK2F5LbgM`, no `fJgP+wqifno`). Para
  ella se comprueba el convenio de 4.3: los 16 hex del nombre son los bytes del NID decodificado en base64.
  Pasa las 23.
- `test_every_name_is_in_import_names_inc`: cada `"NID#biblioteca","nombre"` está en `import_names.inc`.
  Falla con 8, a propósito, hasta que se añadan sus líneas: `sceMsgDialogGetResult`,
  `sceSharePlayTerminate`, `sceGameLiveStreamingTerminate`, `sceSystemServiceReenableMusicPlayer`,
  `sceUserServiceGetUserColor`, `sceSaveDataCheckBackupData`, `sceSaveDataRestoreBackupData` y
  `sceSysmoduleGetModuleInfoForUnwind`.
- Con la tabla alterada en memoria (NID, hex de DiscMap o biblioteca equivocados), cada error da su fallo.
- Suite Python completa, dentro de `ps4dev`: 257 tests, 35 omitidos, 4 fallos esperados del launcher y solo
  esos 8 fallos. Fuera del contenedor, los tests que ejecutan binarios de `out/` fallan porque no
  encuentran `libavcodec.so.62`.

**Resultado (2026-10-19, commit `4e09b95`).** Se añaden las 8 líneas que faltaban en `import_names.inc`,
cada una junto a una hermana de su biblioteca:

- `test_import_names.py` pasa los dos tests con las 23.
- No se añade ningún NID duplicado.
- `bash build.sh --test` en `ps4dev` pasa sin warnings del compilador.
- Suite Python en `ps4dev`: 257 tests, 35 omitidos y 4 fallos esperados, el resto OK.
- `--check-imports` da 18 (ver sección 2).

El punto 6 de arriba (`GetResult` tras `Open`) ya estaba cubierto por `tests/test_services.c:45-54`, con
el diálogo terminado, en curso y con `NULL`.

**Resultado (2026-10-19, commit `45ba36b`).** `sceSharePlayTerminate`, `sceGameLiveStreamingTerminate` y
`sceSystemServiceReenableMusicPlayer` son tres entradas más de `no_effect`. `tests/test_services.c` prueba
el punto 1 con ellas:

- Cada una se llama dos veces seguidas, dos rondas. La primera ronda escribe una línea y la segunda
  ninguna, y el contador queda en 2 y luego en 4.
- Los Terminate se llaman antes que cualquier `Initialize`, y sus contadores siguen a 0: no dependen de
  él.
- El nombre de cada entrada nueva del `enum` es el de su API, que es lo que se lee en el log.
- Rojo: el test no compila sin las tres funciones. Verde: `services test: ok`.
- `bash build.sh --test` y la suite Python en `ps4dev` pasan (257 tests, 35 omitidos y 4 fallos
  esperados).
- `--check-imports` da 15 (ver sección 2).

**Resultado (2026-10-19, commit `82c7d78`).** `tests/test_services.c` prueba el punto 5 con
`sceUserServiceGetUserColor`:

- Con el usuario 1 devuelve 0 y escribe azul. La variable de salida vale `0x7f` antes de la llamada, para
  que se vea que la función la escribe aunque azul sea 0.
- Con el usuario 2, con -1 (el valor "sin usuario" del getter del juego) y con un puntero nulo devuelve
  `USER_INVALID_ARGUMENT`, y la salida sigue valiendo `0x7f`.
- Rojo: el test no compila sin `user_color`. Verde: `services test: ok`.
- `bash build.sh --test` y la suite Python en `ps4dev` pasan (257 tests, 35 omitidos y 4 fallos
  esperados).
- `--check-imports` da 14 (ver sección 2).

**Resultado (2026-10-19, commit `2ced05c`).** `sceSaveDataCheckBackupData` y
`sceSaveDataRestoreBackupData` se prueban en `tests/test_runtime.c` (`savedata_backup_queries`), no en
`tests/test_services.c`, porque `runtime_savedata.c` no se compila en `services-test`. El punto 2 habla
de un `OrbisSaveDataMountPoint`, pero las dos reciben su propia estructura (ver 4.4); el test usa los
72 bytes que pasa el juego:

- Tras `sceSaveDataTerminate` (antes de inicializar), las dos devuelven `0x809F0001`.
- Tras `Initialize3`, las dos devuelven `0x809F0008` con `savedata000`, en dos llamadas seguidas.
- Con `NULL` o sin nombre de directorio, `0x809F0000`.
- El test no comprueba el log, porque `test_runtime.c` no captura la salida. En el log del verde sale
  una línea por API aunque cada una devuelve `NOT_FOUND` dos veces.
- Rojo: `runtime-test` falla porque las dos no resuelven. Verde: `bash build.sh --test` en `ps4dev`
  pasa sin warnings del compilador.
- Suite Python en `ps4dev`: 257 tests, 35 omitidos y 4 fallos esperados, el resto OK.
- `--check-imports` da 12 (ver sección 2).

**Resultado (2026-10-19, commit `5fcc578`).** `sceSysmoduleGetModuleInfoForUnwind` se prueba en
`tests/test_content.c` (`unwind_unavailable`), porque Sysmodule vive en `runtime_content.c`:

- Con flags 1 (lo que pasa `libc.prx`) y con flags 0 devuelve `0x805a1000`, y los `0x130` bytes de la
  salida siguen a `0xa5`.
- Con la salida a `NULL` devuelve el mismo error, sin desreferenciar.
- `content-test --unwind` hace tres llamadas, y `tests/test_content.py`
  (`test_unwind_info_is_an_error_logged_once`) comprueba que sale una sola línea de log.
- Rojo: `content-test` falla porque la función no resuelve, y `test_content.py` tiene 3 fallos. Verde:
  `content-test` y `test_content.py` (6 tests) pasan.
- `bash build.sh --test` en `ps4dev` pasa sin warnings del compilador.
- Suite Python en `ps4dev`: 258 tests (uno nuevo), 35 omitidos y 4 fallos esperados, el resto OK.
- `--check-imports` da 11 (ver sección 2).

**Resultado (2026-10-19, Tarea 7, cierre).** Verificación final, todo dentro de `ps4dev`:

- `bash build.sh --test`: salida 0, sin warnings del compilador (`out/spec3-t7-build.log`).
- Suite Python: 258 tests, 35 omitidos y 4 fallos esperados del launcher, el resto OK
  (`out/spec3-t7-suite.log`).
- `--check-imports`: `Imports: 11 missing`. Son los 8 de Audiodec y las 3 trampas de kernel, y ninguna de
  las 23 (`out/spec3-t7-check-imports.log`).
- Arranque real de 60 s (`GOW3_GAME_DIR=Tsukihime/CUSA47999 GOW3_TIMEOUT=60 python3 run.py`,
  `out/spec3-t7-run.log`). Salen las mismas ocho líneas "sin efecto" que en `out/run-2a-10b.log` (dos
  `Initialize` y la política de captura), y no hay ningún `STOP:` hasta el watchdog. El juego monta la
  partida que ya existe (solo lectura), y la carga no falla como rota: no sale la línea de
  `CheckBackupData`. Después arranca el reproductor del prólogo (hilo `av audio` y puerto de audio
  abiertos).
- Las APIs nuevas no se llaman en ese arranque, y era lo esperado. Los Terminate están en el cierre del
  juego. `CheckBackupData` solo se llama si la carga falla como rota. La información de desenrollado
  solo se pide al desenrollar la pila, por ejemplo en una excepción de C++. `ReenableMusicPlayer` y
  `GetUserColor` están en funciones a las que nada llama. `RestoreBackupData` tampoco se llama; no se ha
  comprobado qué camino lleva a sus tres sitios.

## 7. Archivos

`src/runtime_services.c`, `src/runtime_savedata.c`, `src/runtime_content.c` (Sysmodule),
`src/import_names.inc` (23 entradas, 22 con nombre), `tests/test_services.c`, `tests/test_runtime.c`
(SaveData), `tests/test_content.c` y `tests/test_content.py` (Sysmodule), `tests/test_import_names.py`
(nuevo).

## 8. Criterios de aceptación

- [x] `--check-imports` no lista ninguna de las 23 (2026-10-19, `5fcc578`: da 11, los 8 de Audiodec de la
  Spec 4 y las 3 trampas de libkernel de la Spec 2).
- [x] `out/services-test` cubre las APIs nuevas y pasa (2026-10-19: cubre las tres "sin efecto",
  `45ba36b`, y `GetUserColor`, `82c7d78`. Las APIs de otros archivos se prueban donde se compila su
  código: las de copia de seguridad en `out/runtime-test`, `2ced05c`, y `GetModuleInfoForUnwind` en
  `out/content-test`, `5fcc578`).
- [x] `tests/test_import_names.py` confirma NID y nombre de cada una (2026-10-19, `4e09b95`: pasan los
  23 NID y los 23 nombres).
- [x] En un arranque real, cada API "sin efecto" que se llame deja su línea de log (2026-10-17: se
  cumple para las ocho llamadas hasta ahora, `out/run-2a-10b.log`; 2026-10-19: las mismas ocho, en
  `out/spec3-t7-run.log`. Las tres nuevas no se llaman en el arranque, y sus tests comprueban la línea).
- [x] Ninguna API de PSN/red devuelve éxito (2026-10-19). Las únicas APIs de Np que importa el juego son
  `sceNpSetNpTitleId`, que es configuración local, y las de trofeos, que son locales y funcionan sin
  conexión en la consola. No importa nada de red (`Net`, `Http`, `Ssl`) ni ninguna API que consulte el
  estado de PSN. Las que tiene el runtime para otros juegos responden "sin sesión" (`np_signed_out`,
  `np_state`) o "red caída" (`http_fail`). Esta spec no ha tocado ninguna.

## 9. Preguntas abiertas

- ¿El juego usa el resultado de `ParamGetString` para mostrar algo (nombre de consola, etc.)? Se
  sabrá en la primera ejecución. *Respuesta parcial (2026-10-17, ver 4.1): ignora el valor devuelto y
  guarda el texto en su objeto. No se sabe si lo muestra; sigue abierta, sin bloquear nada.*
- ¿Hay un camino de "copia de seguridad" que el juego tome al guardar? Si es así, `UmountWithBackup`
  pasa de "desmontar" a necesitar una copia real (el repositorio ya tiene `scripts/save_backup.py`,
  que no es del runtime). *Respuesta (2026-10-19, Tarea 5, ver 4.4): sí, pero solo para recuperar.
  El único desmontaje después de escribir es `UmountWithBackup`, con un solo sitio (`+0x1214e2`) y el
  resultado ignorado. `Umount` solo aparece en la rutina de carga (`+0x121795` y `+0x1217d4`). La
  copia solo se consulta cuando una partida se lee como rota. Sin copia real, una partida rota lleva
  al camino de "no hay copia" del propio juego. Hacer copias reales es una mejora de robustez, no un
  requisito de la Alpha, y queda fuera de esta spec.*
