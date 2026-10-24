# Spec 5 — Vídeo: AvPlayer

Fecha: 2026-10-14. Estado: **en curso**: la tarea 1 del plan (trazador y contrato) está cerrada desde el
2026-10-24 (§9); la siguiente es el arnés de la tarea 2. Roadmap: `docs/tsuki/ROADMAP.md`, sección "Spec 5".
Depende de: Specs 2 y 3 (el juego tiene que llegar a pedir un vídeo).

Marcas: **[V]** comprobado; **[H]** hipótesis a confirmar con el primer vídeo.

## 1. Objetivo

Que los 134 vídeos de `image/movie/` (opening, títulos de capítulo, finales, créditos) se reproduzcan
completos y sincronizados con su sonido, que terminen o se salten generando el evento que el juego
espera, y que el juego continúe después.

## 2. Hechos comprobados

- **[V]** El juego usa `sceAvPlayer`, no `sceVideodec`. Dump: 134 `.mp4` en `image/movie/` (4,5 GB),
  con variantes `_en`, `_zc`, `_zt` y sin sufijo (japonés). Existen
  `op1_arc_en.mp4` y `phasetitle_01_00_en.mp4`.
- **[V]** El AvPlayer es el de shadPS4, vendorizado sin el "handle streamer"
  (`gpu/VENDOR.txt`): `gpu/shadps4/core/libraries/avplayer/` (14 archivos). Registra 27 funciones
  (`avplayer.cpp`): `Init/InitEx/PostInit`, `AddSource/AddSourceEx`, `Start/Stop/Pause/Resume`,
  `GetVideoData/GetVideoDataEx`, `GetAudioData`, `StreamCount/GetStreamInfo/EnableStream/DisableStream`,
  `JumpToTime/CurrentTime`, `SetLooping`, `SetAvSyncMode`, `SetTrickSpeed`, `IsActive`, `Close`, entre otras.
  Se registra desde `gpu/shim/gow3gpu.cpp:261` (`Libraries::AvPlayer::RegisterLib`).
- **[V]** El streamer de archivos **no abre rutas por sí mismo**: usa `AvPlayerFileReplacement`
  (`avplayer.h:165`), un conjunto de **callbacks que entrega el juego** (`open`, `close`, `size`,
  `read_offset`; `avplayer_file_streamer.cpp:33`). Es decir, el juego (con su FIOS) es quien lee el
  `.mp4`, y los datos llegan a FFmpeg por esos callbacks.
- **[V]** AvPlayer lanza hilos propios del host (por ejemplo `shadPS4:AvAudioDecoder`,
  `avplayer_source.cpp:826`) y llama desde ellos a callbacks del guest (asignador de memoria, lectura de
  archivo, eventos). Un hilo del host que ejecuta código del guest necesita su TCB: el shim lo hace con
  `runtime_thread_attach_host` (`gpu/shim/core/libraries/kernel/threads.h:14-24`).
- **[V]** El audio de los vídeos lo entrega AvPlayer al juego con `GetAudioData`; el juego lo envía a
  `AudioOut`. El vídeo lo entrega con `GetVideoData` y el juego lo sube a una textura. *Corregido el
  2026-10-24 con la traza:* el juego llama a `GetVideoData`, no a `GetVideoDataEx` (§9).
- **[V] 2026-10-24: contrato real del juego** (tarea 1, `docs/avplayer.md`). Usa `Init`, no `InitEx`, con
  `FileReplacement` y su callback de eventos. Usa `AddSource` con la ruta `/app0/image/movie/…`. Arranca
  desde su callback de `StateReady`, que corre en el hilo del controlador de AvPlayer. `SetLooping(1)`
  solo en `titlebg1.mp4`. Nunca llama a `SetAvSyncMode`, `Pause`, `Resume`, `SetTrickSpeed`,
  `DisableStream` ni a las variantes `Ex`. Sí llama a `JumpToTime`: siempre con
  `CurrentTime() + ~1 000` y seguido de `Stop` en el mismo milisegundo.
- **[V]** ffmpeg completo de RPM Fusion instalado en el contenedor `ps4dev` (`ffmpeg-free` no decodifica H.264).
- **[V]** Primer vídeo real (2026-10-17, fin de la fase 2A de la Spec 2, `out/run-2a-10b.log`): tras
  la init y la política de captura, el juego abre `image/movie/prologue_en.mp4` **con
  `FileReplacement`** ("through the game-provided file replacement"). Tiene dos pistas: audio (0) y
  vídeo (1). El juego activa las dos y AvPlayer emite `StateReady` y `StatePlay`. El juego crea su hilo
  `av audio` (prioridad 714) y abre el puerto de audio 3 (2 canales, float, 256 muestras). Sin
  errores en 60 s. Falta ver si la imagen y el sonido son correctos (hito y paso 2).
- **[V]** Imagen y sonido del prólogo (2026-10-17, después de la fase 2A):
  - las capturas del escritorio (hito, H1) muestran el vídeo bien, con sus subtítulos en inglés, que
    vienen dentro del MP4;
  - el usuario confirma que el sonido se oye bien;
  - sin watchdog (`out/run-prologo.log`), el vídeo se ve entero y, según el usuario, después llega el
    `STOP:` de `sceAudiodecCreateDecoder` (Spec 4, 2.5). No hay error de AvPlayer.
- **[V]** El prólogo no se salta con Options, Cruz ni Círculo (pulsaciones de 0,3 s, Spec 2, sección
  4.1). Es un dato para el caso 3 del paso 2, que todavía no se puede valorar.
- **[V] 2026-10-20: el prólogo solo sale con una partida sin progreso.** La primera vez que el juego
  pasó del vídeo (Spec 4, tarea 7), la partida de `user/savedata/1/CUSA47999/` guardó progreso en la primera ejecución que pasó del prólogo (22:58:10), y desde entonces el arranque va directo al menú de título, sin el vídeo.
  Para las pruebas de esta spec con el prólogo hay que restaurar antes la partida sin progreso
  (`out/savedata-sin-progreso/`, copia de la de antes de esa ejecución) en `user/savedata/`.
- **[V] 2026-10-21: el juego se cuelga en `sceAvPlayerStop` al pasar al título del capítulo 1.** Pasó
  dos de dos veces. La primera fue en la ejecución `a2` de la Spec 4 (tarea 7, `out/run-audiodec-a2.log`,
  ~695 s). La segunda, al reproducirlo a propósito (`out/run-hang-a3.log`).
  - Lo que ve el usuario: el vídeo empieza y se queda parado. El juego ya no responde.
  - Archivo abierto en ese momento: `image/movie/phasetitle_01_00_en.mp4` (`/proc/<pid>/fd`).
  - Antes del cuelgue, el log muestra lo mismo que en los vídeos que funcionan:
    `Stop: Could not stop playback: already stopped` y un hilo `av audio` nuevo. Después, nada.
  - Pilas, volcadas con `SIGALRM` sobre el proceso colgado (el manejador del watchdog de
    `src/probe.c`) y simbolizadas con `addr2line` sobre `libgow3gpu.so`, cargada en `0x7f9de1000000`.
    Las pilas y la lista de hilos están en `out/spec4-t7/a3-stacks.txt` y `a3-hang-snapshot.txt`, sin
    versionar.
    - `task_game` está en `pthread_join`. La llamada es eboot `+0x10225a` → `sceAvPlayerStop` →
      `AvPlayer::Stop` → `AvPlayerState::Stop` → `AvPlayerSource::Stop` (en línea) → `std::thread::join`.
    - De AvPlayer solo queda vivo `shadPS4:AvController`, que duerme en su bucle de 5 ms. No hay ningún
      hilo `AvDemuxer`, `AvVideoDecoder` ni `AvAudioDecoder`, así que el `join` espera a un hilo que ya
      no existe con ninguno de esos nombres.
    - Audiodec no interviene: todas sus llamadas volvieron y no quedaba ningún decodificador vivo.
  - **[H] Causa:** una carrera en `Kernel::Thread` (`gpu/shim/core/libraries/kernel/threads.h`). Al
    acabar, los hilos de AvPlayer hacen `Join()` sobre su propio objeto, que los desvincula (`detach`).
    El demultiplexor (`avplayer_source.cpp:602-607`) y `AvPlayerSource::Stop` (`:316-318`), desde el
    hilo del juego, hacen `Join()` o `Stop()` sobre el mismo `std::jthread` sin ninguna sincronización.
    Si el hilo del juego lee el identificador antes del `detach`, puede hacer `pthread_join` sobre un
    hilo ya desvinculado, cuya memoria de control ha podido reutilizar otro hilo.
    - Apoya la hipótesis que la carrera ya se dio sin colgar: el 2026-10-20 (`out/run-audiodec-a1.log`,
      línea 246) salió `HLE thread: join failed (Invalid argument)` en un cambio de vídeo.
    - Para confirmarla: en otra reproducción, `gdb -p <pid>` con `info threads` y la pila de
      `task_game`. El `pthread_t` que espera el `join` es la dirección del bloque de control del hilo,
      y el volcado da `rdi` = `0x7f9bf7446ce8`, el campo `tid` de ese bloque. Hay que ver de qué hilo
      vivo es.
  - **[V] 2026-10-24, tercera reproducción de tres, con la traza** (`out/run-t1-trace.log`). En
    `phasetitle_01_00_en.mp4` (solo vídeo, 5 005 ms) se entregan los 150 fotogramas. En el
    milisegundo del último, `IsActive()` pasa a falso y el juego llama a `Stop`. El controlador emite
    `StateStop` 3 ms después, y `Stop` tarda 17 135 ms: vuelve solo cuando el watchdog de 60 s vuelca
    las pilas. Pila de `task_game`: la misma que en `a3`, con `rdi=0x7f7887447ce8` (mismo
    desplazamiento `…ce8`). Es el disparador "EOF y `Stop` inmediato" del plan. El prólogo hace lo
    mismo sin colgarse, porque su `Stop` sale por "already stopped" sin llegar a `join`.
- **[V] 2026-10-24: el vídeo NO va más rápido que el tiempo real (la [H] era falsa; §9).** El prólogo
  dura 388 637 ms de pared desde `Start`, con 388 667 de archivo. Llegan todos los fotogramas y bloques,
  sin deriva (±20 ms), y llegan EOF y `StateStop`. El `STOP:` de 2026-10-17 llegó a los ~364 s del
  vídeo: el juego crea su primer decodificador ATRAC9 a los 365,4 s, 23 s antes del final. Datos
  originales de la [H], de `out/run-prologo.log`:

  | Dato | Valor |
  |---|---|
  | Lanzamiento (copia de partidas, justo antes de arrancar) | 13:20:50 |
  | Última línea del log (el `STOP:`) | 13:27:06, es decir, 6 min 16 s de proceso |
  | Inicio del vídeo tras el lanzamiento, en otra ejecución | ~12 s (captura de los 15 s ≈ segundo 3 del MP4) |
  | Tiempo de pared para el vídeo | ~6 min 4 s |
  | Duración del archivo | 6 min 29 s (`ffprobe`), ~25 s más |

  - Si el vídeo terminó antes del `STOP:`, se reprodujo ~6 % más rápido o se saltó algún trozo. Los
    últimos ~25 s del archivo son créditos sobre negro y la luna que se apaga.
  - Tampoco hay `StateStop` en el log, y AvPlayer lo registra a nivel `info` cuando lo emite
    (`avplayer_state.cpp:375`).
  - El aviso de autoguardado que el juego muestra antes del vídeo (hito, H1) no lo explica. Está
    dentro de los ~12 s previos al vídeo, y cualquier pantalla previa resta tiempo de pared al vídeo
    en vez de sumarlo: con 6 min 16 s de proceso, el vídeo no cabe aunque empezara en el segundo 0.
  - O el juego dejó el vídeo antes de su final, o decide el final por su cuenta. Hay que medirlo
    en el paso 2: tiempo de pared contra la marca de tiempo de los fotogramas y del audio entregados.
    Es la fila "Se corta o se desincroniza" del paso 3.

## 3. Diseño: validar antes de corregir

La spec es un ciclo de validación con casos fijos. El AvPlayer de shadPS4 no se reescribe: se arregla
lo que falle con los vídeos reales, marcando cada cambio con `gow3:` (luego `tsuki:`).

### Paso 1 — Qué pide el juego
Con `GOW3_GPU_LOG=info` y un trazador de AvPlayer (`GOW3_TRACE_AVPLAYER=1`, nuevo y pequeño), registrar
cada llamada: la estructura de inicio (`Init/InitEx`, tamaño de las estructuras), si entrega
`FileReplacement` o una ruta, el origen (`AddSource`/`AddSourceEx`), si pide `SetAvSyncMode`, `SetLooping`,
y el orden de `GetVideoDataEx`/`GetAudioData`. Entregable: nota de 15 líneas con el contrato real del
juego.

### Paso 2 — Caso de prueba, de menor a mayor
1. `phasetitle_01_00_en.mp4` (corto): se abre, se reproduce, termina y el juego continúa.
2. `op1_arc_en.mp4` (opening): completo, con vídeo y audio sincronizados.
3. Saltar un vídeo con un botón: el juego cierra el reproductor y sigue.
4. Un vídeo en bucle, si el juego lo usa (títulos).

### Paso 3 — Qué mirar y dónde corregir
| Síntoma | Dónde mirar |
|---|---|
| Se cierra al abrir | Tamaño de las estructuras de `Init/InitEx` del SDK 11.008 frente a las de shadPS4 (la versión de AvPlayer puede haber cambiado) |
| Se queda negro | `GetVideoDataEx`: formato de píxel, dimensiones, alineación del búfer; textura creada por el juego |
| Se corta o se desincroniza | Reloj de AvPlayer (`avplayer_source.cpp`), `SetAvSyncMode`, tiempo de `AudioOut` |
| Cuelgue al terminar o saltar | Evento de fin de flujo (`AvPlayerEvent`), cierre y `Close` desde el guest, hilos de decodificación. *2026-10-21: visto con `phasetitle_01_00_en.mp4`, en `sceAvPlayerStop`, con un `join` sobre un hilo que ya no existe (§2)* |
| No encuentra el archivo | Los callbacks del juego pasan por su FIOS → `runtime_file.c` (`/app0/image/movie/…`); mirar `open(...) -> not found` en el log |
| Cuelga el hilo del host al llamar al guest | Falta de `runtime_thread_attach_host` en un hilo nuevo de AvPlayer |

## 4. Requisitos

- Cuando el juego abra un vídeo, el sistema debe mostrarlo completo y sincronizado con su sonido.
- Cuando el vídeo termine o se salte, el juego debe recibir el evento correspondiente y continuar.
- Un vídeo inexistente o corrupto no cuelga el juego: se informa con el error de AvPlayer y se registra.

## 5. Pruebas

- Automatizable: un test de integración que abre un MP4 de prueba pequeño (generado con `ffmpeg
  -f lavfi`) a través de los mismos callbacks de `FileReplacement` y comprueba que se obtienen N
  fotogramas y que el evento de fin llega. Si se hace, `tests/test_avplayer.cpp` (nuevo, linkado
  contra la biblioteca de GPU). Si resulta demasiado costoso por el arrastre de dependencias de
  shadPS4, se documenta y se confía en las ejecuciones reales.
- Reales (las juzga el usuario): los tres casos del paso 2, con el recorrido grabado
  (`GOW3_PAD_REPLAY`) para repetir el salto de vídeo.

## 6. Archivos

`gpu/shadps4/core/libraries/avplayer/` (cambios marcados), `gpu/shim/gow3gpu.cpp` (si cambia el
registro), `src/runtime_file.c` (si hay problemas de rutas), `src/runtime_thread.c` (si faltan hilos
por adjuntar). Nuevos: `tests/test_avplayer.cpp` (opcional).

## 7. Criterios de aceptación

- [ ] El opening en inglés (`op1_arc_en.mp4`) se reproduce completo, sincronizado.
- [ ] El título del capítulo 1 en inglés (`phasetitle_01_00_en.mp4`) se reproduce y el juego continúa.
- [ ] Saltar un vídeo no cuelga el juego.
- [ ] El log del recorrido no contiene `STOP:` ni `Fault:` relacionados con AvPlayer.
- [x] Hay una nota con el contrato real del juego con AvPlayer (paso 1). *2026-10-24:* `docs/avplayer.md`,
  con la traza de `out/run-t1-trace.log` y `out/run-t1-prologo.log` (tarea 1, §9).

## 8. Riesgos

- AvPlayer de shadPS4 es la parte menos madura que se reutiliza (riesgo "Medio" del roadmap).
- 4,5 GB de vídeo: el recorrido de la Alpha solo exige dos; el resto se valida por muestreo, no uno a uno.
- Puede que el juego pase una ruta en lugar de callbacks para algunos vídeos; el paso 1 lo aclara.

## 9. Resultados

### Tarea 1 — trazador y contrato del juego (2026-10-24, `c06b6a1`)

- **Trazador `GOW3_TRACE_AVPLAYER=1`** (`avplayer_trace.h`, `avplayer.cpp`, `EmitEvent` en
  `avplayer_state.cpp`). Escribe en stderr una línea `AvTrace +<ms> t<tid>: …` por llamada exportada, con
  sus argumentos y su resultado, y otra por evento. Las cuatro llamadas que el juego sondea cada
  fotograma se cuentan por reproductor, y `Close` da los totales. De ellas solo se trazan:
  - los fotogramas y bloques entregados, con `play=` (ms desde `Start`);
  - los cambios de `IsActive`;
  - un `CurrentTime` por segundo.

  `Stop` y `Close` trazan el inicio y la duración. Sin la variable, cada llamada cuesta la lectura de
  un `bool` estático.
- **Suites, iguales antes y después:**
  - `build.sh --test`: rc 0, con las 17 líneas `PASS`/`ok` de siempre (`out/t1-baseline-build.log`,
    `out/t1-after-build.log`);
  - Python: `Ran 267`, `OK (skipped=35, expected failures=4)`. El plan citaba 255 ejecutados; la cifra
    había crecido con las Specs 3 y 4.

  El trazador no tiene test propio: lo verifican las ejecuciones reales, y el arnés de la tarea 2 lo
  ejercitará.
- **Ejecución a** (`out/run-t1-trace.log`): partida actual, `GOW3_TIMEOUT=60`.
  - Vídeos, en orden: `titlebg1` → `TM_CI` → `titlebg1` → `phasetitle_01_00_en`. El usuario confirma
    que pulsó botones para llegar al capítulo 1.
  - Los tres primeros salen con `JumpToTime` + `Stop`.
  - El cuarto acaba de forma natural y **cuelga `Stop` 17 s**, hasta el watchdog (§2).
- **Ejecución b** (`out/run-t1-prologo.log`): partida sin progreso restaurada, el prólogo entero,
  cierre con SIGTERM a los 440 s. Después se devolvió a `user/savedata` la partida con progreso, que
  queda copiada en `out/savedata-con-progreso/`.
  - Pared contra marcas: la tabla de `docs/avplayer.md` §2.
  - Tras el `Stop` final (`0x806a0002`, ya parado), el juego guarda la partida y sigue. En los 40 s
    siguientes no llama a `Close`.
- **Hilos** (`out/run-t1-prologo-threads.txt`):
  - `1: task_game`: `Init`, `AddSource`, `GetVideoData`, `Stop`, `Close`;
  - `shadPS4:AvContr`: el callback de eventos del juego, que arranca la reproducción;
  - `av audio`: `IsActive` y `GetAudioData` sin pausa.
- **Lo que esto decide para el plan:**
  - Tarea 4: solo `JumpToTime` está en uso, y siempre justo antes de `Stop`, sin pedir datos entre
    medias. `DisableStream`, `SetTrickSpeed` y `ChangeStream` no se llaman y quedan fuera.
  - Tarea 6: el cuelgue se reproduce sin ruta grabada larga, en ~30 s desde el menú de título. El
    disparador es el del plan: fin natural de un vídeo solo vídeo y `Stop` en el mismo milisegundo
    en que `IsActive()` pasa a falso.
  - Tarea 7, paso 2: la medición del prólogo ya está hecha y da el caso (a), la [H] es falsa.
  - Tarea 7, paso 4: el bucle sí se usa (`titlebg1.mp4`).
