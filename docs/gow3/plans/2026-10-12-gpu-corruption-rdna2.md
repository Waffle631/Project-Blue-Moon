# Líneas negras en las RX 6600 y otras RDNA2 (AMD en Windows)

Diagnóstico del 2026-10-12 y 2026-10-13, hecho leyendo el código, los reportes y los logs, más
cuatro builds de prueba ejecutados por quienes tienen la tarjeta. No hay ninguna Navi 23 disponible localmente
(la máquina de desarrollo tiene una GTX 1650; la de pruebas tiene una RX 6700 XT, Navi 22, que no
mostró el defecto, pero no se ejecutó en las condiciones de quienes reportan: ver la sección 9.1). La corrupción
en la Intel Arc está en la PR separada de Intel (`docs/plans/2026-10-12-gpu-corruption-intel.md`
en esa rama). La sección 9 es la revisión del 2026-10-13 y corrige hechos de las secciones 1 y 6. **La sección
10 contiene la causa encontrada por kalil03 y la corrección candidata (`GOW3_NO_MSAA`), en el build de prueba
`windows-v2.1.7-teste3` (que sustituye a `amd-teste1` y `teste2`, borrados, y activa la corrección también en Intel).**

Issues: [#65](https://github.com/FelipePrado0/gow3_pc/issues/65) (canónica),
[#61](https://github.com/FelipePrado0/gow3_pc/issues/61),
[#43](https://github.com/FelipePrado0/gow3_pc/issues/43),
[#27](https://github.com/FelipePrado0/gow3_pc/issues/27),
[#70](https://github.com/FelipePrado0/gow3_pc/issues/70) y
[#97](https://github.com/FelipePrado0/gow3_pc/issues/97) (cerradas como duplicados de la #65),
[#91](https://github.com/FelipePrado0/gow3_pc/issues/91) (RX 6750 XT, HUD); shadPS4 eltutz:
[cuesta4/shadPS4#8](https://github.com/cuesta4/shadPS4/issues/8).

## 1. Síntoma

| | AMD RDNA2 en Windows |
|---|---|
| Tarjetas | RX 6600, 6600M, 6600 XT, 6650 XT (Navi 23). También reportadas: RX 6800 XT con captura en el menú y RX 6900 XT solo por reporte (#70, Navi 21); RX 6750 XT con trazos en las barras del HUD a 2560x1440 (#91, Navi 22, aún sin comparación con un HUD sano). La 6700 XT "limpia" de la máquina de pruebas no se ejecutó a 1080p, sin patch y con el upscaler apagado. |
| Drivers | Adrenalin 26.6.4, 26.8.1 y 26.9.2 (Vulkan 2.0.353), con `VK_KHR_pipeline_binary` y `VK_EXT_subgroup_size_control`. Solo Windows hasta ahora. |
| Apariencia | Franjas oscuras de 4 líneas con periodo de 8: oscuras las líneas con `y mod 8` de 0 a 3, intactas las de 4 a 7 (captura 1920x1080 de la #61: y = 920-923, 928-931, ...). Continuas en horizontal durante decenas de px, solo dentro de las huellas de las luces (piel de Kratos en el menú, cielo, fuego, brillo del HUD, especulares; nunca en las sombras). En las líneas intactas el valor es el del área circundante; en la piedra dentro de la huella de una luz, las líneas oscuras quedan con el color de la piedra fuera de la huella (falta la contribución de esa luz). |
| Resolución | 1080p lo muestra. Por debajo de 1080p las franjas se ven más grandes en pantalla (el periodo de 8 líneas es del render). 1800p: desaparecen (FilipiEPTeixeira, 2.1.7). 4K: desaparecen para FilipiEPTeixeira; para wyz-ez queda un bloque en la parte de abajo (sección 9.1). Los patches de resolución también cambian la Video Arena (2,25 / 2,75 / 3,25 / 3,75 GiB) y exigen más DMEM. |
| Comportamiento | Aleatorio por lanzamiento (casi siempre aparece), estático durante la ejecución, en el mismo lugar de la escena. En un lanzamiento sin líneas, apareció en su lugar el defecto de las texturas amarillas (#70, con la "Texture Corruption Fix" desactivada). |
| Causa | La lectura por shader de un depth D32S8 de 2 muestras (`texelFetch` en `sampler2DMS`) devuelve 0 en la mitad inferior de muchos tiles de 8x8 en el driver de AMD para Windows; la profundidad lineal sale con bloques en el near plane y la luz, la niebla y el bloom heredan las franjas (sección 10). |
| En otros builds | Igual en shadPS4 eltutz **y en el shadPS4 0.19.0 oficial** (jleandrocampos, 6650 XT, 2026-10-13). Contradicción: matheusnovak95 (cuesta4/shadPS4#8, RX 6600) dice que el 0.19.0 oficial no muestra las líneas y va bastante más lento; ejecución única, evidencia débil. El build `KrachDev/shadPS4_GOW3` `v1.0.0-gow3-fix` dice eliminar las líneas en una RX 6600 (sección 9.3, pista 1). |

## 2. Hechos comprobados

- El fotograma del juego (docs/upscaler.md): pre-pase de profundidad, luz y sombras, materiales,
  transparencias, post con pirámide de bloom a media resolución, combine del post en **compute**
  (hash `9a9cf8a9`) hacia el color de escena RGBA8 sRGB, HUD encima en la misma imagen.
- `Physical device subgroup size 64 (32 to 64, pinnable stages { Compute })` (build de prueba 1,
  RX 6600): el driver de AMD solo acepta `requiredSubgroupSize` en compute. El pin de 64 en las etapas
  gráficas, heredado de shadPS4, es un uso no válido según la especificación
  (VUID-VkPipelineShaderStageCreateInfo-pNext-02755); el driver elige el wave size del pixel
  shader por sí solo. Una wave32 de PS cubre 8x4 px, la geometría medida en las capturas. **Probado y
  descartado como causa** (sección 4).
- `VkPipelineExecutablePropertiesKHR::subgroupSize` viene 0 para todo ejecutable gráfico en AMD
  (Windows), 64 solo en compute: el histograma de `GOW3_WAVE_STATS=1` es ciego para vertex y
  fragment en esa tarjeta. `VK_AMD_shader_info` (build 3) devuelve estadísticas e ISA por etapa.
- Ningún fragment shader depende del tamaño de la wave: barrido de 1152 módulos SPIR-V de la caché
  (jleandrocampos): 76 `OpGroupNonUniformQuadBroadcast` (quad, independiente de la wave) y 4 ballots,
  todos en compute. El barrido no buscó los builtins de AMD (`BaryCoord*AMD`,
  `InterpolateAtVertexAMD`): ver la sección 9.3, pista 6.
- eltutz tiene 104 commits propios sobre la base del 2026-08-21 (256 por detrás de main). El fork
  vendora el upstream `b4e7ae7` (2026-10-02, la fecha del 0.19.0) más tres correcciones de eltutz:
  alias de imágenes (`189ca3b6`), depth sin uso no anexado (`eccbeae3`) y blend MIN/MAX con
  escala, con un "square pass" (`45f3d1f9`, clave `GOW3_SQUARE_PASS=0`).
- Umbrales del recolector de VRAM en una tarjeta discreta de 8 GB (`texture_cache.cpp`, constructor):
  `trigger_gc_memory = 0` (el recolector se ejecuta en cada envío), presión en unos 2,7 GB y
  crítico en unos 5,4 GB. En 12 GB: disparador unos 1,35 GB, presión 5,9 GB. Las imágenes
  tiled escritas por la GPU nunca se desalojan; los targets por fotograma nunca entran en la LRU.
- Ocho logs leídos enteros (seis reporteros; uno de ellos de la Intel Arc B390, los demás de RX
  6600/6600M): ejecuciones sanas, sin error, sin presión de memoria, Fifo e Immediate, ventana y
  pantalla completa, cuatro builds de eboot distintos. Ninguna línea se correlaciona con el defecto.
  En todos los logs de AMD aparece `Unimplemented IT_SET_PREDICATION` (el juego predica unos mil
  draws por fotograma y todos se ejecutan; es igual en todos los fabricantes). El log de Momexdxd (#65,
  RX 6650 XT, release) tiene un `Unimplemented depth overlap copy` (`vk_runtime.cpp`,
  `CopyColorAndDepth`), ausente en los demás logs: una copia entre una imagen de profundidad y
  otra que no es el par 1x color a profundidad multisample.
- El paquete de release no incluye caché de shaders; cada reportero compila la suya.
- El port solo graba líneas de GPU de nivel Warning en `last_run.log`
  (`gpu/shim/common/logging/log.cpp`); `GOW3_GPU_LOG=info` lo libera. Los marcadores para testers
  deben ser `std::printf("GPU: ...")`. Hasta el build 4 solo `Play God of War III.exe`
  (`--play`) grababa el archivo; el botón PLAY de la ventana enviaba la salida a la pestaña Log. Corregido
  en esta PR. El archivo queda en la carpeta de saves (la "Saves folder" del launcher, por ejemplo
  `Documents\god of war 3 saves`), no en una carpeta `user\` junto al juego.
- Qué build generó un log, según los números de línea de los avisos de `vk_instance.cpp`: 2.1.6, 2.1.7
  y `main` tienen `Extension ... unavailable` en `:304` y `CollectImageFormatInfo` en `:855`; el
  build 2 tiene `:309`/`:878`; los builds 3 y 4 tienen `:309`/`:880`. Desde el build 2 en adelante la línea
  `GPU: subgroup policy: ...` aparece en cualquier nivel de log.

## 3. Hipótesis refutadas por lectura (2026-10-12)

Ninguna sobrevivió a la verificación adversarial. Motivos, para no repetir el trabajo:

| Hipótesis | Por qué cayó |
|---|---|
| Los draws omitidos por la compilación asíncrona dejan targets indefinidos | Las imágenes nuevas nacen `Dirty` y pasan por `RefreshImage` antes del primer uso; los targets de bloom se rehacen en cada fotograma; solo existe en el fork, no en eltutz; el primer uso de un pipeline se omite en toda máquina, incluida la 6700 XT. |
| Recolector de VRAM en 8 GB | La cuenta es correcta, pero los targets tiled escritos por la GPU nunca se desalojan, solo entran las imágenes quietas desde hace 16 a 160 ticks, las imágenes recreadas vuelven de la memoria del guest, y el síntoma aparece en el menú con poca memoria en uso. |
| VMA derramando a RAM | Solo cuesta rendimiento. Endurecimiento: pasar `VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT` en `vk_instance.cpp` (la extensión está activada, la flag no). |
| Copia de alias (`aliasing.cpp`) o memo con imagen vieja | Copias de extensión completa desde el origen, solo con el mismo layout. No generan bloques de 8x4. |
| Memos, draw pipe, grabación en hilo | Deterministas e iguales en la 6700 XT. Laguna real confirmada, pero de otro efecto: una meta-clear mediante compute o `FillBuffer` con el render pass abierto y acierto en `begin_memo` omite `IsMetaCleared/TouchMeta` (target entero sin clear, no líneas). |
| Clave de la caché de binarios | `CreateCachedGraphicsPipeline` recibe la `GraphicsPipelineKey` y no la usa; el archivo lleva solo la clave del driver (`vk_pipeline_serialization.cpp`). El efecto sería determinista, también en la 6700 XT, y no existe en eltutz. Endurecimiento: incluir el hash de la clave en el nombre y eliminar `LoadLegacyBinary`. |
| Toggle de binarios sin efecto | Cierto solo sin `VK_KHR_pipeline_binary`; el log de la RX 6600 tiene la extensión. |
| Combine `9a9cf8a9` en wave32 | Todo compute pasa por el mismo camino con `requiredSubgroupSize=64`; compute no usa binarios. |

## 4. Pruebas en hardware (2026-10-13)

**Bisección en las optimizaciones del port (FilipiEPTeixeira y Gabrielsln, dos RX 6600, 2.1.6, #65):
las 7 pruebas fallaron en ambas**, 5 aperturas cada una, líneas en todas. Una a la vez, caché de
shaders limpia antes:

| # | Prueba | Cómo | Resultado |
|---|---|---|---|
| 1 | Sin caché de binarios y sin carga en segundo plano | desmarcar "Use pipeline binary cache" y "Load shader cache in the background" | líneas |
| 2 | Sin compilación en segundo plano | desmarcar "Compile new shaders in the background" | líneas |
| 3 | Sin el square pass del MIN/MAX | `GOW3_SQUARE_PASS=0` | líneas |
| 4 | Sin el recolector de VRAM | `GOW3_GC_BUDGET_MB=20000` | líneas |
| 5 | Draw pipe apagado | "Pipeline de GPU em dois estágios" = Off | líneas |
| 6 | Grabación en el hilo principal | `GOW3_VK_RECORD_THREAD=0` | líneas |
| 7 | Lecturas con espera | desmarcar "Game reads GPU data without waiting" y "Deferred GPU readbacks" | líneas |

Los toggles en vivo no alcanzan lo que se decide en la creación de las imágenes (flags, formato,
tiling): estas siete pruebas eliminan las optimizaciones, no el núcleo compartido.

**Pin de subgrupo (build de prueba 2, `windows-v2.1.6-test2`):** tres ejecuciones con caché limpia,
(1) política normal, (2) `GOW3_SUBGROUP_PIN=allowed`, (3) `GOW3_SUBGROUP_PIN=none` con
`GOW3_LDS_BARRIERS=1`. MarcosAlexandre30 (RX 6600): líneas en las tres. jleandrocampos (RX 6650 XT):
líneas en la 1 y en la 3, con el log confirmando la política aplicada (`compute pinned to 64, fragment
pinned to 64` frente a `compute pinned to 0, fragment pinned to 0, LDS barriers on`). **El pin no
es la causa.** Las variables quedaron en la PR de Intel; esta PR no las trae. Salvedad: AMD ignora
`requiredSubgroupSize` en gráficos, así que la prueba nunca varió el wave size del pixel shader.

**42 toggles apagados en vivo** (`GOW3_TOGGLE_FILE` = 182395781688328191) y
`GOW3_SQUARE_PASS=0` (jleandrocampos): las líneas continúan.

**shadPS4 0.19.0 oficial** en la 6650 XT, misma escena: líneas. No hay regresión de eltutz ni del
fork que buscar; la causa está en el núcleo compartido o en el driver.

**Builds de prueba 3 y 4** (`windows-v2.1.7-test3`: ISA mediante `VK_AMD_shader_info`;
`windows-v2.1.7-test4`: `GOW3_FULL_BARRIERS=1`), resultados hasta el 2026-10-13 04:30 UTC:

- Ed1z19 (#61): el log "con configuraciones predeterminadas" de las 02:12 UTC es del binario de release
  (`:304`); el de las 02:21 UTC es de un build de prueba (`:309`, 2, 3 o 4, el log no distingue):
  líneas en el mismo lugar.
- MarcosAlexandre30 (#65, 02:28 UTC): build 3 o 4 (`:309`/`:880`) con `GOW3_VK_VALIDATION=1
  GOW3_VK_VALIDATION_SYNC=1`. La validación de sincronización acusó solo
  `SYNC-HAZARD-WRITE-AFTER-PRESENT` en las imágenes del swapchain (10 veces, límite de repetición),
  con caída de fotogramas; **ningún hazard dentro del fotograma** y ningún error de la validación core
  (siempre activada, `emulator_settings.h`). Debilita la pista de la barrera faltante, dentro de lo que
  las capas pueden modelar.
- FilipiEPTeixeira (#65, 02:38 UTC): build 3 o 4; las líneas desaparecieron en las dos primeras
  ejecuciones y volvieron después (los archivos de los builds de prueba quedaron en carpetas equivocadas, y el
  build no registraba si `GOW3_FULL_BARRIERS` estaba activado: esta PR pasa a imprimir
  `GPU: full barriers on`). El resultado de `GOW3_FULL_BARRIERS=1` sigue siendo desconocido. Y las
  líneas desaparecen a 1800p y a 4K en la 2.1.7; por debajo de 1080p se ven más grandes.
- wyz-ez (#65, 04:08 UTC): RenderDoc derriba el juego antes de unos 45 fotogramas; el Radeon Developer
  Tool Suite (RGP, RMV) no muestra el contenido de las imágenes, así que no ayuda aquí.
- Nadie con `GOW3_WAVE_STATS=1` todavía: los logs enviados no tienen la línea `GPU: wave sizes`.

## 5. Qué entrega esta PR

Ninguna corrección: diagnóstico. No cambia nada para quien no activa las variables.

- `GOW3_WAVE_STATS=1` activa `VK_KHR_pipeline_executable_properties` y, en AMD,
  `VK_AMD_shader_info`. Graba `GPU: wave sizes after N pipelines: ...` con un elemento por par
  (conjunto de etapas, ancho): en AMD `{ Fragment }/vgprV@W xN` (V = VGPRs físicos, que
  difieren entre wave32 y wave64; W = 0 en las etapas gráficas), en los demás fabricantes
  `{ Fragment }@W xN`. La ISA de los tres primeros ejecutables de cada clave (etapas, VGPRs) va
  a `user\shaders\wave_dumps\<etapa>_vgpr<V>_<n>_<W>.txt`, donde wave32 y wave64 aparecen
  directamente (`vk_wave_stats.h`, `vk_pipeline_common.cpp`, test `tests/test_wave_stats.cpp`, target
  `wave-stats-test`).
- `GOW3_FULL_BARRIERS=1` graba una barrera completa de memoria antes y después de cada render
  pass y de cada dispatch de compute del juego, y también de los dispatches del propio port (tiling y
  detiling de imágenes en `tile_manager.cpp`, copias por shader en `vk_shader_hle.cpp`). Imprime
  `GPU: full barriers on` en el log. Lento; solo para la prueba: líneas que desaparecen en una escena que las tenía
  = barrera faltante en el núcleo; líneas iguales = la sincronización sale de la lista. Es solo una
  `VkMemoryBarrier2` global, sin transición de layout: no prueba metadatos de compresión (DCC,
  HTILE), que el driver solo resuelve en un cambio de layout.
- El botón **PLAY** del launcher graba `last_run.log` en la carpeta de saves como
  `Play God of War III.exe` (`launcher/gow3_launcher_win.py`, test
  `tests/test_launcher_run_log.py`). Hasta ahora solo lo hacía el segundo, por eso tantos reporteros
  no encontraban el archivo. Excepción: con "Close the launcher when the game starts" activado el
  launcher se cierra 5 s después y la lectura muere con él, así que en ese modo PLAY no toca el
  archivo (el log anterior queda intacto). Un error de escritura en el log ya no interrumpe la lectura
  de la salida del juego.
- **Censo por pase en la captura del analizador de fotogramas** (Insert > Diagnostics > Capture frame,
  o `GOW3_CAPTURE_TRIGGER`): cada pase del `frame_*.txt` gana una línea
  `state samples c1 d4 r4 mixed-amd shading 1 | stencil t1 w1 c0 | zclear htile | ds
  DepthReadOnlyStencilAttachmentOptimal | fb 0 | area 1920x1080 (depth 1920x1088)` con las
  muestras de los targets de color y de profundidad del guest, las muestras del pipeline, el camino de
  muestras que eligió `vk_pipeline_cache.cpp` (`equal`, `mixed-amd`, `mixed-nv`,
  `forced-equal`), `sampleShadingEnable`, test, escritura y clear de stencil, el origen del clear
  de profundidad (`register`, `htile`, `none`), el layout del target de profundidad, el feedback
  loop y el área de render frente al tamaño del target de profundidad. Solo durante la captura
  (`vk_pass_census.h`, `vk_frame_capture.cpp`, `vk_rasterizer.cpp` en `BeginRenderingFull`;
  test `tests/test_pass_census.cpp`, target `pass-census-test`). Responde, en una sola captura, si el
  juego usa MSAA o muestras mixtas, si algún pase testea o muestrea el stencil, y si un target es
  menor que los demás.
- `GOW3_LIGHTING_TEST=shading,msdepth,dref,bias` (o `all`): los cuatro cambios del build
  `KrachDev/shadPS4_GOW3` (sección 9.3, pista 1), cada uno tras un token para bisección.
  Imprime `GPU: lighting test on: <tokens>`. `shading`: `sampleShadingEnable` siempre falso
  (`vk_graphics_pipeline.cpp`). `msdepth`: el blit de color a profundidad multisample usa el
  canal rojo para las muestras que leen 0 (`color_to_ms_depth.frag`, variante
  `SAMPLE_FALLBACK`). `dref`: las comparaciones de sombra devuelven el resultado en los cuatro canales
  en lugar de `(v, 0, 0, 0)` (`emit_spirv_image.cpp`, campo `Profile::dref_broadcast`). `bias`:
  el slope del depth bias pasa como lo manda el guest, sin dividir entre 16 (`vk_rasterizer.cpp`).
  Los tokens que cambian el SPIR-V entran en la versión de la caché de binarios de shaders
  (`vk_pipeline_serialization.cpp`), así que no hace falta limpiar la caché entre ejecuciones
  (`vk_lighting_test.h`, test `tests/test_lighting_test.cpp`, target `lighting-test-knob-test`).
- `GOW3_AMD_PATHS_OFF=mixed,shaderext` (o `all`): ejecuta el núcleo sin las extensiones que solo AMD
  expone. Imprime `GPU: amd paths off: <tokens>`. `mixed`: sin `VK_AMD_mixed_attachment_samples`,
  los pases con color y profundidad de muestras distintas pasan a tener muestras iguales, como en
  Intel. `shaderext`: sin `VK_AMD_shader_explicit_vertex_parameter`, `VK_AMD_gcn_shader` y
  `VK_AMD_shader_trinary_minmax`, el código de shader usa `VK_KHR_fragment_shader_barycentric`
  (el log dice `GPU: fragment barycentrics: ...`) y los caminos genéricos; también entra en la versión
  de la caché (`vk_amd_paths.h`, `vk_instance.cpp`, test `tests/test_amd_paths.cpp`, target
  `amd-paths-test`).

- `GOW3_NO_MSAA` (sección 10, patch de kalil03): las imágenes multisample del juego se crean y
  se muestrean con 1 muestra; los tamaños y direcciones en el guest siguen con el recuento real
  (`ImageInfo::guest_samples`, `GuestNumSamples()` en `regs_color.h`, `regs_depth.h`,
  `resource.h`). Activado por defecto en los drivers propietarios de AMD e Intel en Windows
  (`VK_DRIVER_ID_AMD_PROPRIETARY`, `VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS`; la regla vive en
  `video_core/amdgpu/gow3_msaa_default.h`, en C, compartida con `gow3-gpu-capabilities`);
  `GOW3_NO_MSAA=0` lo desactiva y `=1` lo fuerza en cualquier tarjeta. El log dice `GPU: guest multisampling
  off (...)` u `on` (`gow3_msaa.h`, `vk_instance.cpp`; test `tests/test_no_msaa.cpp`, target
  `no-msaa-test`, que también cubre la tabla de la regla). **Es el único cambio de esta PR que altera el
  render por defecto, y solo en los drivers de AMD e Intel en Windows.**
  En el launcher: Performance > GPU data > "Multisampling do jogo" (clave `no_msaa` en
  `settings.json`, vacía por defecto). `gow3-gpu-capabilities --list` pasó a imprimir, por GPU,
  el valor predeterminado del multisampling en ese driver (`off`/`on`) y qué GPU elige el juego sin
  `GOW3_GPU_ID`; Automático muestra la tarjeta que se va a usar y la decisión, por ejemplo "Automático
  (Intel(R) Arc(TM) A750 Graphics: desligado)", sigue la elección en "Placa de vídeo" en la misma
  sesión y no exporta la variable (el juego decide por la misma regla); Desligado y Ligado exportan
  `GOW3_NO_MSAA=1` o `0` en cualquier tarjeta (tests `tests/test_launcher_no_msaa.py`,
  `tests/test_launcher_controls.py`). Salvedades: la regla vale para toda GPU en los drivers de AMD
  e Intel en Windows, integradas incluidas (UHD e Iris Xe sin reportes aún); en portátiles
  híbridos la preferencia de GPU por aplicación de Windows puede ordenar las GPUs de forma distinta
  para la herramienta y para el juego, y entonces lo que vale es la línea `GPU: guest multisampling ...` del
  log.
- `GOW3_DIAG_IMAGES=1` (patch de kalil03, exige `GOW3_DRAW_PIPE=0`): durante una captura de fotograma
  (`GOW3_CAPTURE_TRIGGER`), graba en `GOW3_DIAG_DIR` (por defecto `out/diag`, la carpeta debe existir)
  el target de color de cada pase y las imágenes de cada dispatch, en raw
  (`<seq>_<tag>_<dirección>_<w>x<h>_p<pitch>_b<bytes>_f<formato vk>.raw`). Espera a la GPU en cada
  imagen; solo para diagnóstico. Así se encontró el pase de las franjas.

## 6. Pistas restantes

Sustituida por la sección 9.3. De la lista original del 2026-10-12: la DCC en el color (pista 1) está casi
muerta por el código abierto del driver, la barrera faltante (pista 2) sigue sin resultado y perdió
la parte de la Infinity Cache, la resolución (pista 3) ganó 1440p y 720p como casos que separan
las pistas, y la captura RenderDoc (pista 4) derriba el juego en las tarjetas de los reporteros.

### Endurecimientos opcionales (no son la causa)

- Invalidar el `begin_memo` en `TextureCache::ClearMeta` y en `TouchMeta(is_clear=true)`.
- `VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT` cuando `supports_memory_budget`.
- Registrar en el log los umbrales del recolector al iniciar; `DownloadImageMemory` solo graba el mip 0.
- Nombre del archivo de binario con el hash de la `GraphicsPipelineKey`; eliminar `LoadLegacyBinary`.
- Pin de subgrupo solo en las etapas que el driver lista (`GOW3_SUBGROUP_PIN=allowed` en la PR de Intel):
  saca al port de fuera de la especificación en AMD, sin efecto en el defecto.

## 7. Cómo probar (build `windows-v2.1.7-teste3`)

El `windows-v2.1.7-teste3` (commit `e2cd838`, 2026-10-13) sustituye a `amd-teste1` y `teste2`,
borrados: misma corrección, ahora activada por sí sola también en el driver de Intel en Windows, y el launcher
muestra la tarjeta y la decisión. Sin `.bat`. El teste2 se detenía en una carpeta nueva con `NameError: name
'source' is not defined` (`scripts/link_libc.py` y `scripts/link_modules.py`, bug de main en
`5dc44e4`; corregido en `6d580ba` y `e2cd838`). La petición de validación se publicó en #65, #28 y en los duplicados; los pasos
de abajo siguen valiendo (3 aperturas bastan; el control con `GOW3_NO_MSAA=0` es opcional).

El build activa `GOW3_NO_MSAA` por sí solo en toda tarjeta AMD con el driver propietario. Reglas: 1080p,
sin patch de resolución, upscaler apagado, siempre las mismas escenas (menú principal con Kratos,
apertura en tiempo real, primera lucha). No hace falta limpiar la caché de shaders. El `last_run.log`
queda en la carpeta de saves (la "Saves folder" del launcher, por ejemplo `Documents\god of war 3 saves`)
y debe tener la línea `GPU: guest multisampling off (single-sampled images; GOW3_NO_MSAA=0 keeps
it)`; sin ella, la ejecución no cuenta.

1. **Predeterminado (corrección activada):** 5 aperturas desde cero, una captura por apertura de las tres escenas. Di
   en cuántas aparecieron las líneas y adjunta un `last_run.log`.
2. **Control (`GOW3_NO_MSAA=0`):** 2 aperturas mediante el `teste.bat` de abajo. Si las líneas vuelven, confirma
   que es el cambio el que resuelve, y no el build.

   ```bat
   @echo off
   set GOW3_NO_MSAA=0
   start "" "God of War III.exe"
   ```

3. **Bordes y rendimiento:** una captura de la misma escena parada con y sin la corrección (el juego pierde las
   2 muestras de profundidad; buscar aliasing distinto en los bordes de los objetos) y los FPS
   en ambos casos (F3 o "Frame stats" del launcher).
4. **Quien juegue más adelante:** di si las líneas vuelven en alguna fase (la corrección solo se probó
   hasta la primera lucha) y en 1440p, 1800p o 4K.
5. **RDNA2 sin las líneas antes (6700 XT) y RDNA3/RDNA4:** el build también cambia vuestro render.
   Pruebas 1 y 3: si algo empeora, `GOW3_NO_MSAA=0` es el camino de vuelta.

Las pruebas de diagnóstico de la sección 9 (`GOW3_LIGHTING_TEST`, `GOW3_AMD_PATHS_OFF`, barreras,
captura por pase) siguen en el build, pero solo tienen sentido si la corrección falla en alguna tarjeta.

## 8. Preguntas abiertas

1. ¿La corrección elimina las líneas en todas las RDNA2 reportadas (6600, 6650 XT, 6750 XT, 6800 XT,
   6900 XT) y en todas las fases, o solo al principio?
2. ¿Perder las 2 muestras de profundidad cambia los bordes o el rendimiento de forma visible? Si cambia,
   vale la pena restringir la corrección al fetch (expandir el depth antes de la lectura por shader, o copiar
   a una imagen de 1 muestra solo para la lectura) en lugar de desactivar el MSAA del guest entero.
3. Por qué 1800p y 4K no lo mostraban (o lo mostraban menos): ¿el driver desactiva la compresión del depth
   por encima de cierto tamaño, o cambia el layout? Solo interesa si se hace la restricción de la pregunta 2.
4. ¿Los trazos en las barras del HUD (#65, #91) vienen del mismo depth lineal o de otra lectura?
5. ¿La Steam Deck (RADV) muestra las líneas con `GOW3_NO_MSAA=0`? Decide si es un bug del driver de
   Windows o del hardware.

## 9. Revisión del 2026-10-13 (ronda adversarial)

Ronda solo de lectura: siete lentes (tiling, sincronización, driver RDNA2, memoria del guest,
pre-pase de luz, búsqueda web, mapas de código) y tres verificaciones adversariales por hipótesis,
con un crítico de completitud al final. **Ninguna de las 27 hipótesis sobrevivió** a la verificación. Lo
que quedó está en 9.3, en orden de coste de la prueba decisiva.

### 9.1 Hechos corregidos

| Elemento | Antes | Ahora |
|---|---|---|
| Tarjetas | Solo Navi 23; 6700 XT limpia | Navi 23 (6600, 6600M, 6600 XT, 6650 XT). Navi 21: RX 6800 XT con captura en el menú (#70) y RX 6900 XT solo por reporte (#70). Navi 22: RX 6750 XT con trazos en las barras del HUD a 2560x1440 (#91), aún sin comparación con un HUD sano. La 6700 XT "limpia" vale solo para las condiciones en que se ejecutó: repetir a 1080p, sin patch, upscaler apagado, 5 aperturas. |
| Drivers | 26.6.4, 26.8.1 | 26.6.4, 26.8.1 y 26.9.2. Solo Windows. |
| Forma | Bloques de 8x4 px | Franjas de 4 líneas con periodo de 8: oscuras las líneas con `y mod 8` de 0 a 3, intactas las de 4 a 7 (medido con PIL en la captura 1920x1080 de la #61: y = 920-923, 928-931, ...; la captura de 1365x767 de la #65 da el mismo periodo de 8 líneas del render). **Continuas en horizontal** durante decenas de píxeles (36 px, 50 px), no trazos de 8 px. La región de las franjas sigue el borde entre la parte iluminada y la parte en sombra del cuello. |
| 4K | "Disminuyen y se van hacia abajo"; "el fondo entero, independiente del contenido" | Medición rehecha en la captura 4K de wyz-ez (ventana 1917x1078, escala 2,003, muestreo bilineal del post): la fase es la misma que en 1080p (4 encendidas, 4 apagadas). La región es una escalera de bordes nítidos: borde superior en la línea 1928 del render para x ≥ 960, borde superior en la línea 2056 más a la izquierda (allí el borde izquierdo queda oculto por el panel del rostro). A la izquierda de x = 960, entre 1928 y 2055, el contenido es claro y sin franjas. Los bordes caen cerca de 1920 = 15·128, 2048 = 16·128 y x = 960 = 15·64 dentro del error de la reducción 2:1: la forma de "direcciones a partir de A" en una superficie 2D-tiled del guest con macro tiles de 64x128, o de un quad de UI. Para FilipiEPTeixeira las líneas desaparecen por completo a 4K. |
| Infinity Cache | 32 MB frente a 96 MB lo explicaría | Descartado. La 6800 XT (128 MB) también muestra las líneas, y la MALL es una caché del lado de la memoria, coherente para todos los agentes de la GPU. |
| Video Arena y memoria del guest | Confundida con la resolución | A 1080p, arenas de 1,39 y 2,39 GiB muestran líneas. La secuencia de asignaciones de DMEM es idéntica byte a byte entre lanzamientos (logs de los reporteros; `runtime_memory.c` es first-fit determinista), pero eso solo habla de los mapeos: dónde coloca el juego cada superficie dentro de la arena (pools `WAD_...` cargados por hilos de streaming) no se midió. El patch 720p tiene la misma arena que la "Texture Corruption Fix" y separa el tamaño de render de la arena. |
| Prueba del pin (sección 4) | "El pin no es la causa" | Sigue valiendo, pero la prueba no varió el wave size del pixel shader: AMD ignora `requiredSubgroupSize` en gráficos. El barrido de los SPIR-V solo buscó operaciones de subgrupo, no los builtins de AMD. |
| `GOW3_FULL_BARRIERS` | Decide "barrera faltante" | Es solo una `VkMemoryBarrier2` global (`vk_scheduler.cpp`), sin transición de layout. No prueba metadatos de compresión (DCC, HTILE). |
| Herramientas | `GOW3_DUMP_DIR` = captura de fotograma | `GOW3_DUMP_DIR` solo sirve a `GOW3_FRAME_DUMP_TRIGGER`, que graba `display_*.raw` y `output_*.raw` (la carpeta debe existir antes; `vk_presenter.cpp`). El analizador de fotogramas (Insert > Diagnostics > Capture frame, `GOW3_CAPTURE_TRIGGER`) graba solo texto en `GOW3_CAPTURE_DIR` o `<saves>\captures`; antes de esta PR no registraba muestras, stencil ni layouts. `GOW3_DUMP_TRIGGER` y `GOW3_PRESENT_DUMP_TRIGGER` solo funcionan con el upscaler activado. |
| Stencil | No considerado | El emulador nunca lo sube (`texture_cache.cpp`, `RefreshImage` quita el aspecto de stencil de la subida), nunca lo copia con `CopyImage` (`vk_runtime.cpp`) y nunca limpia por HTILE el plano de stencil: la primera vez que se anexa un D32S8 nuevo, la profundidad se limpia (`htile_clear_mask = -1`) y el stencil se carga desde una imagen `UNDEFINED`. Los draws con stencil op distinto de Keep y `Rasterizer::DepthStencilCopy` escriben el stencil. GoW3 lee el stencil como textura R8Unorm mediante la redirección stencil a profundidad (`vk_rasterizer.cpp`, descripción de la PR upstream shadPS4#5118), con tipo de imagen float sobre el aspecto UINT (`image_view.cpp`, `spirv_emit_context.cpp`), lo que la especificación deja indefinido y los fabricantes pueden resolver de forma distinta. |

Caminos de código que solo recorre AMD en Windows, confirmados por los logs (las únicas
extensiones pedidas y ausentes son `VK_EXT_attachment_feedback_loop_dynamic_state` y
`VK_NV_framebuffer_mixed_samples`):

1. Una imagen muestreada y target en el mismo draw va al layout `GENERAL` (`vk_instance.cpp`,
   `vk_rasterizer.cpp`). Entre draws hay barrera (las máscaras de acceso alternan); dentro de un
   draw sería una carrera y parpadearía.
2. El MSAA mixto (color y profundidad con distinto número de muestras) pasa por
   `VK_AMD_mixed_attachment_samples` (`vk_instance.cpp`, `vk_pipeline_cache.cpp`). Intel fuerza
   muestras iguales; NVIDIA usa la extensión NV.
3. Código de shader generado con `VK_AMD_shader_explicit_vertex_parameter`, `gcn_shader` y
   `trinary_minmax` (`vk_instance.cpp`, `vk_pipeline_cache.cpp`, `emit_spirv.cpp`).

Precedente en el mismo juego, verificado en el repositorio: el release `v1.0.0-gow3-fix` de
`KrachDev/shadPS4_GOW3` ("God of War III Remastered - Lighting, Scanline & AMD Fix",
2026-10-09, binario `shadps4-win64-sdl-2026-10-09-9f48028.zip`, PR abierta cuesta4/shadPS4#6)
dice que las líneas negras desaparecen en una RX 6600 con el commit `eaa1027f`, que cambia exactamente cuatro
cosas: `sampleShadingEnable = false`, el fallback de muestra en `color_to_ms_depth.frag`, el
resultado del Dref en los cuatro canales y el slope del depth bias sin el `/16`. El texto del release dice
que el pase de iluminación del juego usa profundidad 2x MSAA con color 1x; eso no se verificó
aquí (la captura por pase de esta PR lo verifica). Las explicaciones del release para el mecanismo no
cuadran con la geometría medida (describe "rayas de 1 px"), pero el resultado reportado es el único
fix candidato con prueba en una RX 6600 hasta ahora. Los cuatro tokens de `GOW3_LIGHTING_TEST` son esos
cambios, uno a uno.

Verificación local en la GTX 1650 (2026-10-13, build de esta PR, 1080p, upscaler apagado): la captura
por pase de la pantalla de ajuste de pantalla del primer arranque (menú "Exibição" con el rostro de Kratos al
fondo) tiene 9 pases, todos `c1 d1 r1 equal shading 0`, sin stencil y sin clear: esa pantalla es 2D.
El menú principal y la escena con las luces aún no se han capturado localmente.

### 9.2 Refutadas en esta ronda

| Hipótesis | Por qué cayó |
|---|---|
| DCC en el color, mitad de 128 B leída como "limpia" (pista 6.1 del 2026-10-12) | En el PAL/XGL abierto, una imagen `MUTABLE_FORMAT` sin `VkImageFormatListCreateInfo` pasa a `viewFormatCount = AllCompatibleFormats` (xgl `vk_image.cpp`). Eso da `DccFormatEncoding::Incompatible` fuera de GFX11 (PAL `gfx9Device.cpp`) y desactiva el DCC (`gfx9MaskRam.cpp`, `mustDisableDcc`). En GFX10 el CMASK solo existe con MSAA. Todas las imágenes del port son `MUTABLE_FORMAT` sin lista (`texture_cache/image.cpp`). Salvedad: Adrenalin es cerrado. |
| Línea de caché de 128 B vieja, pipe desalineado | El XOR de pipe y banco actúa del bit 8 hacia arriba, así que las dos mitades de 256 B quedan en el mismo canal. Una línea vieja parpadearía entre fotogramas, y el defecto es estático. El paso de target a muestreo siempre cambia de layout y siempre genera barrera (`vk_rasterizer.cpp`, `texture_cache/image.cpp`). |
| Plano de profundidad en los volúmenes de luz | El HTILE es 8x8. La profundidad nunca usa el camino de feedback en ningún fabricante y se limpia y reescribe en cada fotograma. No explica HUD, cielo ni 4K. |
| Layouts separados de profundidad y stencil (`DepthReadOnlyStencilAttachment`) | Los mismos motivos; descriptor y attachment en layouts distintos en el mismo draw darían error de validación core en la ejecución de MarcosAlexandre30, y no lo dieron. |
| Detile de superficie de 64 bpp o con tile split | Todas las superficies del tamaño del render son de 32 bpp. El tile split de color nunca se dispara (`amdgpu/tiling.cpp`). El camino es igual en todos los fabricantes. |
| Feedback loop en `GENERAL` en AMD | Entre draws hay barrera. Dentro de un draw sería una carrera y parpadearía. Solo falta confirmar, gratis en la GTX 1650, que el juego ni siquiera pasa por ahí (columna `fb` del censo). |
| Orden de los anillos gráfico y compute, mantenimiento de caché ignorado | Hay una sola cola en el host (`vk_scheduler.cpp`) y `WAIT_REG_MEM` se respeta (`liverpool.cpp`). La bisección ya movió el timing sin efecto. |
| Stencil no inicializado como basura de memoria (forma original) | La basura de memoria no produciría el mismo patrón exacto (líneas 0 a 3 de cada 8) en máquinas y tarjetas distintas. Queda la variante estrecha de la pista 3 (9.3). |
| Límite de 2.097.152 px del clear de profundidad y stencil de PAL (gráfico por debajo, compute por encima) | El límite existe en el código abierto (`gfx9RsrcProcMgr.cpp`, `settings_gfx9.json`), pero los dos caminos tratan el HTILE del stencil igual en un clear solo de profundidad, y ninguno toca 4 de 8 líneas (HTILE es por tile de 8x8). El 4K (8,3 Mpx) queda por encima del límite y aún muestra el bloque de abajo para wyz-ez. Queda solo como motivo para incluir 1440p en el mapa de resolución. |
| Wave64 del PS descartando la mitad baja (líneas 0 a 3) | El rasterizador empaqueta los quads que sobreviven a la cobertura, al early-Z y al stencil, atravesando tiles y primitivas: la mitad descartada no quedaría alineada con `y mod 8`. Las franjas medidas son continuas durante 36 a 50 px y siguen el borde de iluminación, no las aristas de las primitivas. |
| Sample shading por sí solo | Con `minSampleShading = 0` y targets de 1 muestra, `sampleShadingEnable` no cambia nada; solo marca diferencia con MSAA (por eso el censo va primero y `GOW3_LIGHTING_TEST=shading` es un token aparte). |
| Cadena del presenter (FSR/RCAS a 1:1) | El 0.19.0 oficial también muestra las líneas, a 4K el RCAS está apagado, y un pase de pantalla completa no quedaría restringido a las luces. |

Siguen valiendo las refutadas de la sección 3 y las eliminaciones por prueba de la sección 4.

### 9.3 Pistas restantes, en orden

1. **Los cuatro cambios del build KrachDev** (`GOW3_LIGHTING_TEST=all`, luego un token a la
   vez). Único candidato con resultado positivo reportado en una RX 6600; ninguno de los cuatro
   explica por sí solo la geometría ni la aleatoriedad por lanzamiento, y solo `shading` y `msdepth`
   tocan un camino exclusivo de AMD (muestras mixtas). Prueba 1 de la sección 7.
2. **MSAA mixto mediante `VK_AMD_mixed_attachment_samples`.** Solo vale si el juego usa muestras > 1,
   lo que responde el censo por pase (prueba 2). Si las usa: `GOW3_AMD_PATHS_OFF=mixed` (prueba 6)
   fuerza el camino de Intel. Un micro tile de 32 bpp con 2 muestras es 8x4 px x 2 muestras =
   256 B: leído como 1x, las líneas 0 a 3 y 4 a 7 vendrían de muestras distintas, y
   `color_to_ms_depth.frag` escribe 0 en la muestra 1 de una fuente R32F. Es la única lectura que
   liga la geometría a un camino exclusivo de AMD, y depende de que el juego tenga profundidad multisample.
3. **Contenido del plano de stencil que el emulador nunca escribe**, leído por el juego (test de
   stencil o la textura R8Unorm redirigida). Explica aleatorio por lanzamiento y estático por
   ejecución, pero exige un patrón estructurado de 32 B, no basura, y la parte "solo AMD" depende de
   lo que el driver haga con un stencil `UNDEFINED` leído como float. El censo muestra si las luces
   testean stencil y si se limpia (`stencil t1 w1 c0`); si lo muestra, el siguiente build gana una
   variable que limpia el stencil en el primer bind (`GOW3_STENCIL_INIT=<valor>`).
4. **Barrera faltante entre pases** (`GOW3_FULL_BARRIERS=1` con el marcador; prueba 3).
5. **Región que depende de la posición en la memoria del guest.** Bordes de la escalera a 4K cerca de
   128·k + 8 líneas y x = 960: macro tiles de 64x128 de una superficie 2D-tiled, o un quad de
   UI. El patch 4K solo aumenta tamaños y la arena, así que un target 4K invadiendo la asignación vecina es
   plausible. Prueba 5 en 3 aperturas: bordes que cambian de lugar = posición en la memoria.
6. **Código de shader exclusivo de AMD** (`GOW3_AMD_PATHS_OFF=shaderext`). Determinista, no
   explica la aleatoriedad ni el 1800p; barato.
7. **DCC en el color.** Casi muerta por el código abierto del driver; una sonda que compare el tamaño de
   memoria de una imagen con y sin lista de formatos cerraría la pista en un lanzamiento. No
   implementada.

### 9.4 Verificaciones locales, sin reportero

- **GTX 1650:** censo y captura en el menú principal y en la entrada de Gaia (¿el juego usa MSAA? ¿las
  luces testean stencil? ¿el stencil se limpia?) y una captura del HUD sano para comparar con los
  trazos de las #65 y #91. El registro del guest es igual en todo fabricante, así que esa captura
  responde gratis a las preguntas 2 y 3 de 9.3.
- **Steam Deck** (RDNA2, RADV), 1080p: si muestra las líneas, el driver de Windows sale de la lista y
  `RADV_DEBUG=nodcc,nohiz,nofastclears` bisecta los metadatos en minutos; `RADV_DEBUG=img`
  lista DCC y HTILE por imagen.
- **RX 6700 XT de pruebas:** ejecutar en las condiciones de los reporteros antes de contarla como control.

## 10. Causa encontrada (kalil03, 2026-10-13)

Reporte en [#65](https://github.com/FelipePrado0/gow3_pc/issues/65#issuecomment-6138650285), RX
6600, Adrenalin 26.8.1, 1080p nativo, sobre main `3459726`.

**Mecanismo.** El juego dibuja con depth D32S8 de 2 muestras y color de 1 fragmento (el "2x MSAA
depth + 1x colour" que el release de KrachDev afirmaba sin prueba). El pase de profundidad lineal (ps
`782d705bc1c01219`, un triángulo de pantalla completa que escribe en un R32F) lee ese depth con `texelFetch`
en un `sampler2DMS`. En esa lectura el driver de AMD para Windows devuelve 0 en la mitad inferior de muchos
tiles de 8x8 píxeles: la profundidad lineal sale valiendo 0,25 (el near plane) en ellos, y la luz, la niebla y el
bloom, que usan esa imagen, heredan las franjas. Por eso las líneas solo aparecen en áreas claras y
dentro de las luces. El dump por pase (`GOW3_DIAG_IMAGES`) mostró unos 16.100 bloques de 8x4
erróneos en esa imagen a 1080p y todo limpio antes de ella. Los bloques que escapan quedan donde el depth varía
más (bordes); en áreas lisas casi todos fallan. Tiene pinta de tile de depth comprimido (compresión
Z del MSAA) leído por el TC sin descompresión.

**Cómo encaja con lo medido.** La mitad inferior de los tiles en el R32F se convierte en las líneas 0 a 3
de las capturas si hay un flip vertical entre esa imagen y la presentada (1080 = 135 x 8). La decisión
de comprimir es por imagen y por contenido, lo que da aleatorio por apertura y estático por
ejecución. Era la pista 2 de la sección 9.3 (muestras mixtas); las demás caen con ella. Leer el depth en
layout `General` y desactivar `VK_AMD_mixed_attachment_samples` no cambiaron nada en su prueba, lo
que cierra también `GOW3_AMD_PATHS_OFF=mixed`. El `Unimplemented depth overlap copy` del log de
Momexdxd es una copia que involucra ese depth multisample; con la corrección el camino desaparece.

**Corrección (`GOW3_NO_MSAA`, patch de kalil03 aplicado en esta PR con test).** Las imágenes
multisample del guest se crean con 1 muestra y el sharp `Color2DMsaa` se trata como `Color2D`,
de modo que el shader se genera con `texture2D` y sin índice de muestra (el tipo entra en la especialización
del shader, `specialization.h`, y `emit_spirv_image.cpp` solo emite el índice para `Color2DMsaa`:
una caché antigua no contamina). Tamaño y dirección en el guest siguen con el recuento real
(`ImageInfo::guest_samples`, `GuestNumSamples()`). Efectos secundarios conocidos:
`aa_config.NumSamples()` pasa a 1 para shaders con sample coverage; `motion_possible`
(`vk_pipeline_cache.cpp`, exige depth de 1 muestra) pasa a valer en los pases de 2 muestras (solo
importa con el upscaler activado); `ReinterpretColorAsMsDepth` y `SetBackingSamples` quedan sin uso
en los drivers de AMD e Intel en Windows. NVIDIA, RADV, AMDVLK, ANV y MoltenVK no cambian.

Su resultado, abriendo el juego desde cero hasta la apertura en tiempo real y la primera lucha:

| | aperturas con líneas | bloques malos en la profundidad lineal |
|---|---|---|
| sin el patch, o `GOW3_NO_MSAA=0` | 10 de 11 | unos 16.100 |
| con el patch (predeterminado) | 0 de 5 | 30 a 80 (ruido del detector) |

La apertura limpia sin el patch cuadra con el "a veces abre sin las líneas" de los reportes. No probado:
el resto del juego, FPS, otras resoluciones, otras tarjetas, bordes con y sin las 2 muestras. El build
`windows-v2.1.7-teste3` es para eso (sección 7).

**Resultados del build (2026-10-13, #65).** uviiitor (5 aperturas), Tempus3214,
eduhhh10zx-hue (RX 6650 XT, varias aperturas), rafaelsilvaiasd-lang (con su save) y
FilipiEPTeixeira: las líneas desaparecieron. Ningún reporte de vuelta de las líneas, de bordes peores o de FPS
hasta ahora; el control con `GOW3_NO_MSAA=0` nadie lo ha reportado.

**Intel Arc (#28).** Con el build `windows-v2.1.7-amd-teste1` y `GOW3_NO_MSAA=1` mediante un `.bat`:
ZeroTwoMiu (Arc A750, driver 101.9034), yisus230291, Hussein798o (Arc 140V; sus logs anteriores
decían `guest multisampling on`, el `.bat` del release era el de control),
silvajeangomes2026-spec (MSI Claw 8 AI+, Arc 140V) y skinnersweet24 (Arc B580): menú y juego
correctos; con el valor predeterminado (`guest multisampling on`) la corrupción continuaba. Por eso el valor predeterminado
pasó a desactivar el multisampling también en el driver propietario de Intel en Windows
(`gow3_msaa_default.h`), y el launcher muestra la tarjeta y la decisión en "Multisampling do jogo"
(sección 5), sin `.bat`. La HD 630 tiene otra causa (`VK_EXT_vertex_input_dynamic_state`, PR de
Intel).
