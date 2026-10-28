# Spec 6 — 2D correcto, ratón, saves e idioma

Fecha: 2026-10-14. Estado: **en curso**. Los bloques B (ratón), C (saves) y D (idioma) se hicieron el 2026-10-25, en la
rama `tsuki/spec-6bcd`; falta el bloque A (render 2D). Roadmap: `docs/tsuki/ROADMAP.md`, sección "Spec 6".
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

*2026-10-25, pendiente de triage (visto en la Tarea 8, bloque C):* la miniatura de una ranura en el menú de
guardar/cargar del juego sale «un poco pixelada» (lo dice el usuario; la fecha y el resto del menú se ven bien).
No es `icon0.png`, que es una imagen fija del título de 228×128. La miniatura la genera el juego y la guarda
dentro de `savedata000`. **[H]** Puede venir del propio juego (una captura pequeña ampliada) o de cómo se lee
la imagen de la GPU (lectura diferida/obsoleta, filtro). Sin una captura de la PS4 no se puede saber.

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

#### Resultado de la Tarea 1 del bloque A (2026-10-28): entrada determinista

- **[V] El reloj de la repetición son las lecturas del mando, no los ms.** `src/runtime_pad.c` cuenta cada
  llamada a `sample()` en `pad_reads`, también las que se hacen con el menú Insert abierto (la cuenta va antes de
  la vuelta temprana). Antes la repetición iba por reloj y la armaba un script escribiendo `replay` en
  `GOW3_PAD_FILE`. Entre una ejecución y otra, la entrada caía unos fotogramas antes o después.
- **Formato v2, sin compatibilidad hacia atrás.** Primera línea `reads`, luego
  `<lectura> <botones> <lx> <ly> <rx> <ry> <l2> <r2>` en cada cambio de estado. La grabación
  (`GOW3_PAD_RECORD`) y la repetición (`GOW3_PAD_REPLAY`) empiezan las dos en la primera lectura, y F9 solo para la
  grabación. En la lectura `r` vale la última línea con `<lectura> <= r`. La repetición acaba 30 lecturas después de
  la última línea (`Runtime: pad replay finished at read <r>`) y vuelve la entrada real. El token `replay` de
  `GOW3_PAD_FILE` ya no existe, y `out/spec5-route.txt` (en ms) deja de valer.
- **Archivos rotos.** Un archivo sin cabecera (el formato en ms) no se repite: sale
  `... has no 'reads' header (old ms format); record it again`. Ante una línea mal formada, un eje o un gatillo
  mayor que 255, o lecturas que van hacia atrás, la carga para y se avisa con `... line <k> malformed, <n> samples kept`.
  Lo anterior a esa línea sí se repite.
- **[V] `GOW3_MOUSE=0`** (se lee en `sceMouseInit`) hace que el juego vea un ratón ausente, con todo a cero. El
  puente se vacía igualmente, para que lo movido entretanto no llegue después. Log: `Runtime: mouse disabled (GOW3_MOUSE=0)`.
- **Tests.**
  - `out/pad-test`: `replay_waits_for_reads`, `replay_rejects_ms_format`, `replay_stops_at_bad_line`,
    `replay_finishes_and_releases`, `record_round_trip` y `clock_counts_captured_reads`.
  - `out/mouse-test`: `mouse_off`.
  - Se vio el RED de los dos antes de implementar: en el de mando faltaban las funciones, y en el de ratón la
    aserción `connected==0`.
  - `bash build.sh --test` en verde. `--check-imports` sigue en las 3 trampas.
  - Un cambio frente al plan: la entrada «real» que vuelve al acabar la repetición la da el mando virtual de SDL
    del test, no `GOW3_PAD_FILE`. Lo inyectado con `GOW3_PAD_FILE` se suma también durante la repetición, así que
    con él no se distinguiría.
- **[H]** Que las capturas por número de fotograma coincidan entre repeticiones lo mide la Tarea 3.

#### Resultado de la Tarea 2 del bloque A (2026-10-28): herramientas del recorrido

- **`tools/compare_route.sh a/ b/ frames.txt`** generaliza `out/spec1b-t2/compare-oracle.sh`. Cada línea de
  `frames.txt` es `<N> exact`, `<N> mask <x> <y> <w> <h> […]` o `<N> eye`, con `#` para el nombre de la escena.
  Revisa el archivo entero, que estén todas las capturas y que cada recuadro quepa en la imagen antes de comparar
  nada, así que un `frames.txt` roto no imprime ningún resultado (sale 2). Las máscaras se pintan en RGB, sin
  pasar por YUV, para que una diferencia de un paso junto al recuadro no se pierda.
- **`tools/route.sh record | replay <nombre>`** trabaja en `out/route/` (decisión 4 del plan): restaura `user/`
  desde `save/`, escribe `gow3.ini` con `async_shaders=0` si falta, y arranca `run.py` con `GOW3_FULLSCREEN=1`,
  `GOW3_MOUSE=0` y `GOW3_FILE_TRACE=1`. `record` no pisa una grabación que ya exista. `replay` espera a
  `pad replay finished`, da 10 s a las últimas capturas y cierra con SIGTERM. Con `STOP:` o `Fault:` sale con 1.
- **Prueba.** `tests/test_compare_route.py` (6 tests; RED con el script ausente, luego verde). La suite Python
  entera: 273 tests, OK. `route.sh` se probó con una grabación sintética de una muestra y `frames.txt` = 120 y 300
  (el aviso de autoguardado). Las dos repeticiones acabaron en 7 s con las dos capturas. Enteras salen
  diferentes (PSNR ≈ 54.7, el icono «Saving…» que gira), y con el recuadro del icono tapado (`1750 955 80 80`)
  salen idénticas. El save de `user/` no cambió (`sha256sum -c`). La copia fija `out/route/save/` y su
  `user-save.sha256` (paso 1 de la Tarea 3) se hicieron ya para esta prueba.
- **La primera grabación del usuario (2026-10-28) no sirvió.** F9 la cortó en la lectura 1109, cuando acababa de
  salir el menú de título, así que `pad.txt` solo tenía la muestra en reposo y las 4 capturas F12 (2141–7609)
  quedaron fuera. Las instrucciones decían «F9 para la grabación», que se lee como «F9 sirve para grabar». Ahora
  `route.sh record` dice que ya graba desde el arranque y que no se pulse F9. Al acabar, rechaza una grabación sin
  pulsaciones o cortada antes de alguna captura (el juego lee el mando una vez por flip, así que lectura y
  fotograma coinciden) y la mueve a `out/route/failed-<hora>/`.

#### Resultado de la Tarea 3 del bloque A (2026-10-28): el recorrido grabado y su determinismo

- **El recorrido** (`out/route/pad.txt`, 149 muestras hasta la lectura 12669, solo teclado) parte de la copia fija
  de la partida del usuario (`out/route/save/`). Va del menú de título a Load, elige la cuarta ranura («Alice in the
  River of Flames», 2026/10/24 22:57) y sigue la novela hasta la escena del hotel. El usuario pulsó F12 en 6 escenas.
  Los fotogramas 2700 y 2890 (el menú de carga, con las miniaturas) los añadí yo: el usuario pasó por él sin F12, y
  en él está el único defecto conocido. Cada repetición tarda 3 min 40 s.
- **[V] Determinismo.** Se hicieron 4 ejecuciones (la grabación y r1, r2, r3). Se compararon todos los pares en las 6
  escenas, y los dos fotogramas del menú de carga entre r1, r2 y r3. Clasificación (`out/route/frames.txt`):

  | Fotograma | Escena | Clase | Entre ejecuciones |
  |---|---|---|---|
  | 2700, 2890 | menú de carga, cursor en la ranura 1 y en la 4 | `eye` | PSNR 46–49: detrás está `titlebg1.mp4`, que va por reloj real |
  | 4859 | texto largo sobre fondo (prólogo) | `mask 1446 845 64 64` | PSNR 68.3 entero; idéntico con máscara |
  | 5573 | «Wha—?», fundido a negro, texto corto | `mask 216 365 64 64` | PSNR 66.7–72.8 entero; idéntico con máscara |
  | 7744 | salpicadura roja, sin texto | `exact` | idéntico |
  | 10593 | Arcueid en la habitación, sprite sobre fondo | `exact` | idéntico |
  | 11408 | Arcueid en el callejón, filtro gris | `exact` | idéntico |
  | 12964 | diálogo sobre el fundido habitación / Arcueid | `mask 1050 601 64 64` | PSNR 71.1–71.3 entero; idéntico con máscara |

  Las diferencias de los fotogramas con texto caen siempre en el cursor animado de «espera al clic», al final del
  texto: el icono `≡`, o un arco que gira. Ocupa como mucho 42×44 y la máscara es de 64×64 alrededor. Se comprobó a
  ojo que la máscara no tapa texto. Ninguna escena 2D difiere fuera de un recuadro fijo, así que no se aplica la
  regla de parada.
- **Menú de carga.** El fondo cambia en cada ejecución, pero las miniaturas de las ranuras no resaltadas salen
  idénticas. La ranura resaltada lleva un brillo animado. La corrección de la miniatura (Tarea 5/6) podrá comparar
  un recorte de esas ranuras.
- **[V] Sin `STOP:` ni `Fault:`** en las 6 repeticiones (dos tandas de r1–r3) ni en la grabación. Las repeticiones
  acabaron con `pad replay finished at read 12700`. El save de `user/` no cambió (`sha256sum -c`).
- **La primera grabación** no sirvió: F9 la cortó en la lectura 1109 (ver la Tarea 2).

#### Resultado de la Tarea 4 del bloque A (2026-10-28): línea base aprobada

- **[V] El usuario aprobó las 8 capturas** de `out/route/r1/`: 2700 y 2890 (menú de carga), 4859, 5573, 7744,
  10593, 11408 y 12964. No rechazó ninguna. Junto con `frames.txt`, son la línea base en `out/baseline/6a/`, que no
  se versiona. `compare_route.sh out/baseline/6a out/route/r3 out/route/frames.txt` sale con 0.
- **[V] Texto legible.** El usuario dio por buenos los recortes a tamaño real de las ventanas de texto
  (`out/route/review/texto-*.png`): no hay glifos rotos ni cortes de línea incorrectos.
- La miniatura «algo pixelada» de A2 sigue pendiente de triage (Tarea 5). Que el usuario aprobara la captura 2890
  no la descarta: lo que se aprobó es la línea base contra la que se compara el arreglo.

## 4. Bloque B — Ratón (trabajo nuevo)

**Qué falta:** `libSceMouse` debe informar de un dispositivo conectado y entregar movimiento, botones y
rueda reales.

- **Estructura `SceMouseData` [V]** (40 bytes, igual que el `memset` actual): `timestamp (u64)`,
  `connected (u8)`, `buttons (u32)`, `xAxis (i32)`, `yAxis (i32)`, `wheel (i32)`, `tilt (i32)`, resto
  reservado. Se confirma con el sitio de llamada y con `mouse_read` frente a lo que lee el juego.
- **Botones:** primario `0x1` [V], secundario `0x2` [V], medio `0x4` [V]; `0x8` y `0x10` para los extras [H], el juego no los mira.
- **Puente SDL → runtime.** La ventana y sus eventos viven en la biblioteca de GPU (hilo de ventana);
  el runtime solo ve C. Se añade a `gpu/gow3gpu.h` una función
  `gow3gpu_mouse_read(int32_t *dx, int32_t *dy, int32_t *wheel, uint32_t *buttons)` que devuelve y
  reinicia los acumuladores desde `gow3_overlay.cpp` (donde ya se reciben los eventos de ratón) y se
  usa desde `mouse_read`. ~~El movimiento es relativo y se acumula entre lecturas~~ (Tarea 6: el cursor
  del juego sigue la **posición** del puntero, ver «Resultado de la Tarea 6»); los botones son
  estado. Mismo patrón que `gow3gpu_text_input_*` y `gow3gpu_overlay_captures_input`.
- **Con el menú Insert abierto**, el ratón es del menú: el juego recibe un ratón sin movimiento
  (`gow3gpu_overlay_captures_input() == 1`), como el mando.
- **Cursor.** `window.cpp:65` oculta el cursor. Cuando el juego abre el ratón, el cursor de SDL se
  mantiene oculto (el juego dibuja el suyo) ~~y se usa movimiento relativo~~ (Tarea 6: sin modo relativo; el
  cursor del sistema se muestra con el menú Insert abierto); el comentario de esa línea se
  corrige para que no diga "el juego no tiene ratón".
- `mouse_open`/`mouse_close` llevan cuenta de manejadores; `mouse_read` devuelve el número de lecturas
  escritas (1) y `connected=1` solo si SDL tiene un ratón (`SDL_HasMouse()`).

Decisión **[D]**: dónde vive el código. Opción recomendada: sacarlo de `runtime_services.c` a un
`src/runtime_mouse.c` nuevo (resolvedor propio en la cadena de `runtime.c`), porque deja de ser un
dispositivo ausente.

### Resultado de la Tarea 3 (2026-10-25): lo que el eboot hace con el ratón

Desensamblado de `out/eboot.elf` (primer segmento LOAD volcado a `out/spec6-d/eboot.dis`; los huecos GOT salen de
`out/boot.bin`: `sceMouseInit` `0x4356b0`, `sceMouseOpen` `0x4356c0`, `sceMouseRead` `0x4355f8`, vía los
saltos PLT `0x173940`/`0x173960`/`0x1737d0`). **Cada función tiene un solo sitio de llamada y el eboot no importa
`sceMouseClose`.**

- **Arranque `0xef091`–`0xef0bf` [V].** `sceMouseInit()`; si `eax<0` no sigue. Después
  `sceUserServiceGetInitialUser(&user)` y, si devuelve 0, `sceMouseOpen(user, 0, 0, &param)`, con `param` = un
  byte `1` en `[rsp+0x30]` y el resto de la pila sin inicializar. El resultado se guarda **tal cual** en un
  global (`0x2085150`, valor inicial `-1`), error o handle. **Init va antes de Open [V]**, así que exigir
  `NOT_INITIALIZED` no rompe al juego.
- **Lectura `0xcf341` [V].** Una vez por llamada de su función de entrada (`0xcf240`):
  `sceMouseRead(handle_global, buf, 8)` con `buf` global de **320 bytes** (`0x178ef20`–`0x178f05f`, 8 × 40).
  Si `eax<=0` no hay movimiento ni rueda. Si no, recorre **`eax` informes** (zancada `0x28`), así que
  **`eax` nunca puede pasar de `count`**: un valor mayor desbordaría el buffer del juego.
- **Campos que lee de cada informe [V]:** `+12` botones (si el bit 31 está puesto, descarta el informe),
  `+16` `xAxis` y `+20` `yAxis` (los lee juntos como 8 bytes y los suma entre informes), `+24` `wheel`.
  **No lee `timestamp` (+0), `connected` (+8) ni `tilt` (+28).** Lo que el juego nunca lee no puede
  distinguir un ratón conectado de otro ausente: para «sin ratón» sirve cualquier informe con ceros.
- **Qué hace con ellos [V].** `xAxis`/`yAxis` son **deltas** que suma a un cursor propio en un espacio
  virtual de **640×448** (`0x178ec34`/`0x178ec38`, recortado a `0..0x27f` y `0..0x1bf`), sin escala: un
  píxel de movimiento relativo de SDL es una unidad de ese espacio. Los botones del último informe válido
  se guardan (`0x178ef18`) y se convierten en banderas de pulsación/mantenido: bit `0x1` → `0x2`
  (pulsado) y `0x8001` (mantenido), bit `0x2` → `0x8` y `0x4`, bit `0x4` → `0x20` y `0x10`. La rueda suma a
  `0x178ec58`/`0x178ec5c`. Los botones `0x8` y `0x10` no se miran.
- **El cursor del ratón solo manda si no hay dedo en el panel táctil [H].** Antes consulta una fuente
  alternativa (`0x12b420` sobre `0x11f300`, un objeto de dos puntos de contacto de `0x70` bytes con un
  identificador en `+0x20`). Si ese identificador es `>=0` usa esa posición y **ignora los deltas del
  ratón**; si es `<0` usa el ratón. Que sea el panel táctil del mando es una lectura del código, no
  una comprobación: se mira en la Tarea 6 (si el cursor no se mueve con el ratón, empezar por aquí).

**Consecuencias para la Tarea 5 [V]:** la estructura de la spec (`connected` +8 `u8`, `buttons` +12, `xAxis`
+16, `yAxis` +20, `wheel` +24, `tilt` +28) **coincide** con lo que lee el eboot; no hay campos ni tamaños
nuevos. `mouse_read` devuelve 1 y escribe un solo informe de 40 bytes, lo que cumple `ret<=count` con el
`count` 8 de este juego. **El juego no itera hasta `ret==0`**, así que el plan no necesita
`reads_zero_when_idle` (paso 6 de la Tarea 3). Con `count>8` el runtime sigue escribiendo solo un informe.
Devolver `0` sin ratón o un informe de ceros dan lo mismo al juego. El factor 1:1 del movimiento (640
unidades para cruzar el espacio virtual) es una decisión de la Tarea 6, no del runtime.

### Resultado de la Tarea 4 (2026-10-25): el puente `gow3gpu_mouse_read`

- `MouseAccumulator` (`gpu/shim/gow3_mouse.h`, solo cabecera) suma movimiento y rueda como `float` y los entrega
  truncados hacia cero **conservando el resto**: tres movimientos de `(3,-2)`, `(3,-2)` y `(0.6,0.6)` dan
  `(6,-3)` y dejan `(0.6,-0.4)` para la siguiente lectura. Los botones son estado y `Take` no los borra [V]
  (`mouse-accumulator-test`, ejecutado: `ok`). Como `Take` lo llama el hilo del juego y `Motion` el de ventana,
  el acumulador lleva su propio `std::mutex`.
- `HandleEvent` alimenta el acumulador **antes** de sus guardas, sin cambiar lo que devuelve, de modo que el
  juego sigue viendo el estado real de los botones con el menú abierto y ImGui recibe sus eventos [V en el
  compilado; el efecto con el menú abierto se comprueba en la Tarea 5/6].
- `UpdateMouseMode` (cada vuelta de `PollEvents`, también durante el cuadro de texto del juego) activa el modo
  relativo de SDL solo si el juego ha leído el ratón y `CapturesInput()` es falso [H: efecto real en la Tarea 6].
- `gow3gpu_mouse_read` está exportado por `libgow3gpu.so` (`nm -D`) y devuelve `SDL_HasMouse()`. Se llama desde
  el hilo del juego sin que el runtime inicialice vídeo; sin ventana no hay ratones y devuelve 0 [H: lo confirma
  la Tarea 6].

### Resultado de la Tarea 5 (2026-10-25): `src/runtime_mouse.c`

- `libSceMouse` sale de `runtime_services.c` (donde `Open` devolvía un handle y `Read` un informe vacío) a su
  propio archivo, enlazado en `runtime.c` antes de `services`. `tests/test_mouse.c` entra en `build.sh --test`
  como `out/mouse-test`, el nombre del criterio de §10 (RED visto: faltaba `runtime_mouse.c`; GREEN después;
  `bash build.sh --test` completo verde).
- `SceMouseData` de 40 bytes con `_Static_assert` y los offsets que el juego lee (`+12` botones, `+16`/`+20`
  ejes, `+24` rueda) [V: son los que lee el eboot, Tarea 3]. `sceMouseRead` escribe **un**
  informe y devuelve 1 aunque `count` sea 8, porque el juego recorre `ret` informes (probado con un buffer de
  320 bytes: el resto queda intacto).
- Con un menú abierto (`gow3gpu_overlay_captures_input`) el ratón es «mudo»: `connected==1`, ejes, rueda y
  botones a 0, pero el puente se **vacía igualmente**, así que lo movido durante el menú se descarta y no
  llega de golpe al cerrarlo; los botones vuelven con su estado real. Sin ratón (`SDL_HasMouse()==0`) todos los
  campos, también el `timestamp`, son 0.
- Manejadores: tabla de 8 puertos (uno por usuario) con mutex. `Init` repetido devuelve 0; sin `Init`,
  `Open`/`Read`/`Close` dan `NOT_INITIALIZED`; un handle cerrado da `INVALID_HANDLE` en `Read` y `Close`
  y no libera un puerto abierto; reabrir el mismo usuario sin cerrar da `ALREADY_OPENED`; con los ocho
  puertos ocupados `Open` da `FATAL` [H]. Las condiciones son [H] (sin SDK); los valores, [V].
- `sceMouseOpen` imprime siempre `Runtime: sceMouseOpen(user, type, index) -> handle N|error 0x...`.
- `--check-imports` solo lista las 3 trampas de siempre (`scePthreadCancel`, `sceKernelGetModuleInfoForUnwind`,
  `sceKernelInternalMemoryGetModuleSegmentInfo`).

### Resultado de la Tarea 6 (2026-10-25): el ratón en el juego

Probado por el usuario con la partida actual (logs `out/spec6-b/raton.log`, `raton-2.log`, `raton-3.log`; en los
tres `sceMouseOpen(user 1, type 0, index 0) -> handle 0x1` y ningún `STOP:` ni `Fault:`).

- **Primera prueba (deltas 1:1 y modo relativo de la Tarea 4) [V]:** el cursor y la selección del menú de título
  se mueven, los clics funcionan y ratón, mando y teclado avanzan el texto. El panel táctil no tapa al ratón (el
  [H] de la Tarea 3 no se da). Dos defectos: (1) el cursor **se detiene en los bordes**: con un píxel de SDL por
  unidad, 640 píxeles de mano cruzaban todo el espacio de 640×448, tres veces más rápido que el puntero en una
  ventana de 1920 de ancho, y se clavaba en el borde virtual; (2) al abrir el menú Insert **el cursor
  desaparece**.
- **Cambio de diseño:** el cursor del juego sigue la **posición absoluta** del puntero. `MouseAccumulator::Position`
  traduce la posición en la ventana a una celda de 640×448 y `Take` entrega el delta que lleva el cursor del
  juego a esa celda. El acumulador lleva un espejo del cursor del juego: como no sabe dónde empieza, la primera
  lectura tras ver el puntero lo empuja al origen (`-1024`, que el juego recorta a 0); al **llegar** a un borde
  lo empuja contra él (`±1024`), lo que borra cualquier desfase. Quieto en el borde no envía nada, para que un
  puntero aparcado no le quite la selección al mando. Se quita el modo relativo (`UpdateMouseMode`,
  `MarkMouseInUse`).
- **Menú abierto:** lo decide el puente (`Take(..., quiet=CapturesInput())`) y no el runtime, para que el espejo no
  se desincronice entre dos comprobaciones. Mientras el menú esté abierto el juego no ve movimiento, rueda ni
  botones. Al cerrarlo, el cursor del juego salta a donde está el puntero; la rueda girada en el menú se
  descarta. `runtime_mouse.c` ya no consulta `gow3gpu_overlay_captures_input` (`menu_open_is_neutral` pasa de
  `tests/test_mouse.c` a `menu_is_quiet` en `mouse-accumulator-test`).
- **Cursor en el menú:** ImGui no dibujaba su cursor por software (`MouseDrawCursor`). La causa no se buscó: se
  sustituyó por el **cursor del sistema**, que `Gow3Overlay::UpdateCursor` muestra con el menú Insert abierto y
  oculta al cerrarlo. Al abrirlo, ImGui recibe la posición actual del puntero (`SDL_GetMouseState`). Esto no
  tiene test: es una llamada a SDL en el hilo de ventana, y la comprobó el usuario.
- **Segunda y tercera prueba [V]:** el cursor sigue a la mano y llega a los cuatro bordes; clics y avance de texto
  bien con los tres dispositivos; con Insert se ve el cursor del PC y todo funciona. En ventana, el puntero puede
  salir de ella y el cursor del juego se queda en el borde.
- **Sin probar:** el diálogo de texto y la lista de saves con el ratón (pasos 3 y 4 de la Tarea 6). Con ellos
  abiertos, el juego también recibe un ratón quieto (`CapturesInput`). Signos de ejes y rueda: el usuario no
  vio nada invertido.
- **Fuera del bloque:** el usuario notó que el overlay de rendimiento no muestra CPU, RAM ni VRAM. En Linux,
  `gow3_perf_stats.cpp` solo mide el nombre de la GPU: esos valores solo existen en Windows desde `d9358cd`.
  No es de esta spec: el usuario la añadió a la Spec 7 (§9) el 2026-10-25.

#### Códigos de error de libSceMouse

Fuente: tabla de psdevwiki (el usuario la pegó el 2026-10-25); no se tiene el SDK. Los **valores y nombres**
son [V] contra esa fuente; las **condiciones** en que el SDK real los devuelve siguen [H].

| Constante | Valor | Condición que usa este runtime [H] | ¿Lo distingue el eboot? |
|---|---|---|---|
| `SCE_MOUSE_ERROR_INVALID_ARG` | `0x80DF0001` | `data==NULL` o `count<1` en `sceMouseRead` | no: solo mira `eax<=0` |
| `SCE_MOUSE_ERROR_INVALID_PORT` | `0x80DF0002` | `sceMouseOpen` con `type`/`index` distintos de 0/0 | no: guarda el valor y lo usa como handle |
| `SCE_MOUSE_ERROR_INVALID_HANDLE` | `0x80DF0003` | `sceMouseRead`/`Close` con un handle no abierto | no: ídem |
| `SCE_MOUSE_ERROR_ALREADY_OPENED` | `0x80DF0004` | `sceMouseOpen` de un puerto ya abierto | no: ídem (solo llama a Open una vez) |
| `SCE_MOUSE_ERROR_NOT_INITIALIZED` | `0x80DF0005` | `Open`/`Read`/`Close` antes de `sceMouseInit` | no; `Init` va siempre antes |
| `SCE_MOUSE_ERROR_FATAL` | `0x80DF00FF` | no se usa | no |

Ningún sitio compara con una constante `0x80DF....`: `Init` solo mira el signo, `Open` guarda el valor sin
mirarlo y `Read` solo mira `eax<=0` (caso a/c/d de la clasificación). Un `Open` fallido deja un código
negativo como handle, que `Read` rechaza y el juego trata como «sin movimiento»; por tanto **un código mal
elegido no cambia lo que hace el juego**, solo lo que muestra el log.

## 5. Bloque C — Saves

- Grabar una partida desde el menú del juego, cerrarlo, reiniciar y cargar.
- Comprobar en `user/` o en la carpeta de saves del runtime que aparece el archivo/directorio, que
  `sceSaveDataMount2`/`Umount` se llaman en el orden esperado y que el contenido sobrevive al reinicio.
- Si el juego usa `sceSaveDataDirNameSearch`/`SetParam`/`SaveIcon`, validar sus respuestas (ya existen).
- Resultado esperado de los saves con copia (`CheckBackupData`): ver la Spec 3.

### Resultado de la Tarea 7 (2026-10-25): el orden de llamadas en el log

- **[V]** `src/runtime_savedata.c` imprime, además del montaje que ya existía
  (`Runtime: save data 'savedata000' mounted at /savedata0 (created|existing[, read-only])`), tres líneas:
  `Runtime: save data param set (type <t>) on /savedataN`, `Runtime: save data icon saved on /savedataN` y
  `Runtime: save data unmounted from /savedataN`. Solo se imprimen cuando la llamada tiene éxito: un desmontaje de un punto
  que ya no está montado devuelve `0x809F0008` y no imprime nada. `sceSaveDataUmountWithBackup` pasa por el mismo desmontaje
  y también imprime la línea.
- Test: `savedata_call_log` en `tests/test_runtime.c` comprueba el orden montaje → parámetro → icono → desmontaje y que el
  segundo desmontaje no imprime nada. Lo vi fallar antes de añadir las líneas.

### Resultado de la Tarea 8 (2026-10-25): grabar, cerrar, reiniciar y cargar

- **[V]** Test `savedata_survives_remount` (`tests/test_runtime.c`): monta con `CREATE2`, escribe 4096 bytes y desmonta.
  Después lanza un **proceso nuevo** (`--savedata-load`) que monta en solo lectura con 0x60 bloques, como el juego, y lee
  los mismos bytes. Comprueba también que un directorio que no existe da `0x809F0008`. El código ya lo hacía; sin save,
  la mitad de carga aborta.
- **[V] En el juego** (logs `out/spec6-c/save-1.log` y `save-2.log`, con `GOW3_FILE_TRACE=1`): el usuario grabó 3 veces
  desde el menú del juego y cerró la ventana; al reiniciar cargó la partida y volvió donde la había dejado.
  La fecha y la miniatura de la ranura se ven (la miniatura, algo pixelada: ver A2).
  - `savedata000/savedata000` cambió (sha256 `3dbd36eb…` → `0901a20c…`) y sigue midiendo 4 194 304 bytes.
    `icon0.png` se reescribió; `param.bin` no.
  - Ningún `STOP:`, `Fault:`, `CheckBackupData` ni `RestoreBackupData`. La carga no modificó el save.
- **[V] Qué llama el juego y en qué orden:**
  - Al arrancar (en las dos ejecuciones): dos veces `Mount2` en solo lectura → lee entero `/savedata0/savedata000` →
    `Umount`. La carga desde el menú no vuelve a montar: usa lo que leyó al arrancar.
  - Al grabar, desde un hilo `save thread`: `Mount2` para escritura (`existing`) → abre `savedata000` truncándolo
    (`0 bytes` al abrir) y lo reescribe entero → `SaveIcon` → `UmountWithBackup`. Entre un grabado y el siguiente,
    un hilo `load thread` vuelve a montar en solo lectura y relee los 4 MiB.
  - **No llama** a `SetParam`, `DirNameSearch`, `Delete` ni a SaveDataMemory. Las ranuras van todas dentro del único
    archivo de 4 MiB, y `param.bin` se queda como lo dejó el primer montaje (vacío).
- **Riesgo (foco 5), no se cambia aquí:** como el juego trunca el archivo antes de reescribirlo, si el proceso muere a
  mitad de un grabado queda un `savedata000` corto. Además `UmountWithBackup` no guarda copia (Spec 3 §4.4), así que
  `CheckBackupData` no tendría nada que restaurar. En esta prueba la ventana se cerró después de grabar y el archivo
  quedó completo. Un cierre a mitad no se probó.

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
- [x] El texto en inglés es legible, sin glifos rotos ni cortes de línea incorrectos (lo juzga el usuario; Tarea 4 del bloque A, 2026-10-28).
- [x] `out/mouse-test` pasa y, en el juego, el ratón avanza el texto y maneja los menús (Tarea 6).
- [x] Mando, teclado y ratón avanzan el texto (Tarea 6).
- [x] Un save grabado sobrevive al reinicio y se carga (Tarea 8).
- [x] El log con `GOW3_FILE_TRACE=1` muestra recursos `_en` y ninguno de otro idioma (Tarea 2). **[D]** 2026-10-25: el
  usuario lo da por cumplido aunque `ft_ja.mrg` se abra al arrancar, porque se abre con cualquier idioma (§6, «Resultado»).
- [x] El recorrido grabado se repite entero con `GOW3_PAD_REPLAY`, sin `STOP:` ni `Fault:` (Tarea 3 del bloque A, 2026-10-28).

## 11. Riesgos y preguntas abiertas

- El número de defectos 2D es desconocido hasta el hito. Si son muchos, esta spec se divide por tipo
  y el esfuerzo del roadmap (1–3 semanas) se revisa.
- Un único oráculo humano: cada iteración visual pasa por el usuario. Mitigación: la línea base y el
  recorrido repetible reducen cuánto hay que mirar a mano.
- **[D]** ¿Qué escenas exactas forman la línea base? Se propone la lista de A1; el usuario puede
  ampliarla.
