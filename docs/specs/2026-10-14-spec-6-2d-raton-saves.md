# Spec 6 — 2D correcto, ratón, saves e idioma

Fecha: 2026-10-14. Estado: **borrador para aprobación**. Roadmap: `docs/tsuki/ROADMAP.md`, sección "Spec 6".
Empieza tras el hito de primer fotograma; se cierra cuando están hechas las Specs 1b, 3, 4 y 5.

Marcas: **[V]** comprobado en el repo; **[H]** hipótesis a confirmar; **[D]** decisión del usuario.
Esta spec es la más incierta del proyecto: se divide en cuatro bloques (A–D) que pueden convertirse en
specs separadas cuando el trabajo lo pida.

## 1. Objetivo

Que el recorrido del prólogo hasta `phasetitle_01_00` se vea bien (texto, fondos, sprites, fundidos),
se pueda manejar con mando, teclado **y ratón**, se pueda guardar y cargar, y arranque en inglés.

## 2. Hechos comprobados

- **[V]** El **ratón es hoy un stub** (`src/runtime_services.c:536-544`): `mouse_open` devuelve un
  identificador y `mouse_read` rellena 40 bytes a cero ("timestamp 0, connected=false") y devuelve 1.
  Los eventos de ratón de SDL (`SDL_EVENT_MOUSE_*`, `gow3_overlay.cpp:1033-1059`) alimentan solo a
  ImGui, y `gpu/shim/window.cpp:65` oculta el cursor porque "el juego no tiene ratón". No existe
  `src/runtime_mouse.c`.
- **[V]** El mando entra por SDL directamente en el runtime (`src/runtime_pad.c`, `SDL_Gamepad`), con
  teclado como mando y grabación/reproducción (`GOW3_PAD_FILE`, `GOW3_PAD_RECORD` con F9,
  `GOW3_PAD_REPLAY`, `:256-296`).
- **[V]** No hay captura de píxeles (ver el hito): `GOW3_FRAME_CAPTURE_KEY` solo escribe una lista de
  pasos de texto. La herramienta de captura de píxeles y `tools/compare_captures.sh` son entregables
  del hito.
- **[V]** El log de archivos abiertos solo informa de aperturas **fallidas** (`open(...) -> not found`,
  `runtime_file.c:218-233`) y de las que están bajo `sound/` con `GOW3_AUDIO_TRACE` (`:248`), ruta de GoW3.
- **[V]** `sceSystemServiceParamGetInt` ya devuelve idioma 1 (inglés US) salvo `GOW3_LANGUAGE`
  (`runtime_services.c:74-78`). El juego trae `ft_en.mrg` y vídeos `_en`.
- **[V]** `src/runtime_savedata.c` (345 líneas) implementa Initialize, Mount/Mount2, Umount, SetParam,
  SaveIcon, Delete, DirNameSearch y memoria de datos; montajes en `/savedataN`.
  `tests/test_save_backup.py` y `scripts/save_backup.py` cubren copias del lado del lanzador.
- **[V]** Las optimizaciones heredadas que quedan tras la Spec 1b son las genéricas (lectura diferida,
  caché de pipelines, upscalers): ninguna pensada para GoW3.

## 3. Bloque A — Render 2D

### A1 — Línea base de capturas
Con la herramienta del hito, capturar en `out/baseline/` (no versionado) las escenas de la Alpha:
logo(s), título, menú principal, partida nueva, fondo con sprite, ventana de texto con texto largo,
un fundido, un fundido a negro, un vídeo sobre la escena (opening) y el título de capítulo. El usuario
revisa **cada captura** y aprueba o rechaza; solo las aprobadas son la línea base.

### A2 — Triage
Para cada captura rechazada, clasificar el defecto:

| Defecto | Sospecha |
|---|---|
| Texto con bordes rotos o ilegible | Formato de textura de fuente, filtro, alfa premultiplicado, escalado |
| Fundido o máscara incorrecta | Mezcla (blend), modos de mezcla `MIN/MAX`, operación de alfa |
| Pantalla negra entre escenas | Orden de presentación, `Flip`, imágenes de destino sin escribir |
| Colores raros | Formato de superficie (hay avisos de `A2B10G10R10…` y `R32G32B32…` no soportados por la tarjeta) |
| Textura corrupta | Caché de texturas, formato tileado, mip |

### A3 — Corregir con captura antes y después
Cada corrección: captura antes → cambio → captura después → `compare_captures`. Si una optimización
heredada causa el error, **se elimina**, no se desactiva.

### A4 — Recorrido repetible
Grabar el recorrido del prólogo con `GOW3_PAD_RECORD` (F9) y reproducirlo con `GOW3_PAD_REPLAY`, para
repetirlo tras cada cambio y comparar capturas en los mismos fotogramas.
*2026-10-20: lo que muestra el arranque depende de la partida. Con progreso va directo al menú de
título; sin progreso pasa antes por el vídeo del prólogo (Spec 5). Una grabación solo se repite si
cada reproducción arranca con la misma partida, así que el recorrido debe fijar cuál
(`out/savedata-sin-progreso/` es la de sin progreso) y restaurarla antes de reproducir.*

## 4. Bloque B — Ratón (trabajo nuevo)

**Qué falta:** `libSceMouse` debe informar de un dispositivo conectado y entregar movimiento, botones y
rueda reales.

- **Estructura `SceMouseData` [H]** (40 bytes, igual que el `memset` actual): `timestamp (u64)`,
  `connected (u8)`, `buttons (u32)`, `xAxis (i32)`, `yAxis (i32)`, `wheel (i32)`, `tilt (i32)`, resto
  reservado. Se confirma con el sitio de llamada y con `mouse_read` frente a lo que lee el juego.
- **Botones [H]:** primario `0x1`, secundario `0x2`, opcional `0x4`, `0x8` y `0x10` para los extras.
- **Puente SDL → runtime.** La ventana y sus eventos viven en la biblioteca de GPU (hilo de ventana);
  el runtime solo ve C. Se añade a `gpu/gow3gpu.h` una función
  `gow3gpu_mouse_read(int32_t *dx, int32_t *dy, int32_t *wheel, uint32_t *buttons)` que devuelve y
  reinicia los acumuladores desde `gow3_overlay.cpp` (donde ya se reciben los eventos de ratón) y se
  usa desde `mouse_read`. El movimiento es relativo y se acumula entre lecturas; los botones son
  estado. Mismo patrón que `gow3gpu_text_input_*` y `gow3gpu_overlay_captures_input`.
- **Con el menú Insert abierto**, el ratón es del menú: el juego recibe un ratón sin movimiento
  (`gow3gpu_overlay_captures_input() == 1`), como el mando.
- **Cursor.** `window.cpp:65` oculta el cursor. Cuando el juego abre el ratón, el cursor de SDL se
  mantiene oculto (el juego dibuja el suyo) y se usa movimiento relativo; el comentario de esa línea se
  corrige para que no diga "el juego no tiene ratón".
- `mouse_open`/`mouse_close` llevan cuenta de manejadores; `mouse_read` devuelve el número de lecturas
  escritas (1) y `connected=1` solo si SDL tiene un ratón (`SDL_HasMouse()`).

Decisión **[D]**: dónde vive el código. Opción recomendada: sacarlo de `runtime_services.c` a un
`src/runtime_mouse.c` nuevo (resolvedor propio en la cadena de `runtime.c`), porque deja de ser un
dispositivo ausente.

## 5. Bloque C — Saves

- Grabar una partida desde el menú del juego, cerrarlo, reiniciar y cargar.
- Comprobar en `user/` o en la carpeta de saves del runtime que aparece el archivo/directorio, que
  `sceSaveDataMount2`/`Umount` se llaman en el orden esperado y que el contenido sobrevive al reinicio.
- Si el juego usa `sceSaveDataDirNameSearch`/`SetParam`/`SaveIcon`, validar sus respuestas (ya existen).
- Resultado esperado de los saves con copia (`CheckBackupData`): ver la Spec 3.

## 6. Bloque D — Idioma

- Verificar con el log que el juego eligió inglés: carga `ft_en.mrg` y los `*_en.mp4`, y ningún
  recurso de otro idioma. Para eso hace falta un **trazador de aperturas de archivo genérico**:
  reemplazar el filtro `sound/` de `GOW3_AUDIO_TRACE` por `GOW3_FILE_TRACE=1` que registre toda
  apertura (ruta, descriptor, tamaño), no solo las fallidas. Es pequeño y vale para el resto del
  proyecto.
- Si el juego pide otro idioma, mirar el id de `ParamGetInt` que consulta y el valor devuelto.

### Resultado (2026-10-25)

- **[V]** `GOW3_FILE_TRACE=1` (`src/runtime_file.c`) registra cada apertura: `File trace: open(<ruta>) -> fd <n>, <bytes> bytes`
  o `-> errno <e>` (cualquier errno); la línea `Runtime: open(...) -> not found` se conserva. `GOW3_AUDIO_TRACE` queda para
  lecturas fallidas y `runtime_ajm.c`. Test: `tests/test_file_mods.c`.
- **[V]** `sceSystemServiceParamGetInt(language)` se registra la primera vez que se consulta
  (`Runtime: sceSystemServiceParamGetInt(language) -> 1`). Test: `tests/test_services.c`. Logs en `out/spec6-d/`.
- **[V]** Con la partida actual (menú de título) y con `out/savedata-sin-progreso/` (prólogo): una sola consulta de idioma, devuelve 1;
  se abren `if_en.png`, `ft_en.mrg` y `movie/prologue_en.mp4`; ningún `_zc`/`_zt`/`_ko`; sin `STOP:` ni `Fault:`.
- **Hallazgo:** `image/ft_ja.mrg` (9 MB) se abre al arrancar, antes de leer la partida, y `ft_en.mrg` (1,2 MB) después. Control con
  `GOW3_LANGUAGE=0` (`out/spec6-d/idioma-ja.log`): las mismas aperturas en el mismo orden. Es decir, ni `ft_ja.mrg` ni `ft_en.mrg`
  dependen del valor que devuelve `ParamGetInt` en los primeros 60 s; `ft_ja.mrg` no es señal de que el juego tomara el idioma
  equivocado. En el eboot, uno de los sitios que consulta el id 1 (`+0x41755`) traduce 0 → índice 0 (ja), 11 → 2 (zc) y cualquier otro
  valor → 1 (en): hay consumidores del valor, pero no los que cargan estas fuentes. **[H]** Qué consumidor decide qué (menús, textos del
  guion) queda sin comprobar; la lista de aperturas de Tsukihime en inglés es la que se aceptó en el criterio de la §10.

## 7. Requisitos

- Cuando una optimización heredada cause un error visual en Tsukihime, se debe **eliminar** del
  código, no dejarla desactivada.
- Cada corrección de render se valida con captura antes y después.
- El ratón nunca se reporta como conectado si no hay ratón en SDL.

## 8. Pruebas

- `tests/test_mouse.c` (nuevo): con el acumulador del puente simulado, comprobar que `mouse_read`
  entrega movimiento y botones, que el acumulador se reinicia tras cada lectura, que `connected=0`
  sin dispositivo y que con el menú abierto el movimiento es 0. Se enlaza en `build.sh --test`.
- Render, saves e idioma: ejecuciones reales con el recorrido grabado. El criterio visual es del
  usuario; el criterio de ausencia de `STOP:`/`Fault:` es automatizable en el log.

## 9. Archivos

`gpu/shadps4/video_core/...` (a definir en A2), `src/runtime_mouse.c` (nuevo, o la sección actual de
`src/runtime_services.c`), `gpu/gow3gpu.h` y `gpu/shim/gow3_overlay.cpp` (puente de ratón),
`gpu/shim/window.cpp` (cursor), `src/runtime_file.c` (trazador genérico), `src/runtime_savedata.c` si el
guardado revela fallos, `tests/test_mouse.c`.

## 10. Criterios de aceptación

- [ ] Las capturas del recorrido coinciden con la línea base que el usuario aprobó.
- [ ] El texto en inglés es legible, sin glifos rotos ni cortes de línea incorrectos (lo juzga el usuario).
- [ ] `out/mouse-test` pasa y, en el juego, el ratón avanza el texto y maneja los menús.
- [ ] Mando, teclado y ratón avanzan el texto.
- [ ] Un save grabado sobrevive al reinicio y se carga.
- [ ] El log con `GOW3_FILE_TRACE=1` muestra recursos `_en` y ninguno de otro idioma.
- [ ] El recorrido grabado se repite entero con `GOW3_PAD_REPLAY`, sin `STOP:` ni `Fault:`.

## 11. Riesgos y preguntas abiertas

- El número de defectos 2D es desconocido hasta el hito. Si son muchos, esta spec se divide por tipo
  y el esfuerzo del roadmap (1–3 semanas) se revisa.
- Un único oráculo humano: cada iteración visual pasa por el usuario. Mitigación: la línea base y el
  recorrido repetible reducen cuánto hay que mirar a mano.
- **[D]** ¿Qué escenas exactas forman la línea base? Se propone la lista de A1; el usuario puede
  ampliarla.
