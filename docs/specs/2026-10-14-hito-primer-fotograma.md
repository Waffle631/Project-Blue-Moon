# Hito — Primer fotograma (y herramienta de captura de píxeles)

Fecha: 2026-10-14. Estado: **borrador para aprobación**. Roadmap: `.waffle/ROADMAP.md`, sección "Hito".

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
- **[V]** No hay captura de píxeles. `vk_presenter.cpp:130` dice "screenshot capture and ImGui overlays
  removed", y `gow3gpu.cpp:328-331` tiene `RequestScreenshot`/`Consume*ScreenshotRequests` como
  funciones vacías. Esto afecta al roadmap: las comparaciones "antes y después" de la Spec 1b y de la
  Spec 6 necesitan esta herramienta, que no existe.
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

### H2 — Contadores de GPU en el log
Si el log no permite saber cuántos draws, flips y envíos hizo el juego, añadir una línea de resumen al
final de la ejecución (en `runtime_report` o en `gow3gpu.cpp`). Entregable: una línea con
`submits`, `draws`, `flips` y número de `Unimplemented …` vistos. Pequeño y genérico.

### H3 — Herramienta de captura de píxeles (decidido: en proceso, 2026-10-14)
Captura en proceso del fotograma presentado, en PNG.

- Disparo por tecla (F12) y por variable `GOW3_SCREENSHOT_AT=<n1,n2,…>` (números de fotograma de
  presentación), con salida en `GOW3_SCREENSHOT_DIR` (por defecto `out/baseline/`).
- Implementación en el presentador (`vk_presenter.cpp`, donde está el comentario de código quitado):
  copia de la imagen presentada a un buffer del host y codificación PNG con FFmpeg (ya enlazado, lo
  usa `gow3_png.cpp` para decodificar). Reutiliza las funciones vacías de `gow3gpu.cpp:328-331` o las
  elimina si sobran.
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
| Coste | Código nuevo en el presentador (copia a CPU + PNG con FFmpeg) y un test; ~1–2 días | Cero código |
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

`gpu/shadps4/video_core/renderer_vulkan/vk_presenter.cpp`, `gpu/shim/gow3gpu.cpp`,
`gpu/shim/gow3_overlay.cpp` (tecla), `tools/compare_captures.sh` (nuevo),
`tests/test_screenshot.cpp` (nuevo: codifica un buffer sintético a PNG y lo decodifica con
`Gow3Png::DecodeRgba`).

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
