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
| `BpFoboUJoZU` | `sceKernelCreateEventFlag` | falta |
| `8mql9OcQnd4` | `sceKernelDeleteEventFlag` | falta |
| `JTvBflhYazQ` | `sceKernelWaitEventFlag` | falta |
| `IOnSvHzqu6A` | `sceKernelSetEventFlag` | falta |
| `PZku4ZrXJqg` | `sceKernelCancelEventFlag` | falta |
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
| `NhpspxdjEKU` | **sin nombre** (166 candidatos probados) | falta |

Total: 29. Esta spec añade a `import_names.inc` los 28 nombres resueltos; el 29.º se resuelve desde
su sitio de llamada (sección 5.7).

## 3. Diseño

### 3.1 Bloque A — Arranque de `libc.prx` (callbacks de hilo)

`libc.prx` llama en su inicialización a `_sceKernelSetThreadDtors` con un puntero de función de su
propia imagen (primer argumento `0x802145e90`). Las otras cuatro (`...SetThreadAtexitCount`,
`...SetThreadAtexitReport`, `...RtldThreadAtexitIncrement`, `...RtldThreadAtexitDecrement`) forman el
mismo mecanismo: el runtime del sistema registra callbacks de libc y los usa al terminar un hilo para
ejecutar los destructores de TLS y los `atexit` por hilo de C++.

- **[H]** Semántica de `_sceKernelSetThreadDtors(callback)`: guarda el callback del proceso; el
  kernel lo llama al terminar cada hilo, antes de liberar su TCB.
- **[H]** `...SetThreadAtexitCount(cb)` y `...SetThreadAtexitReport(cb)` guardan dos callbacks que el
  kernel consulta al terminar un hilo (cuántos `atexit` por hilo quedan y su informe).
- **[H]** `...RtldThreadAtexitIncrement/Decrement(module?)` ajustan un contador de handlers de
  `atexit` por hilo asociado a un módulo, para impedir descargarlo.

Diseño: un objeto de estado en `src/runtime_thread.c` con los tres callbacks guardados (cada uno
protegido por el mismo mutex que el resto de la tabla de hilos). Se llaman desde el camino de salida
de un hilo guest, **en el hilo que termina**, justo donde hoy se llama
`runtime_thread_keys_cleanup()` (`src/runtime_thread.c:260`, función `host_start`), y desde
`thread_exit` (`:320`). El hilo principal no termina por esa vía (`thread_exit` lo rechaza con
`STOP`); su caso queda explícito en el paso 5.1.

Nunca se devuelve éxito sin guardar el callback: si la verificación del paso 5.1 muestra que la
semántica es otra, se detiene con `STOP:` y el motivo, no se ignora.

### 3.2 Bloque B — Event flags (`src/runtime_eventflag.c`, nuevo)

Modelo: igual que los semáforos. Un registro global protegido por un mutex; cada flag tiene su patrón
de 64 bits, atributos, una lista de esperas y un contador de esperas activas para liberar el objeto
cuando se borra con hilos aún dentro.

Handle: el tipo del SDK es un puntero opaco de 8 bytes. Se escribe en el destino un **identificador
de 64 bits creciente** (empezando en 1, nunca reutilizado), validado contra el registro. Así un
handle viejo falla de forma limpia (como en `sema_*`) y no se entrega memoria del host al guest.

Firmas **[H]** (a confirmar en el paso 5.2 con el código de shadPS4 upstream y el uso del juego):

```
int sceKernelCreateEventFlag(SceKernelEventFlag *ef, const char *name, uint32_t attr,
                             uint64_t init_pattern, const void *opt /* NULL */);
int sceKernelDeleteEventFlag(SceKernelEventFlag ef);
int sceKernelSetEventFlag   (SceKernelEventFlag ef, uint64_t bits);
int sceKernelWaitEventFlag  (SceKernelEventFlag ef, uint64_t bits, uint32_t mode,
                             uint64_t *result_pattern /* puede ser NULL */, uint32_t *timeout_us /* puede ser NULL */);
int sceKernelCancelEventFlag(SceKernelEventFlag ef, uint64_t set_pattern, int *num_waiters /* puede ser NULL */);
```

Constantes **[H]**: atributos `TH_FIFO=0x01`, `TH_PRIO=0x02`, `SINGLE=0x10`, `MULTI=0x20`; modos de
espera `AND=0x01`, `OR=0x02`, `CLEAR_ALL=0x10`, `CLEAR_PAT=0x20`.

Reglas de espera **[H]**:

| Caso | Resultado |
|---|---|
| Modo AND | se cumple si `(patrón & bits) == bits` |
| Modo OR | se cumple si `(patrón & bits) != 0` |
| `CLEAR_ALL` | al cumplirse, el patrón pasa a 0 |
| `CLEAR_PAT` | al cumplirse, `patrón &= ~bits` |
| Sin modo de limpieza | el patrón no cambia al cumplirse |
| `result_pattern` | el patrón del evento **antes** de aplicar la limpieza; en timeout, el patrón actual |
| `bits == 0` o modo sin AND/OR, o bits de modo desconocidos | `EINVAL` |
| `timeout_us` | microsegundos; al volver contiene el tiempo restante, y 0 si hubo timeout |
| `timeout_us` apuntando a 0 con condición no cumplida | `ETIMEDOUT` sin bloquear |
| `Set` | `patrón \|= bits`; recorre las esperas en orden y despierta las que se cumplen, aplicando la limpieza de cada una **antes** de evaluar la siguiente |
| `Cancel` | despierta todas las esperas con `ECANCELED`, pone el patrón a `set_pattern` y devuelve cuántas despertó |
| `Delete` con esperas | despierta todas con el error de "borrado" y libera el objeto cuando salga la última |
| Atributo `SINGLE` con una espera activa | segunda espera falla con `EPERM` |
| Atributo `TH_PRIO` | se atiende en FIFO (no hay planificador de prioridades en el host), igual que los semáforos |

Códigos de error del guest (valores `0x8002xxxx`, del archivo `orbis_error.h` vendorizado):
`EINVAL=0x80020016`, `ESRCH=0x80020003` (handle desconocido, como los semáforos), `ETIMEDOUT=0x8002003C`,
`ECANCELED=0x80020055`, `EPERM=0x80020001`, `ENOMEM=0x8002000C`. El error al borrar con esperas
sigue el de los semáforos (`0x8002000D`, probado en `tests/test_sema.c:115`) **[H]**.

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
  `src/runtime_thread.c:47`) al buffer del guest, con terminador y `ERANGE`/`EINVAL` según el tamaño.
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

## 5. Pasos de verificación (antes de escribir código de cada bloque)

1. **Callbacks de hilo.** Desensamblar `libc.prx` en `+0x790` y en el sitio de `_sceKernelSetThread*`
   y `_sceKernelRtldThreadAtexit*`. Entregable: una nota de 10 líneas con la firma del callback, el
   momento en que debe llamarse y qué pasa con el hilo principal. `tools/got_import.py <offset>` da
   el import ligado a una ranura de GOT.
2. **Event flags.** Leer en el repositorio upstream de shadPS4 (no está vendorizado) la
   implementación de event flags: firmas, atributos, modos y códigos de error exactos. Contrastar
   con la tabla de la sección 3.2 y corregirla donde difiera. Entregable: tabla corregida en este
   documento.
3. **Uso real.** Con `GOW3_TRACE_EVENTFLAG` ver con qué argumentos llama el juego (atributos,
   patrones, timeouts) y cuántos hilos esperan a la vez.
4. **`scePthreadCancel`.** Averiguar quién la llama y a qué hilo. Decidir entonces la estrategia.
5. **Conversión de hora y sanitizer.** Mirar el sitio de llamada para fijar firma y valor devuelto.
6. **Bloque F.** Mirar quién llama `Utimes` y `Chmod` y sobre qué rutas.
7. **`NhpspxdjEKU`.** Mismo método que el resto de nombres: sitio de llamada, primer argumento y
   lo que hace el código después con el valor devuelto. Si se identifica, se añade a
   `import_names.inc`.

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
`src/import_names.inc` (28 nombres), `src/runtime_thread.c` (callbacks de salida de hilo,
`scePthreadGetname`), `src/runtime_rwlock.c` (variantes POSIX), `src/runtime_kernel.c` (conversión de
hora y respuestas explícitas), `src/runtime_file.c` (Getdents, Utimes, Chmod), `build.sh` (test
nuevo). Windows: comprobar si `gpu/CMakeLists.txt` enumera `runtime_*.c` explícitamente.

## 8. Criterios de aceptación

- [ ] `out/eventflag-test` pasa con los 9 casos; el estrés no se cuelga ni pierde señales.
- [ ] `bash build.sh --test` y `python3 -m unittest discover -s tests` siguen en la línea base.
- [ ] El juego pasa de `libc` a ejecutar su propio código (el log muestra su primera llamada).
- [ ] Se llega al hito de primer fotograma sin `STOP:` de `libkernel`.
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
