# Spec 5 — Vídeo: AvPlayer

Fecha: 2026-10-14. Estado: **cerrada** el 2026-10-25. Las ocho tareas del plan están hechas (§9), salvo
la revisión adversarial de la tarea 8 (paso 4), que queda pendiente a petición del usuario. Fuera del
juego, el arnés pasa entero y los 134 vídeos se reproducen. En el juego, el título del capítulo 1 termina
y el juego sigue, el opening y el prólogo llegan enteros y a tiempo real, saltar el logo no cuelga y el
bucle del menú de título va a su ritmo. Las cinco casillas de §7 están marcadas.
Roadmap: `docs/tsuki/ROADMAP.md`, sección "Spec 5".
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
  `CurrentTime() + ~1 000` y seguido de `Stop` en el mismo milisegundo. *Era un stub que devolvía 0;
  desde la tarea 4 salta de verdad (§9).*
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
  - **[V] 2026-10-24: reproducido fuera del juego** con `avplayer-test` (tarea 2, §9), sin juego ni
    watchdog. Confirma la [H] y la precisa:
    - A un mismo hilo de AvPlayer se le hace `join` desde dos sitios: el demultiplexor al llegar a EOF
      (`avplayer_source.cpp:602-603`) y `AvPlayerSource::Stop` desde el hilo del juego (`:316-318`).
      El hilo unido, al terminar, se desvincula a sí mismo.
    - El segundo `pthread_join` (glibc 2.43) a veces devuelve `EINVAL`, el `join failed (Invalid
      argument)` de `a1`. Otras veces espera para siempre. No hace falta que otro hilo reutilice el
      bloque de control: en el cuelgue solo viven el hilo del test y el controlador, como en `a3`.
    - `gdb` sitúa a `Stop` en el `join` del hilo de audio (`:317`) o del de vídeo (`:316`).
    - La espera solo se rompe si algo la interrumpe: el volcado del watchdog en el juego, el `attach`
      de `gdb` en el test.
  - **[V] 2026-10-24: arreglado fuera del juego** (tarea 3, `1442f25`, §9).
    - Ahora un solo llamador saca el `std::thread` de `Kernel::Thread`, bajo un mutex, y lo une. Los
      demás esperan a que la función vuelva.
    - `kernel-thread-test` y los tres casos del arnés que se colgaban pasan.
    - *Confirmado en el juego en la tarea 6 (siguiente punto).*
  - **[V] 2026-10-24: confirmado en el juego** (tarea 6, §9). Con una ruta grabada desde el menú de
    título, tres de tres ejecuciones:
    - se entregan los 150 fotogramas;
    - en el milisegundo en que `IsActive()` pasa a falso, el `Stop` del juego devuelve `0x806A0002` en
      0 ms, y `StateStop` llega 2–4 ms después;
    - la escena siguiente sale en pantalla;
    - no hay `STOP:`, `Fault:` ni `join failed`.
- **[V] 2026-10-24: en un vídeo sin audio, el fin no llega como evento** (tarea 2).
  - El decodificador de audio arranca aunque no haya pista de audio, y espera paquetes para siempre
    (`avplayer_source.cpp:829-831`). Por eso el demultiplexor se queda en su `join` (`:603`) y nunca
    llama a `OnEOF`.
  - `IsActive()` pasa a falso, pero `StateStop` solo llega después del `Stop` del juego: en
    `phasetitle`, 3 ms después.
  - Con audio (`tiny_av.mp4`), `StateStop` sí llega al final por sí solo.
  - **Causa precisa y arreglo** (tarea 3, `3d33791`):
    - el demultiplexor avisa a los decodificadores al llegar a EOF, pero su espera de paquetes solo
      miraba si había paquetes;
    - ahora también mira `m_is_eof`, y `StateStop` llega 7–12 ms después del último fotograma;
    - el `Stop` del juego que viene después devuelve `0x806A0002`, como en el prólogo.
- **[V] 2026-10-24: `Start` con la reproducción en curso aborta el proceso** ("Resource deadlock
  avoided", tarea 2). `AvPlayerState::Start` toma el mutex del origen en exclusiva y llama a `Stop`,
  que lo pide compartido (`avplayer_state.cpp:178-179, 282`). El juego no lo hace (tarea 1).
  - *Arreglado en la tarea 3* (`aeaf4ee`): `Start` llama a `StopLocked`, que no toma el mutex.
  - Ahora el segundo `Start` devuelve 0 y la reproducción vuelve a empezar desde 0.
- **[V] 2026-10-24: `Stop` rebobinaba el lector antes de parar los hilos**
  (`m_up_data_streamer->Reset()`, `avplayer_source.cpp:313`). Era [H] en la tarea 2.
  - Si el demultiplexor estaba leyendo, seguía desde el byte 0, y el decodificador recibía la cabecera
    del archivo como datos: `[h264] Invalid NAL unit size (-618261609 > 243)`, unas 20 líneas por
    cada `Stop` justo después de `Start`.
  - Si la cola estaba llena y el demultiplexor dormía, no salía ninguna.
  - *Confirmado y arreglado en la tarea 3* (`e20fc9c`), con un oráculo nuevo del arnés: una queja del
    decodificador H.264 hace fallar el caso.
    - Con el `Reset()`: 450 errores en `stop_right_after_start_and_twice` y 5 352 en
      `repeated_open_close`. Sin él, 0.
    - No hace falta rebobinar: un `Start` posterior busca con `avformat_seek_file`, que mueve la
      posición a través del `AVIOContext`.
- **[V] 2026-10-24: archivos que fallan** (tarea 2).
  - Un archivo inexistente y 1 KB de basura hacen que `AddSource` devuelva `0x806A0002`.
  - Con una lectura que falla a mitad, el demultiplexor registra `Could not read AV frame: error = -1` y
    pasa a `Error`; `IsActive()` pasa a falso a los 1,4 s.
  - No cuelga, pero el juego no recibe ningún evento de error, solo `Ready` y `Play`. El requisito 3
    de §4 ("se informa con el error") se cumple solo en el log.
  - *Decisión (tarea 3): se deja así.*
    - El juego ve `IsActive()` falso, su `Stop` devuelve 0 y recibe `StateStop`: sigue por su camino de
      siempre.
    - No se añade un evento de error, porque no sabemos si el AvPlayer real lo emite.
- **[V] 2026-10-24: el bucle se congelaba en la primera vuelta** (tarea 4). `titlebg1.mp4`, el fondo
  del menú de título, va en bucle (`SetLooping(1)`).
  - Al llegar a EOF, el demultiplexor hacía `avio_seek(pb, 0)` antes de `avformat_seek_file`
    (`avplayer_source.cpp`). Con el `AVIOContext` de `FileReplacement`, el demultiplexor MOV releía el
    `moov` ("Found duplicated MOOV Atom"). Desde ahí cada lectura daba EOF y cada búsqueda fallaba con
    -1. Con una ruta de archivo no pasa (`out/t4-exp/loop2.c`, sin versionar).
  - El vídeo se quedaba parado y el demultiplexor giraba sin pausa, con un `WAR_LOOPING_BACK` por giro:
    12,7 millones en 2 s con `tiny_v20.mp4`, todos al callback del juego.
  - En el juego ya pasó: `out/run-audiodec-a1.log:1280` (2026-10-20) tiene el "duplicated MOOV Atom",
    unos 345 s después de abrirse el audio. Sin log `info` no se ve el giro.
  - *Arreglado en la tarea 4* (`1ccd0f3`): sin el `avio_seek`, sale un aviso por vuelta.
  - **Quedaba para la tarea 7, paso 4:** en un vídeo sin audio, `CurrentTime()` se queda en la duración
    tras la primera vuelta. Las vueltas siguientes salen sin ritmo: 5 480 fotogramas en 2 s de
    `tiny_v20.mp4`, en vez de unos 60. *Visto y arreglado en la tarea 7 (siguiente punto).*
- **[V] 2026-10-24: el fondo del menú de título iba 3,3 veces más rápido desde su segunda vuelta**
  (tarea 7, §9). En 11 minutos sin entrada, cada vuelta de `titlebg1.mp4` (120 s) duró 36,3 s desde la
  segunda, con sus 3 600 fotogramas y `CurrentTime` clavado en 120 000.
  - Causa: sin audio, el reloj es el tiempo de pared desde `Start` limitado a la duración
    (`avplayer_source.cpp`, `CurrentTime`). Los fotogramas de la segunda vuelta vuelven a marcar 0, así
    que todos vencían a la vez y salían en cuanto el juego los pedía. Con audio no pasa: ese reloj sigue
    al último bloque de audio, cuya marca también vuelve. Los cuatro `titlebg` son solo vídeo.
  - *Arreglado* (`d8fd321`): una marca que retrocede empieza la vuelta siguiente, sus fotogramas vencen
    una duración más tarde y `CurrentTime` descuenta las vueltas entregadas. Caso
    `loops_back_on_the_clock`: rojo con 7 472 fotogramas en 2,5 s, verde con 75.
  - En el juego, después del arreglo (`out/run-t7-loop-2.log`), la primera vuelta dura 120,0 s y las
    siguientes 112,0 s, a tiempo real. Al empezar cada vuelta el juego salta él solo a 8 033 ms
    (`JumpToTime`, y luego `WAR_JUMP_COMPLETE`) y se salta los 8 s de entrada, que empieza desenfocada.
    El último fotograma se parece al de 8 033 ms (SSIM 0,989) y no al primero (0,789): es el punto de
    bucle del vídeo. Antes del arreglo no saltaba nunca, aunque también recibía el fotograma 0 de cada
    vuelta: lo decide con `CurrentTime`, que se quedaba en 120 000 (deducido de las dos ejecuciones).
  - La memoria no crece (VmRSS entre 1 388 904 y 1 399 792 kB en las dos ejecuciones de 11 min), y el
    juego sigue en el menú sin `STOP:` ni `Fault:`.
- **[V] 2026-10-24: el vídeo NO va más rápido que el tiempo real (la [H] era falsa; §9).** El prólogo
  dura 388 637 ms de pared desde `Start`, con 388 667 de archivo. Llegan todos los fotogramas (menos
  el último, que el decodificador retenía: tarea 5) y bloques, sin deriva (±20 ms), y llegan EOF y
  `StateStop`. El `STOP:` de 2026-10-17 llegó a los ~364 s del vídeo: el juego crea su primer
  decodificador ATRAC9 a los 365,4 s, 23 s antes del final. Datos originales de la [H], de
  `out/run-prologo.log`:

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
  - *Tarea 7:* con el arreglo de la tarea 5, el prólogo da 11 660 de 11 660 fotogramas en el juego,
    con la misma pared (388 637 ms hasta `StateStop`) y sin deriva.
- **[V] 2026-10-24: el opening en el juego** (tarea 7, §9). `op1_arc_en.mp4` lo pide la historia, no un
  menú: `_TOMP(5)` en el guion de la entrada 90 de `allscr.mrg`, tras la elección "...will help her."
  del callejón, a unas 5 900 líneas de texto del título del capítulo 1. El usuario llegó jugando.
  - Llegan los 3 211 fotogramas y 5 023 bloques de audio (los que decodifica `ffmpeg`), con pared −
    marca de −16 a +15 ms en 107 s y el último audio entregado a −21…+17 ms de cada fotograma.
  - `StateStop` a los 107 121 ms, y el `Stop` del juego devuelve `0x806A0002` en 0 ms.
  - El usuario lo vio y lo oyó entero y sincronizado ("no vi nada raro").
  - Confirma la [H] de la tarea 5: los 835 ms de `late` del arnés eran de su consumidor de audio.
- **[V] 2026-10-24: `TM_CI` se salta con Cruz, tres de tres** (tarea 7, §9). Una Cruz 1,5 s después de su
  `AddSource`: el juego llama a `JumpToTime(actual + ~1 s)` (1–3 ms) y a `Stop` (0 ms), llega
  `StateStop`, el segundo `Stop` da `0x806A0002` y el juego sigue hasta el menú de título. El aviso de fin
  de salto (`0x806A00A3`) llega 3–4 ms después de `StateStop`, y al juego no le importa.
- **[V] 2026-10-24: orden de arranque sin entrada** (tarea 6, cinco ejecuciones con la partida actual).
  - `titlebg1` sale sola a los 1,1 s: el propio juego llama a `JumpToTime` y a `Stop`.
  - `TM_CI` (con audio) llega a su final: `StateStop` a los 6 s, antes del `Stop` del juego, que
    devuelve `0x806A0002`.
  - Siguen la advertencia legal y `titlebg1` en bucle con el menú de título, ~18 s después del primer
    vídeo.
  - *Corrige la tarea 1:* allí `TM_CI` duró 2,5 s porque se saltó con una pulsación. La salida de
    `titlebg1` a los 1,1 s no responde a ninguna pulsación.
- **[V] 2026-10-24: en el juego, a 1920×1088, `JumpToTime` tarda 0–5 ms** (11 llamadas de la tarea 6). El
  `Stop` que lo sigue tarda 0 o 15 ms.
- **[V] 2026-10-24: los 134 vídeos** (`tools/probe_movies.sh`, `docs/avplayer.md` §6). Son todos H.264
  1920×1080 `yuv420p` a 30 o 29,97 fps fijos. 91 son solo vídeo y 43 llevan AAC LC estéreo a 48 kHz;
  en 37 de ellos el audio es la primera pista. 75 tienen fotogramas B (`has_b_frames` 1 o 2), que los
  fixtures no tenían.
- **[V] 2026-10-24: con fotogramas B se perdían los últimos fotogramas** (arreglado, `a0b2c86`). El
  decodificador de vídeo salía en EOF sin el paquete nulo que lo vacía, y H.264 retiene tantos
  fotogramas como reordena. Les faltaba el último a los 64 vídeos de ≤ 10 s con `has_b_frames=1`, y
  también al prólogo de la tarea 1 (11 659 de 11 660).

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
| Cuelgue al terminar o saltar | Evento de fin de flujo (`AvPlayerEvent`), cierre y `Close` desde el guest, hilos de decodificación. *2026-10-21: visto con `phasetitle_01_00_en.mp4`, en `sceAvPlayerStop`, con un `join` sobre un hilo que ya no existe (§2). 2026-10-24: arreglado fuera del juego (tarea 3, §9) y confirmado en el juego, tres de tres (tarea 6). Saltar `TM_CI` con Cruz no cuelga, tres de tres (tarea 7)* |
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
  shadPS4, se documenta y se confía en las ejecuciones reales. *2026-10-24:* hecho
  (`tests/test_avplayer.cpp`, tarea 2, §9).
- Reales (las juzga el usuario): los tres casos del paso 2, con el recorrido grabado
  (`GOW3_PAD_REPLAY`) para repetir el salto de vídeo.

## 6. Archivos

`gpu/shadps4/core/libraries/avplayer/` (cambios marcados), `gpu/shim/gow3gpu.cpp` (si cambia el
registro), `src/runtime_file.c` (si hay problemas de rutas), `src/runtime_thread.c` (si faltan hilos
por adjuntar). Nuevos: `tests/test_avplayer.cpp` (opcional).

## 7. Criterios de aceptación

- [x] El opening en inglés (`op1_arc_en.mp4`) se reproduce completo, sincronizado. *2026-10-24:* en el
  juego, 3 211 de 3 211 fotogramas y 5 023 bloques sin deriva (`out/run-t7-op-1.log`), y el usuario lo
  vio y oyó sincronizado (tarea 7, §9).
- [x] El título del capítulo 1 en inglés (`phasetitle_01_00_en.mp4`) se reproduce y el juego continúa.
  *2026-10-24:* tres de tres ejecuciones con la ruta grabada (`out/run-t6-{1,2,3}.log`), con 150 de 150
  fotogramas, `StateStop` y la escena siguiente en las capturas (tarea 6, §9).
- [x] Saltar un vídeo no cuelga el juego. *2026-10-24:* `TM_CI` con Cruz, tres de tres
  (`out/run-t7-skip-tmci-{1,2,3}.log`), más el salto del usuario en la tarea 6; el juego sigue hasta el
  menú de título (tarea 7, §9).
- [x] El log del recorrido no contiene `STOP:` ni `Fault:` relacionados con AvPlayer. *2026-10-25:* el
  recorrido final con la ruta grabada (`out/run-t8-route.log`) no tiene `STOP:`, `Fault:`, `join failed`
  ni `<Error>` de AvPlayer; los logs de las tareas 6 y 7 tampoco (tarea 8, §9).
- [x] Hay una nota con el contrato real del juego con AvPlayer (paso 1). *2026-10-24:* `docs/avplayer.md`,
  con la traza de `out/run-t1-trace.log` y `out/run-t1-prologo.log` (tarea 1, §9).

## 8. Riesgos

- AvPlayer de shadPS4 es la parte menos madura que se reutiliza (riesgo "Medio" del roadmap).
- 4,5 GB de vídeo: el recorrido de la Alpha solo exige dos; el resto se valida por muestreo, no uno a uno.
  *2026-10-24:* el barrido de la tarea 5 los reproduce todos fuera del juego (§9).
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

### Tarea 2 — arnés de AvPlayer, en rojo (2026-10-24, `4d9dfb6`)

- **Qué es.** `tests/test_avplayer.cpp`, destino `avplayer-test` (solo Linux):
  `ninja -C out/gpu avplayer-test && timeout 600 out/gpu/avplayer-test tests/data [caso …]`.
  - Usa el AvPlayer de `libgow3gpu.so` como el juego (`docs/avplayer.md` §1): `Init` (no `InitEx`),
    asignadores del host, `FileReplacement` sobre un archivo, `PostInit` con sus valores y `AddSource`.
  - El callback de eventos arranca en `StateReady`, en el hilo del controlador: `StreamCount`,
    `GetStreamInfo`, `EnableStream` por pista, `Start` y `SetLooping`.
  - El hilo del test sondea `GetVideoData`/`IsActive`. Un hilo "av audio" consume el audio al ritmo de
    una salida real, 21,3 ms por bloque.
  - Cada caso corre en un hijo con `fork`. Si su paso no avanza en 15 s, se mata y el caso falla, con
    el paso y los hilos vivos en el informe. Una línea `join failed` en su stderr también lo hace
    fallar.
  - Opciones: `AVPLAYER_TEST_GDB=1` añade las pilas; `AVPLAYER_TEST_KEEP=<dir>` guarda el stderr de
    cada caso.
- **Fixtures** (`tests/data/`, ≤ 42 KB, la orden de `ffmpeg` está en la cabecera del test). Son H.264
  Main sin B-frames a 29,97 fps, con el `moov` delante como los vídeos reales:
  - `tiny_v150.mp4`: réplica de `phasetitle_01_00_en.mp4` (solo vídeo, 150 fotogramas, 5,005 s);
  - `tiny_v20.mp4`: 20 fotogramas;
  - `tiny_av.mp4`: 2 s con AAC estéreo a 48 kHz.
- **De paso:** `3648a99` añade el stub de `runtime_report` a `tests/test_gpu_runtime_stubs.cpp`. Sin
  él, desde `80d5fb0` no enlazaba ningún test que use `libgow3gpu.so` con esos stubs.
- **En rojo sobre el código actual.** Logs sin versionar:
  - tres ejecuciones completas, con 5 o 6 de 10 casos en rojo cada vez; se conservan las dos últimas
    (`out/t2-red.log`, `out/t2-red-2.log`);
  - tres parciales (`out/t2-smoke.log`, `out/t2-diag1.log` con la traza, `out/t2-red-gdb.log` con
    `gdb`).

  | Caso | Resultado |
  |---|---|
  | `plays_to_end_video_only` | **Rojo.** 150/150 fotogramas y ningún adelanto, pero no llega `StateStop` en 1 s tras el fin (§2). Después `Stop` se cuelga en el `join` del hilo de audio en 4 de 5 ejecuciones |
  | `plays_to_end_with_audio` | Verde. 60/60 fotogramas, 94 bloques, `StateStop` a los 2 009 ms; vídeo − audio entre 0 y 21 ms (media 10) |
  | `eof_then_stop_trigger` | **Rojo.** `join failed (Invalid argument)` en el ciclo 0 y `Stop` colgado en el ciclo 1, en las cuatro ejecuciones |
  | `eof_with_undrained_buffers_then_stop` | **Rojo.** `Stop` colgado en el ciclo 0 (`join` del hilo de vídeo), con el demultiplexor en el `join` del hilo de audio, 4 de 4 |
  | `stop_during_playback_without_draining` | Verde (10 ciclos) |
  | `stop_right_after_start_and_twice` | **Rojo** solo por un `join failed`, 3 de 3. El primer `Stop` da 0, `IsActive()` falso y el segundo `Stop` da `0x806A0002`, como en el juego |
  | `start_while_playing` | **Rojo.** El proceso aborta con "Resource deadlock avoided" (§2), 3 de 3 |
  | `close_from_event_callback` | Verde. `Close` dentro de `StateReady` y dentro del `StateStop` de un fin natural |
  | `repeated_open_close` | Verde en 2 de 3 (un `join failed` en la otra). 200 ciclos; hilos 1 → 1 y asignaciones vivas 0 → 0 |
  | `missing_garbage_and_truncated_stream` | Verde. Archivo inexistente y basura → `0x806A0002`. Lectura cortada → `IsActive()` falso a los 1,4 s, sin evento de error (§2) |
- **Lo que esto decide para la tarea 3:**
  - La causa del cuelgue es la del contrato nuevo de `Kernel::Thread`: dos `join` sobre el mismo
    hilo más el `detach` del propio hilo (§2). La regla 3 (retirar el `std::thread` bajo el mutex y
    que los demás esperen a `done`) cubre los tres cuelgues.
  - Del paso 5 ("otras causas") ya hay tres concretas:
    1. el decodificador de audio de un vídeo sin audio, que nunca termina y retiene el `StateStop`;
    2. `Start` con la reproducción en curso;
    3. el `Reset()` del lector en `Stop`, que es [H].

    La lectura que falla a mitad no cuelga, pero no avisa al juego: se decide en la tarea 3 si basta
    con el log.

### Tarea 3 — carrera de `Kernel::Thread` y otras causas (2026-10-24, `1442f25`, `e20fc9c`, `3d33791`, `aeaf4ee`)

- **`Kernel::Thread` nuevo** (`gpu/shim/core/libraries/kernel/threads.h`, `1442f25`). Sigue las cinco
  reglas del plan:
  - estado compartido (`shared_ptr`) con mutex, `done`, `stop_source`, el `std::thread` y el id del
    hilo vivo, que se borra al volver la función porque los id se reutilizan;
  - `Stop()` pide siempre la parada y después une;
  - `Join()` desde el propio hilo suelta el `std::thread` y sigue sin esperar. Desde otro hilo, el
    primero que lo saca lo une fuera del mutex, y todos esperan a `done`;
  - `Joinable()` significa "nadie lo ha sacado aún", lo que mantiene el "already stopped" de AvPlayer;
  - `Run()` detiene el anterior y asigna el estado bajo su mutex: el hilo nuevo no lo lee antes de
    tiempo.
- **`tests/test_kernel_thread.cpp`** (destino `kernel-thread-test`; sin juego ni FFmpeg, con vigilante
  por caso). Viejo: 3 de 3 ejecuciones, caso a caso (`out/t3-red.log`). Nuevo: 3 de 3
  (`out/t3-green.log`).

  | Caso | Código viejo | Nuevo |
  |---|---|---|
  | `self_join_vs_immediate_stop` (5 000 iteraciones) | **Rojo.** `Stop` vuelve con la función aún en marcha (iteración 3–5), con `join failed (Invalid argument)` | Verde, ~0,85 s |
  | `stop_wakes_a_thread_being_joined_by_another` | **Rojo.** El segundo `join` no vuelve, sin `join failed`: lo corta el vigilante a los 5 s | Verde |
  | `two_joiners_both_wait_for_the_function` | **Rojo.** Igual | Verde |
  | `restart_and_destructor` | Verde. Con TSan, rojo: un hilo que se une a sí mismo antes de que `Run` asigne el `jthread` se queda "joinable" | Verde |

  Con ThreadSanitizer (`libtsan` instalado en el contenedor con permiso del usuario):
  - el código viejo da `data race` en `Thread::Join` (salida 66);
  - el nuevo no da ninguno en 3 de 3 (`out/t3-tsan.log`).
- **Otras causas, un commit por cada una.** Cada una tenía antes su caso del arnés en rojo:
  1. `e20fc9c`: `Stop` ya no rebobina el lector bajo el demultiplexor (§2).
     - Es el [H] de la tarea 2, ahora confirmado.
     - Oráculo nuevo del arnés: una línea `[h264 @` hace fallar el caso, salvo en el de entrada rota.
  2. `3d33791`: los decodificadores despiertan al llegar a EOF (§2). Con eso llega el `StateStop`
     natural de un vídeo sin audio.
  3. `aeaf4ee`: `Start` con la reproducción en curso ya no aborta (§2).
  4. La lectura que falla a mitad no cambia (decisión en §2).
- **Verde** (`out/t3-final-*.log`, sin versionar):
  - `bash build.sh` y `bash build.sh --test`, con rc 0 y los 17 PASS/ok de la línea base;
  - `python3 -m unittest discover -s tests`: 267 ejecutados, OK (35 omitidos, 4 fallos esperados);
  - `kernel-thread-test`: 3 de 3;
  - `avplayer-test`: 10 de 10 en dos ejecuciones completas.

  | Caso | Tarea 2 | Ahora |
  |---|---|---|
  | `plays_to_end_video_only` | Sin `StateStop` natural; `Stop` colgado en 4 de 5 | 150/150; `StateStop` a 4 979 ms, último fotograma a 4 972; `Stop` → `0x806A0002` en 0 ms |
  | `plays_to_end_with_audio` | Verde | Igual: 60/60, 94 bloques, `StateStop` a 2 009 ms |
  | `eof_then_stop_trigger` | `join failed` y cuelgue en el ciclo 1 | 500 ciclos de 20/20 fotogramas; ciclo ≤ 643 ms, `Stop` ≤ 0 ms, `Close` ≤ 5 ms |
  | `eof_with_undrained_buffers_then_stop` | Cuelgue en el ciclo 0 | 20 ciclos, `Stop` = 0 |
  | `stop_right_after_start_and_twice` | `join failed`; con el oráculo nuevo, 450 errores H.264 | 20 ciclos sin errores |
  | `start_while_playing` | Aborto | `Start` = 0 en 3 ms, y vuelve a empezar desde 0 |
  | `repeated_open_close` | `join failed` en 1 de 3; con el oráculo nuevo, 5 352 errores H.264 | 200 ciclos; hilos 1 → 1, asignaciones vivas 0 → 0 |
  | `stop_during_playback_without_draining`, `close_from_event_callback`, `missing_garbage_and_truncated_stream` | Verde | Verde |
- **Para las tareas siguientes:**
  - **T6:** el cuelgue de `phasetitle` debería desaparecer. Con el `StateStop` natural ya arreglado, el
    `Stop` del juego devolverá `0x806A0002` o 0, según llegue antes o después de que el demultiplexor
    suelte su hilo. Los dos valores son los del prólogo.
  - **T5, [H]:** al llegar a EOF, los decodificadores no vacían el códec
    (`avcodec_send_packet(nullptr)`). Un vídeo con B-frames perdería sus últimos fotogramas; los
    fixtures no tienen B-frames. El barrido debe comparar los fotogramas entregados con los del
    archivo.

### Tarea 4 — `JumpToTime` real y bucle (2026-10-24, `77bd6aa`, `1ccd0f3`)

- **Uso real** (paso 1, `docs/avplayer.md` §4). Son 20 llamadas en los 10 logs que lo tienen, del
  2026-10-17 al 2026-10-24:
  - siempre con el vídeo en marcha, a `CurrentTime()` + 999–1 033 ms;
  - siempre seguidas de `Stop`; en la traza, en el mismo milisegundo y sin ningún fotograma ni bloque
    de audio entregado entre medias;
  - `DisableStream`, `SetTrickSpeed` y `ChangeStream` no salen en ningún log y siguen como estaban.
- **Decisión** (paso 2): implementarlo. El juego no puede recibir `ORBIS_OK` de algo que no se hizo.
  Un error explícito habría cambiado lo que ve el juego (hasta ahora 0) sin saber si mira el valor.
- **Qué hace** (`77bd6aa`):
  - `AvPlayerSource::JumpToTime` para los hilos como `Stop` y busca el fotograma clave anterior
    (`avformat_seek_file`). Vacía colas y códecs (`avcodec_flush_buffers`), devuelve a su reserva los
    búferes de los fotogramas en cola, pone el reloj en el destino y vuelve a arrancar los hilos.
  - Los decodificadores descartan lo que acaba antes del destino. El primer fotograma entregado es el
    que está en pantalla en ese instante: 968 para un salto a 1 000, cuando el plan pedía
    ≥ 1 000 − 1 fotograma.
  - Más allá del final: en un vídeo en bucle, el tiempo da la vuelta (módulo de la duración). Si no
    hay bucle, va al final y llegan EOF y `StateStop`.
  - `AvPlayerState::JumpToTime` solo salta en `Play`, `Pause` o `Buffering`. Antes de `Start`, tras
    `Stop` o al final devuelve `0x806A0002` y lo registra. Mientras salta tiene en exclusiva el mutex
    del origen.
  - Al acabar emite `WarningId` con `0x806A00A3` (`WAR_JUMP_COMPLETE`). **[H]** Que el AvPlayer real
    lo emita: el código está en la lista de errores del SDK, igual que el `WAR_LOOPING_BACK` que ya se
    emitía.
- **Tests** (paso 3; `avplayer-test`, logs `out/t4-*.log` sin versionar):

  | Caso | Con el stub | Con el salto |
  |---|---|---|
  | `jump_then_stop` | **Rojo:** `CurrentTime()` 300 tras saltar a 1 000; el vídeo sigue en 300 | Verde: salto en 3 ms, `CurrentTime` 1 000, primer fotograma 968 a los 2 ms, 121 fotogramas hasta 4 972 sin huecos, aviso y `StateStop` |
  | `jump_then_stop_like_the_game` | Verde: es una guarda del patrón del juego, no detecta el stub | Verde: 20 ciclos con y sin audio; salto ≤ 4 ms, `Stop` ≤ 5 ms, segundo `Stop` → `0x806A0002` |
  | `jump_past_the_end_and_when_stopped` | **Rojo:** activo 1 s después de saltar más allá del final | Verde: fin a los 21 ms con `StateStop`; en bucle, el tiempo da la vuelta (968); antes de `Start` y tras `Stop`, `0x806A0002` |
  | `loops_back` (nuevo, por el bucle) | **Rojo:** 20 fotogramas, ninguna vuelta y 12,7 M avisos; el salto posterior da `0x806A0002` | Verde: 274 vueltas y 274 avisos en 2 s; el salto tras las vueltas da 467, el fotograma de los 500 ms |

  Los subcasos (b) y (c) de `jump_past_the_end_and_when_stopped` no llegaron a ejecutarse con el stub,
  porque (a) falló antes. (b) pasó de `tiny_v20` a `tiny_v150` después del rojo, porque con el bucle
  roto el clip corto ya había dado la vuelta antes de saltar. Después lo cubre `loops_back`.
- **El bucle** (`1ccd0f3`, §2) salió con `jump_past_the_end_and_when_stopped`. Se arregla en un
  commit propio porque congelaba el fondo del menú de título y bloqueaba probar el salto en un bucle.
  El ritmo de las vueltas siguientes queda para la tarea 7.
- **Verde** (`out/t4-final-*.log`):
  - `bash build.sh --test`: rc 0, con las mismas 17 líneas `PASS`/`ok` que en la tarea 3;
  - `avplayer-test`: 14 de 14 en dos ejecuciones completas, y los casos nuevos, 3 de 3 por separado;
  - `kernel-thread-test`: 3 de 3;
  - Python: 267 ejecutados, OK (35 omitidos, 4 fallos esperados).
- **Para las tareas siguientes:**
  - **T6:** medir cuánto dura `JumpToTime` con 1920×1088 (traza: `JumpToTime … -> 0 (N ms)`) al salir
    de `titlebg1` y `TM_CI`.
  - **T7, paso 4:** el reloj del bucle sin audio (§2). Hay que verlo con `titlebg1.mp4` pasados
    120 s en el menú de título, con la traza.


### Tarea 5 — barrido de los 134 vídeos (2026-10-24, `bd61943`, `a0b2c86`)

- **Qué hay** (paso 1, `tools/probe_movies.sh` → `out/movies-probe.csv`; detalle en `docs/avplayer.md`
  §6). Valores distintos de los fixtures:
  - todos son 1920×1080 (los fixtures, 320×180), en H.264 High (48) y Constrained Baseline (4),
    además de Main;
  - 52 van a 30 fps justos;
  - en 37 de los 43 con audio, el audio es la primera pista;
  - 75 tienen fotogramas B: `has_b_frames` 1 en 71 y 2 en 4.

  Lo que no hay: fps variable, audio que no sea AAC LC estéreo a 48 kHz, ni subtítulos u otras pistas.
- **Modo barrido** (paso 2, `bd61943`): `avplayer-test --movie-dir <dir> [--seconds N] [file.mp4 …]`, un
  hijo por vídeo, una línea `OK|FAIL <archivo> v= a= ms= [end=] late=` y un resumen final. Además de lo
  que pedía el plan (un fotograma y un bloque si hay audio), comprueba marcas que no retroceden, `Stop` = 0
  con el vídeo en marcha, `Stop`/`Close` < 2 s y, si el vídeo acaba en la ventana, su último fotograma.
- **Ejecuciones** (paso 3; logs sin versionar en `out/`):

  | Pasada | Antes del arreglo | Después |
  |---|---|---|
  | `--seconds 3`, los 134 (`avplayer-sweep.log`, el de antes en `avplayer-sweep-before.log`) | 134 OK; 90–91 fotogramas, 140 bloques con audio, `late` ≤ 37 ms | 134 OK |
  | `--seconds 12`, los 117 de ≤ 10 s, que llegan al final (`avplayer-sweep-end*.log`) | **64 FAIL**: los 64 con `has_b_frames=1`, sin su último fotograma | 117 OK, cada uno con sus `nb_frames` |

  Ningún `FAIL` en la pasada de 3 s del plan: con 3 s ningún vídeo llega a su final. La segunda pasada
  no estaba en el plan y es la que encontró el defecto.
- **Arreglo** (`a0b2c86`, §2): con EOF y la cola vacía, el decodificador de vídeo recibe el paquete nulo y
  entrega lo que retiene. Caso fijo `plays_to_end_with_b_frames` con `tests/data/tiny_b150.mp4`
  (`has_b_frames=2`): **rojo** 148 de 150 fotogramas (3 de 3), **verde** 150 de 150 (3 de 3).
- **Deuda anotada, sin rojo:**
  - `IsActive()` no cuenta lo que el decodificador tiene entre manos. Solo importaría si decodificara
    por detrás del reloj; ya pasaba con el último paquete.
  - El decodificador de audio no se vacía. AAC no retiene tramas.
  - El bucle de un vídeo con fotogramas B (`titlebg1`–`3`, `has_b_frames=2`) no tiene caso propio. En
    la vuelta no hay EOF, así que este cambio no lo toca.
- **Los dos vídeos de §7, enteros fuera del juego:**
  - `phasetitle_01_00_en.mp4`: 150 de 150 fotogramas, en la pasada de 12 s.
  - `op1_arc_en.mp4`, con `--seconds 115` (`out/avplayer-sweep-op1.log`): 3 211 de 3 211 fotogramas y
    5 023 bloques de audio; de sus 5 024 paquetes AAC, el propio `ffmpeg` también decodifica 5 023.
  - `late` llega a 835 ms en el opening. **[H]** Es la deriva del consumidor de audio del arnés, no
    de AvPlayer: el consumidor despierta ~0,17 ms tarde por bloque (sondeo de 200 µs), y el reloj con
    audio sigue al audio, así que 5 023 × 0,17 ≈ 835 ms. La sincronía real se juzga en el juego (T7,
    paso 1).
- **§7:** la tarea no cierra ninguna casilla, pero los criterios 1 y 2 ya solo dependen del juego.
- **Verde** (`out/t5-*.log`):
  - `avplayer-test`: 15 de 15 en tres ejecuciones completas;
  - `kernel-thread-test`: 4 de 4, tres veces;
  - `bash build.sh --test`: rc 0, con las mismas 17 líneas `PASS`/`ok` que en la tarea 4;
  - Python: 267 ejecutados, OK (35 omitidos, 4 fallos esperados).
- **Para las tareas siguientes:**
  - **T6/T7:** la traza del juego cuenta fotogramas en `Close`. Con el arreglo, el prólogo debería dar
    11 660.
  - **T7, paso 4:** el bucle de `titlebg1` tiene `has_b_frames=2`; mirar también que la vuelta no
    pierda ni repita fotogramas.

### Tarea 6 — título del capítulo 1 en el juego (2026-10-24)

- **Ruta** (paso 1). Antes de nada, la partida se copió en `out/savedata-pre-ruta/`; es la misma que
  `out/savedata-con-progreso/`. El plan pedía que el usuario grabara la ruta con F9. En su lugar, la
  saqué de dos ejecuciones de exploración (`out/spec5-t6/explore*.log`) que pulsaban por
  `GOW3_PAD_FILE`, mirando las capturas para decidir:
  - **NEW GAME** empieza el texto del prólogo, y 60 pulsaciones de Cruz no llegaron al capítulo 1.
  - **CONTINUE**, la opción marcada, carga la última partida (2026/10/21 20:15, cerca del final del
    prólogo). La decimosexta Cruz, a los 28,6 s, pide `phasetitle_01_00_en.mp4`. Es el camino de la
    tarea 1.
  - Una pulsación de 0,3 s en el menú baja dos opciones, porque el cursor se repite. Las de la ruta son
    de 120 ms.

  Con esos tiempos se escribió `out/spec5-route.txt`, en el formato de `GOW3_PAD_RECORD`: 17
  pulsaciones de Cruz. `out/spec5-t6/run-t6.sh <n>` hace cada ejecución:
  1. restaura la partida y arranca el juego con la traza;
  2. 3 s después del segundo `titlebg1` (el menú de título), escribe `replay` en el archivo de mando;
  3. cierra con SIGTERM 10 s después del `Stop` del juego. Si `Stop` no vuelve en 10 s, guarda
     `/proc/<pid>/task/*/comm`, `fd` y `maps` y manda SIGALRM (paso 4).

  La ruta, el guion y las capturas no se versionan.
- **Tres ejecuciones** (paso 2, `out/run-t6-{1,2,3}.log`):

  | Ejecución | `phasetitle`, desde el primer vídeo | Fotogramas | `IsActive()` falso | `Stop` del juego | `StateStop` |
  |---|---|---|---|---|---|
  | 1 | 50,5 s | 150/150, el último en 4 972 | `play` 4 988 | `0x806A0002`, 0 ms | 4 ms después |
  | 2 | 50,9 s | 150/150 | `play` 4 988 | `0x806A0002`, 0 ms | 3 ms después |
  | 3 | 39,1 s | 150/150 | `play` 4 975 | `0x806A0002`, 0 ms | 2 ms después |

  La 3 llega antes porque `TM_CI` se saltó a los 1,6 s: el usuario pulsó Cruz (lo confirma; el log solo
  registra el archivo de mando, no la entrada del host). Así, `TM_CI` se salta con Cruz, y ese salto de
  un vídeo con audio (`JumpToTime` + `Stop`) tampoco cuelga. Que `titlebg1` saliera a los 1,1 s también
  en esa ejecución no lo cambia: en las cinco sale a los 1 108–1 113 ms, lo que no es una pulsación.
- **Criterio** (paso 3):
  - `grep -E 'STOP:|Fault:|join failed' out/run-t6-*.log` no da nada.
  - Tampoco hay `<Error>` de AvPlayer, líneas `[h264 @` ni "duplicated MOOV". El único aviso es `Stop:
    Could not stop playback: already stopped`, 4 o 5 por ejecución: el `Close` y el segundo `Stop` de
    cada salida.
  - Capturas con `GOW3_SCREENSHOT_AT` cada 60 fotogramas, resumidas en `out/spec5-t6/t6-capturas.png`.
    A mitad del vídeo sale "DAY 1 — Homecoming I". 7 s después del final, la escena del metro entra en
    fundido. En la exploración, con la misma ruta, esa escena llega a mostrar su texto ("The tracks
    take a sudden dive…", `out/spec5-t6/explore2/frame_005040_1781x1002.png`).
  - El marco blanco que rodea el vídeo en algunas capturas es del propio vídeo (lo confirma el
    usuario).
  - Como decía el plan, tres de tres confirman, pero no demuestran: la carrera es probabilística, y la
    prueba son las tareas 2 y 3.
- **Paso 4:** no hizo falta, porque todos los `Stop` volvieron.
- **Lo que se mide de paso** (§2):
  - el orden de arranque sin entrada, que corrige lo que se dedujo en la tarea 1;
  - lo que tarda `JumpToTime` a 1920×1088, pendiente desde la tarea 4: 0–5 ms en 11 llamadas.
- **§7:** casilla 2 marcada. Las salidas del menú de título y el salto de `TM_CI` de la ejecución 3 son
  indicio para la casilla 3, que sigue en la tarea 7, paso 3.
- **Sin cambios de código:** `bash build.sh` no reenlazó nada, así que las suites no se repiten. Siguen
  como en la tarea 5.
- **Para la tarea 7:** la ruta llega al capítulo 1 en ~30 s desde el menú, pero en ella no sale el
  opening `op1_arc_en.mp4`.

### Tarea 7 — opening, prólogo, salto y bucle en el juego (2026-10-24, `d8fd321`)

- **Dónde está el opening** (paso 1). La traza solo muestra que lo pide la historia, sin ningún menú
  delante. Para saber dónde, descomprimí los guiones (`image/allscr.mrg`: entradas MZX con los
  literales en XOR 0xFF; herramientas en `out/tsuki-scripts/`, sin versionar).
  - `_TOMP(n)` reproduce el vídeo `n` de la tabla del eboot: 4 es el prólogo, 5 `op1_arc`, 6
    `op2_ciel` y del 8 en adelante los títulos de capítulo. Lo confirma el menú de depuración
    `SEL_MOVIETEST`, que no se puede abrir.
  - `_TOMP(5)` está en la entrada 90, tras la elección "...will help her." del callejón. La otra opción,
    "...can't help her. There's no way.", lleva a otra escena. Entre el título del capítulo 1 (entrada
    5) y el opening hay 5 877 líneas de texto y 15 elecciones, contando las ramas; el juego guarda
    solo antes de cada elección (`_ATSV(1)`).
  - Las escenas no leídas no se saltan (lo dice el usuario), así que no hay atajo. El usuario jugó hasta
    la persecución del callejón (línea 5 993; copia en `out/savedata-cerca-op/`). Un guion pulsó
    CONTINUE y Cruz cada 0,8 s durante 1 min 40 s, y luego el usuario pidió dejar la automatización y
    llegó él al opening.
- **Opening** (`out/run-t7-op-1.log`, una ejecución):

  | Dato | Valor |
  |---|---|
  | Fotogramas | 3 211 de 3 211; el último, en 107 107 |
  | Bloques de audio | 5 023, los que decodifica `ffmpeg` de sus 5 024 paquetes; el último, en 107 136 |
  | Pared − marca | vídeo de −16 a +15 ms, audio de −16 a 0 ms; sin deriva en 107 s |
  | Último audio entregado − fotograma | de −21 a +17 ms |
  | Fin | `StateStop` a los 107 121 ms de `Start` (el archivo dura 107 140); el `Stop` del juego devuelve `0x806A0002` en 0 ms |
  | Memoria | VmRSS 1 757 300 kB al empezar y 1 757 272 kB al acabar |

  El usuario lo vio y lo oyó sincronizado ("no vi nada raro"). No hay `STOP:`, `Fault:`, `join failed`,
  `<Error>` ni `[h264 @`. El juego guardó después (22:57:42; copia en `out/savedata-tras-op/`), y esa es
  la partida que queda en `user/savedata`.
- **Prólogo** (paso 2, `out/run-t7-prologue-1.log`). La medición de la tarea 1 ya daba el caso (a): la
  [H] "más rápido" es falsa. Se repitió para ver en el juego el arreglo de la tarea 5, porque el prólogo
  es el único vídeo del juego con fotogramas B que llega a EOF:
  - 11 660 de 11 660 fotogramas (en la tarea 1, 11 659); el último en 388 633 (antes, 388 600);
  - 18 219 bloques de audio, pared − marca de −15 a +19 ms;
  - `StateStop` a los 388 637 ms de `Start`, y el `Stop` del juego devuelve `0x806A0002` en 0 ms.

  El guion pone la partida sin progreso y, al cerrar, devuelve la que había (`out/spec5-t7/run-prologue.sh`).
- **Salto** (paso 3). Qué se sale y cómo:
  - el primer `titlebg1` lo deja el propio juego a los 1,1 s (tarea 6); el del menú, al pulsar CONTINUE
    (`JumpToTime` en 5 ms y `Stop` en 0 ms en la ejecución del opening);
  - `TM_CI`, el logo, se salta con Cruz. Tres ejecuciones con una Cruz 1,5 s después de su `AddSource`
    (`out/spec5-t7/run-skip-tmci.sh`, `out/run-t7-skip-tmci-{1,2,3}.log`):

    | Ejecución | Salto, desde `Start` | `JumpToTime` | `Stop` | `StateStop` | Fotogramas / bloques |
    |---|---|---|---|---|---|
    | 1 | 1 548 ms | a 2 561, 3 ms | 0, 0 ms | en el mismo ms | 47 / 74 |
    | 2 | 1 543 ms | a 2 556, 2 ms | 0, 0 ms | en el mismo ms | 47 / 73 |
    | 3 | 1 544 ms | a 2 557, 1 ms | 0, 0 ms | en el mismo ms | 47 / 74 |

    En las tres, el segundo `Stop` devuelve `0x806A0002`, `Close` llega 11 s después (antes del `Init`
    siguiente) y el juego sigue hasta el menú de título. El aviso de fin de salto (`0x806A00A3`) llega
    3–4 ms después de `StateStop`.
  - el prólogo no se salta (§2). No se probó a saltar el opening ni los títulos de capítulo.
- **Bucle** (paso 4). `titlebg1.mp4` bajo el menú de título, 11 minutos sin entrada
  (`out/spec5-t7/run-loop.sh`), antes y después del arreglo:

  | | Antes (`out/run-t7-loop-1.log`) | Después (`out/run-t7-loop-2.log`) |
  |---|---|---|
  | Vueltas en 11 min | 16 | 6 |
  | Primera vuelta | 3 600 fotogramas en 119,9 s | 3 600 fotogramas en 120,0 s |
  | Las siguientes | 3 600 fotogramas en 36,1–36,5 s; `CurrentTime` clavado en 120 000 | 3 360 fotogramas (de 8 033 a 119 967 ms) en 112,0 s; `CurrentTime` de ~8 100 a ~119 300 |
  | Repetidos o perdidos | ninguno | ninguno repetido; faltan los 240 primeros de cada vuelta, que el juego salta |
  | `WAR_LOOPING_BACK` | 15, uno por vuelta | 5, uno por vuelta; tras cada uno, `JumpToTime(8 033)` y `WAR_JUMP_COMPLETE` |
  | VmRSS a los 0, 5 y 10 min | 1 388 904, 1 393 056 y 1 395 468 kB | 1 390 172, 1 399 792 y 1 396 880 kB |

  Después del arreglo, el juego salta 6 ms después de recibir el fotograma 0 de la vuelta, no con el
  `WAR_LOOPING_BACK`, que llega 1,2 s antes porque el demultiplexor lee por delante. Antes recibía el mismo
  fotograma 0 y no saltaba: lo que cambió es `CurrentTime`, que ahora vuelve a 0 con la vuelta, en
  vez de quedarse en 120 000. El aviso no rompe nada: en las dos ejecuciones el juego sigue en el menú,
  sin `STOP:` ni `Fault:`.
- **Arreglo** (`d8fd321`, causa en §2), con TDD:
  - caso nuevo `loops_back_on_the_clock` (`tiny_v20`, 2,5 s): **rojo** con 7 472 fotogramas, 373 vueltas,
    un fotograma 246 s adelantado y `CurrentTime` 667 a los 1 500 ms; **verde** (3 de 3) con 75
    fotogramas, 3 vueltas, ninguno adelantado y `CurrentTime` 165, cuando se esperaba ~166;
  - `loops_back` sigue en verde, y ahora da 60 fotogramas en 2 s en vez de 5 470.
- **Suites** (`out/t7-final-*.log`): `avplayer-test`, 16 de 16 tres veces; `kernel-thread-test`, 4 de 4
  tres veces; `bash build.sh --test`, rc 0 con las mismas 17 líneas `PASS`/`ok`; Python, 267 ejecutados,
  OK (35 omitidos, 4 fallos esperados).
- **§7:** casillas 1 y 3 marcadas. La 4, el log del recorrido, queda para la tarea 8. En las ejecuciones
  de esta tarea tampoco hay `STOP:` ni `Fault:`.
- **Deuda anotada, sin caso:** **[H]** en un bucle con audio, el reloj vuelve a 0 con el primer bloque
  de la vuelta siguiente. Si quedara en cola un fotograma de la vuelta anterior, el vídeo esperaría casi
  una vuelta. Ningún vídeo del juego hace bucle con audio.
  **[H]** El juego recibe el fotograma 0 (desenfocado) de cada vuelta 6 ms antes de saltar, y el
  siguiente ~50 ms después. Si lo dibuja, cada 112 s hay un destello de unos tres cuadros. El usuario
  cree que no ocurre (2026-10-24, tarea 8), así que no se persigue.
- **Para la tarea 8:**
  - `out/spec5-t6/run-t6.sh` pone `out/savedata-pre-ruta/` en `user/savedata` sin guardar lo que había.
    Ahora la partida del usuario va más allá (`out/savedata-tras-op/`), así que hay que devolverla
    después del recorrido.
  - Repetir el opening exige jugar desde `out/savedata-cerca-op/`: de ahí al vídeo hay ~500 líneas y la
    elección.

### Tarea 8 — cierre (2026-10-25)

- **Suites** (paso 1, `out/t8-*.log`, resumen en `out/spec5-t8/suites.txt`), iguales a las de la tarea 7:
  - `bash build.sh --test`: rc 0, con las mismas 17 líneas `PASS`/`ok`;
  - `kernel-thread-test`: 4 de 4, tres veces; `avplayer-test`: 16 de 16, tres veces;
  - Python: 267 ejecutados, OK (35 omitidos, 4 fallos esperados).
- **Barridos fuera del juego**, repetidos porque `d8fd321` cambió `GetVideoData` en todos los vídeos sin
  audio:
  - `--seconds 3`, los 134 (`out/t8-sweep-3s.log`): 134 OK.
  - `--seconds 12`, los 117 que llegan al final (`out/t8-sweep-end.log`): 117 OK. Fotogramas y bloques de
    audio, vídeo a vídeo, iguales a los de la tarea 5 (`out/avplayer-sweep-end.log`); `end` varía ±2 ms y
    `late` llega a 69 ms (antes, 70).
- **Recorrido final** (paso 2, `out/run-t8-route.log`): la ruta de la tarea 6 (CONTINUE y 16 Cruz) con
  la traza, sobre la partida de prueba `out/savedata-pre-ruta/`. `out/spec5-t8/run-route.sh` es
  `run-t6.sh` con un añadido: antes copia la partida del usuario a `out/savedata-usuario-t8/` y al cerrar
  la devuelve. Quedó igual a `out/savedata-tras-op/`.
  - `TM_CI` llega entero (180 fotogramas, 282 bloques), y el `titlebg1` del menú se deja al pulsar
    CONTINUE (`JumpToTime` en 5 ms, `Stop` en 0 ms).
  - `phasetitle_01_00_en.mp4`, a los 50,9 s del primer vídeo: 150 de 150 fotogramas, el último en 4 972;
    `IsActive()` falso en `play` 4 973; el `Stop` del juego devuelve `0x806A0002` en 0 ms y `StateStop`
    llega 4 ms después. Es lo mismo que en las tres ejecuciones de la tarea 6.
  - En las capturas (`out/spec5-t8/shots/`) sale "DAY 1 — Homecoming I" a mitad del vídeo y, 10 s
    después del final, la escena del metro en fundido, como en la tarea 6.
  - `grep -E 'STOP:|Fault:|join failed'` no da nada, y tampoco hay `<Error>`, `[h264 @` ni "duplicated
    MOOV". Los únicos avisos de AvPlayer son los 5 `Stop: Could not stop playback: already stopped` de
    siempre. La línea `GPU:` da `unimplemented=0`.
  - El mismo `grep` sobre los logs de las tareas 6 y 7 (`out/run-t6-*.log`, `out/run-t7-*.log`) tampoco
    da nada.
- **Paso 4** (revisión adversarial): pendiente, a petición del usuario (2026-10-24). Como en la Spec 4
  (su tarea 13), la spec se cierra sin esa segunda mirada.
- **§7:** casilla 4 marcada. Las cinco lo están.
- **Queda abierto**, todo **[H]** y sin un caso que falle:
  - el bucle con audio (tarea 7), que ningún vídeo del juego usa;
  - el destello del fotograma 0 al dar la vuelta `titlebg1` (tarea 7), que el usuario cree que no ocurre;
  - `IsActive()` sin contar lo que tiene el decodificador, y el decodificador de audio sin vaciar (tarea 5);
  - no se probó a saltar el opening ni los títulos de capítulo.
