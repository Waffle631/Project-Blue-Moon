# Spec 1b — Eliminar las heurísticas de GPU de GoW3

Fecha: 2026-10-14. Estado: **aprobada (2026-10-24), en ejecución**: el plan de implementación está aprobado y sus
tareas 0 (línea base de tests), 1 (clasificación, §2.1) y 2 (capturas «antes» y fotogramas oráculo) hechas (§7). La
Tarea 1 amplió el alcance, por decisión del usuario, a los hashes fijos de shaders del upscaler temporal. Tras la
Tarea 2 el usuario redujo el alcance (§2.2, 2026-10-25): de la lectura lineal solo se quita el valor por defecto, y el
MSAA por vendor sale de la spec. La Tarea 4 (ese valor por defecto) está hecha: no tenía efecto en Tsukihime (§7).
Roadmap:
`docs/tsuki/ROADMAP.md`, sección "Spec 1b".
Depende de: Spec 1a y del hito de primer fotograma (necesita su herramienta de captura de píxeles).

Marcas: **[V]** comprobado en el repo; **[H]** hipótesis a confirmar; **[D]** decisión del usuario.

## 1. Objetivo

Quitar del traductor de GPU los ajustes pensados para God of War III, de uno en uno, comprobando con
la misma captura antes y después que Tsukihime no cambia (o que el cambio está explicado).

## 2. Inventario (hechos comprobados el 2026-10-14)

| Heurística | Dónde | Qué hace | ¿Activa para Tsukihime? |
|---|---|---|---|
| `gbuffer_draw` | `vk_rasterizer.cpp:269-270` (se define), `:1336` (se pone a falso), `:1819`, `:2121`, `:2182`, `:2187` | Marca el draw de G-buffer (≥5 render targets con profundidad). Alimenta el vector de movimiento y la captura | **[V] Dormida**: depende de `camera_motion->Enabled()`, que está apagado por defecto ("Object motion: off" en `out/first-run.log`) |
| Constante de cámara de 864 bytes | `vk_rasterizer.cpp:2121`, `:2182` | Reconoce el buffer de constantes de cámara de GoW3 por su tamaño | **[V] Dormida**, la condición exige `gbuffer_draw` |
| Escalado de la escena a 1920×1080 | `vk_rasterizer.cpp:3044-3068` | "El viewport de la película de UI es 1920×1080 aunque la escena sea 960×540"; los pasos de media resolución mantienen el factor de la escena (`size/1920`, `size/1080`) | **Decidido: se conserva.** El proyecto trabaja de base a 1080p, así que esta lógica es la resolución base y no es específica de GoW3. No se borra; se documenta y, si hace falta, se renombra en la Spec 7 |
| MSAA por vendor | `gpu/shadps4/video_core/amdgpu/gow3_msaa_default.h`, decisión en `vk_instance.cpp`, propiedades en `tools/gpu_capabilities.c` | En AMD y Intel propietario en Windows, crea las imágenes multimuestra con una muestra | **[V]** No incluye RADV (el driver de Linux): la 6650 XT de pruebas mantiene el multimuestreo del juego. **[D] Se queda (2026-10-25, §2.2):** es un parche de driver, no del juego |
| `GOW3_READBACK_LINEAR=1` por defecto | `run.py:216` | Activa lectura lineal de imágenes para la CPU | **[H] Activa**; cambia rendimiento más que imagen. **[D] Solo se quita el valor por defecto (2026-10-25, §2.2):** la opción de shadPS4 que enciende se queda. **[V] Sin efecto (Tarea 4, §7):** con la opción encendida, Tsukihime no metió ninguna imagen en `download_images`. Valor por defecto quitado |
| Hashes fijos de shaders en el upscaler temporal | `ui_composition.h:25-30` (`MovieShader`: 5 hashes de vertex shaders de la UI Scaleform), `vk_temporal_upscaler.h:197` (`trigger_hash = 0x9a9cf8a9`, el compute antes del cual se escala), `:215` (`ui_trigger_vs = 0x34e8a281`); usos en `vk_temporal_upscaler.cpp:361,1306,1321` y `tests/test_ui_composition.cpp:19-21` | Deciden en qué compute se escala la escena y qué draws son UI. Son de otro juego: entraron el 2026-09-26 (`0403703`, `ff221f7`), cuando el upscaler aún se llamaba `BB_UPSCALER`; no son shaders de Tsukihime | **[V] Dormidos** bajo `run.py`: fija `GOW3_UPSCALER=none` (`run.py:214-215`) y todas las llamadas al upscaler exigen `upscaler->Enabled()` (`vk_rasterizer.cpp:1012,1236,1353`). **[D] Se borran (2026-10-25)**: los valores por defecto pasan a «sin disparador»; `GOW3_UPSCALE_BEFORE_CS` y `GOW3_UI_TRIGGER_VS` siguen permitiendo fijarlos |
| Otros restos de GoW3 en el núcleo | `liverpool.cpp` (46 coincidencias `gow3`), `texture_cache.cpp`, `vk_pipeline_cache.cpp` (43), `videoout/driver.cpp` (65), `vk_frame_capture.cpp`, `camera_motion.comp`, `motion_history.h`, `vk_lighting_test.h`, `vk_temporal_upscaler.h` | Mezclan marcas `gow3:` (cambios nuestros sobre shadPS4, que **se conservan**) con comentarios o nombres de GoW3 | **[V] Clasificados (2026-10-25, §2.1)**: comentarios y lógica genérica. Hay dos excepciones: los hashes de la fila anterior y el clasificador de 864 bytes de `motion_history.h`, que se queda |

Nota importante: las 2551 coincidencias de `gow3` no son todas código de GoW3. Casi todas son la
**marca de cambios propios** (`gow3:`), que es genérica y se renombra en la Spec 7. Esta spec solo borra
lógica específica de GoW3.

### 2.1 Clasificación de coincidencias (Tarea 1, 2026-10-25)

`git grep -nE 'God of War|GoW3 specific|gbuffer|864|1920|1080|Kratos' -- gpu/shadps4/video_core gpu/shim` da
**65 coincidencias** en 23 archivos (`out/spec1b-t1/grep.txt`, sin versionar). Todas están en la tabla, sin
cambiar código. Rutas relativas a `gpu/shadps4/video_core/` salvo las de `gpu/shim/`.

| Coincidencias (archivo:línea) | Clase | Detalle y destino |
|---|---|---|
| `renderer_vulkan/vk_rasterizer.cpp:269,270,1336,1819,2187`, `vk_rasterizer.h:336` | lógica específica de GoW3 | `gbuffer_draw`. Se borra en la Tarea 3 |
| `renderer_vulkan/vk_rasterizer.cpp:2121,2182` | lógica específica de GoW3 | Constante de cámara de 864 bytes; exige `gbuffer_draw`. Se borra en la Tarea 3 |
| `renderer_vulkan/motion_history.h:45` (`size == 864`) y su comentario `:40` | lógica específica de otro juego | Clasificador de buffers de los vectores de movimiento por objeto. Según su comentario, la calibración es de otro juego y está «still to be recalibrated for God of War III». Lo prueba `tests/test_motion_history.cpp:86` y lo usa también `vk_pipeline_cache.cpp:892-900`. **Se queda, como excepción escrita** (decisión del 2026-10-24: `ObjectMotion` queda inerte) |
| `renderer_vulkan/vk_camera_motion.h:72` | comentario | Describe la firma de 864 bytes de `CameraMotion::OnConstants`, que se queda sin llamadores tras la Tarea 3. Es excepción escrita, como la fila anterior |
| `renderer_vulkan/vk_camera_motion.h:57` | comentario | Documenta `RenderSize()`: los destinos pueden ser mayores que la escena (1916×1080 para 1916×1078). Es genérico |
| `renderer_vulkan/vk_rasterizer.cpp:3044,3063,3068` | lógica genérica | Escalado a 1920×1080. Se conserva (comprobación del paso 3, abajo) |
| `renderer_vulkan/ui_composition.h:21-22` (`NativeViewport`), `:35-36` (`Scale`) | lógica genérica | Viewport nativo de 1080p y factor de escala de la UI |
| `renderer_vulkan/ui_composition.h:26` | comentario | Justifica `MovieShader` (hashes fijos, abajo). Se borra con ella |
| `renderer_vulkan/scene_resolution.h:10`, `vk_scene_resolution.cpp:72,74,96,111`, `vk_scene_resolution.h:29,32` | lógica genérica | Proxies de la escena a 1920×1080 y a media resolución (960×540); solo actúan con `upscaler->RasterScaling()` |
| `renderer_vulkan/vk_temporal_upscaler.cpp:194,195,352,354,1283,1288`, `vk_temporal_upscaler.h:8,9,150,152,214` | lógica genérica | Tamaño base de 1080p del upscaler y comentarios que lo describen |
| `renderer_vulkan/vk_temporal_upscaler.h:2` | comentario | «Off for God of War III». El upscaler sigue apagado por defecto (`run.py:215`) |
| `renderer_vulkan/fsr411/fsr411.cpp:581,582,742,743`, `fsr411.h:45` | lógica genérica | Juegos de recursos de FSR 4 según el tamaño de salida (1080p o 2160p) |
| `host_shaders/fsr/ffx_fsr1.h:146` | comentario | Ejemplo del SDK de AMD (FSR 1) |
| `host_shaders/camera_motion.comp:3` | comentario | `CameraMotion` se queda (decisión del 2026-10-24) |
| `renderer_vulkan/vk_presenter.h:114-116` | lógica genérica | Tamaño de fotograma esperado de 1080p |
| `renderer_vulkan/vk_frame_capture.cpp:229` | comentario | La regla es genérica: una pasada que tiene un display buffer como único destino marca la frontera de fotograma. El comentario explica, con el caso de GoW3, por qué no basta con que escriba un display buffer |
| `renderer_vulkan/vk_lighting_test.h:3` | comentario | Origen de los cuatro ajustes de `GOW3_LIGHTING_TEST` (build de KrachDev). Es un diagnóstico opcional: sin la variable está apagado, y no depende del juego |
| `amdgpu/liverpool.cpp:914` | comentario | El aviso único de `IT_SET_PREDICATION` es genérico; el comentario da como motivo que GoW3 lo emitía unas 1000 veces por fotograma |
| `amdgpu/gow3_msaa.h:4` | comentario | Explica el MSAA por vendor. El archivo se queda: la Tarea 5 salió del alcance (§2.2) |
| `texture_cache/texture_cache.cpp:109` | comentario | Regla de bytes del modo `GOW3_STALE_READBACK=1`, que exige `GOW3_DEFERRED_READBACK=1`. Sin variable los dos están apagados, pero `run.py` los enciende (`scripts/graphics_options.py:14-15`; corregido en la Tarea 2, §7). La regla es genérica; el comentario cita el caso de GoW3 que la motivó |
| `texture_cache/texture_cache.cpp:161` | comentario | Registrar todas las copias y esperar a la GPU una sola vez es genérico; el comentario cita el rendimiento medido en GoW3 |
| `gpu/shim/core/emulator_settings.h:25-26` | lógica genérica | Resolución interna por defecto de 1920×1080 |
| `gpu/shim/gow3_settings.h:23,24,26,77` | lógica genérica | Resoluciones de salida. El «the game's own size» de `:26` (1920×1080) también vale para Tsukihime |
| `gpu/shim/gow3_overlay.cpp:338,991,992` | lógica genérica | Etiquetas de resolución y escala de la UI del menú |
| `gpu/shim/gow3_overlay.cpp:847` | comentario | El acorde R3+L2 que abre el menú se eligió porque L3+R3 era una acción de GoW3. El acorde es genérico y se queda |

Ninguna coincidencia queda **por probar**. Lo único por probar de esta spec es `GOW3_READBACK_LINEAR`, que el
patrón no encuentra y mide la Tarea 4. **Medido (§7, Tarea 4):** 0 inserciones con la opción encendida.

**Fuera del patrón.** Para el paso 4 del plan se hizo una búsqueda complementaria en `video_core`,
`shader_recompiler` y `gpu/shim`: hashes comparados con constantes, `_hash = 0x`, `Scaleform`, `GoW`. Encontró:

- **Hashes fijos de shaders del upscaler temporal** (fila nueva de la tabla de §2): `MovieShader`
  (`ui_composition.h:27-30`), `trigger_hash` (`vk_temporal_upscaler.h:197`) y `ui_trigger_vs` (`:215`). Son
  lógica de otro juego y no forman parte de las cuatro heurísticas. **[D] El usuario decidió el 2026-10-25 borrarlos
  en esta spec** (Tarea 3b del plan).
- `vk_pipeline_serialization.cpp:470`, nombre de hilo `GoW3:ShaderWarmup`: es un nombre y se renombra en la Spec 7.
- `vk_pipeline_cache.cpp` y `core/libraries/videoout/driver.cpp` no tienen coincidencias del patrón. Sus `gow3`
  son marcas y variables `GOW3_*` de opciones genéricas: estadísticas de fotograma, límite de FPS, hilo de
  presentación, `GOW3_INTERP_INT_FIX` para Pascal y shaders asíncronos.

**Comprobación del paso 3 (escalado, `vk_rasterizer.cpp:3041-3069`).** El bloque no tiene constantes de GoW3: solo
usa la base 1920×1080 (`NativeViewport`, `Scale`, `size / 1920`). Sus dos entradas, en cambio, vienen del upscaler
temporal:

- `redirect.native_ui` solo es verdadero en la fase de UI del upscaler (`RedirectColor`,
  `vk_temporal_upscaler.cpp:1792-1798`). Esa fase empieza con los hashes fijos de arriba, o con `scaled_session` y
  un viewport nativo.
- `reduced` exige `upscaler->RasterScaling()` (`vk_rasterizer.cpp:2775`).

Bajo `run.py` el bloque no se ejecuta con Tsukihime. Se conserva tal cual: su única dependencia de otro juego son
esos hashes, que se borran en la Tarea 3b. Después, la fase de UI solo empezará con `GOW3_UI_TRIGGER_VS`, o con
`GOW3_RENDER_RES` y un viewport nativo.

### 2.2 Revisión del alcance (2026-10-25)

Tras la Tarea 2, el usuario preguntó si hace falta borrar las heurísticas. Para que Tsukihime funcione, no: ninguna le
hace falta ni le estorba hoy. Lo que obliga a borrarlas es la regla del proyecto (`CLAUDE.md`: el código de GoW3 se
borra, no se desactiva, y no queda código muerto detrás de un interruptor). Esa regla encaja de lleno en las tareas 3
y 3b, y peor en la 4 y la 5. **[D] El usuario decidió (2026-10-25)** aceptar la propuesta para la Tarea 4 y no
implementar la Tarea 5. Las tareas 4 y 5 del plan y los criterios de §5 ya están reescritos.

| Tarea | Con Tsukihime hoy | Decisión |
|---|---|---|
| 3: `gbuffer_draw` y la constante de 864 | **[V] Dormida** (§2). Solo se activa con 5 o más render targets y profundidad, que una novela visual 2D no tiene **[H]** | Sin cambios: se borra |
| 3b: hashes del upscaler | **[V] Dormidos** (§2). Son shaders de otro juego y no coinciden con los de Tsukihime | Sin cambios: se borran (**[D]** 2026-10-25) |
| 4: lectura lineal | **[V] La única encendida** bajo `run.py`, pero sin efecto: 0 inserciones (§7, Tarea 4) | **[D]** Quitar solo el valor por defecto de `run.py`. La opción de shadPS4 se queda. **Hecho** |
| 5: MSAA por vendor | **[V] Sin efecto en RADV**: el valor por vendor siempre fue 0 en la máquina de pruebas | **[D] No se implementa.** El mecanismo se queda tal cual |

**Tarea 4.** Lo único de GoW3 es `env.setdefault('GOW3_READBACK_LINEAR', '1')` (`run.py:216`), y su comentario lo
justifica por las texturas corruptas de GoW3. Lo que enciende es la opción `readback_linear_images` de shadPS4
(`texture_cache.cpp:38,1066,1097`, sin marca `gow3:`). Con la opción encendida se copian de vuelta a la CPU, en cada
fotograma, todos los render targets lineales o de 8 píxeles de ancho o menos (`FindRenderTarget`, `:1097`) y las imágenes
Storage lineales con dirección del guest (`FindTexture`, `:1066`). El paso 2 del plan borra además la opción entera y
`download_images`, y eso va más allá de esta spec: es núcleo de shadPS4, que `CLAUDE.md` conserva. **[D] Aceptado
(2026-10-25):**
- se quita solo el `setdefault` y su comentario de `run.py`;
- la prueba es un contador temporal de inserciones en `download_images`. Con el valor por defecto da N; sin él, 0; y con
  `GOW3_READBACK_LINEAR=1` en el entorno, otra vez N, lo que demuestra que la opción sigue viva. A eso se suman las
  capturas;
- la opción se queda en shadPS4, y la variable se renombra en la Spec 7;
- el criterio de §5 que exigía que `GOW3_READBACK_LINEAR` desapareciera del árbol ahora exige que `run.py` no la fije.

Resultado (§7, Tarea 4): N = 0, así que el contraste quedó en 0 / 0 / 0. Que la opción sigue viva lo prueba su estado,
que el contador también imprimía: apagada sin la variable y encendida con ella.

**Tarea 5.** `gow3_msaa_default.h` no es una heurística del juego, sino un parche para un fallo de dos drivers de
Windows. El propietario de AMD (#65, RX 6600) y el de Intel (#28, Arc) leen mal una profundidad de 2 muestras usada como
textura multimuestra. GoW3 solo era el juego que lo destapaba. En la RX 6650 XT con RADV el antes y el después salen
idénticos por construcción, así que la comparación no prueba nada. Además es la tarea más grande: unos 10 archivos, el
renombrado de `GuestNumSamples` y una opción del launcher que dejaría de hacer algo. **[D] El usuario decidió no
implementarla (2026-10-25).** El mecanismo se queda tal cual: `gow3_msaa.h`, `gow3_msaa_default.h`, `GOW3_NO_MSAA` y la
columna de MSAA de `gow3-gpu-capabilities --list`. Se renombra en la Spec 7 con el resto de marcas `gow3`. No se hace
el censo de imágenes multimuestra. Esto sustituye la decisión del 2026-10-24 de borrar todo `GOW3_NO_MSAA`. El launcher,
que esta spec no toca, sigue funcionando entero, y no aparece el riesgo de que vuelvan las líneas negras en Windows.

## 3. Diseño

### Paso 1 — Clasificar (sin cambiar código)
Recorrer las coincidencias de `God of War|GoW3 specific|gbuffer|864|1920|1080|Kratos` en los archivos
de la tabla y marcar cada una como: **comentario**, **lógica específica de GoW3**, **lógica genérica**
o **por probar**. Entregable: tabla en este documento. Solo la columna "lógica específica" se borra.

### Paso 2 — Línea base
Con las heurísticas aún presentes, capturar (herramienta H3 del hito) las escenas del recorrido que
existan en ese momento (logo, y las siguientes que alcance el juego). Guardarlas en `out/baseline/`
como `antes-<n>.png`.

### Paso 3 — Borrar una heurística por commit
Orden: de la más dormida a la más activa. (1) `gbuffer_draw` y la constante de 864 bytes; (1b) hashes fijos
de shaders del upscaler temporal (§2.1, decisión del 2026-10-25); (2) lectura lineal por defecto en `run.py` y
`scripts/graphics_options.py`; (3) MSAA por vendor. El escalado a
1920×1080 **no se borra** (1080p es la resolución base del proyecto); el paso 1 solo verifica que no
contenga lógica de GoW3 mezclada. *2026-10-25 (§2.2): (2) se limita al valor por defecto de `run.py`, y (3) sale de la spec.*

Tras cada una: compilar, `bash build.sh --test`, `out/gow3-probe --vulkan-only`, recapturar las mismas
escenas (`despues-<n>.png`) y comparar con `tools/compare_captures.sh`.

### Paso 4 — Resolver diferencias
- Idéntico: seguir.
- Distinto: entender la causa antes de seguir. Si la heurística actuaba sobre algo de Tsukihime, se
  decide si el comportamiento nuevo es mejor; si es peor, la heurística no era específica de GoW3 y se
  conserva, **renombrada y documentada** (no desactivada).

## 4. Requisitos

- Lo genérico (loader, runtime, núcleo de shadPS4, upscalers) no cambia de comportamiento fuera de lo
  explicado en el paso 4.
- La documentación del defecto de RDNA2 (`docs/plans/2026-10-12-gpu-corruption-rdna2.md`) se conserva
  como historia; el riesgo se revisa si aparece algo parecido en Tsukihime.
- Cada cambio de este documento lleva su captura antes/después referida por nombre.

## 5. Pruebas y criterios de aceptación

- [ ] `bash build.sh --test` y `python3 -m unittest discover -s tests` pasan; los tests C++ de GPU
  que sigan siendo genéricos (`ninja -C out/gpu frame-capture-test`, etc.) también.
- [ ] `out/gow3-probe --vulkan-only` sigue en PASS en la RX 6650 XT.
- [ ] *(Viene de la Spec 6, **[D]** 2026-10-28.)* Después de las tareas 3 y 3b, el recorrido grabado de la Spec 6 se
  repite sin cambios: `bash tools/route.sh replay spec1b-despues`, y luego
  `bash tools/compare_route.sh out/baseline/6a out/route/spec1b-despues out/route/frames.txt`, que debe salir con 0.
  El log de la repetición no debe tener `STOP:` ni `Fault:`.
- [ ] Para cada heurística borrada: `compare_captures` reporta "idéntico" en los fotogramas oráculo (§7, Tarea 2)
  o la diferencia está explicada por escrito en la tabla del paso 1.
- [ ] Búsqueda por lista cerrada, con palabra completa, sin `docs/` ni `CLAUDE.md` (que nombra lo borrado):
  `git grep -nwE 'gbuffer_draw'` sin resultados, y `run.py` ya no fija `GOW3_READBACK_LINEAR`. Antes la lista incluía
  `GOW3_READBACK_LINEAR` y `gow3_no_msaa_default`; desde el 2026-10-25 (§2.2) la opción de shadPS4, su variable y el
  MSAA por vendor se quedan.
- [ ] Sin la constante de 864 bytes en `vk_rasterizer.cpp`. Excepción escrita (§2.1): `motion_history.h:45` y el
  comentario `vk_camera_motion.h:72` se quedan con `CameraMotion`/`ObjectMotion`.
- [ ] Sin hashes fijos de shaders en el upscaler temporal:
  `git grep -nE 'MovieShader|0x9a9cf8a9|0x34e8a281' -- . ':!docs' ':!CLAUDE.md'` sin resultados.
- Retirado el 2026-10-25 (§2.2): «el log de arranque ya no menciona `gow3_msaa` ni lectura lineal; `GPU: guest
  multisampling` queda decidido por el driver, no por el vendor». El MSAA por vendor se queda, y la línea
  `GPU: guest multisampling` sigue en el log.

## 6. Riesgos

- **Sin oráculo no se puede hacer.** Depende de la herramienta de captura de píxeles en proceso del
  hito (decidida el 2026-10-14). Si esa herramienta se retrasa, esta spec espera: no se borra nada del
  renderizador sin poder comparar antes y después. **Cubierto (2026-10-25):** la Tarea 2 fijó cuatro fotogramas
  oráculo de dos pantallas distintas (§7).
- Borrar `gbuffer_draw` afecta a los upscalers temporales (usan el vector de movimiento). Están
  fuera de la Alpha; se dejan funcionando solo si lo hacen sin esa heurística.

## 7. Resultado

### Tarea 0 — línea base de tests (2026-10-25)

Rama `tsuki/spec-1b`, cortada de `tsuki/main` en `4567014` con el árbol limpio (solo el submódulo
`fsr-vulkan` aparece modificado: son los parches que aplica `build.sh`). Logs en `out/spec1b-t0/` (sin
versionar), generados por `out/spec1b-t0/run-baseline.sh` dentro del distrobox.

- `bash build.sh --test` (`build-test.log`): sale con 0; 17 líneas PASS/ok (pad, pad-map, runtime,
  file-mods, sema, eventflag ×4, content, mp3, Audiodec, HEVAG, videodec, services, isa-emulate,
  isa-stub) y ningún `SKIP`, igual que al cerrar la Spec 4.
- `python3 -m unittest discover -s tests` (`py.log`): **267 ejecutados / 35 omitidos / 4 fallos
  esperados**, OK. Al cerrar la 1a eran 254 / 37 / — (`out/baseline-py-1a.log`); los tests nuevos vienen
  de las specs 2 a 5. Es la referencia que tienen que repetir las tareas 4, 5 y 6.
- `out/gow3-probe --vulkan-only`: `AMD Radeon RX 6650 XT (RADV NAVI23); command submission + 4096-byte
  readback PASS`.
- **Línea base C++** (`cpp.txt`), que se repite al final de las tareas 3, 4 y 5 compilando con
  `ninja -C out/gpu <destino>` y ejecutando `out/gpu/<destino>`. Los 24 dan 0:
  `motion-history`, `png-decode`, `screenshot`, `aliasing`, `blend-rewrite`, `depth-stencil-state`,
  `async-compiler`, `warmup-inbox`, `wave-stats`, `pass-census`, `frame-capture`, `lighting-test-knob`,
  `amd-paths`, `no-msaa`, `async-pipeline-cache`, `async-fetch-snapshot`, `ui-composition`,
  `upscaler-support`, `readback-queue`, `wait-diagnostics`, `readback-sources`, `page-state`,
  `kernel-thread` y `avplayer` (este con el argumento `tests/data`), todos con el sufijo `-test`.
  Los 22 primeros son los que `out/baseline-cpp-1a.log` daba como `=0` y siguen existiendo
  (`stat-multipliers-test` y `signatures-test` se borraron en la 1a). `kernel-thread-test` y
  `avplayer-test` son posteriores a la 1a; entran en la lista porque pasan y enlazan `libgow3gpu`.
  Fuera de la lista, porque ya fallaban antes de esta spec: `camera-motion-test` y los otros que no
  enlazan en Linux (`runtime_report` sin definir), los que no compilan en Linux y los 9 solo de Windows.
  En la Tarea 5, `no-msaa-test` iba a pasar a llamarse `msaa-samples-test`. Como la Tarea 5 salió del alcance
  (§2.2), conserva su nombre, y la línea base C++ se repite solo al final de las tareas 3, 3b y 4.

### Tarea 1 — clasificación de coincidencias (2026-10-25)

Sin cambios de código. La tabla está en §2.1 y la salida del grep en `out/spec1b-t1/grep.txt`.

- 65 coincidencias. **Lógica específica de GoW3:** las 8 de `gbuffer_draw` y la constante de 864 de
  `vk_rasterizer.cpp`/`.h`, que se borran en la Tarea 3. **Excepciones escritas:** `motion_history.h:45`, con su
  comentario `:40`, y el comentario `vk_camera_motion.h:72`, que se quedan con `CameraMotion`/`ObjectMotion`. El
  resto son comentarios (12) o lógica genérica (42), casi siempre la base de 1080p. No queda ninguna por probar.
- El escalado a 1920×1080 (`vk_rasterizer.cpp:3041-3069`) no tiene lógica de GoW3 propia. Solo corre con el
  upscaler activo, y el único dato de otro juego que recibe son los hashes de shaders.
- **Ampliación del alcance:** la búsqueda complementaria encontró hashes fijos de shaders de otro juego en el
  upscaler temporal, fuera de las cuatro heurísticas. Siguiendo el paso 4 del plan, se paró a preguntar. El usuario
  decidió **borrarlos en esta spec** (2026-10-25): nueva Tarea 3b, después de `gbuffer_draw`, porque también están
  dormidos. Hay un criterio nuevo en §5.

### Tarea 2 — línea base «antes» (2026-10-25)

Mismo binario que en la Tarea 0: desde `4567014` la rama solo ha cambiado la spec. Se usó la partida del usuario, que
tiene progreso y arranca al menú del título; antes de empezar se copió a `out/savedata-usuario-1b/`. Fueron seis
arranques de 40 s sin entrada, a pantalla completa (`GOW3_FULLSCREEN=1`, ventana sin bordes). Los lanzó
`out/spec1b-t2/run-antes.sh`: tomas a–c, fotogramas 120, 300, 420 y 900. Después los lanzó `run-antes-2.sh`: tomas d–f,
con los fotogramas del 960 al 1560 de 60 en 60, para buscar el aviso legal. Capturas en
`out/baseline/1b/antes-<toma>/` y logs en `out/spec1b-antes-<toma>.log`, todo sin versionar. Las capturas usan el nombre de
la herramienta, `frame_<n>_1920x1080.png`, no `antes-<n>.png` (§3, paso 2).

- En las seis tomas las capturas salen de 1920×1080 y `run.py` sale con 142, que es el watchdog a los 40 s. Ninguna da
  `Fault:` ni un `STOP:` de import. Todas llegan al menú del título: su música abre el único `sceAudiodecCreateDecoder`.
  La partida mantiene el mismo `sha256` (`out/spec1b-t2/save-sha-*.txt`).
- Línea `GPU:` final, en este rango: `submits` 4678–4691, `draws` 268 393–276 682, `dispatches=0`, `flips` 2339–2345,
  `unimplemented=0`. Cambia de una toma a otra porque el vídeo sigue el reloj real, así que sirve como orden de magnitud y
  no como oráculo. Las seis tomas dan `GPU: guest multisampling on` y
  `GPU: deferred image readback enabled (64 copies, 64 MiB pending limit); CPU reads use the previous frame's copy`.
  Esta última línea muestra que `run.py` enciende las dos lecturas diferidas (`scripts/graphics_options.py:14-15`). En
  la §2.1 esta fila decía que estaban apagadas; ya está corregida, y la clase de la fila no cambia.
- Fotogramas. Las comparaciones están en `out/spec1b-t2/compare.txt`, `diffbox-def.txt` y `compare-oracle.txt`.
  - **120, 300 y 420: aviso de autoguardado.** De una toma a otra cambian solo en el recuadro del icono «Saving…»
    (x 1751–1829, y 960–1029), que sale en dos fases de su giro. Con el recuadro x 1750–1829, y 955–1034 tapado en negro
    en las dos capturas, salen idénticos en las seis tomas. Sin tapar, ni siquiera el 300 es exacto.
  - **900–1020: vídeo `TM_CI.mp4`** (el logo de TYPE-MOON y su fundido en blanco). Cambia de una toma a otra, así que no
    sirve de oráculo. El 900 ya no es el aviso legal: desde la Spec 5 el logo dura sus 6 s completos y el aviso llega más
    tarde.
  - **1080–1560: aviso legal.** El 1080 y el 1560 caen en los fundidos y cambian. **Del 1140 al 1500 son idénticos** en
    d, e y f.
- **Fotogramas oráculo.** El 120, el 300 y el 420 con el recuadro del icono tapado, y el 1320 entero (aviso legal; está
  en el centro del tramo estable, con 180 fotogramas de margen a cada lado). Son dos pantallas 2D distintas, sin vídeo.
  La referencia es `out/baseline/1b/antes/`: una copia de la toma d, con su log como `run.log`.
- **Receta de recaptura para las tareas 3 a 6.**
  - Un arranque con el mismo entorno y `GOW3_SCREENSHOT_AT=120,300,420,1320`.
  - Dentro del distrobox, `out/spec1b-t2/compare-oracle.sh out/baseline/1b/antes out/baseline/1b/despues-<n>`. Sale
    con 0 solo si los cuatro fotogramas dan «idéntico».
  - Se comprobó que el script detecta un solo píxel cambiado fuera del recuadro y un fotograma de fundido en el lugar
    del 1320.

### Tarea 4 — sin el valor por defecto de la lectura lineal (2026-10-25)

Se hizo antes que las tareas 3 y 3b, a petición del usuario. Es la única que podía cambiar lo que ve la Spec 6.
Desde esta tarea se trabaja en `tsuki/main`, donde se fusionó `tsuki/spec-1b` (avance rápido) el 2026-10-25. Solo
cambia `run.py`: se borraron `env.setdefault('GOW3_READBACK_LINEAR', '1')` y la frase del comentario que lo justificaba
por las texturas corruptas de GoW3. La opción `readback_linear_images` de shadPS4, `download_images` y la variable no
cambian. Logs y scripts en `out/spec1b-t4/`, sin versionar. Cada toma sigue la receta de la Tarea 2 (`run-t4.sh`: 40 s, a
pantalla completa, con la partida del usuario, que siguió intacta tras cada toma).

- **Contador temporal.** Contaba las inserciones en `download_images` en sus dos únicos escritores, `FindRenderTarget` y
  `FindTexture`, y las imprimía cada 5 s junto con el estado de `readback_linear_images`. No se commitea: se quitó
  restaurando el archivo desde git, porque todas sus líneas eran añadidas.

  | Toma | `run.py` | Entorno | Opción | Inserciones |
  |---|---|---|---|---|
  | `t4-con-defecto` | con el `setdefault` | — | encendida | 0 |
  | ruta de la Spec 5 (`run-route-t4.sh`) | con el `setdefault` | — | encendida | 0 |
  | `t4-sin-variable` | sin él | — | apagada | 0 |
  | `t4-con-variable` | sin él | `GOW3_READBACK_LINEAR=1` | encendida | 0 |

- **N = 0: la opción no hacía nada con Tsukihime.** El juego no tiene ningún render target lineal ni de 8 píxeles de
  ancho o menos, y no usa imágenes Storage (`dispatches=0` en todas las tomas). Pasa en el arranque y también en la
  novela. La ruta de la Spec 5 hizo 9056 envíos por el menú, CONTINUE, el título del capítulo 1 y la escena del metro, y
  dio 0. Esa ruta no estaba en el plan: se añadió porque un 0 solo en el arranque no cubría las escenas de la novela. El
  contraste N / 0 / N del plan queda en 0 / 0 / 0. Que la opción sigue viva lo prueba su estado: apagada sin la variable
  y encendida con ella.
- **Tests.**
  - `bash build.sh --test` sale con 0, con las mismas líneas PASS/ok que en la Tarea 0.
  - Python: 267 ejecutados, 35 omitidos y 4 fallos esperados, OK.
  - `out/gow3-probe --vulkan-only`: PASS.
  - Línea base C++: los 24 destinos dan 0, igual que en la Tarea 0 (`out/spec1b-t4/cpp.txt`). El plan decía que no hacía
    falta porque no cambia código de GPU. Se repitió porque esta sección, en la Tarea 0, la pide al final de la Tarea 4.
  - `git grep -n 'GOW3_READBACK_LINEAR' -- run.py`: sin resultados.
- **Capturas.** `despues-4` se tomó con el binario sin contador. `compare-oracle.sh antes despues-4` da **idénticos** el
  120, el 300, el 420 y el 1320 (`out/spec1b-t4/compare-oracle.txt`).
  - Línea `GPU:` final: `submits=4692 draws=272707 dispatches=0 flips=2346 unimplemented=0`, del mismo orden que en la
    Tarea 2.
  - El único `STOP:` es el watchdog de los 40 s, como en la línea base.
  - Sin diferencia de imagen no hacía falta medir el tiempo de fotograma (paso 5 del plan).
- **Para la Spec 6:** lo que queda de esta spec (tareas 3, 3b y 6) está dormido bajo `run.py`, así que su línea base de
  capturas (A1) ya no depende de la 1b.

### Tareas 3 y 3b: preparación (2026-10-28)

Plan nuevo para lo que faltaba: `~/.claude/plans/quiero-que-hagas-un-iterative-gem.md`, que también recoge el criterio
del recorrido de la Spec 6 (§5). Rama `tsuki/spec-1b-cierre`, desde `tsuki/main` en `06401c1`, que ya incluye la Spec 6.
Scripts, logs y capturas en `out/spec1b-t3/` y `out/baseline/1b/`, sin versionar.

- **Línea base de suites en `06401c1`** (`out/spec1b-t3/run-baseline.sh`, copia de la de la Tarea 0; `D=` elige la
  carpeta; resultado en `out/spec1b-t3/base/`). Es la referencia de las tareas 3, 3b y 6.
  - `bash build.sh --test` sale con 0.
  - Python: 273 ejecutados, 35 omitidos y 4 fallos esperados, OK.
  - `out/gow3-probe --vulkan-only`: PASS.
  - C++: los 24 destinos de la Tarea 0 dan 0. Se añadió `mouse-accumulator-test` (nuevo de la Spec 6), que compila y
    pasa, así que ahora son 25. Los que ya no compilaban en la Spec 1a siguen fuera; entre ellos `camera-motion-test`.
- **Receta de recaptura nueva** (`out/spec1b-t3/run-oracle.sh <toma> [VAR=valor …]`, en el lado del host). La de la
  Tarea 2 ya no valía tal cual por dos motivos:
  - usaba la partida de `user/savedata`, que cambió el 2026-10-25 con los guardados de la Spec 6. La que corresponde a
    `out/baseline/1b/antes/` es la copia `out/savedata-usuario-1b/`;
  - desde la Spec 6 el juego ve un ratón conectado, y la receta no ponía `GOW3_MOUSE=0`.

  En cada toma la receta rehace un `GOW3_USER_DIR` propio, `out/spec1b-t3/user/`: copia `user/` sin `savedata` ni
  `savedata_backups` y pone ahí la partida fija. Después arranca 40 s a pantalla completa, sin ratón, con los fotogramas
  oráculo en `out/baseline/1b/<toma>/`. Las variables extra van detrás de las fijas y mandan sobre ellas.
- **Control antes de tocar código:** dos tomas con el binario de `06401c1`, `control-a` y `control-b`
  (`out/spec1b-t3/control-compare.txt`).
  - `compare-oracle.sh antes control-a`: **idénticos** el 120, el 300, el 420 y el 1320. `control-a` frente a
    `control-b`: también idénticos. La receta nueva reproduce la referencia, y `antes/` sigue siendo el oráculo.
  - Las dos salen con 142 (el watchdog de los 40 s) y llegan al menú del título (un `sceAudiodecCreateDecoder`).
    Dan `unimplemented=0`, `GPU: guest multisampling on` y `Runtime: mouse disabled (GOW3_MOUSE=0)`. Línea `GPU:`
    final: `submits` 4682/4690, `draws` 275 001/272 415, `dispatches=0`, del mismo orden que en la Tarea 2.
  - La partida fija sigue igual tras cada toma, y nada de `user/savedata` cambió.
- **Lo único que una toma escribe en `user/`** es `user/custom_trophy/Notice.txt`, con su texto fijo de siempre.
  `run.py:153` lanza `gow3-probe --cpu-features` antes de fijar `GOW3_GPU_USER_DIR` (`run.py:252`). El inicializador
  estático de `path_util.cpp` de shadPS4 cae entonces en `./user` y rehace sus carpetas y ese aviso. Ya pasaba antes de
  esta spec y también pasa con el recorrido de la Spec 6. No toca partidas ni cachés, y no se corrige aquí.

### Tarea B: contraste antes de borrar (2026-10-28)

Bajo `run.py` (`GOW3_UPSCALER=none`) las dos heurísticas están dormidas, así que sus capturas salen iguales por
construcción. La prueba de que no hacían nada con Tsukihime es un contador temporal con las dos encendidas
(`GOW3_EXPERIMENTAL_UPSCALER=1`, que activa a la vez `CameraMotion` y el upscaler temporal). Se midió una vez, para las
tareas 3 y 3b, sobre `61e34a3`. El contador (prefijo `T1B:`, impreso cada 5 s) no se commiteó: se quitó con
`git checkout --`, `git diff` quedó vacío y se recompiló el árbol limpio.

| Contador | Arranque (40 s) | Recorrido de la Spec 6 |
|---|---|---|
| Draws que evalúan `gbuffer_draw` | 272 268 | 5 165 990 |
| … con `camera_motion->Enabled()` | 272 268 | 5 165 990 |
| `gbuffer_draw` verdadero (= llamadas a `OnGBufferPass`) | 0 | 0 |
| Buffers de constantes de 864 bytes, con o sin `gbuffer_draw` | 0 | 0 |
| `OnConstants` | 0 | 0 |
| `TemporalUpscaler::OnDispatch` (`cs_hash == trigger_hash`) | 0 (0) | 0 (0) |
| `TemporalUpscaler::OnDraw` | 272 268 | 5 165 990 |
| … `vs_hash == ui_trigger_vs` | 0 | 0 |
| … `MovieShader(vs_hash)` | 0 | 0 |

- **Arranque:** `run-oracle.sh contraste-antes GOW3_EXPERIMENTAL_UPSCALER=1` (`out/spec1b-t3/contraste-antes.log`).
  El log dice `Upscaler: FSR 3.1 available (off) on scene color before compute shader 000000009a9cf8a9`. Sale con 142
  (el watchdog) con `unimplemented=0`. Sus cuatro fotogramas oráculo son **idénticos** a `antes/`.
- **Novela:** `GOW3_EXPERIMENTAL_UPSCALER=1 bash tools/route.sh replay spec1b-contraste-antes`, dentro del distrobox
  (`out/route/spec1b-contraste-antes.log`). Sale con 0, sin `Fault:`, con `unimplemented=0`. `compare_route.sh` frente a
  `out/baseline/6a/` da 6 fotogramas idénticos y los 2 `eye` (2700, 2890), y sale con 0.
- Aquí la línea del upscaler dice `(on)`: el `on`/`off` es el ajuste `upscaler` del `gow3.ini`. El de la raíz pone
  `upscaler=off`, y `out/route/gow3.ini` deja el valor por defecto. En las dos tomas el upscaler estaba disponible y
  recibía las llamadas.
- **Lectura:** Tsukihime nunca tiene un pase con 5 o más color targets y profundidad, ni buffers de 864 bytes, ni
  dibuja con los shaders de otro juego. `OnDispatch` da 0 porque el juego no lanza compute (`dispatches=0`), así que
  `trigger_hash` no tiene nada con que compararse. Ninguna coincidencia: se puede borrar sin cambiar nada.

### Tarea 3 — sin `gbuffer_draw` ni la constante de 864 bytes (2026-10-28)

Borrado en `vk_rasterizer.cpp` y `vk_rasterizer.h` (38 líneas, todas quitadas):

- en `UpdateRenderTargets`, la asignación de `gbuffer_draw` (pase con 5 o más color targets y profundidad) y la
  llamada a `OnGBufferPass`. El bloque siguiente (`OnColorTarget` y el color de escena) se queda;
- `gbuffer_draw = false;` al principio de `DispatchRecord`;
- las dos lecturas de la constante de cámara de 864 bytes (rama del anillo de constantes y rama de los V#), que
  llamaban a `OnConstants`. En la del anillo queda solo el `push_data.AddOffset` genérico;
- las dos notas de captura de «Object motion research» (`gb vb` y `gb vs`), que solo salían en draws de G-buffer.
  El `FrameCapture::Buffer` genérico de los V# se queda;
- el miembro `gbuffer_draw` de `Rasterizer`.

`OnGBufferPass` y `OnConstants` siguen definidos en `CameraMotion`, sin llamadores (decisión del 2026-10-24).

- **Por qué no cambia nada:** el contador de la Tarea B dio 0 draws con `gbuffer_draw` verdadero y 0 buffers de 864
  bytes, en el arranque y en el recorrido, con `CameraMotion` encendido.
- **Tests** (`D=out/spec1b-t3/t3`): iguales a la base de la Tarea A. `bash build.sh --test` sale con 0; Python da 273
  ejecutados, 35 omitidos y 4 fallos esperados, OK; `--vulkan-only` PASS; los 25 destinos C++ dan 0.
- **Capturas:** `run-oracle.sh despues-3`. `compare-oracle.sh antes despues-3` da **idénticos** el 120, el 300, el 420
  y el 1320. Sale con 142 (el watchdog), sin `Fault:`, con `unimplemented=0`. Línea `GPU:` final:
  `submits=4686 draws=274122 dispatches=0 flips=2343`.
- **Búsquedas:** `git grep -nwE 'gbuffer_draw' -- . ':!docs' ':!CLAUDE.md'` y
  `git grep -n '864' -- gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp` salen vacías. Los 864 que quedan son
  la excepción escrita: el clasificador de `motion_history.h:45` (y su comentario de `:40`), el comentario de
  `OnConstants` en `vk_camera_motion.h:72` y su test, `test_motion_history.cpp:86`.
- **Consecuencia de interfaz:** `depth_id` solo se asignaba en `OnGBufferPass`, y `current.valid` solo en
  `OnConstants`. Sin llamadores, `CameraMotion::Ready()` nunca es verdadero y `camera_motion->Depth()` siempre es nulo.
  Los tres usos de `Depth()` en `vk_rasterizer.cpp` (color de escena en `UpdateRenderTargets`, en el draw y en el
  pase de display) piden antes `db_desc.first` no nulo, así que ya no se cumplen. `CameraMotion`, `ObjectMotion` y la
  parte de escena de `TemporalUpscaler` quedan inalcanzables, incluso con `GOW3_EXPERIMENTAL_UPSCALER=1`: el upscaler
  temporal ya no puede escalar con este juego.
- **Decisión del usuario (2026-10-28):** ese código no se borra. Lo que se quita son las opciones que permiten
  encenderlo (launcher, `gow3.ini`, `run.py`, variables de entorno), porque encendido no hace nada. Va a la Spec 7
  (§10), antes del renombrado.

### Tarea 3b — sin los hashes fijos del upscaler temporal (2026-10-28)

Commit `76b739f`. Se borraron los hashes de shaders de God of War III que el upscaler traía de serie:

- `trigger_hash = 0x9a9cf8a9` (el compute antes del que escalaba la escena) y `ui_trigger_vs = 0x34e8a281` (primera
  pasada de la interfaz), en `vk_temporal_upscaler.h`. Ahora valen 0;
- `UiComposition::MovieShader`, con sus cinco hashes de Scaleform, en `ui_composition.h`.

En su lugar, `UiComposition::Trigger(hash, configurado)` solo dispara con un hash dado por `GOW3_UPSCALE_BEFORE_CS` o
`GOW3_UI_TRIGGER_VS`. 0 significa «sin disparador», así que un shader con hash 0 nunca dispara. Lo usan
`OnDispatch`, el `done_this_frame` de `OnDraw` y la decisión de qué draw es interfaz, que se queda con el disparador
configurado o con el viewport nativo en una sesión escalada. La lectura de las dos variables no cambia. Sin
disparador, la línea del arranque dice `Upscaler: FSR 3.1 available (…) on scene color: no trigger (GOW3_UPSCALE_BEFORE_CS)`.

- **TDD:** `tests/test_ui_composition.cpp` cambia las tres aserciones de `MovieShader` por cuatro de `Trigger`
  (`(0,0)`, `(h,h)`, `(h,0)`, `(h,otro)`). Antes del cambio no compilaba (`'Trigger' was not declared in this scope`,
  `out/spec1b-t3/t3b-red.log`). Después: `UI composition: PASS (…, trigger only when configured)`.
- **Por qué no cambia nada:** el contador de la Tarea B dio 0 en `trigger_hash`, `ui_trigger_vs` y `MovieShader`, en el
  arranque y en el recorrido, con el upscaler encendido.
- **Tests** (`D=out/spec1b-t3/t3b`): iguales a la base de la Tarea A. `build.sh --test` sale con 0; Python da 273/35/4,
  OK; los 25 destinos C++ dan 0; `--vulkan-only` PASS.
- **Capturas, con el upscaler apagado (`run.py`):** `compare-oracle.sh antes despues-3b` da **idénticos** los cuatro
  fotogramas. Sale con 142 (el watchdog), sin `Fault:`, con `unimplemented=0`.
- **Contraste después, con el upscaler encendido** (`GOW3_EXPERIMENTAL_UPSCALER=1`, tareas 3 y 3b aplicadas, sin
  contador):
  - Arranque: `compare-oracle.sh contraste-antes contraste-despues` da **idénticos** los cuatro. El log dice «no
    trigger» y no tiene `Fault:`; el único `STOP:` es el watchdog.
  - Recorrido: `route.sh replay spec1b-contraste-despues` sale con 0, sin `STOP:` ni `Fault:`, con
    `unimplemented=0`. `compare_route.sh` sale con 0 frente a `spec1b-contraste-antes` y también frente a
    `out/baseline/6a/`: 6 idénticos y los 2 `eye` (2700, 2890).
- **Búsquedas:** `git grep -nE 'MovieShader|0x9a9cf8a9|0x34e8a281' -- . ':!docs' ':!CLAUDE.md'` sale vacía.
  `docs/upscaler.md` explica que `9a9cf8a9` era el compute de GoW3 y que ahora hay que darlo con
  `GOW3_UPSCALE_BEFORE_CS`. `docs/plans/2026-10-12-gpu-corruption-rdna2.md` se queda como historia.
