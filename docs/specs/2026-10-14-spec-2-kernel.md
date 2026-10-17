# Spec 2 — Kernel: arranque de libc, event flags, tiempo, hilos y archivos

Fecha: 2026-10-14. Estado: **borrador para aprobación**. Roadmap: `docs/tsuki/ROADMAP.md`, sección "Spec 2".

Marcas usadas en este documento: **[V]** hecho comprobado en el repo o en los logs (con el comando o
la ruta); **[H]** hipótesis que hay que confirmar antes de implementar (cada una tiene su paso de
verificación en la sección 5).

## 1. Objetivo

Que el juego pase de la inicialización de `libc.prx` y llegue a ejecutar su propio código, a los
logos y al hito de primer fotograma, sin ningún `STOP:` de `libkernel`. Después, cerrar el resto de
las 29 APIs de `libkernel` para que `--check-imports` no liste ninguna de esta biblioteca.

Fuera de alcance: servicios de plataforma (Spec 3), audio (Spec 4), vídeo (Spec 5).

## 2. Hechos comprobados

- **[V]** `out/first-run.log`: el loader mapea 9 segmentos (37 012 780 bytes, 42 319 relocaciones),
  arranca `libc.prx` y se detiene con
  `STOP: first unsupported PS4 import: rNhWz+lvOMU#libkernel (index 337)`,
  `Caller in linked module 0 (libc.prx): +0x790`, `first argument: 0x802145e90`.
- **[V]** Un import sin implementación solo detiene el juego **cuando se llama** (la trampa salta en
  la llamada, no al cargar: cada import de función apunta a una trampa de 32 bytes que imprime el
  `STOP:` al ejecutarse, `src/probe.c:839-854` y `:86`; los imports de **datos** apuntan a páginas
  protegidas y fallan al acceder, pero ninguno de los 61 es de datos). Por eso la lista de 61 de
  `--check-imports` es un **límite superior**:
  una API que el juego nunca llama no estorba. Esto permite trabajar guiado por los `STOP:`.
- **[V]** `runtime_lookup` (`src/runtime.c:269`) traduce NID → nombre con `src/import_names.inc` y
  solo entonces busca el nombre en las tablas `RuntimeExport`. Una función con código pero sin su
  línea `{"NID#libkernel","nombre"}` en `import_names.inc` es **inalcanzable**.
- **[V]** Eso le pasa ya a dos APIs de esta spec, que tienen código: `sceKernelGetProcessTimeCounter`
  (`src/runtime_kernel.c:314`, NID `fgxnMeTNUtY`) y `sceKernelGetProcessTimeCounterFrequency`
  (`src/runtime_kernel.c:315`, `BNowx2l588E`). Ambas devuelven nanosegundos y 1 000 000 000. Basta
  añadir sus líneas a `import_names.inc`.
- **[V]** El juego importa solo cinco funciones de event flags: Create, Delete, Wait, Set y Cancel.
  No importa Poll, Clear, Open ni Close; no se implementan (no hay código muerto).
- **[V]** Plantilla a seguir: `src/runtime_sema.c` (semáforos: mutex global, lista de esperas FIFO,
  un `pthread_cond_t` por espera, plazo con `CLOCK_MONOTONIC`, microsegundos que se actualizan con
  el tiempo restante) y `tests/test_sema.c` con su envoltorio `tests/test_sema.py`.
  `build.sh` compila `src/runtime*.c` por patrón (`build.sh:82`), así que un archivo nuevo se
  incluye solo.
- **[V]** `src/runtime_rwlock.c` ya implementa los `scePthreadRwlock*` con un registro de objetos y
  una tabla de titulares por hilo. Faltan solo las variantes con nombre POSIX.
- **[V]** NID → nombre (calculados con `nid()` de `scripts/prepare.py`; los hallazgos de esta tabla
  son nuevos respecto al roadmap):

| NID | Nombre | Estado |
|---|---|---|
| `rNhWz+lvOMU` | `_sceKernelSetThreadDtors` | falta |
| `pB-yGZ2nQ9o` | `_sceKernelSetThreadAtexitCount` | falta |
| `WhCc1w3EhSI` | `_sceKernelSetThreadAtexitReport` | falta |
| `Tz4RNUCBbGI` | `_sceKernelRtldThreadAtexitIncrement` | **nombre nuevo**; falta |
| `8OnWXlgQlvo` | `_sceKernelRtldThreadAtexitDecrement` | **nombre nuevo**; falta |
| `BpFoboUJoZU` | `sceKernelCreateEventFlag` | hecha (`src/runtime_eventflag.c`) |
| `8mql9OcQnd4` | `sceKernelDeleteEventFlag` | hecha (`src/runtime_eventflag.c`) |
| `JTvBflhYazQ` | `sceKernelWaitEventFlag` | hecha (`src/runtime_eventflag.c`) |
| `IOnSvHzqu6A` | `sceKernelSetEventFlag` | hecha (`src/runtime_eventflag.c`) |
| `PZku4ZrXJqg` | `sceKernelCancelEventFlag` | hecha (`src/runtime_eventflag.c`) |
| `fgxnMeTNUtY` | `sceKernelGetProcessTimeCounter` | **código existe**, falta línea en `import_names.inc` |
| `BNowx2l588E` | `sceKernelGetProcessTimeCounterFrequency` | **código existe**, falta línea |
| `-o5uEDpN+oY` | `sceKernelConvertUtcToLocaltime` | falta |
| `0NTHN1NKONI` | `sceKernelConvertLocaltimeToUtc` | falta |
| `WslcK1FQcGI` | `sceKernelIsNeoMode` | falta |
| `py6L8jiVAN8` | `sceKernelGetSanitizerMallocReplaceExternal` | falta |
| `bnZxYgAFeA0` | `sceKernelGetSanitizerNewReplaceExternal` | falta |
| `jh+8XiK4LeE` | `sceKernelIsAddressSanitizerEnabled` | falta |
| `-YTW+qXc3CQ` | `sceKernelInternalMemoryGetModuleSegmentInfo` | falta |
| `RpQJJVKTiFM` | `sceKernelGetModuleInfoForUnwind` | falta |
| `j2AIqSqJP0w` | `sceKernelGetdents` | falta (existe `sceKernelGetdirentries`, que es otra) |
| `0Cq8ipKr9n0` | `sceKernelUtimes` | falta |
| `fgIsQ10xYVA` | `sceKernelChmod` | falta |
| `qBDmpCyGssE` | `scePthreadCancel` | falta |
| `How7B8Oet6k` | `scePthreadGetname` | falta |
| `iGjsr1WAtI0` | `pthread_rwlock_rdlock` | falta |
| `sIlRvQqsN2Y` | `pthread_rwlock_wrlock` | falta |
| `EgmLo6EWgso` | `pthread_rwlock_unlock` | falta |
| `NhpspxdjEKU` | `_nanosleep` (**resuelto en el paso 5.7**) | falta (mismo contrato que `nanosleep`) |

Total: 29, todos con nombre. Esta spec añade los 29 a `import_names.inc`.

## 3. Diseño

### 3.1 Bloque A — Arranque de `libc.prx` (callbacks de hilo)

`libc.prx` llama en su inicialización a `_sceKernelSetThreadDtors` con un puntero de función de su
propia imagen (primer argumento `0x802145e90`). Las otras cuatro (`...SetThreadAtexitCount`,
`...SetThreadAtexitReport`, `...RtldThreadAtexitIncrement`, `...RtldThreadAtexitDecrement`) forman el
mismo mecanismo: el runtime del sistema registra callbacks de libc y los usa al terminar un hilo para
ejecutar los destructores de TLS y los `atexit` por hilo de C++.

Semántica **[V]** (paso 5.1, ver la nota allí):

- `_sceKernelSetThreadDtors(void (*cb)(void))`: el kernel lo llama **en el hilo que termina**; el
  callback ejecuta los `atexit` por hilo de libc (destructores de `thread_local`) de ese hilo.
- `_sceKernelSetThreadAtexitCount(int (*cb)(int module))` y
  `_sceKernelSetThreadAtexitReport(void (*cb)(int module))`: cuentan e imprimen los `atexit` por
  hilo que quedan de un módulo. Solo sirven al descargar un módulo.
- `_sceKernelRtldThreadAtexitIncrement/Decrement(void *dso)`: contador de referencias del módulo
  que registra un `atexit` por hilo, para impedir que se descargue. Increment devuelve 0 si va bien.

Diseño: en `src/runtime_thread.c`, los tres callbacks guardados bajo el mutex de la tabla de hilos y
un contador atómico para Rtld. El callback de dtors se llama en `host_start`
(`src/runtime_thread.c:255`) después de que la entrada del hilo vuelva **o** de que `thread_exit`
salte allí con `longjmp`, y **antes** de `runtime_thread_keys_cleanup()`: el TCB y el TLS siguen
vivos, y es el orden de glibc (`thread_local` antes que las claves TSD). Count y Report se guardan y
no se llaman: el runtime nunca descarga módulos (`sceKernelStopUnloadModule` devuelve `ESRCH`). El
hilo principal no sale por `host_start`; sus `thread_local` los gestiona `exit()` de libc
(`runtime_finalize`), como hasta ahora.

Nunca se devuelve éxito sin guardar el callback: si la verificación del paso 5.1 muestra que la
semántica es otra, se detiene con `STOP:` y el motivo, no se ignora.

### 3.2 Bloque B — Event flags (`src/runtime_eventflag.c`, nuevo)

Modelo: igual que los semáforos. Un registro global protegido por un mutex; cada flag tiene su patrón
de 64 bits, atributos, una lista de esperas y un contador de esperas activas para liberar el objeto
cuando se borra con hilos aún dentro.

Handle: el tipo del SDK es un puntero opaco de 8 bytes. Se escribe en el destino un **identificador
de 64 bits creciente** (empezando en 1, nunca reutilizado), validado contra el registro. Así un
handle viejo falla de forma limpia (como en `sema_*`) y no se entrega memoria del host al guest.

Firmas **[V]** (paso 5.2, shadPS4 upstream `src/core/libraries/kernel/threads/event_flag.cpp`;
falta confirmar el uso del juego, paso 5.3):

```
int sceKernelCreateEventFlag(SceKernelEventFlag *ef, const char *name, uint32_t attr,
                             uint64_t init_pattern, const void *opt /* NULL */);
int sceKernelDeleteEventFlag(SceKernelEventFlag ef);
int sceKernelSetEventFlag   (SceKernelEventFlag ef, uint64_t bits);
int sceKernelWaitEventFlag  (SceKernelEventFlag ef, uint64_t bits, uint32_t mode,
                             uint64_t *result_pattern /* puede ser NULL */, uint32_t *timeout_us /* puede ser NULL */);
int sceKernelCancelEventFlag(SceKernelEventFlag ef, uint64_t set_pattern, int *num_waiters /* puede ser NULL */);
```

Constantes **[V]**: atributos `TH_FIFO=0x01`, `TH_PRIO=0x02`, `SINGLE=0x10`, `MULTI=0x20`; modos de
espera `AND=0x01`, `OR=0x02`, `CLEAR_ALL=0x10`, `CLEAR_PAT=0x20`.

Reglas **[V]** (corregidas en el paso 5.2; las diferencias con el borrador van en negrita):

| Caso | Resultado |
|---|---|
| Create: `ef` o `name` NULL, u `opt` distinto de NULL | `EINVAL` |
| Create: nombre de 32 caracteres o más | **`ENAMETOOLONG`** |
| Create: `attr` | nibble bajo 0 o 1 = FIFO, 2 = PRIO; nibble alto 0 o 0x10 = SINGLE, 0x20 = MULTI. **`attr = 0` es válido (FIFO + SINGLE)**. `attr > 0x22`, nibble bajo 3 o nibble alto 0x30 → `EINVAL` (upstream: UNREACHABLE) |
| Handle NULL o desconocido (todas las funciones) | `ESRCH` |
| Modo AND | se cumple si `(patrón & bits) == bits` |
| Modo OR | se cumple si `(patrón & bits) != 0` |
| `CLEAR_ALL` | al cumplirse, el patrón pasa a 0 |
| `CLEAR_PAT` | al cumplirse, `patrón &= ~bits` |
| Sin modo de limpieza | el patrón no cambia al cumplirse |
| `result_pattern` | el patrón del evento **antes** de aplicar la limpieza; en timeout, el patrón actual |
| `bits == 0`; modo sin AND/OR o con los dos; CLEAR_ALL y CLEAR_PAT a la vez; bits de modo desconocidos | `EINVAL` (upstream: UNREACHABLE salvo `bits == 0`) |
| `timeout_us` | microsegundos; NULL = sin límite; al volver contiene el tiempo restante, y 0 si hubo timeout |
| `timeout_us` apuntando a 0 con condición no cumplida | `ETIMEDOUT` sin bloquear |
| `Set` | `patrón \|= bits`; recorre las esperas en orden y despierta las que se cumplen, aplicando la limpieza de cada una **antes** de evaluar la siguiente |
| `Cancel` | **`*num_waiters` = esperas en cola**; pone el patrón a `set_pattern` y despierta todas con `ECANCELED` **y `result_pattern = set_pattern`** |
| `Delete` con esperas | despiertan con **`EACCES`** y el objeto se libera cuando sale la última |
| Atributo `SINGLE` con una espera activa | segunda espera falla con `EPERM` |
| Atributo `TH_PRIO` | se atiende en FIFO (no hay planificador de prioridades en el host), igual que los semáforos. Upstream ordena por prioridad del hilo |

Códigos de error del guest (valores `0x8002xxxx`, del archivo `orbis_error.h` vendorizado):
`EINVAL=0x80020016`, `ESRCH=0x80020003`, `ETIMEDOUT=0x8002003C`, `ECANCELED=0x80020055`,
`EPERM=0x80020001`, `ENOMEM=0x8002000C`, `EACCES=0x8002000D` (el mismo que usan los semáforos al
borrar, `tests/test_sema.c:115`), `ENAMETOOLONG=0x8002003F`.

Diagnóstico: `GOW3_TRACE_EVENTFLAG` imprime las esperas bloqueantes (igual que `GOW3_TRACE_SEMA`), e
`runtime_report` añade una línea de contadores.

### 3.3 Bloque C — Tiempo

- `GetProcessTimeCounter(+Frequency)`: solo `import_names.inc`.
- `sceKernelConvertUtcToLocaltime` / `...LocaltimeToUtc` **[H]**: convierten un `time_t` usando la
  zona horaria del host y devuelven el desfase; la firma exacta se saca del sitio de llamada del
  juego (paso 5.5). Reutilizar `kernel_gettimezone` de `src/runtime_kernel.c`.

### 3.4 Bloque D — Hilos y rwlock

- `pthread_rwlock_rdlock/wrlock/unlock` (nombre POSIX): reutilizan `rw_read`, `rw_write` y
  `rw_unlock` de `src/runtime_rwlock.c`. La diferencia es el valor devuelto: las variantes POSIX de
  este runtime devuelven un `errno` FreeBSD positivo (ver `posix(...)` en `src/runtime_thread.c`),
  no `0x8002xxxx`. Se escribe un envoltorio que convierta `0x8002xxxx` → `xxxx`. Los inicializadores
  estáticos (`PTHREAD_RWLOCK_INITIALIZER`) se comprueban con un test.
- `scePthreadGetname(thread, buf)`: copia `GuestThread.name` (campo que ya existe,
  `src/runtime_thread.c:47`, 32 bytes) al buffer del guest, con terminador. La API no recibe tamaño
  (el SDK usa un buffer de 32 bytes), así que no hay `ERANGE`: `ESRCH` si el hilo no existe y
  `EINVAL` si `buf` es NULL.
- `scePthreadCancel(thread)` **[H]**: la cancelación asíncrona de un hilo del juego dentro de
  nuestras esperas (variables de condición) puede dejar el estado del runtime a medias. No se
  implementa a ciegas: primero se mira quién la llama (paso 5.4). Hasta entonces queda como trampa
  con `STOP:`, que es el comportamiento correcto para una API no implementada.

### 3.5 Bloque E — Respuestas explícitas

| API | Respuesta | Motivo |
|---|---|---|
| `sceKernelIsNeoMode` | 0 | El runtime representa una PS4 base. Neo hace que algunos juegos pidan más resolución y memoria |
| `sceKernelIsAddressSanitizerEnabled` | 0 | No hay sanitizer |
| `sceKernelGetSanitizerMallocReplaceExternal` / `...NewReplaceExternal` **[H]** | devuelven "no hay reemplazo" | libc consulta si el malloc está reemplazado; la forma exacta (valor devuelto o parámetro de salida) sale del sitio de llamada |
| `sceKernelGetModuleInfoForUnwind` | trampa (`STOP:`) hasta que el juego la llame (decidido, sección 9) | Lo usa el desenrollado de excepciones de C++. Devolver datos falsos corrompería el desenrollado. Si el juego lanza excepciones, aparecerá como `STOP:` y se implementa con los datos reales de `probe.c` |
| `sceKernelInternalMemoryGetModuleSegmentInfo` | igual: trampa hasta ver el sitio de llamada | |

Estas respuestas no son éxito simulado: devuelven un valor definido y se registran en el log la
primera vez que se llaman.

### 3.6 Bloque F — Archivos

- `sceKernelGetdents(fd, buf, nbytes)`: como `sceKernelGetdirentries` pero sin puntero de base. Usa
  `do_getdents` (`src/runtime_file.c:367`) con una base local.
- `sceKernelUtimes(path, times)` y `sceKernelChmod(path, mode)`: traducen la ruta con
  `runtime_file_translate`; `/app0` es de solo lectura (`EROFS`/`EACCES`). Se detalla el valor tras
  ver quién las llama. Montajes escribibles (`/savedataN`, `/temp0`, `/download0`, `/data`): operan
  sobre el archivo del host.

## 4. Fases

- **Fase 2A — guiada por `STOP:` (hasta el hito de primer fotograma).** Ciclo: ejecutar, leer el
  `STOP:`, nombrar la API con la tabla de la sección 2, implementarla con su test y repetir. El orden
  esperado es: Bloque A → event flags → lo que salga. Si el siguiente `STOP:` es de la Spec 3
  (`Sysmodule`, `SystemService`…), se implementa **solo lo que bloquea** y se anota en la Spec 3.
- **Fase 2B — cierre.** Con el juego ya arrancando, implementar el resto de la lista hasta que
  `--check-imports` no liste nada de `libkernel`.

### 4.1 Fase 2A: registro de iteraciones

Antes de la fase 2A, la ejecución real ya pasaba la init de `libc.prx` y de `libSceFios2.prx` y entraba
en el código del juego (`0xef390`, pasos 2.5 y 2.7 del plan). Las iteraciones del bucle, cada una con
su `STOP:`, su sitio de llamada, la respuesta, un test y un commit (logs `out/run-2a-N.log`):

| # | `STOP:` | Sitio de llamada **[V]** | Respuesta | Spec | Commit |
|---|---|---|---|---|---|
| 1 | `SsC-m-S9JTA` `sceSystemServiceParamGetString` | eboot `+0x129991`: `(6, obj+4, 0x41)` tras `ParamGetInt(1)`; ignora el resultado | `"PS4"` para el id 6 (nombre del sistema); `0x80A10003` para otro id o buffer corto | 3 (4.1) | `6e40c0e` |
| 2 | `hHrGoGoNf+s` `sceSysmoduleLoadModuleInternalWithArg` | Fios2 `+0x394a5`: `(0xd7, 0, NULL, 0, &res)`; si falla después, `sceSysmoduleUnloadModule(0xd7)` | igual que `sceSysmoduleLoadModule`, `*res = 0`; ids de más de 16 bits → `STOP:` | 3 | `673559f` |
| 3 | `VjhsmxpcezI` `sceAudiodecInitLibrary` | eboot `+0x10e20c`: `(1)` (ATRAC9); ignora el resultado | error provisional `0x807F0001` (fase A de la Spec 4) | **4** | `18ad4dc` |
| 4 | `TywrFKCoLGY` `sceSaveDataInitialize3` | eboot `+0xed2eb`: `(NULL)` | ya existía; faltaba la línea de `import_names.inc` | 3 | `2a43953` |
| 5 | `isruqthpYcw` `sceSharePlayInitialize` | eboot `+0xed32e`: `(NULL, 0x1800)`; ignora el resultado | "sin efecto": 0, una línea de log y un contador | 3 (4.1) | `1f701d3` |
| 6 | `kvYEw2lBndk` `sceGameLiveStreamingInitialize` | eboot `+0xed338`: `(0x4000)`, justo después de Share Play; ignora el resultado | "sin efecto" | 3 (4.1) | `93a9b2b` |
| 7 | `0z45PIH+SNI` `sceSaveDataMount2` | eboot `+0x121716`: `{usuario, dir, 0x60 bloques, modo 1 = solo lectura}`; con `0x809F0008` (sin partida) toma su camino de "partida nueva" | ya existía; faltaba la línea de `import_names.inc` | 3 | `7b14f96` |
| 8 | `fJgP+wqifno` DiscMap, sin nombre | Fios2 `+0x3411a`: `(ruta, 0, n, &ubicación, &lba, &tamaño)`, la "LBA package location" del archivo; con un resultado ≠0 usa su camino de reserva | `sceDiscMap_7C980FFB0AA27E7A` (los bytes del NID, como su gemela `ioKMruft1ek`): `NO_BITMAP_INFO` (`0x81100004`) sin tocar las salidas | 3 (4.3) | `d80dea5` |
| 9 | `tIYf0W5VTi8` `sceScreenShotDisable` | eboot `+0xee582`, dentro de la política de captura del juego (`+0xee550`), que llama a seis APIs seguidas e ignora todos los resultados | las seis "sin efecto" en una sola iteración | 3 (4.1) | `61ec55c` |
| 10 | `VwadwBBBJ80` `sceSaveDataUmountWithBackup` | eboot `+0x1214e2`, sobre la partida `savedata000` que el juego acaba de crear; ignora el resultado | desmonta como `sceSaveDataUmount` y no guarda copia; una línea de log | 3 (4.4) | `991fc5d` |

**Punto de parada (iteración 10).** Después de la iteración 10 no aparece ningún `STOP:` en 60 s. El
watchdog de `GOW3_TIMEOUT=60` corta el proceso con el juego vivo: no hay cuelgue. Con
`GOW3_GPU_LOG=debug` (`out/run-2a-10c.log`) se ve lo siguiente:

- el juego abre `VideoOut` nada más entrar en su código: 1920×1080, 3 búferes, `A8R8G8B8Srgb`;
- empieza a llamar a `sceGnmSubmitAndFlipCommandBuffers` justo después de ocultar el splash y montar
  la partida, y sigue a ~60 por segundo (2344 envíos en ~40 s);
- después abre `image/movie/prologue_en.mp4` con AvPlayer (una pista de audio y una de vídeo) y su
  bucle de tareas sigue girando.

Este es el punto de parada de la fase 2A y el resultado (a) del H1 del hito de primer fotograma. Falta
saber si esos envíos contienen draws y qué se ve en pantalla: el log no tiene contadores (H2 del
hito) ni captura de píxeles (H3).

Conclusiones de las cinco iteraciones (2026-10-17):

- **Ningún `STOP:` de `libkernel`** desde que el juego entró en su código: todo lo que bloquea es de la
  Spec 3 y, una vez, de la Spec 4. Hasta aquí, el kernel que hay basta para el arranque.
- **El audio va antes que el primer fotograma.** La iteración 3 para en Audiodec antes de cualquier
  draw. El roadmap suponía que el hito de primer fotograma solo dependía de esta spec. Mientras el
  juego ignore lo que devuelve Audiodec, basta la respuesta provisional de la fase A de la Spec 4. El
  hito tampoco ha llegado aún: el log no muestra ningún envío a la GPU ni un `VideoOut` abierto por el
  juego (solo las líneas de arranque del propio runtime). *Corregido en la iteración 10: el juego sí
  abría `VideoOut` desde la entrada (antes de Audiodec). `sceVideoOutOpen` se registra a nivel `info`
  y, con el nivel por defecto (`warn`), no sale en el log.*
- Hilos y sincronización hasta el `STOP:` de la iteración 5: 39 hilos guest (ninguno ha salido),
  61 mutex, 8 variables de condición, 3 rwlocks, 0 semáforos. Sin cuelgues: con `GOW3_TIMEOUT=60`,
  las cinco ejecuciones terminan en su `STOP:` sin que salte el watchdog.

Conclusiones de las iteraciones 6 a 10 (2026-10-17):

- **Tampoco hay ningún `STOP:` de `libkernel`.** Las diez iteraciones paran en la Spec 3 (nueve) y en
  la Spec 4 (una). El kernel que hay basta para llegar a presentar fotogramas.
- **Hay dos caminos de arranque, según exista partida o no:**
  - Primer arranque: `Mount2` en solo lectura devuelve `NOT_FOUND` y el juego crea `savedata000`
    (la monta con `CREATE`, escribe el archivo `savedata000`, `SaveIcon` y `UmountWithBackup`).
  - Con partida: la monta en solo lectura (dos veces).

  Los dos llegan al vídeo. La ejecución de la iteración 9 creó una partida en `user/savedata/1/CUSA47999`
  y se paró antes de desmontarla; desde entonces, `run.py` sin más arranca por el segundo camino. Para
  repetir el primer arranque hay que apuntar `GOW3_USER_DIR` a una carpeta vacía (así se verificó la
  iteración 10, con `out/user-2a-10`).
- Hilos nuevos tras la init: `task0`, `task_game`, `load thread` (tres), `troph`, `save thread` y
  `av audio`. El bucle de tareas despierta ~120 veces por segundo (`task event` + semáforo 1).
- Dos decisiones de esta tanda:
  - DiscMap devuelve `NO_BITMAP_INFO` y no "OK con ceros" como shadPS4 upstream, porque eso sería una
    ubicación inventada. Si Fios2 rinde mal por ello, es una línea.
  - La iteración 9 resuelve seis APIs de una vez: van seguidas en la misma función, sin ramas entre
    ellas, y el juego ignora todos los resultados.

**Después del punto de parada (2026-10-17, `out/run-prologo.log`, sin watchdog, con partida).** El
juego reproduce el vídeo del prólogo (`prologue_en.mp4`, 6 min 29 s) y, según el usuario, después se
para:

```
STOP: first unsupported PS4 import: O3f1sLMWRvs#libSceAudiodec (index 123)
Caller return offset: 0x113177; first argument: 0x1006eb2048
```

- Es `sceAudiodecCreateDecoder(ctrl, 1)` desde eboot `+0x113172`, el primer decodificador ATRAC9. No
  es de libkernel: la fase 2A tampoco encuentra ninguno aquí.
- **No se implementó** (decisión del usuario: solo documentar). El sitio de llamada y la estructura
  de control están en la Spec 4, sección 2.5.
- Contadores al final (6 min 16 s de proceso):
  - 45 hilos guest, de los que salieron 3;
  - 80 event flags (42 borrados), con 90021 esperas y ningún timeout;
  - 44990 adquisiciones de semáforo;
  - 4 puertos de audio y 174036 búferes enviados;
  - 22495 lecturas del mando.

  No hubo `Fault:` ni cuelgue.
- **El prólogo no se pudo saltar** (`out/run-skip-1.log`, con partida). Se pulsaron Options, Cruz y
  Círculo durante 0,3 s por `GOW3_PAD_FILE`, entre los 20 y los 60 s de ejecución. El juego lee el mando
  todo el tiempo, pero el vídeo siguió. **[H]** Es obligatorio la primera vez, o hace falta otro
  botón o una pulsación más larga. Hasta saberlo, cada prueba de lo que va después del prólogo cuesta
  6 min 29 s.

## 5. Pasos de verificación (antes de escribir código de cada bloque)

Método común: `objdump -D -b binary -mi386:x86-64 -M intel` sobre `out/boot-linked.bin` (la
imagen está al final del archivo; un desplazamiento de imagen es `archivo - (tamaño - longitud de
la imagen)`; libc empieza en `+0x2090000` de la imagen) y `tools/got_import.py --linked <slot>` para
nombrar el import de una ranura de GOT.

1. **Callbacks de hilo. ✅ (2026-10-17)** Nota:
   - La init de libc en `+0x780` llama, en orden, a `_sceKernelSetThreadDtors(+0xb5e90)`,
     `_sceKernelSetThreadAtexitCount(+0xb6170)` y `_sceKernelSetThreadAtexitReport(+0xb63a0)`. Si
     una init interna falla, después llama a `sceKernelDebugRaiseException(0xa0020013, 0)`.
   - `+0xb5e90` es `void (void)`: no lee argumentos. Toma el mutex de libc `+0x128f48`, recorre la
     lista `+0x128f50` y ejecuta las entradas cuyo hilo es `pthread_self()`. → se llama en el hilo que
     termina.
   - `+0xb6170` es `int (int module)`: cuenta las entradas de un módulo. `+0xb63a0` es
     `void (int module)`: las imprime. Solo sirven para descargar un módulo.
   - Rtld Increment/Decrement los llama `+0xb5c70`, el registro de un `atexit` por hilo (equivalente a
     `__cxa_thread_atexit(dtor, obj, dso)`): Increment(`dso`) al entrar (si no devuelve 0, el
     registro devuelve -1) y Decrement(`dso`) en los caminos de error (sin memoria).
   - Hilo principal: no sale por `host_start`; ver la sección 3.1.
2. **Event flags. ✅ (2026-10-17)** Tabla de la sección 3.2 corregida con shadPS4 upstream.
3. **Uso real.** Con `GOW3_TRACE_EVENTFLAG` ver con qué argumentos llama el juego (atributos,
   patrones, timeouts) y cuántos hilos esperan a la vez.
   Primeros datos, sin traza (2026-10-17, `out/run-2a-5.log`): 69 flags, **todos con `attr = 0x22`
   (PRIO + MULTI) y patrón inicial 0**: `scheduler event` (1), `waveout thread` (2), `at9 event` y
   `at9 thread` (32 de cada), `hevag decode thread` y `hevag waveout thread` (1 de cada). Hay 36
   borrados, 36 esperas, 36 `Set`, ningún timeout y ningún `Cancel`. Como son PRIO, se atienden en
   FIFO (sección 3.2). Faltan los argumentos de `Wait` (modos y timeouts): hay que repetirlo con
   `GOW3_TRACE_EVENTFLAG`.
   Con traza (2026-10-17, iteración 10, `out/run-2a-10b.log`, 60 s): los flags nuevos (`task event`,
   `task0`, `task_game`, `load thread`, `troph`, `save thread`, `av audio`) también son `0x22` con
   patrón 0. **Todas las esperas bloqueantes usan `bits = 0x1` y modo `0x11` (AND + CLEAR_ALL)**:
   ~7100 de `task event` (el bucle de tareas, que alterna con el semáforo 1) y unas pocas de los
   hilos de audio y del planificador. No se usan OR, CLEAR_PAT ni varios bits. Quedan sin medir los
   timeouts (la traza no los imprime y el watchdog corta antes de `runtime_report`) y cuántos hilos
   esperan a la vez.
4. **`scePthreadCancel`.** Averiguar quién la llama y a qué hilo. Decidir entonces la estrategia.
5. **Conversión de hora y sanitizer.** Mirar el sitio de llamada para fijar firma y valor devuelto.
6. **Bloque F.** Mirar quién llama `Utimes` y `Chmod` y sobre qué rutas.
7. **`NhpspxdjEKU`. ✅ (2026-10-17)** Es `_nanosleep` (NID calculado con `nid()`). libc lo llama en
   bucles de espera activa con `(&timespec, NULL)` (`+0x277f5`, `+0xbf745`, `+0xbf805`). Mismo
   contrato que `nanosleep`: reutiliza `posix_nanosleep` de `src/runtime_kernel.c`.

## 6. Pruebas

`tests/test_eventflag.c` (modelo: `tests/test_sema.c`), compilado y ejecutado desde
`build.sh --test`, con envoltorio `tests/test_eventflag.py`. Casos mínimos:

1. Ciclo de vida: parámetros inválidos, handle viejo, borrado doble (códigos de la sección 3.2).
2. AND y OR: cumplida de inmediato, no cumplida con timeout 0, cumplida tras `Set`.
3. `CLEAR_ALL` y `CLEAR_PAT`: patrón resultante y `result_pattern`.
4. Timeout con tiempo restante actualizado; timeout 0.
5. Varios hilos: dos esperas con patrones distintos, un `Set` que cumple solo una; un `Set` que
   cumple las dos con `CLEAR_PAT` solapado (el orden de la limpieza importa).
6. `Cancel`: despierta todos, devuelve el número, deja el patrón pedido.
7. `Delete` con esperas activas: todas reciben el error de borrado; el objeto se libera una vez.
8. `SINGLE`: segunda espera falla.
9. Estrés: 8 hilos, 100 000 `Set`/`Wait` sin pérdida ni bloqueo (con `--timeout`).

Más: `tests/test_runtime.c` o `tests/test_services.c` para las respuestas explícitas (Bloque E) y un
test de `pthread_rwlock_*` con nombre POSIX. Ejecución real: `GOW3_GAME_DIR=Tsukihime/CUSA47999
python3 run.py` con `--timeout` y el log bajo `out/` (el `/tmp` del sandbox no es visible en el
contenedor).

## 7. Archivos

Nuevos: `src/runtime_eventflag.c`, `tests/test_eventflag.c`, `tests/test_eventflag.py`.
Modificados: `src/runtime.h` y `src/runtime.c` (resolver y `runtime_report`),
`src/import_names.inc` (29 nombres), `tools/got_import.py` (`--linked`), `src/runtime_thread.c` (callbacks de salida de hilo,
`scePthreadGetname`), `src/runtime_rwlock.c` (variantes POSIX), `src/runtime_kernel.c` (conversión de
hora y respuestas explícitas), `src/runtime_file.c` (Getdents, Utimes, Chmod), `build.sh` (test
nuevo). Windows: comprobar si `gpu/CMakeLists.txt` enumera `runtime_*.c` explícitamente.

## 8. Criterios de aceptación

- [x] `out/eventflag-test` pasa con los 9 casos; el estrés no se cuelga ni pierde señales
  (2026-10-17: 4 pares de hilos, 100 000 idas y vueltas en ~0,4 s; prueba de mutación del orden de
  limpieza, el resultado antes de limpiar, SINGLE, Cancel y Delete: todas detectadas).
- [x] `bash build.sh --test` y `python3 -m unittest discover -s tests` siguen en la línea base
  (2026-10-17, tras la iteración 10 de la fase 2A: todos los tests C pasan; Python 244 OK, 37 omitidos).
- [x] El juego pasa de `libc` a ejecutar su propio código (el log muestra su primera llamada)
  (2026-10-17: `Entering original x86-64 code at guest offset 0xef390` y, después, sus llamadas a
  SystemService, Sysmodule, UserService, AudioOut…; ver 4.1).
- [x] Se llega al hito de primer fotograma sin `STOP:` de `libkernel` (2026-10-17, iteración 10: el
  juego presenta ~60 fotogramas por segundo y reproduce el vídeo del prólogo. La captura, los
  contadores y la decisión son del hito).
- [ ] Fase 2B: `out/gow3-probe out/boot-linked.bin --app0 Tsukihime/CUSA47999 --check-imports` no lista
  nada de `libkernel`, salvo lo que esta spec deje explícitamente como trampa (por ejemplo
  `scePthreadCancel`) con su motivo anotado.
- [ ] Cada respuesta explícita escribe una línea de log la primera vez que se llama.

## 9. Riesgos y preguntas abiertas

- Una semántica de event flags mal copiada produce cuelgues aleatorios, justo el riesgo "Alto" del
  roadmap. Mitigación: el paso 5.2 antes de escribir código, el test 5 (orden de limpieza) y el de estrés.
- Los callbacks de hilo se ejecutan código del juego en el camino de salida de un hilo del host: hay
  que cuidar que el TCB y el TLS sigan vivos en ese momento.
- **Decidido (2026-10-17, usuario):** `sceKernelIsNeoMode` devuelve 0 (PS4 base). Si el juego tuviera
  un camino mejor para Neo, no importa para la Alpha.
- **Decidido (2026-10-17, usuario):** `scePthreadCancel`, `GetModuleInfoForUnwind` y
  `InternalMemoryGetModuleSegmentInfo` quedan como trampa (`STOP:`) hasta que el juego las llame, en vez
  de implementarlas "por si acaso". Es la opción que respeta la regla de no fingir éxito.
