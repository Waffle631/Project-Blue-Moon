# AvPlayer en Tsukihime (CUSA47999)

Cómo usa el juego `libSceAvPlayer` para los vídeos de `image/movie/`, medido con el trazador
`GOW3_TRACE_AVPLAYER=1` (`gpu/shadps4/core/libraries/avplayer/avplayer_trace.h`). La spec es
`docs/specs/2026-10-14-spec-5-avplayer.md`.

Marcas: **[V]** comprobado en una ejecución con traza; **[H]** hipótesis pendiente.

**Cómo se reproduce.** `GOW3_GAME_DIR=Tsukihime/CUSA47999 GOW3_TRACE_AVPLAYER=1 GOW3_GPU_LOG=info
python3 run.py > out/run-x.log 2>&1`. Cada llamada deja `AvTrace +<ms> t<tid>: …` en stderr; `+0` es la
primera llamada, cuya hora da la primera línea. Los `t<tid>` se nombran con `/proc/<pid>/task/<tid>/comm`
durante la ejecución. `GetVideoData`, `GetAudioData`, `IsActive` y `CurrentTime` se sondean cada
fotograma: solo se trazan los fotogramas y bloques entregados (con `play=`, ms desde `Start`), los cambios
de `IsActive` y un `CurrentTime` por segundo; `Close` da los totales.

## 1. Contrato real (2026-10-24)

Ejecuciones: `out/run-t1-trace.log` (partida actual, menú de título y título del capítulo 1, 60 s) y
`out/run-t1-prologo.log` (partida sin progreso, prólogo entero). Los dos logs están sin versionar.

- **[V] Inicio:** `sceAvPlayerInit`, no `InitEx`. Pasa los cuatro asignadores, `FileReplacement`
  (`open`/`close`/`read_offset`/`size`) y su callback de eventos. Los demás campos: `game_obj` `0x10076c38b0`
  (el mismo en todos los reproductores), 3 framebuffers, `auto_start=0`, `debug_level=0`,
  `base_priority=700` y `default_language` nulo.
- **[V] `PostInit`:** `demux_video_buffer_size` = 3 MiB, decodificador de vídeo tipo 3 (`Software2`) y de
  audio 0 (`Default`).
- **[V] Origen:** `sceAvPlayerAddSource` (no `Ex`) con la ruta `/app0/image/movie/<nombre>.mp4`. La ruta
  llega a los callbacks de `FileReplacement`, no a `fopen`. Justo después llama a `SetLooping`: 1 en
  `titlebg1.mp4` (fondo del menú de título, 120 s), 0 en los demás.
- **[V] Arranque desde el callback de eventos:** al recibir `StateReady`, el juego llama a `StreamCount`,
  a `GetStreamInfo` y a `EnableStream` por cada pista (vídeo y audio), a `Start` y otra vez a `SetLooping`
  con el mismo valor. Todo eso corre en el hilo del controlador de AvPlayer (`shadPS4:AvContr`), no en
  un hilo del juego.
- **[V] Hilos del juego:** `1: task_game` hace `Init`, `AddSource`, `GetVideoData`, `Stop` y `Close`, y
  también sondea `IsActive`. El hilo `av audio` (prioridad 714) sondea `IsActive` y `GetAudioData` sin
  pausa, aunque el vídeo no tenga audio: con `titlebg1.mp4`, 7 417 `IsActive` y 7 148 `GetAudioData`
  vacíos en 1,1 s.
- **[V] Datos:** el vídeo se pide con `sceAvPlayerGetVideoData` (no `Ex`), unas 3 llamadas por fotograma
  entregado. Todos los vídeos vistos son 1920×1088 a 30 fps. El audio llega en bloques de 4 096 bytes
  (1 024 muestras estéreo s16 a 48 kHz, 21,3 ms).
- **[V] Nunca llamadas:** `InitEx`, `AddSourceEx`, `GetVideoDataEx`, `SetAvSyncMode` (sincronía por
  defecto), `Pause`, `Resume`, `SetTrickSpeed`, `DisableStream`, `ChangeStream`, `SetLogCallback` y
  `Vprintf`.
- **[V] Fin natural** (`prologue_en.mp4`, `phasetitle_01_00_en.mp4`): el juego ve `IsActive()` falso y
  llama a `Stop` en el mismo milisegundo. AvPlayer emite `StateStop` al vaciar las colas tras el EOF.
  Hasta la tarea 3 de la spec, eso solo pasaba si el vídeo tenía audio: sin audio, como en
  `phasetitle`, `StateStop` llegaba después del `Stop` del juego (§3). En el prólogo, `Stop` devuelve
  `0x806a0002` porque ya estaba parado. En `phasetitle_01_00_en.mp4` se bloquea (§3; arreglado fuera
  del juego en la tarea 3). En esos dos vídeos el juego no llama a `CurrentTime` ni a `JumpToTime`.
- **[V] Salida anticipada** (`titlebg1.mp4` y `TM_CI.mp4`): el juego sondea `CurrentTime` unas 3 veces por
  fotograma y luego llama a `JumpToTime(CurrentTime() + ~1 000)`: 2 087 con el tiempo en 1 054, 3 509 con
  2 495 y 6 513 con 5 514. En el mismo milisegundo llama a `Stop`, sin `GetVideoData` entre medias. Ese
  `Stop` devuelve 0 en 1–2 ms. Cuando `IsActive()` pasa a falso, llama a `Stop` otra vez, que devuelve
  `0x806a0002`. `Close` llega enseguida o más tarde (0 s, 3,2 s y 17 s), siempre justo antes del
  siguiente `Init`. `JumpToTime` era un stub que
  devolvía 0; desde la tarea 4 salta de verdad (§4). **[V]** Estas salidas responden a pulsaciones: el usuario confirma (2026-10-24)
  que pulsó botones en esa ejecución para llegar al capítulo 1.
- **[V] Orden de arranque con la partida actual:** `titlebg1` (1,1 s) → `TM_CI` (2,5 s de 6 s) → `titlebg1`
  (5,5 s) → 17 s sin vídeo → `phasetitle_01_00_en`. Cada vídeo va con un `Init`/`Close` propio, y la
  dirección del reproductor se reutiliza.

## 2. Prólogo: tiempo de pared contra marcas de tiempo

Medido desde `Start` en `out/run-t1-prologo.log`. Es la [H] "el vídeo va más rápido" de la spec (§2).

| Dato | Valor |
|---|---|
| Duración (`GetStreamInfo`, las dos pistas) | 388 667 ms |
| `Start` → `StateStop` | 388 637 ms de pared |
| Último fotograma | marca 388 600, entregado a los 388 590 ms |
| Último bloque de audio | marca 388 651, entregado a los 388 635 ms |
| Entregados | 11 659 fotogramas y 18 219 bloques (18 219 × 21,33 ms = 388,67 s) |
| Pared − marca | vídeo de −15 a +19 ms (media −12); audio de −16 a 0 ms (media −15,7) |
| EOF | sí: `EOF reached in demuxer`, los tres hilos salen normalmente y llega `StateStop` |
| Lo que hace el juego al acabar | `IsActive` → falso, `Stop` (→ `0x806a0002`), guarda la partida y sigue |

**La [H] es falsa.** AvPlayer reproduce el prólogo a tiempo real y entero, sin deriva. El `STOP:` de la
ejecución del 2026-10-17 no llegó al final del vídeo. El juego crea su primer decodificador ATRAC9
(`sceAudiodecCreateDecoder`) a los **365,4 s del vídeo**, 23 s antes del final. Entonces esa API no
existía y la ejecución paró ahí: 6 min 16 s de proceso menos ~12 s de arranque dan unos 364 s de vídeo.

## 3. Cuelgue en `Stop` al final de `phasetitle_01_00_en.mp4` (2026-10-24)

En `out/run-t1-trace.log` el título del capítulo 1 (solo vídeo, 5 005 ms) entrega sus 150 fotogramas.
El último tiene marca 4 972. En ese mismo milisegundo (+34 423) `IsActive` pasa a falso y el juego llama
a `Stop`. El controlador emite `StateStop` a +34 426, y `Stop` no vuelve hasta +51 558 (17 135 ms).
Vuelve justo después de que el watchdog (`GOW3_TIMEOUT=60`) vuelque las pilas.

La pila de `task_game` en el volcado es la misma que en `a3` (spec §2): eboot `+0x10225f` →
`sceAvPlayerStop` → `AvPlayer::Stop` → `AvPlayerState::Stop` → `std::thread::join` → `pthread_join`, con
`rdi=0x7f7887447ce8`, el mismo desplazamiento `…ce8` que en `a3`. La base de `libgow3gpu.so`, deducida con `nm`, era
`0x7f7a70a00000`.

Es el disparador que plantea el plan: EOF con las colas vacías y `Stop` inmediato. El prólogo hace lo
mismo y no se cuelga: cuando el juego llama a `Stop`, ningún hilo de AvPlayer es ya unible y `Stop`
sale por "already stopped" sin llegar a `join`. Es la tercera reproducción de tres.

**Fuera del juego** (tarea 2, `tests/test_avplayer.cpp`). El arnés reproduce el cuelgue con
`tests/data/tiny_v150.mp4`, una réplica de este vídeo, y con un clip de 20 fotogramas. Muestra dos
cosas:

- **Por qué se cuelga:**
  - a un mismo hilo de AvPlayer se le hace `join` desde el demultiplexor y desde el `Stop` del juego,
    y ese hilo, al terminar, se desvincula a sí mismo;
  - el segundo `pthread_join` devuelve `EINVAL` (`join failed`) o espera para siempre.
- **Por qué `StateStop` llega tarde en un vídeo sin audio:**
  - el hilo de audio arranca igual y espera paquetes que no llegan;
  - el demultiplexor se queda esperándolo y no marca el fin hasta que `Stop` despierta a ese hilo.

**Arreglo** (tarea 3, 2026-10-24; falta confirmarlo en el juego, tarea 6):

- **`Kernel::Thread`:** un solo llamador saca el hilo, bajo un mutex, y lo une. Los demás esperan a
  que la función termine, y el propio hilo lo suelta sin esperar.
- **Decodificadores:** se despiertan con el aviso de EOF del demultiplexor, así que el `StateStop` de
  un vídeo sin audio llega 7–12 ms después del último fotograma. El `Stop` del juego que viene detrás
  devuelve `0x806a0002`, como en el prólogo, o 0 si llega antes de que el demultiplexor termine.

Detalle en la spec, §2 y §9.

## 4. `JumpToTime` (tarea 4, 2026-10-24)

**Uso real.** Hay 20 llamadas en los 10 logs que lo tienen (del 2026-10-17 al 2026-10-24). Solo
`out/run-t1-trace.log` tiene la traza; los demás, el log de AvPlayer.

- **[V]** El juego solo salta con el vídeo en marcha, a `CurrentTime()` + 999–1 033 ms.
- **[V]** En los 20 casos, la siguiente llamada a AvPlayer es `Stop`. En la traza llega en el mismo
  milisegundo, sin ningún fotograma ni bloque de audio entregado entre medias.
- **[V]** `DisableStream`, `SetTrickSpeed` y `ChangeStream` no salen en ningún log: siguen como
  estaban.

**Decisión: implementarlo.** El juego no puede recibir `ORBIS_OK` de algo que no se hizo, y lo que
recibía era 0. Devolver un error explícito habría cambiado lo que ve el juego sin saber si mira el
valor; un salto de verdad le da lo mismo que antes, ahora cierto.

**Comportamiento** (`AvPlayerSource::JumpToTime`, `AvPlayerState::JumpToTime`):

- Solo salta una reproducción en marcha (`Play`, `Pause` o `Buffering`). Antes de `Start`, después de
  `Stop` o al final del vídeo devuelve `0x806a0002` y lo registra.
- Para los tres hilos como `Stop` y busca el fotograma clave anterior al destino
  (`avformat_seek_file`). Vacía las colas de paquetes y los códecs (`avcodec_flush_buffers`), devuelve
  a su reserva los búferes de los fotogramas en cola, pone el reloj en el destino y vuelve a arrancar
  los hilos.
- Los decodificadores descartan lo que acaba antes del destino. El primer fotograma entregado es el
  que está en pantalla en ese instante: 968 para un salto a 1 000. El fotograma que tiene el juego se
  conserva hasta su siguiente `GetVideoData`.
- Más allá del final: en un vídeo en bucle, el tiempo da la vuelta (módulo de la duración). Si no hay
  bucle, va al final y llegan EOF y `StateStop`.
- Al acabar, el juego recibe `WarningId` con `0x806a00a3` (`WAR_JUMP_COMPLETE`), como el
  `WAR_LOOPING_BACK` del bucle. **[H]** Que el AvPlayer real lo emita: el código está en la lista de
  errores del SDK, pero no lo hemos visto en una consola.
- Mientras dura, el salto tiene en exclusiva el mutex del origen: las llamadas del juego esperan. En
  el arnés (320×180) tarda 3–4 ms. En el juego (1920×1088) falta medirlo (tarea 6).

**Tests** (`tests/test_avplayer.cpp`):

- `jump_then_stop`: salto a mitad de reproducción. Comprueba el fotograma, el reloj, que no se pierde
  ninguno hasta el final y que llega el aviso.
- `jump_then_stop_like_the_game`: el patrón del juego, 20 veces. Es `CurrentTime`, `JumpToTime(+1 s)`,
  `Stop`, otro `Stop` y `Close`, con y sin audio.
- `jump_past_the_end_and_when_stopped`: los bordes anteriores.


## 5. Bucle (`titlebg1.mp4`, 2026-10-24)

**[V] El bucle heredado de shadPS4 se rompía en la primera vuelta.** Al llegar a EOF, el
demultiplexor hacía `avio_seek(pb, 0)` antes de `avformat_seek_file`.

- Con el `AVIOContext` de `FileReplacement`, eso hacía releer el `moov` al demultiplexor MOV ("Found
  duplicated MOOV Atom"). Desde ahí cada lectura daba EOF y cada búsqueda fallaba con -1
  (`out/t4-exp/loop2.c`; con una ruta de archivo no pasa).
- El vídeo se congelaba al acabar la primera vuelta. El demultiplexor giraba sin pausa y emitía un
  `WAR_LOOPING_BACK` por giro: 12,7 millones en 2 s con `tiny_v20.mp4`, todos al callback del juego.
  En el juego ya pasó: `out/run-audiodec-a1.log:1280` (2026-10-20) tiene el "duplicated MOOV Atom",
  unos 345 s después de abrirse el audio. Sin log `info` no se ve el giro.
- *Arreglado en la tarea 4:* sin el `avio_seek`. Ahora hay un aviso por vuelta (274 en 2 s), y un
  `JumpToTime` después de varias vueltas cae donde se pide (`loops_back`).

**[V] Queda para la tarea 7, paso 4: el ritmo a partir de la segunda vuelta.** En un vídeo sin audio,
`CurrentTime()` es el tiempo de pared desde `Start` limitado a la duración. Tras la primera vuelta se
queda en la duración y cada fotograma de las vueltas siguientes sale en cuanto se pide: 5 480
fotogramas en 2 s de `tiny_v20.mp4`, en vez de unos 60. En el juego, `titlebg1.mp4` iría al ritmo al
que el juego llama a `GetVideoData` (unas 3 llamadas por fotograma, §1) a partir de los 120 s.
