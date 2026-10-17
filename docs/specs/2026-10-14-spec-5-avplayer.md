# Spec 5 — Vídeo: AvPlayer

Fecha: 2026-10-14. Estado: **borrador para aprobación**. Roadmap: `docs/tsuki/ROADMAP.md`, sección "Spec 5".
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
  `AudioOut`. El vídeo lo entrega con `GetVideoDataEx` y el juego lo sube a una textura.
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
- **[H] El vídeo podría ir más rápido que el tiempo real.** Datos de `out/run-prologo.log`:

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
| Cuelgue al terminar o saltar | Evento de fin de flujo (`AvPlayerEvent`), cierre y `Close` desde el guest, hilos de decodificación |
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
- [ ] Hay una nota con el contrato real del juego con AvPlayer (paso 1).

## 8. Riesgos

- AvPlayer de shadPS4 es la parte menos madura que se reutiliza (riesgo "Medio" del roadmap).
- 4,5 GB de vídeo: el recorrido de la Alpha solo exige dos; el resto se valida por muestreo, no uno a uno.
- Puede que el juego pase una ruta en lugar de callbacks para algunos vídeos; el paso 1 lo aclara.
