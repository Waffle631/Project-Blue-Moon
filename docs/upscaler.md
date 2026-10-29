# Temporal upscaling y frame generation — análisis de fotogramas y plan

## TAA nativo y assets de FSR 4 ausentes (2026-10-01)

`upscaler=taa` / `GOW3_UPSCALER=taa` es un modo de AA temporal independiente a resolución
nativa, disponible en vivo en el launcher y en el overlay. Usa el jitter y los vectores de
movimiento de cámara/objeto existentes, la reproyección, el rechazo por profundidad y el
clipping de vecindad 3×3 en un único pase compute con dos historias RGBA16F en ping-pong.
La historia guarda la profundidad en alfa; el alfa original de la escena se conserva con el
HDR merge. El HUD se compone después y nunca entra en la historia. Los cambios de salida y
los cambios de AA/proveedor reinician la historia. TAA usa las dimensiones de Native AA
conservando el preset de FSR guardado, y no reserva un FSR context ni ejecuta un modelo
FSR/RCAS. Aun así añade trabajo de GPU respecto a desactivar el AA temporal; además se
mantiene el coste de raster nativo en 4K. La ruta de compatibilidad explícita
`GOW3_RENDER_RES` debe estar sin definir.

FSR Native AA no es un pass-through rápido: renderiza a la resolución de salida y además
ejecuta la reconstrucción temporal (incluido el modelo en FSR 4) y el sharpening opcional.
Por eso puede ser más lento que no usar AA con la misma salida.

Los assets de FSR 4.1.1 tienen niveles 1080p/2160p separados y modelos estándar/Ultra:
`t1080_m0`, `t1080_m1`, `t2160_m0`, `t2160_m1`. Las salidas por encima de 1080p necesitan el
nivel 2160, sea cual sea el preset. La falta de `t2160_m0/spd.spv` significa que faltan
assets, no un fallo de una característica de la GPU. Estos assets no vienen incluidos:
construye el conjunto completo con `tools/fsr4cap/build_assets.sh` y colócalo en
`GOW3_FSR411_DIR` o en `fsr4_411` del directorio de datos empaquetado. Los fallos fatales de
assets ahora seleccionan FSR 3.1 en los ajustes en vivo, de modo que el menú muestra el
proveedor activo, conserva el error y permite reintentar FSR 4 tras cambiar la salida o
instalar los assets.

`taa-shader-test` comprueba la salida SPIR-V real para acumulación, clipping, disocclusion
por profundidad, historia no válida y movimiento fuera del fotograma en Vulkan (Lavapipe
funciona). `out/taa-appimage-build.log` registra estas comprobaciones, los round trips de
ajustes y 64 tests de Python. `out/taa-live-validation.log` y
`out/taa-appimage-validation.log` comprueban TAA a 1080p/720p/1440p/4K, TAA ↔ FSR 3 y un
modelo FSR 4.1.1 del nivel 2160 ausente en un único proceso de juego, incluidas capturas de
salida y movimiento de cámara. `out/taa-launcher-validation.log` comprueba los controles GTK
reales y los ajustes guardados. TAA todavía no se ha probado en la GTX 1060 del tester.

## El parche de inicio vuelve a ser el predeterminado para salidas distintas de 1080p (2026-10-02)

La ruta en vivo descrita abajo hizo que la Steam Deck y una GTX 1060 + Haswell de 4 núcleos cayeran a 7–8 FPS
(AppImage de la release 0.1: 40–50 FPS en la Deck). Con las reservas del guest a 1920×1080, el
postprocesado del juego se queda en 1080p incluso con salida 720p, los pases compute del guest resuelven los proxies de escena
de vuelta a 1080p en cada fotograma (RX 7800 XT, 720p FSR 3 Performance con 4 núcleos: 826 frente a 576
draws/fotograma, 185 frente a 210 FPS), y en las GPUs sin shader stencil export cada copia de
depth/stencil requería nueve draws.

- `run.sh` vuelve a parchear el tamaño de render del juego para salidas distintas de 1080p (como en 0.1) y
  lo marca con `GOW3_AUTO_RENDER_RES=1`, de modo que un reinicio dentro del juego lo recalcula. Los cambios de preset y de
  salida requieren entonces un reinicio (el menú lo ofrece). `GOW3_LIVE_RES=1` selecciona la ruta en vivo.
  La salida 1080p y TAA conservan la ruta en vivo.
- Los proxies de escena de depth/stencil vuelven a necesitar `VK_EXT_shader_stencil_export`; sin ella estos
  objetivos se quedan nativos (como en 0.1). `GOW3_SCENE_STENCIL_BITS=1` sigue forzando el
  resampler portable para los tests.

## Cambios de resolución en vivo (2026-10-01)

El selector de salida dentro del juego ahora redimensiona los objetivos del host de 720p/1080p/1440p/2160p en el
límite de un display pass. Los presets usan `output / scale`, incluidos tamaños de raster por encima de
1080p (4K Quality: 2560×1440; Native AA: 3840×2160). Las reservas del guest se mantienen en
1920×1080; `run.sh` ya no inserta un parche de resolución para las elecciones normales de salida.
Se conserva el override de compatibilidad explícito `GOW3_RENDER_RES`.

`SceneTargets` resuelve y retira los proxies antiguos antes de cambiar de tamaño. Los recursos y la
historia de FSR se reconstruyen para la nueva entrada/salida, con esperas de finalización tanto para
FSR 3 como para FSR 4. La ruta de 1080p mantiene la reconstrucción HDR antes del post del juego;
las demás salidas reconstruyen los proxies de escena con tonemapping en el primer pase de Scaleform,
y luego dibujan la geometría de HUD/menú en el objetivo del tamaño de salida. El display buffer también se
redimensiona. La identificación del shader de movie evita que un pase de post a pantalla completa
de tamaño nativo se confunda con UI.

Los pases compute del guest conservan sus tamaños de dispatch nativos y pueden resolver los proxies
de vuelta a 1080p. Este cambio no escala todos los pases de postprocesado; el rendimiento
y el detalle intermedio pueden diferir de los antiguos tamaños de render con parche de inicio.

Verificación: `out/dev/live-resolution-summary.log` cambia salida, preset y FSR
apagado/encendido en un mismo PID. `GOW3_PRESENT_DUMP_TRIGGER` captura el display
buffer del host ya completado (incluido el HUD) en `GOW3_DUMP_DIR`. Las capturas confirman buffers
reales de 720p/1440p/2160p. `scene-resolution-test` cubre los round trips de color/depth/stencil a través de
proxies tanto menores como mayores que la reserva del guest. `GOW3_PRESET_FILE` acepta
`preset [output-index [upscaler-index]]` para cambios con script equivalentes a los del menú.

Analizador de fotogramas: `GOW3_CAPTURE_TRIGGER=<archivo> GOW3_CAPTURE_DIR=<dir>`; al crear el archivo se graba
el siguiente fotograma (límite: el pase que escribe un display buffer) — pases, objetivos, shaders,
texturas muestreadas y el primer 1 KiB de las constantes enlazadas para los pases pequeños y los primeros draws.

## El fotograma de God of War III (mapeado el 2026-10-10, render 2560x1440)

Capturado con el menú del juego (Insert > Diagnostics > Capture frame, archivos en
`user/captures/`). `tools/gow3_frame_map.py <captura>` encuentra cada pieza de abajo por estructura,
no por dirección (las direcciones cambian con el parche de resolución de render), y sale con 1 cuando falta
alguna. No hay buffer de velocidad: los vectores de movimiento hay que calcularlos.

| Etapa | Qué |
|---|---|
| primer pase de un fotograma | 2 draws que copian la imagen final del fotograma anterior al display buffer |
| depth pre-pass | solo profundidad, toda la geometría opaca (500–900 draws): **profundidad de escena**, D32S8 al tamaño de render |
| profundidad lineal | pase a pantalla completa en R32F al tamaño de render |
| sombras + light pre-pass | cascadas D32 de 2048x2048, luces acumuladas en un objetivo RGBA8 sRGB |
| pase de material | la geometría del pre-pass otra vez (mismo número de draws), muestreando el buffer de luz |
| transparencias/efectos | dos objetivos RGBA8 sRGB con un segundo buffer de profundidad |
| post | pirámide de bloom de 1280x720 a 160x90, luminancia en R16G16F, **exposición 1x1 R16G16F** (la lee la CPU en cada fotograma) |
| composite | pase a tamaño completo que muestrea el bloom a mitad de tamaño: **color de escena antes del HUD**, RGBA8 sRGB al tamaño de render |
| HUD | pases posteriores que dibujan sobre esa misma imagen (algunos con el segundo depth); el juego oculta el HUD fuera de combate |

## Constantes de cámara

| Dónde | Qué |
|---|---|
| vertex shader del depth pre-pass (`aed40011eb5259f3`), slot 1, 144 bytes, floats 0–15 | **view-projection**, column-major; igual para todos los draws de un fotograma, cambia con la cámara y su campo de visión |
| mismo buffer, floats 16–31 | la matriz de mundo del objeto (identidad para el escenario estático) |
| buffers de 64 bytes (`*bbe4f109`, slot 0) | solo la proyección: x 1.96855, y 3.49965 en 16:9 (los planos cinemáticos la estrechan, p. ej. 1.5073), near 0.5, plano far muy lejano, profundidad D3D |

Las matrices del fotograma anterior no están en el fotograma; el port debe conservarlas.

## Plan

1. Encontrar las constantes de cámara en cada fotograma (depth pre-pass, arriba), conservar la view-projection anterior.
2. Vectores de movimiento de cámara: pase compute a partir de la profundidad y las matrices actual/anterior hacia un
   objetivo RG16F; vista de depuración para comprobarlos. Movimiento de objetos más adelante (repetición del vertex shader con las
   constantes del fotograma anterior mediante el recompiler).
3. Jitter: desplazamiento subpíxel de las posiciones clip-space en los pases de escena (recompiler).
4. Upscaler en la etapa de color de escena (antes del post y la UI): primero FSR 3.1 (abierto, Vulkan nativo,
   también frame generation), luego DLSS (nativo en Linux), XeSS/XeFG y OptiScaler mediante un
   loader para sus DLL de Windows.

## Estado

- Vectores de movimiento de cámara: verificados reproyectando el fotograma anterior (`GOW3_DEBUG_MOTION=1`,
  toggles 1<<22 fotograma reproyectado, 1<<23 mapa de error): la geometría estática coincide.
- FSR 3.1 (`GOW3_UPSCALER=fsr3`, submódulo FireBurn/FSR-Vulkan): color de escena antes de la
  combinación de postprocesado (compute shader 9a9cf8a9), 1:1, RGB escrito de vuelta (el juego guarda datos
  en el alfa). El toggle 1<<24 lo apaga en tiempo de ejecución.
  `9a9cf8a9` era el compute de God of War III. Desde la Spec 1b (2026-10-28) no hay disparador por defecto: hay
  que darlo con `GOW3_UPSCALE_BEFORE_CS=<hash>` (y el shader de la interfaz con `GOW3_UI_TRIGGER_VS`). Con Tsukihime
  el upscaler temporal no puede escalar (`CameraMotion` sin entradas; ver la Spec 1b, tarea 3).
- Jitter: desplazamiento del viewport de la geometría de escena (dibujada con la profundidad de escena, no con quads
  a pantalla completa), Halton(2,3) 8 fases; el signo se comprobó por nitidez (correcto 255, invertido 208, apagado 271 —
  el jitter 1:1 cambia aliasing de alta frecuencia por una imagen algo más suave). Toggle 1<<25 lo apaga.
- Diferencia entre fotogramas con la cámara quieta: ~33 % menor con FSR.
- Pendiente: movimiento de objetos animados (personajes, tela, vegetación), máscaras de
  reactividad/transparencia para partículas y niebla, escalado de resolución de render, frame generation.

## 2026-10-01: FSR 4 no funcionaba con salida 1440p/2160p (parpadeo y temblor)

Vídeo `video_2026-10-01_03-58-59.mp4` (salida 3840x2160, FSR 4 Performance): parpadeo y temblor
de todos los objetos. La causa: el upscaler en este modo no se ejecutaba en absoluto y el jitter seguía
activado: a la pantalla llegaba un fotograma de escena estirado, desplazado en cada fotograma a su fase Halton.

1. El pase de UI se identificaba por el tamaño exacto del objetivo `GOW3_RENDER_RES` (1916x1078), pero el juego reserva
   objetivos con la altura alineada (1916x1080; en las constantes de escena también 1916x1080). `RunScaled` no
   se llamó ni una vez (en el log no había `UI: native composition`). Ahora se admite alineación
   de hasta 8 píxeles (`RenderTarget`), y el tamaño de escena para FSR se toma de las constantes de escena
   (`CameraMotion::RenderSize`, `SceneSize`).
2. Después de eso FSR 4 caía con `external image registration failed (-1000069000)`: `RunScaled`
   creaba nuevos image views en cada fotograma, y el registro de FSR 4 admite ocho. Ahora se usan las mismas
   `CachedView` que en la ruta Native AA.

Comprobación con volcado (`GOW3_DUMP_TRIGGER=<archivo> GOW3_DUMP_DIR=<directorio>`, `GOW3_DUMP_FRAMES`, por defecto 8:
entrada de FSR, vectores de movimiento y salida, raw): con la cámara quieta el PSNR entre fotogramas vecinos de la salida es
~45 dB frente a ~28 dB en la entrada con jitter; al girar la cámara y caminar no hay estelas. Los FPS en este
modo son ~107 en lugar de ~220 — antes FSR 4 simplemente no se ejecutaba.

Pendiente: los sprites (de 4 índices) en el color de escena con profundidad de escena — destellos, lucecitas —
siguen sin desplazarse con el jitter (regla «≤ 6 índices = pase a pantalla completa»).

## 2026-10-01: FSR 4 más rápido — pase post (2.9 → 0.8 ms en 4K)

Mediciones: `GOW3_FSR4_PROFILE=1` (tiempo de cada pase de FSR 4, parche del proveedor en el submódulo,
ver `gpu/patches/fsr-vulkan`), benchmark fuera del juego `out/gpu/fsr4-bench` (`ninja -C out/gpu
fsr4-bench`; `--stats` — registros e instrucciones de RADV). 4K Balanced (2260x1272 → 3840x2160),
RX 7800 XT: todo FSR 4 — 5.8 ms, de los cuales **post 2.5–2.9 ms** (no la red neuronal: las últimas capas,
pixel shuffle 2x2 y mezcla con la historia), 12 pases del modelo — 2.4 ms, pre 0.55.

Causa: cada hilo calcula un bloque 2x2 de píxeles de salida y los escribe uno a uno en tres imágenes
(estado recurrente, historia, salida) — cada instrucción de escritura de la wave escribe píxeles
alternos. Quitando cualquiera de las tres escrituras, el pase se aceleraba entre 1.3 y 4 veces con el mismo código.

Solución: `tools/fsr4_optimize.sh` decompila el post (spirv-cross), `tools/fsr4_post_lds.pl`
reúne el bloque 16x16 del workgroup en shared memory y escribe en líneas contiguas, glslang
lo compila de vuelta en `fsr4_shaders/opt/`; `vk_fsr4.cpp` lo toma de allí (`GOW3_FSR4_OPT=0` —
el original). Dos sutilezas, encontradas comparando salidas:
- spirv-cross traduce el desempaquetado int8 con signo (`OpBitcast` a `i8vec4`) como `unpack8(uint)`
  (sin signo) — sin corregirlo el resultado es totalmente distinto (PSNR 17 dB);
- los valores en shared memory deben seguir siendo float: el compilador convierte un half de shared memory
  en una escritura de 16 bits, y esta redondea de otra forma a unorm8.

`tools/fsr4_verify.sh` compara el post original y el optimizado para todos los presets con salida
1080p/1440p/4K sobre entradas pseudoaleatorias: 1440p y 4K — idénticos bit a bit; post 4K 2.1–3.5 →
0.81–0.87 ms, 1440p 1.2–1.5 → 0.36–0.38 ms, 1080p 0.37–0.69 → 0.20–0.22 ms. En el juego (4K
Balanced): FSR 4 5.8 → 4.0 ms, fotograma de GPU 13.1 → ~11.6 ms; a partir de ahí los FPS dependen de la CPU (el hilo de
comandos de GPU espera las copias de memoria del guest, «host copies» ~20 %).

Hallado de paso: **el FSR 4 original en el nivel 1080 (salida 1920x1080) no es determinista** — de
una ejecución a otra con entradas idénticas cambia una franja en el borde izquierdo (columnas 0–132), es decir,
en algún lugar hay una carrera o lectura de memoria sin inicializar en el modelo/proveedor. Con salida 1080p
puede provocar parpadeo en el borde izquierdo del fotograma. Sin investigar.

La máscara de reactividad con FSR 4 ya no se calcula (FSR 4 no la acepta).

### Qué más se comprobó (en lo mismo, 4K Balanced)

- **A/B en un mismo proceso** (`ab.sh`, bit 24 — FSR apagado, copia de UI sin upscaling): 86.5 FPS con
  FSR 4 frente a 87.7 sin él. Tras optimizar el post, el fotograma depende de la CPU (hilo de comandos de GPU,
  espera de copias de memoria del guest), no de la GPU. El async compute para FSR (solaparlo con el inicio
  del siguiente fotograma) no daría más que ese ~1 % — aplazado hasta acelerar la parte de CPU; además
  exige reordenar los comandos de UI y de salida del fotograma N tras el trabajo del fotograma N+1.
- **WMMA (`VK_KHR_cooperative_matrix`, RADV en RDNA3 lo admite)**: prototipo del pase 1 del
  modelo (bloque residual de 16 canales: 3x3 16→16, 1x1 16→32 ReLU, 1x1 32→16) — 0.85 ms en la primera
  variante, 0.49 ms con disposición por palabras en shared memory, frente a 0.30 ms del dot4 original.
  La aritmética en WMMA ocuparía ~0.1 ms, pero con 16 canales la disposición de tiles, los epílogos vía shared
  memory y la ocupación (6 waves/SIMD, límite por LDS) se comen la ganancia. El techo es de aproximadamente 1 ms en
  los 12 pases con un trabajo manual fino en cada uno; no iniciado.
- `RADV_PERFTEST=cswave32` (wave32 para compute): FSR 4 más lento, 4.2 → 5.4 ms.
- El pase pre (0.54 ms) no está limitado por la escritura (0.50 ms sin escritura).

## 2026-10-01: parpadeo de FSR 4 en el borde izquierdo con salida 1080p — carrera en el pase 11 del modelo

El no determinismo del nivel 1080 (ver arriba) es un error en el shader del modelo v07, pase 11 (decodificador,
1/4 → 1/2 de resolución). Cada hilo es un píxel de la entrada a 1/4 de resolución y escribe un bloque 2x2 de la salida
a 1/2 de resolución. El dispatch redondea el ancho a 64 hilos, y los hilos fuera del ancho de entrada no
se detienen: en el nivel 1080 (entrada de 480 de ancho) los hilos 480..511 escriben píxeles de salida
960..1023, es decir, los primeros píxeles de la fila siguiente (la fila de cualquier tensor — 15360 bytes), y
compiten con sus autores legítimos. Con salida 4K el ancho 960 es divisible entre 64, no hay error.

`tools/fsr4_pass11_guard.pl` añade una salida temprana para los hilos fuera del límite (el tamaño se toma de
la comprobación de vecinos del propio pase); `tools/fsr4_optimize.sh` lo compila en `fsr4_shaders/opt`
para todos los presets junto con el post; `vk_fsr4.cpp` y el benchmark toman de `opt/` cualquier pase.
`tools/fsr4_verify.sh`: 1440p/4K — idénticos bit a bit al original (la protección allí no se activa); 1080p —
mismo resultado de una ejecución a otra, con diferencias respecto al original tras el primer fotograma solo en la
antigua franja del borde izquierdo.

Hallado en el proceso: spirv-cross traduce todos los desempaquetados int8 con signo (`OpBitcast` a `v4char`)
como `unpack8(uint)` sin signo; en el pase 11 hay nueve formas distintas. La corrección común para los
scripts es `tools/Fsr4SpirvCrossFixes.pm` (todos los `unpack8` mediante un helper con signo).

## 2026-10-01: FSR 4.1.1 — qué hay dentro y cómo ejecutarlo en Vulkan (reconocimiento)

Fuente: `amd_fidelityfx_upscaler_dx12.dll` 4.1.1.2740 de OptiScaler (`FSR4_LATEST`).
- La DLL contiene el proveedor `ffxProvider_FSR4_Int8` / `Fsr4Int8UpscalerModel` (versión «FSR4-i8 4.1.1»).
  1028 contenedores DXIL (25.7 MB): pases del modelo `fsr4_model_v07_fp8_no_scale_pass1..12` (+ `_post`),
  270 variantes de `prepass`, 144 de `postpass`, `fsr_rcas_pass` y un FSR 3.1/2 de reserva. No hay datos comprimidos.
- A pesar del «fp8» en el nombre, los pases del modelo son DXIL normal con `dot4AddPacked` (int8 dot4), sin
  extensiones de AMD (WMMA). La arquitectura es la misma v07 (12 pases), pero los pesos y sesgos no están incrustados en
  el shader sino que se leen de `InitializerBuffer` (t18, typed buffer), los tamaños de los tensores vienen del cbuffer
  `CsTensorSizes`, y el scratch es `u11`. Cada pase tiene 9 variantes: la malla está incrustada (niveles 1080 / 4K /
  8K: 480x270, 960x540, 1920x1080 para el pase 5) y, al parecer, los presets.
- `amdxcffx64.dll` (loader 2.3.0.2913, lo carga Proton) — los mismos pases FP8 de FSR 4 y la generación de
  fotogramas por ML (`mlfi_*`); no hay un modelo INT8 aparte. `FSR4_INT8` 4.0.2 es `v07_i8`, es decir,
  lo que el port ya usa.
- dxil-spirv (HansKristian-Work/dxil-spirv, se compila desde el código fuente) traduce los pases a
  SPIR-V correcto: `dxil-spirv <pass> --enable-shader-i8-dot --ssbo-uav --use-reflection-names`.

Lo que falta para un proveedor 4.1.1 propio: la correspondencia de los contenedores (preset, nivel, pase), el
contenido de `InitializerBuffer`, los valores de `CsTensorSizes`, las permutaciones de prepass/postpass, las constantes y
los tamaños de dispatch. La vía más fiable no es desmontar el código x86 del proveedor, sino grabar un fotograma:
un pequeño programa de prueba con la FFX API (DX12, MinGW) bajo Wine/Proton con vkd3d-proton, selección explícita
del proveedor «FSR4-i8» mediante un override de versión, y una capa Vulkan que registre los pipelines (SPIR-V de
dxil-spirv), el contenido de los buffers y los tamaños de dispatch. Después, la reproducción con un proveedor propio
a través del mismo benchmark (`fsr4-bench`), con comprobación contra la salida grabada.

## 2026-10-01: FSR 4.1.1 en el port (upscaler=fsr411)

El modelo 4.1.1 (INT8) de AMD funciona de forma nativa en Vulkan y coincide con la DLL bit a bit.

**Cómo se obtuvo.** `tools/fsr4cap/fsr4cap.exe` (C, MinGW) llama a la FidelityFX API 2.3 de
`amd_fidelityfx_loader_dx12.dll` con la versión 4.1.1 explícita y, sustituyendo las tablas de métodos de D3D12,
graba todo lo que hace la DLL bajo vkd3d-proton: pipelines (DXIL), firmas raíz,
descriptores, cargas, constantes, dispatches. El fotograma son 29 dispatches: SPD de autoexposición, prepass,
pass0_post, pases del modelo 1..12 (tras cada uno `_post`, puesta a cero del marco del tensor), postpass, RCAS.
- Hay dos modelos: m0 (Native..Performance) y m1 (Ultra Performance, relación 3.0), pesos de 128 KB
  (`InitializerBuffer`). Niveles: t1080 (salida hasta 1920x1080), t2160 (mayor, incl. 2560x1080).
- Los tensores se calculan a partir de la salida alineada a 8; la tabla `CsTensorSizes` (17 tamaños), el número
  de grupos y las constantes (MLSR como en v07, pero width/height sin alinear y `inv_scale = 1 / scale`;
  SPD; RCAS) las comprueba `extract.py` sobre 20 grabaciones.
- dxil-spirv traduce el DXIL igual que vkd3d-proton: `--mixed-float-dot-product`
  (`VK_VALVE_shader_mixed_float_dot_product`, dot2 sobre half con acumulación en float) — sin ello
  PSNR 40 dB en lugar de coincidencia exacta. `--class-bindings` (parche) distribuye los registros SRV/UAV/
  CBV/samplers en los bindings +0/+32/+64/+96, y el runtime enlaza los recursos por nombre.
- `fsr411/fsr411.cpp` — núcleo independiente en Vulkan (el juego y `fsr4-bench --fsr411`).
- `verify.sh`: la DLL y la reproducción con entradas idénticas, 8 fotogramas — idénticos bit a bit en
  1080p (ambos modelos), 1440p, 2560x1080, 4K (ambos modelos). 1600x900 diverge solo en el borde
  derecho, donde la propia DLL no es determinista (carrera en sus shaders cuando el ancho del tensor no es múltiplo de 64).

**Aceleración.** El postpass se reescribió con workgroup memory a nivel del ensamblador SPIR-V
(`postpass_lds.py`; pasar por GLSL es impreciso: se pierden `DenormPreserve` y la colocación exacta de
`RelaxedPrecision`, y algo más cambia el resultado): 2.18 → 0.95 ms en 4K, idéntico bit a bit a la DLL.
Todo FSR 4.1.1: 1.09 ms en 1080p (1280x720), 4.2 ms en 4K Balanced. En el juego (4K Balanced)
el upscaler 4.12 ms frente a 4.32 ms del v07 acelerado.

**Construcción de assets** (por el usuario, a partir de sus DLL; en el repositorio no hay nada de AMD):

    bash tools/fsr4cap/build_assets.sh <amd_fidelityfx_upscaler_dx12.dll 4.1.x> <amd_fidelityfx_loader_dx12.dll 2.3.x>

Hacen falta nix-shell (o MinGW, cmake, ninja, python3, SPIRV-Tools, umu-run), red para dxil-spirv y para las
cabeceras del FidelityFX SDK y Proton (GE-Proton). Las DLL las tiene, por ejemplo, OptiScaler
(`FSR4_LATEST/`) y los juegos con FSR 3.1/4. Construir desde cero da los mismos archivos; `VERIFY=1` al final
compara con la DLL.

**Corrección al A/B anterior («el fotograma depende de la CPU»).** Aquellas mediciones se hicieron mientras 12 procesos colgados
de shadPS4 ocupaban un núcleo cada uno. En un sistema libre: «host copies» 1.6 %, espera de GPU ~41 % — el fotograma
depende de la GPU, así que el async compute para FSR vuelve a tener sentido.
