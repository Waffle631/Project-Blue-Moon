# Hito — Primer fotograma (y herramienta de captura de píxeles)

Fecha: 2026-10-14. Estado: **aprobada el 2026-10-18** (con las correcciones de la sección 2 y el plan
de la sección 7). Roadmap: `docs/tsuki/ROADMAP.md`, sección "Hito".

Marcas: **[V]** comprobado en el repo; **[D]** decisión del usuario.

## 1. Objetivo

Saber, con datos reales, cómo dibuja Tsukihime en el traductor de GPU, y decidir el orden del resto
del proyecto. Hoy el juego no ha pasado de `libc.prx` y no se ha visto ni un draw, y el render es la
mayor incógnita. El hito termina con (a) una o más capturas reales, (b) una lista de avisos de GPU y
(c) una decisión anotada.

## 2. Hechos comprobados

- **[V]** `GOW3_FRAME_CAPTURE_KEY=1` más F11 **no hace una captura de pantalla**: escribe un archivo
  de texto `frame_<fecha>_<n>.txt` con la lista de pasos del fotograma
  (`gpu/shadps4/video_core/renderer_vulkan/vk_frame_capture.cpp:97-113`, en `<user>/captures` o
  `GOW3_CAPTURE_DIR`). La tecla solo se activa si la variable vale exactamente `1`
  (`gpu/shim/gow3_overlay.cpp:969-971`).
- **[V]** No hay captura de píxeles **en PNG ni por número de fotograma**. `vk_presenter.cpp:130` dice
  "screenshot capture and ImGui overlays removed", y `gow3gpu.cpp:328-331` tiene
  `RequestScreenshot`/`Consume*ScreenshotRequests` como funciones vacías. Esto afecta al roadmap: las
  comparaciones "antes y después" de la Spec 1b y de la Spec 6 necesitan una herramienta repetible, que
  no existe. *(Corregido el 2026-10-18; ver el hecho siguiente.)*
- **[V]** *(Añadido el 2026-10-18.)* **Sí existe un volcado en bruto.** `GOW3_FRAME_DUMP_TRIGGER=<archivo>`
  (`vk_presenter.cpp`, `PrepareFrame` y `DumpRaw`): si el archivo existe, se borra y ese fotograma
  escribe `display_NNN_WxH.raw` (búfer del guest) y `output_NNN_WxH.raw` (imagen presentada, tras el
  pase de post-proceso) en `GOW3_DUMP_DIR` (por defecto `out/dump`, que debe existir). Copia a un
  buffer del host y escribe en `DeferPriorityOperation`, sin coste si no se pide. No es PNG, no se
  dispara por número de fotograma ni por tecla, y hay que convertirlo a mano. H3 reutiliza esta
  canalización en vez de empezar de cero.
- **[V]** *(Añadido el 2026-10-18.)* **Ya hay contadores, pero no salen en el log.** `Gow3Stats::draws`,
  `dispatches` y `submissions` (`gpu/shim/gow3_toggles.h`) se incrementan siempre
  (`vk_rasterizer.cpp`, `liverpool.cpp`); solo se imprimen cada 5 s con `GOW3_FRAME_STATS=1`
  (`videoout/driver.cpp`). Faltan `flips` y `unimplemented`. *(Corregido el 2026-10-18, paso 1:
  `submissions` solo se contaba con `GOW3_FRAME_STATS=1`; ahora se cuenta siempre.)*
- **[V]** *(Añadido el 2026-10-18.)* **`runtime_report()` no se ejecuta con `--timeout`.** El watchdog
  (`src/probe.c`, `watchdog`) solo vuelca pilas y hace `_exit`, que es cómo terminó el H1. La línea de
  H2 debe salir también desde el watchdog.
- **[V]** El log de arranque ya imprime la GPU elegida y avisos de formatos no soportados (por ejemplo
  `A2B10G10R10SnormPack32`, `R32G32B32*`). Esos avisos son de la tarjeta, no del juego.
- **[V]** `--timeout N` arma un watchdog que vuelca las pilas de todos los hilos
  (`src/probe.c:648`).
- **[V]** `GOW3_GPU_LOG=info` sube el nivel de log de la GPU (por defecto solo avisos,
  `gpu/shim/common/logging/log.cpp:11`).
- **[V]** `GOW3_PAD_FILE`, `GOW3_PAD_RECORD` (tecla F9) y `GOW3_PAD_REPLAY` dan entrada reproducible
  (`src/runtime_pad.c:256-296`).

## 3. Entregables

### H1 — Ejecución hasta el primer dibujo
Tras la Spec 2: ejecutar `GOW3_GAME_DIR=Tsukihime/CUSA47999 python3 run.py` (vía distrobox, con el log
en `out/`) con `--timeout`, `GOW3_GPU_LOG=info`, hasta que el juego (a) emita su primer
`VideoOut`/flip, (b) llegue a otro `STOP:`/`Fault:` o (c) se quede sin pintar. Anotar cuál.

**Resultado (2026-10-17, fin de la fase 2A de la Spec 2): (a).** Con `GOW3_GPU_LOG=debug`
(`out/run-2a-10c.log`) se ve esto:

- `sceVideoOutOpen` nada más entrar en el código del juego (1920×1080, 3 búferes `A8R8G8B8Srgb`);
- el primer `sceGnmSubmitAndFlipCommandBuffers` tras ocultar el splash y montar la partida, y después
  ~60 por segundo (2344 en ~40 s);
- `image/movie/prologue_en.mp4` por AvPlayer (audio y vídeo).

No hay `STOP:` ni `Fault:` en 60 s. Los únicos avisos de GPU son los de formatos que la tarjeta no
soporta (sección 2); ningún `Unimplemented`. A nivel `info` los flips no salen: se registran a nivel
`debug`. Por eso hace falta el H2. Falta saber si hay draws y qué se ve (H2 y H3).

**Comprobación visual (2026-10-17, después de la fase 2A).**

- Capturas de pantalla del escritorio (Spectacle, recortadas al monitor del juego,
  `out/game-{15,30,45,60,80,100,125}.png`, sin versionar). Muestran el vídeo del prólogo: imagen
  correcta y subtítulos en inglés, que vienen dentro del propio MP4. Incluso el texto con serifa del
  principio ("I awake suddenly.") es del vídeo.
- El usuario confirma que el sonido se oye bien. En otra ejecución (`out/run-prologo.log`, Spec 2,
  sección 4.1) el vídeo se ve entero hasta el `STOP:` de Audiodec.
- **Hay draws:** AvPlayer deja cada fotograma en memoria del guest, y solo llega a la pantalla a
  través de los comandos de GPU del propio juego (textura + dibujo + flip).
- **Primer 2D del propio juego:** antes del vídeo, el juego muestra un aviso sobre el autoguardado
  (lo vio el usuario; no hay captura). No es un vídeo: en esas ejecuciones AvPlayer solo abre
  `prologue_en.mp4`. Lo dibuja, por tanto, el motor del juego.
- **No sustituye a H2 ni a H3:** no hay contadores, las capturas son externas (no del fotograma
  presentado) y del 2D propio del juego solo se ha visto ese aviso, sin captura ni juicio sobre su
  aspecto (fondos, sprites y texto de la novela todavía no se han visto).

### H2 — Contadores de GPU en el log
Si el log no permite saber cuántos draws, flips y envíos hizo el juego, añadir una línea de resumen al
final de la ejecución (en `runtime_report` o en `gow3gpu.cpp`). Entregable: una línea con
`submits`, `draws`, `flips` y número de `Unimplemented …` vistos. Pequeño y genérico.

Decidido (2026-10-18): el log no lo permite (los contadores existen pero no se imprimen). Se reutilizan
`Gow3Stats::draws/dispatches/submissions`, se añaden `flips` (en `Presenter::Present`) y `unimplemented`
(en `Common::Log::Write`, solo a nivel ≥ `warn`), se exponen con `gow3gpu_get_stats` y la línea
`GPU: submits=… draws=… dispatches=… flips=… unimplemented=…` sale de `runtime_report` y del watchdog.

### H3 — Herramienta de captura de píxeles (decidido: en proceso, 2026-10-14)
Captura en proceso del fotograma presentado, en PNG.

- Disparo por tecla (F12) y por variable `GOW3_SCREENSHOT_AT=<n1,n2,…>` (números de fotograma de
  presentación), con salida en `GOW3_SCREENSHOT_DIR` (por defecto `out/baseline/`).
- Implementación en el presentador (`vk_presenter.cpp`): la copia de la imagen presentada a un buffer
  del host **ya existe** (`frame_dump` y `DumpRaw`, ver sección 2); se generaliza para que, además del
  `.raw`, pueda entregar un PNG codificado con FFmpeg (ya enlazado, lo usa `gow3_png.cpp` para
  decodificar). La lógica pura (lista de fotogramas, nombres, codificación PNG) va en un módulo nuevo
  `gpu/shim/gow3_screenshot.{h,cpp}`, sin Vulkan, para poder probarla. Solo se captura `output` (sin el
  overlay de ImGui). Si el formato del swapchain no es `B8G8R8A8*` ni `R8G8B8A8*`, se avisa y no se
  captura (nunca un PNG con los canales cambiados).
- **Decidido (2026-10-18):** las funciones vacías `VideoCore::RequestScreenshot/Consume*` de
  `gow3gpu.cpp:328-331` se dejan como están: son la API de upstream para capturas con y sin overlays,
  y H3 usa su propio módulo.
- El número de fotograma de presentación es un contador de llamadas a `PrepareFrame` (una por flip del
  guest).
- Comparación: `tools/compare_captures.sh a.png b.png` con el filtro `psnr` de ffmpeg
  (`ffmpeg -i a.png -i b.png -lavfi psnr -f null -`); "idéntico" = `inf`. Sin dependencias nuevas.
- Determinismo: las escenas estáticas (logo, ventana de texto, menú) son repetibles; para el resto
  se usan el recorrido grabado (`GOW3_PAD_REPLAY`) y el número de fotograma.

Se descartó la alternativa de captura manual con KDE Spectacle: sirve para mirar, pero no es repetible
fotograma a fotograma y no sirve de oráculo para las Specs 1b y 6. Spectacle puede seguir usándose a
mano para mirar, pero no sustituye a esta herramienta.

**Comparación de las dos opciones (elegida: en proceso)**

| | En proceso (PNG del fotograma presentado) | Manual (KDE Spectacle) |
|---|---|---|
| Qué captura | La imagen exacta que el juego presenta, sin marco ni escalado del escritorio | La ventana tal como la compone Plasma/Wayland (marco, escalado fraccionario, gestión de color) |
| Repetibilidad | Se pide por número de fotograma; con `GOW3_PAD_REPLAY` sale la misma escena cada vez | Depende de cuándo pulses; distinta por pocos milisegundos o por el tamaño de la ventana |
| Comparación "antes/después" | Se puede comparar píxel a píxel (`psnr` = `inf` si es idéntico) | Solo a ojo o con una tolerancia que oculta cambios pequeños |
| Automatización | Total (sin intervención) | Ninguna: la haces tú cada vez |
| Coste | Código nuevo en el presentador (PNG con FFmpeg sobre la copia a CPU que ya existe) y un test; ~1 día (estimación original 1–2, revisada el 2026-10-18) | Cero código |
| Impacto en rendimiento | Solo cuando se pide | Ninguno |
| Sirve de oráculo para Spec 1b y Spec 6 | Sí | No (solo para mirar si "se ve bien") |
| Riesgo | Tocar el presentador de shadPS4 | Reducir la Spec 1b a borrar las heurísticas dormidas |

Motivo de la decisión: es la única opción que permite saber con certeza que un borrado o una
corrección no cambió la imagen. El coste de 1–2 días se paga la primera vez que una comparación evita
una búsqueda larga de un defecto.

Las capturas **no se versionan** (`out/baseline/` está ignorado): son imágenes del juego.

### H4 — Decisión
Con H1–H3 en la mano, el autor y el usuario deciden según la tabla del roadmap y re-estiman las
specs restantes.

| Resultado | Decisión |
|---|---|
| Hay imagen correcta (logo legible, colores bien) | seguir con el orden del roadmap |
| Hay draws pero la imagen está mal (negra, colores raros, corrupción) | abrir una spec de renderizado antes de las Specs 4 y 5 |
| No llega a pintar | es otro `STOP:`/`Fault:`: volver a la Spec 2/3 |

## 4. Archivos

`gpu/shadps4/video_core/renderer_vulkan/vk_presenter.cpp`, `gpu/shim/gow3gpu.cpp`, `gpu/gow3gpu.h`,
`gpu/shim/gow3_toggles.h`, `gpu/shim/common/logging/log.cpp`, `gpu/shim/gow3_overlay.cpp` (tecla),
`gpu/CMakeLists.txt`, `src/runtime.c`, `src/probe.c` (watchdog), `docs/ENTORNO.md`,
`gpu/shim/gow3_screenshot.{h,cpp}` (nuevo), `tools/compare_captures.sh` (nuevo),
`tests/test_screenshot.cpp` (nuevo: codifica un buffer sintético a PNG y lo decodifica con
`Gow3Png::DecodeRgba`), `tests/test_compare_captures.py` (nuevo).

## 5. Criterios de aceptación

- [ ] El log termina con la línea de contadores de GPU (H2) o se documenta que ya existe.
- [ ] Existe al menos una captura PNG del juego en `out/baseline/`, hecha con la herramienta (H3).
- [ ] La herramienta de captura pasa su test con un buffer sintético.
- [ ] `tools/compare_captures.sh` devuelve "idéntico" para una captura contra sí misma y una
  diferencia medible para dos distintas.
- [ ] La decisión (H4) queda escrita en el roadmap, con el esfuerzo de las specs re-estimado.

## 6. Riesgos

- El presentador es código de shadPS4 con cambios nuestros; añadir una copia a CPU puede costar
  rendimiento si siempre está activa. Debe ser a petición.
- Una captura sola puede ser engañosa; por eso se piden varias escenas en la Spec 6.
- El conteo de `unimplemented` depende del nivel de log: con `GOW3_GPU_LOG=error` se pierde.
- El número de fotograma de presentación puede variar entre ejecuciones (el aviso de autoguardado
  puede tardar distinto); el paso 5 comprueba el determinismo antes de fiarse de él.

## 7. Plan de implementación (aprobado el 2026-10-18)

Estimación: H2 ~0,25 día, H3 ~1 día, capturas y H4 ~0,5 día. Cada paso compila y pasa tests antes del
siguiente. Todo se compila y ejecuta en el distrobox `ps4dev` (`docs/ENTORNO.md`), con logs en `out/`.

0. **Spec corregida y aprobada.** ✅ (2026-10-18) Secciones 2, H2, H3 y 4 actualizadas con el volcado
   en bruto, los contadores existentes y el watchdog.
1. **H2, contadores.** ✅ (2026-10-18) Ejecución de 60 s con `--timeout 60`: el log termina con
   `GPU: submits=7074 draws=145653 dispatches=0 flips=3537 unimplemented=0` (~59 flips/s).
   Cambios respecto a lo previsto: `submissions` pasa a contarse siempre (antes solo con
   `GOW3_FRAME_STATS=1`, ver sección 2); `flips` se cuenta al entrar en `Present`, antes de los
   `return` por ventana minimizada o imagen del swapchain perdida; `unimplemented` también cuenta
   `unimplemented` en minúscula (`vk_pipeline_cache.cpp`); el formato de la línea vive en
   `runtime_gpu_stats_line` (`runtime.c`, sin stdio), que usan `runtime_report` y el watchdog.
   - `Gow3Stats` (`gow3_toggles.h`): añadir `flips` y `unimplemented`.
   - `Presenter::Present`: `flips++` con la misma condición que `DebugState.IncFlipFrameNum()`
     (`!is_reusing_frame && is_game_frame`).
   - `Common::Log::Write` (`log.cpp`): `unimplemented++` si el mensaje contiene `Unimplemented`.
   - `gow3gpu.h`/`gow3gpu.cpp`: `Gow3GpuStats` y `gow3gpu_get_stats` (solo lecturas atómicas).
   - `runtime_report` (`runtime.c`) imprime la línea `GPU:`; el watchdog (`probe.c`) la escribe con
     `write(2, …)` desde un búfer local, sin stdio en el manejador.
   - Test: `tests/test_runtime.c` comprueba que `runtime_report` imprime la línea `GPU:`.
2. **H3a, módulo de captura** (`gow3_screenshot.{h,cpp}`, sin Vulkan): `ParseFrameList` sobre
   `GOW3_SCREENSHOT_AT`, `GOW3_SCREENSHOT_DIR` (por defecto `out/baseline/`, se crea),
   `RequestNow`/`ShouldCapture`, `FileName` (`frame_<NNNNNN>_<w>x<h>.png`) y `EncodePng` con FFmpeg
   (`Bgra`/`Rgba`). Test `screenshot-test` en `gpu/CMakeLists.txt`, junto a `png-decode-test`:
   codifica un buffer sintético, lo decodifica con `Gow3Png::DecodeRgba` y compara píxel a píxel; prueba
   también la lista de fotogramas. Se ejecuta con `ninja -C out/gpu screenshot-test && out/gpu/screenshot-test`.
3. **H3b, presentador y tecla.** `PrepareFrame` cuenta fotogramas y, si `ShouldCapture(n)`, reutiliza el
   camino de `frame_dump` para `output` (generalizar `DumpRaw` en vez de duplicarlo); el callback
   codifica el PNG. F12 en `gow3_overlay.cpp:HandleEvent` llama a `RequestNow()` (sin variable de
   activación, a diferencia de F11). Una línea de log por captura: `Screenshot: <ruta> (fotograma N)`.
4. **H3c, comparación.** `tools/compare_captures.sh a.png b.png` (`psnr` de ffmpeg): `idéntico` si el
   promedio es `inf` (salida 0), `diferente: PSNR=<dB>` (salida 1), error de uso (salida 2).
   `tests/test_compare_captures.py` (se salta sin `ffmpeg`) con PNG sintéticos.
5. **H4, ejecución real.** Barrido con `GOW3_SCREENSHOT_AT=$(seq -s, 120 300 3000)` y
   `GOW3_GPU_LOG=info` (log en `out/run-hito.log`); elegir al menos dos escenas (aviso de autoguardado
   y un fotograma del vídeo), repetirlas y comprobar su determinismo con `compare_captures.sh`; leer la
   línea `GPU:`; aplicar la tabla de H4; escribir la decisión y la re-estimación en el roadmap.

**Verificación:** `bash build.sh --test` (10/10 de la línea base), `python3 -m unittest discover -s tests`
(sin regresiones sobre 238 OK / 37 saltados), `out/gow3-probe --vulkan-only` en PASS, ejecución de
60 s con `--timeout` cuyo log termina con `GPU:`, al menos un PNG en `out/baseline/` y comprobar que
sin petición de captura no hay copia a CPU (los FPS no cambian).
