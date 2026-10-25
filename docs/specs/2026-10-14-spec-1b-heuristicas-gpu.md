# Spec 1b — Eliminar las heurísticas de GPU de GoW3

Fecha: 2026-10-14. Estado: **aprobada (2026-10-24), en ejecución**; plan: `docs/plans/2026-10-24-spec-1b-plan.md`. Roadmap: `docs/tsuki/ROADMAP.md`, sección "Spec 1b".
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
| MSAA por vendor | `gpu/shadps4/video_core/amdgpu/gow3_msaa_default.h`, decisión en `vk_instance.cpp`, propiedades en `tools/gpu_capabilities.c` | En AMD y Intel propietario en Windows, crea las imágenes multimuestra con una muestra | **[V]** No incluye RADV (el driver de Linux): la 6650 XT de pruebas mantiene el multimuestreo del juego |
| `GOW3_READBACK_LINEAR=1` por defecto | `run.py:216` | Activa lectura lineal de imágenes para la CPU | **[H] Activa**; cambia rendimiento más que imagen |
| Otros restos de GoW3 en el núcleo | `liverpool.cpp` (46 coincidencias `gow3`), `texture_cache.cpp`, `vk_pipeline_cache.cpp` (43), `videoout/driver.cpp` (65), `vk_frame_capture.cpp`, `camera_motion.comp`, `motion_history.h`, `vk_lighting_test.h`, `vk_temporal_upscaler.h` | Mezclan marcas `gow3:` (cambios nuestros sobre shadPS4, que **se conservan**) con comentarios o nombres de GoW3 | Por clasificar en el paso 1 |

Nota importante: las 2551 coincidencias de `gow3` no son todas código de GoW3. Casi todas son la
**marca de cambios propios** (`gow3:`), que es genérica y se renombra en la Spec 7. Esta spec solo borra
lógica específica de GoW3.

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
Orden: de la más dormida a la más activa. (1) `gbuffer_draw` y la constante de 864 bytes; (2) lectura
lineal por defecto en `run.py` y `scripts/graphics_options.py`; (3) MSAA por vendor. El escalado a
1920×1080 **no se borra** (1080p es la resolución base del proyecto); el paso 1 solo verifica que no
contenga lógica de GoW3 mezclada.

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
- [ ] Para cada heurística borrada: `compare_captures` reporta "idéntico" o la diferencia está
  explicada por escrito en la tabla del paso 1.
- [ ] Búsqueda por lista cerrada, con palabra completa y sin `docs/`:
  `git grep -nwE 'gbuffer_draw|GOW3_READBACK_LINEAR|gow3_no_msaa_default'` sin resultados.
- [ ] El log de arranque ya no menciona `gow3_msaa` ni lectura lineal; `GPU: guest multisampling` queda
  decidido por el driver, no por el vendor.

## 6. Riesgos

- **Sin oráculo no se puede hacer.** Depende de la herramienta de captura de píxeles en proceso del
  hito (decidida el 2026-10-14). Si esa herramienta se retrasa, esta spec espera: no se borra nada del
  renderizador sin poder comparar antes y después.
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
  En la Tarea 5, `no-msaa-test` pasa a llamarse `msaa-samples-test`.
