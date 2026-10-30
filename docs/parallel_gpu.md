# Procesamiento paralelo de comandos de GPU — notas de diseño

Objetivo: eliminar el cuello de botella de un solo núcleo del procesador de comandos de GPU emulado
(`shadPS4:GpuCommandProcessor`, ~100 % de un núcleo mientras el resto de la CPU está ocioso).

## Mediciones (el juego original del port base, escena de prueba, ~70 FPS, `TSUKI_DCB_STATS=1`)

| | |
|---|---|
| Command buffers gráficos enviados | 4200–5800/s, **~65–80 por fotograma** |
| Draws | ~110 000/s, ~1600 por fotograma |
| Draws por command buffer | en su mayoría 10–99, máx. ~200 |
| IndirectBuffer anidado | ninguno |
| Estado antes del primer draw | ~118 dwords de registros de context + ~31 de SH escritos; solo el 8 % empieza con ClearState |

Perfil del hilo de GPU tras el trabajo de un solo hilo (perf, `cpu-clock:u`):
binding de texturas ~25 %, selección de pipeline ~19 % (construcción/comparación de StageSpecialization,
sharp fetches), binding de buffers ~14 %, render targets ~8 %, decodificación PM4 ~3 %.

## Restricciones

- Los command buffers no son autocontenidos: el estado de los registros se hereda, así que un worker necesita
  el estado de los registros al inicio de su buffer (los rangos de context + SH son ~6–8 KiB; un
  snapshot por buffer son ~40 MB/s a 70 FPS).
- La preparación de un draw lee memoria del guest, no solo registros: user data extendido (EUD) y
  código de shaders. Los paquetes anteriores del stream (DMA, WriteData, volcados del constant engine) pueden
  cambiar esa memoria, así que el trabajo hecho por adelantado respecto al hilo de GPU puede ver datos obsoletos.
- Las cachés (buffer, textura, pipeline) y el estado scratch por draw (flags en `Image`,
  user data dentro del `Shader::Info` compartido) son de un solo hilo por diseño.
- El seguimiento de layouts de imagen y de barreras supone un único stream ordenado.

## Plan

1. **Preparación especulativa de draws en workers, validada en el hilo de GPU.**
   Un pase serie barato registra el estado de los registros al inicio de cada command buffer.
   Los workers reproducen las escrituras de registros de su buffer y, por cada draw, precalculan las
   partes puras: clave de pipeline y entradas de la especialización de stages, sharp fetches, descripciones de
   texturas y render targets, valores del estado dinámico, rangos de vertex buffers. Cada resultado
   lleva el user data aplanado a partir del cual se calculó. El hilo de GPU sigue decodificando
   el stream y refrescando el user data aplanado (barato), lo compara con la copia
   del worker y usa el draw preparado solo si coinciden; si no, calcula
   como hoy. La corrección nunca depende de los workers.
   Refactor previo: los consumidores de sharps leen el user data mediante una vista en lugar de
   los miembros compartidos de `Shader::Info`, y el scratch por draw pasa a un struct de contexto.
2. **Búsquedas en caché seguras entre hilos.** Encontrar buffers/imágenes/views existentes pasa a los
   workers; la creación, las subidas y las barreras se quedan en el hilo de GPU.
3. **Grabación Vulkan en paralelo.** Cada worker graba su propio command buffer; se
   ejecutan en orden de envío con barreras en las uniones.

Cada paso conserva un bit de `TSUKI_TOGGLE_FILE` para poder desactivarlo en tiempo de ejecución y
compararlo mediante capturas y tasa de fotogramas.

## Portabilidad

Debe escalar hacia abajo hasta la Steam Deck (4 núcleos / 8 hilos): el número de workers sigue a
`hardware_concurrency()`, sin espera activa cuando los núcleos escasean, sin AVX-512.

## Resultados

Paso 1 (preparación de draws, 4 workers, toggle 8192), escena de prueba, misma vista:
71.5 FPS con draws preparados frente a 64.1 sin ellos (+11.5 %), capturas idénticas.
El 97–98 % de los draws directos usan el pipeline preparado; cada worker `bb:DrawPrep` ~10 % de un núcleo.
El hilo de GPU sigue ocupado ~90 %: el binding de texturas/buffers es el siguiente objetivo (paso 2).

Faults de escritura del guest (misma vista, toggle 65536): el juego llena sus buffers por fotograma
de forma secuencial y cada página de 4 KiB costaba un fault de protección — ~115k faults/s, ~20 % de cada
GXWorker y del hilo principal gastado en el kernel. Desprotegiendo la ventana alineada de 64 KiB
alrededor de un fault: 16k faults/s, tiempo de kernel ~8 %, **81.0 FPS frente a 66.4** (+22 %), fotogramas idénticos.
El hilo de comandos de GPU vuelve a estar en ~100 %: es de nuevo el límite.

Descartado: "hot pages" (no volver a proteger nunca las páginas escritas repetidamente, subirlas en cada
binding) — el conjunto creció hasta ~14k páginas, las resubidas bajaron la tasa de fotogramas a 33 FPS y siguió un timeout
del ring de la GPU. Queda opt-in tras TSUKI_HOT_PAGES=1.

Caché de descripciones de texturas 2-way/4096, ruta rápida de mismo objetivo, omisión del LRU touch, sin
búsqueda de meta por textura: otra vista exterior, 93.9 FPS; con los memos de texturas apagados (máscara 1056) 65.6 FPS.
Cerca del tope de 100 Hz de la pantalla (ritmo por vblank), por lo que más ganancias requieren una prueba sin límite.

## Stutter de streaming (líneas "Stall:" de TSUKI_FRAME_STATS)

Correr por áreas nuevas daba fotogramas de 60–170 ms (también en shadPS4). El hilo de GPU estaba ocupado
todo el fotograma, sobre todo en el kernel, subiendo 50–400 MB de texturas y buffers por fotograma.
Hallazgos, en el orden en que se corrigieron:

1. Las copias guest-a-staging se ejecutaban en un solo hilo (el hilo de grabación; las texturas en el hilo de
   GPU). Ahora empiezan de inmediato en hilos de copia (`Gow3Copy::Async`, `gow3_copy.cpp`);
   las copias pequeñas se agrupan por hilo (un despertar por cada ~512 KiB — uno por copia costaba un 25 % de FPS).
   Los fences visibles para el guest y el envío a la cola las esperan (`Scheduler::WaitHostCopies`),
   lo que mantiene la corrección del parpadeo de la UI (el guest reutilizaba buffers antes de que se ejecutaran las copias diferidas).
2. Entonces las copias iban a 0.2–0.4 GB/s por hilo, casi todo en el kernel: el primer acceso de CPU
   a un nuevo bloque de staging hace que el kernel lo reserve y lo ponga a cero (~2 ms por 16 MiB), y el
   pool de staging liberaba bloques tras 3 s de inactividad, entre ráfagas de streaming. Ahora conserva 512 MiB
   (`TSUKI_STAGING_KEEP_MB`), libera el resto tras 30 s y puebla 128 MiB al inicio.
3. Faults de escritura: una ventana de desprotección de 256 KiB (`TSUKI_FAULT_WINDOW`) los reduce a la mitad otra vez.
   `TSUKI_UFFD=1` rastrea escrituras con la protección de escritura de userfaultfd en lugar de mprotect (sin
   write lock del espacio de direcciones, sin divisiones de mapeos); la protección de lectura para los readbacks sigue usando
   mprotect. Elimina el tiempo de mprotect pero no cambió los stalls de forma medible; opt-in.
4. Las lecturas de archivo hacia páginas del guest protegidas contra escritura fallaban con EFAULT (las copias del kernel no llegan
   al manejador de faults); ahora las lecturas tocan primero cada página de destino.

Resultado: los stalls son sobre todo de 40–50 ms (hilo de GPU ~30 ms de trabajo de draws más ~12 ms de copias)
en lugar de 60–170 ms; el fotograma de carga de área 350 ms en lugar de 430–760 ms.

## Paso 2, primera porción: sharps de recursos en los workers (2026-09-30)

Un stage preparado ahora también lleva los sharps que su worker leyó del user data aplanado:
cada T# con el hash de su descripción de textura, cada S# y V# (`PrepareResources`,
`PreparedStage::image_sharps` y similares, en el arena del envío). El hilo de GPU
los usa cuando `PipelineCache::UsedPrepared()` indica que el pipeline del draw vino del
draw preparado — los datos aplanados se compararon entonces palabra por palabra, y los sharps no dependen
de nada más. Los tamaños de las listas de recursos se comprueban por stage. El toggle 4096 lo apaga.

A/B en una misma ejecución, quieto, `TSUKI_FPS_LIMIT=0`, FSR 4, fases de 20 s: 69–70 FPS con los
sharps preparados, 66.5–67 sin ellos (+3 %), misma imagen.

Costes del upscaler retirados del hilo de GPU antes de esto (perf, grafos de llamadas DWARF): consultas
de formato al driver por draw en `SceneTargets::Eligible` (~13 %), grabación directa forzada por
`CommandBuffer()` en object motion y la reactive mask, escaneos de listas de índices para object motion
(~10 %, ahora `Motion::IndexRangeCache`).

Lo que queda en el hilo de GPU es sobre todo trabajo sobre el estado compartido de las cachés: binding de texturas ~23 %
(FindView ~5.6 %, UpdateImage/Track/Touch ~5 %, barreras), binding de buffers ~12 % (ObtainBuffer
~11 %), render targets ~8 %. El siguiente paso es una división a nivel de draw: el hilo de GPU conserva la decodificación,
la mutación de cachés y las barreras; una segunda etapa ordenada construye las escrituras de descriptores y el estado
de entrada de vértices a partir de los handles ya resueltos.

## Escalado a los hilos disponibles (2026-09-30)

La preparación de draws antes reproducía todo el stream de comandos en cada worker y se desactivaba
por debajo de 12 hilos de hardware (Steam Deck: sin workers). Ahora:

- Un scanner (`bb:DrawScan`) reproduce en orden las escrituras de registros y guarda, por buffer,
  su checksum inicial y un delta de los bloques de registros de 32 palabras que escribió
  (`AmdGpu::RegDirty/RegDelta`, registrados por `ApplyGraphicsRegisterPacket`).
- Los workers (`bb:DrawPrepN`, la mitad de los hilos de hardware de la máscara de afinidad, 1..8) reclaman el
  buffer escaneado más cercano por delante del hilo de GPU, alcanzan su estado inicial aplicando los deltas
  desde su último buffer (o desde el estado de cola de la cola), y preparan sus draws.
  Más workers significan ahora más buffers preparados en paralelo, no más reproducción duplicada.
- Todos los ayudantes son SCHED_IDLE (`gow3_threads.h`): solo toman núcleos ociosos. Si el scanner
  se queda sin recursos (CPU ocupada), el hilo de GPU lo rebasa desde su propio estado de registros una vez que el retraso
  supera 64 buffers, en lugar de encolar sin límite (`scanner rebases` en las estadísticas).
- Los hilos de copia usan el mismo recuento de afinidad (camino crítico, prioridad normal).

A/B en una misma ejecución (toggle 8192, quieto, `TSUKI_FPS_LIMIT=0`, FSR 4), ~98 % de los draws directos
preparados, sin rebases:

| CPU | con preparación | sin |
|---|---|---|
| 16 hilos, 8 workers | ~74 FPS | ~57.7 FPS |
| `taskset -c 0-3,8-11` (4 núcleos / 8 hilos, similar a la Deck), 4 workers | ~68 FPS | ~53 FPS |

El diseño anterior (4 workers que reproducían) daba ~69 FPS en el mismo punto con 16 hilos, y
ninguno con 8.

## Vertex inputs en los workers (2026-09-30)

`PreparedDraw::vertex` contiene, para la ruta de vertex input dinámico, las descripciones de atributos y bindings
(`GetVertexInputs`), el V# de cada stream, la memoria de los streams fusionada en
rangos con el índice de rango de cada stream, y el XXH3 de los streams (object motion). El hilo
de GPU solo obtiene los buffers de los rangos fusionados y graba los bindings. Se usa cuando se tomó el
pipeline del draw preparado, el número de atributos coincide con el fetch shader del pipeline
y no hay una captura de fotograma en curso; el toggle 4096 lo apaga junto con los sharps.

A/B (quieto, `TSUKI_FPS_LIMIT=0`, FSR 4, 16 hilos): ~69.3 FPS activado, ~65.7 desactivado (+5.5 %;
los sharps solos daban +3 %). Capturas idénticas.

Se examinaron los render targets y se dejaron en el hilo de GPU: sus descripciones ya están
memoizadas por slot (copia de clave + comparación), la pista de tamaño (`last_cb_extent`) es estado del hilo de GPU
y no estado de registros, y el resto de `BeginRendering` es búsqueda de views, barreras y los
proxies de resolución reducida — todo sobre estado mutable compartido.

## Rutas cortas del motor: investigación (2026-09-30, en curso)

- El eboot medido no tiene símbolos pero enlaza el Gnmx de Sony (`sdk\target\src\gnmx\gfxcontext.cpp`,
  `lwgfxcontext.cpp`); el motor propio del juego se apoya encima, con middleware para el
  postprocesado.
- `TSUKI_BUFFER_STATS=1` (buffer_cache.cpp) imprime los bindings de buffers por región del guest cada 5 s.
  Casi todo el tráfico viene del frame ring del motor, ~0x1043400000–0x1049xxxxxx dentro de la
  reserva directa de 2.4 GB en 0x1042c00000: ~400k copias pequeñas de constantes/s (~190 MB/s) y
  ~850 MB/s de resubidas de arena tras escrituras de la CPU (~13 MB por fotograma).
- Ruta corta candidata: importar ese ring a Vulkan (VK_EXT_external_memory_host) para que la GPU
  lo lea en el sitio — sin copias, sin seguimiento de páginas, muchas menos operaciones del hilo de GPU.
  Bloqueo a resolver primero: los fences EOP se señalizan cuando el hilo de GPU graba el paquete,
  no cuando la GPU lo ejecuta, así que el guest puede reescribir datos del ring que la GPU aún no ha leído.
  Con ~100 MB de ring y ~13 MB por fotograma, la vuelta completa es de ~7–8 fotogramas; el retraso de la GPU está acotado
  por el pool de fotogramas del presenter (`present_frames`, número de imágenes del swapchain). Siguiente: medir
  el periodo de vuelta del ring por fotograma y el retraso real de la GPU, luego prototipar la importación tras un toggle.

### Mediciones para la ventana de datos de fotograma (TSUKI_BUFFER_STATS=1)

- Distancia de reutilización de bloques de 64 KiB en 0x104xxxxxxx: abrumadoramente 1 fotograma (luego 2). El motor
  reescribe la misma memoria en cada fotograma; no hay un ring largo.
- Retraso de la GPU cuando el hilo de GPU inicia un fotograma (`GPU lag at frame start`): casi siempre 0 —
  la GPU ha terminado el fotograma anterior. Pero el motor escribe los datos del siguiente fotograma mientras
  la GPU aún ejecuta el actual, y los fences EOP se señalizan en el momento de grabar.
  **Leer esta memoria en el sitio (VK_EXT_external_memory_host) es por tanto inseguro** a menos que
  los fences esperen a la finalización real de la GPU, lo cual le cuesta al guest (que los espera). Descartado.
- Por segundo en la ventana caliente: ~370k copias pequeñas de constantes (~179 MB, ~480 B cada una) y ~3.6k
  bindings de arena que cubren ~13.8 GB (≈3.8 MB por binding) de los cuales solo ~516 MB se resuben.
  Copiar los rangos enlazados en lugar de rastrear páginas multiplicaría el tráfico por ~27 (por eso el
  intento de "hot pages" cayó a 33 FPS); el seguimiento de páginas es el mecanismo correcto ahí.
- Candidatos restantes: snapshots por envío del área de constantes (menos copias, más grandes,
  en lugar de ~6000 por fotograma) y recortar los bindings de vertex buffers al rango de índices que usa un draw
  (los V# cubren pools de vértices enteros, así que cada binding recorre ~950 páginas rastreadas).

### Probado y revertido: chunks de constantes por época

Constantes pequeñas de solo lectura servidas desde un snapshot por chunk y época de tarea de la cola (la época
cambiaba en cada entrada y reanudación de tarea, de modo que un chunk solo servía datos escritos antes de su copia).
A/B en una misma ejecución, quieto: chunks de 32 KiB 82/82/79 FPS frente a 85/84/84 sin ellos (más lento: los
bytes extra copiados cuestan más que las operaciones ahorradas); chunks de 8 KiB 85/85/85 frente a 85/83/85
(sin diferencia). Las copias de constantes por binding no son lo que limita el fotograma; el cambio
se eliminó. `TSUKI_BUFFER_STATS=1` (bindings por región, distancia de reutilización, retraso de GPU) se mantiene.

Estado tras este trabajo, quieto, `TSUKI_FPS_LIMIT=0`, FSR 4: 82–85 FPS; hilo de comandos de GPU
~90 % de un núcleo, hilo de grabación ~92 % (sobre todo su spin), GPU ~70 % ocupada.

### Binding de texturas: sets repetidos y la ruta rápida de UpdateImage

- Medido y descartado: solo ~13 % de los draws enlazan exactamente las texturas y samplers del
  draw anterior en cada stage (recorriendo el nivel de prueba), por lo que omitir sets de texturas completos
  ahorraría como mucho ~3 % del hilo de GPU.
- `TextureCache::UpdateImage` se ejecuta para cada binding de textura; para una imagen limpia y registrada
  ya rastreada y tocada en este periodo de GC solo tomaba el mutex de la caché de texturas (compartido
  con los manejadores de faults de los hilos del guest). Ahora retorna sin el lock en ese caso,
  leyendo los flags de forma atómica (el toggle 1073741824 = 1 << 30 restaura la ruta con lock).
  A/B, 8 fases de 20 s: media de 81.8 frente a 80.2 FPS (+2 %), 3 de 4 pares por delante; la ventana del juego
  estaba parcialmente cubierta por otras aplicaciones durante la ejecución, así que tómalo como orientativo.
- El script A/B ahora registra la línea del log en cada cambio de fase e imprime las medias por fase.

## LTO y PGO (2026-09-30)

`build.sh` compila `libgow3gpu` con LTO (`-flto=auto`, también para sirit y FSR-Vulkan enlazados
en ella) y, cuando `pgo/` contiene un perfil, con `-fprofile-use` (`-fprofile-partial-training
-fprofile-correction`; las funciones modificadas desde el perfil se compilan sin él). Sin `-march`:
la misma build funciona en la Steam Deck.

Recoger un perfil: `TSUKI_PGO=generate bash run.sh` compila una biblioteca instrumentada
(`-fprofile-generate -fprofile-update=atomic`) que escribe `pgo/` cada 30 s (hilo `bb:pgo`:
`__gcov_dump` + `__gcov_reset`, ya que el juego suele terminar mediante `_exit`); juega unos minutos
de gameplay normal. La siguiente build normal lo usa. `TSUKI_PGO=off` / `TSUKI_LTO=OFF` los desactivan.
Regenera el perfil tras cambios de código mayores.

Comparación, tres ejecuciones en el juego (mediana de ventanas de 5 s dentro del juego, >800 draws/fotograma), con el nuevo
campo de `Frame stats` "GPU thread us/draw" (tiempo de CPU del hilo de comandos de GPU por draw, que
tolera mejor las pequeñas diferencias de escena que los FPS):

| build | FPS | GPU thread µs/draw |
|---|---|---|
| sin LTO, sin PGO | 73.2 | 7.72 |
| LTO | 74.4 | 7.58 (−1.8 %) |
| LTO + PGO | 76.3 | 7.35 (−4.8 %) |

## Segundo pase: adónde va el tiempo del hilo de GPU, y un helper de texturas (2026-09-30)

De pie en la escena de prueba, FSR 4, `TSUKI_FPS_LIMIT=0`, ~85 FPS, ~1610 draws/fotograma.

**Perfil on-CPU** (perf, hijos directos): `Draw` 74 % — `BindResources` 32 % (texturas 16 %,
buffers 13 %), `BeginRendering` 8 %, `GetGraphicsPipeline` 5 %, `BindVertexBuffers` 4.5 %,
`ResetBindings` 4 %, estado dinámico 2 %; `DispatchDirect` 6 %; decodificación PM4 y hash de registros ~9 %.
Ninguna función por encima de ~5 % de tiempo propio: el coste son fallos de caché repartidos por las estructuras de
texturas, buffers y barreras (`slot_images[id]`, descripciones de imagen, estado de backing, escrituras de sets).

**Tiempo de reloj** (nuevos campos de `Frame stats`): el hilo de GPU está en la CPU el 91 % del tiempo y
espera envíos del guest el 0.5 %. Alrededor del 9 % está bloqueado en `Scheduler::WaitHostCopies`: ~130
veces por fotograma (eventos EOP/EOS, `WriteData`, envíos) espera las pequeñas copias del guest
encoladas en el stream de grabación, es decir, hasta que el hilo de grabación ha grabado todos los
comandos anteriores, más ~4 % por los hilos de copia (`Gow3Copy::WaitAsync`). El propio hilo de grabación
trabaja ~40 % (el resto es su spin); no es el límite.

### Binding de texturas en un hilo auxiliar (opt-in: `TSUKI_TEXTURE_HELPER=1`, toggle 1 << 22)

`bb:TexBind` enlaza las texturas de un draw mientras el hilo de GPU enlaza sus buffers y resuelve sus
vertex/index buffers (dividido en Resolve/Emit, toggle 1 << 23); el hilo de GPU hace join antes de
`BeginRendering`. El helper toma solo la ruta memoizada (memo de FindImage válido, imagen limpia,
sin proxy de scene target, sin redirección de upscaler, sin storage images, sin arrays de mips, sin storage
buffers en el pipeline); todo lo demás se enlaza tras el join. El hilo de GPU hace join antes de
cambiar el estado de las imágenes (`Runtime::BeforeImageAccess` en `Transit`, `FlushBarriers`,
`SetBackingSamples`, y en `SynchronizeMemoryFromImage` una vez que un texel buffer es alias de una imagen).
El 68 % de los draws se enlazaron en paralelo, la imagen no cambió — ni la tasa de fotogramas:
85.9 FPS con el helper frente a 85.8 sin él (A/B, 6 × 16 s).

Instrumentado con `rdtsc`: la tarea del helper tomó ~6200 ciclos por fork cuando el mismo trabajo
costaba ~4600 en el hilo de GPU, y el hilo de GPU aún esperaba ~2500 ciclos por fork en el
join. Las infos de descriptores, los estados de imagen y los flags de binding que escribe el helper los lee
el hilo de GPU justo después, así que cada draw los mueve entre núcleos; el trabajo propio del hilo de GPU se volvió
más lento aproximadamente lo que cedió. Un fork/join por draw sobre ~7 µs de trabajo limitado por caché
no compensa en esta CPU; el helper queda opt-in para otras CPUs.

### Otros intentos

| cambio | A/B | conservado |
|---|---|---|
| memo de FindView en la caché de descripciones de imagen (1 << 21) + prefetch de escritura en chunks de grabación (1 << 20) | 81.5 frente a 80.8 FPS | sí |
| comprobación de formato por tabla en lugar de `magic_enum::enum_contains` (un escaneo lineal por textura por draw); campos `Image` calientes (flags, binding, rango de seguimiento, backing, ticks) movidos delante de `ImageInfo` | dentro del ruido | sí (sin desventajas) |
| cola de copias del host: copias pequeñas en una cola MPMC lock-free, ejecutadas por el hilo de grabación ocioso, el resto por el hilo de GPU en la espera | bloqueado 9.5 % → 4.8 %, pero 86.5 frente a 87.2 FPS: la copia pasó al hilo de GPU | no |
| sin seguimiento de barreras para rangos de buffers de stream de solo lectura | 85.9 frente a 87.4 FPS, en ambos órdenes de fase (causa no encontrada) | no |
| copias pequeñas del host agrupadas, un comando grabado por cada 32 | 86.4 frente a 86.2 FPS | no |

Corregido de paso: un worker de preparación de draws podía detener el proceso con
`SurfaceFormat: Unknown data_format=15` — leía un V# que el guest aún estaba escribiendo y
`SurfaceFormat` hace assert. Ahora los workers usan `TrySurfaceFormat` y dejan esos draws al hilo de
GPU. Apareció con la build instrumentada para PGO, que es más lenta.

Perfil PGO regenerado para el código modificado (solo rotación de cámara, fusionado con el antiguo).
Antes/después, tal como las compila un usuario (37c8eac con su perfil frente a este estado con el nuevo),
tres reinicios alternados × 50 s: 86.9 frente a 86.4 FPS, 6.47 frente a 6.51 µs/draw — sin cambio más allá de
la dispersión entre ejecuciones (±1.5 FPS).

### Qué escalaría

Mover trabajo a otro núcleo por draw cuesta aproximadamente lo que ahorra, porque los datos son compartidos.
Escalar requiere divisiones donde cada hilo sea dueño de sus datos durante largos periodos:

1. **Pipeline de dos etapas.** Un segundo hilo es dueño de la caché de texturas y del estado de imágenes
   (`PrepareRenderState`, `BindTextures`, `BeginRendering`, barreras, escrituras de descriptores, estado
   dinámico, grabación de draws) y ejecuta un draw por detrás del hilo de GPU, que conserva la decodificación, los pipelines
   y la caché de buffers. El segundo hilo no puede leer `liverpool->regs` (el hilo de GPU ya
   está en paquetes posteriores): mantiene su propia copia de registros a partir de los deltas por buffer
   que el scanner de draws ya registra (`AmdGpu::RegDelta`). Los paquetes que tocan memoria o imágenes
   fuera de los draws (DMA, `WriteData`, EOP/EOS, dispatches, fast clears) vacían el pipeline.
   Estimación: la segunda etapa toma ~30 % del trabajo actual del hilo de GPU, menos los vaciados. Un
   refactor grande y delicado.
2. **Menos trabajo por draw.** Un memo entre fotogramas de las texturas resueltas de un stage (ids de imagen,
   views, samplers) con clave en los hashes T#/S# preparados y validado por la generación del registro,
   la limpieza de las imágenes y los layouts, en lugar de búsquedas por textura.
3. **Las copias antes de los fences** (~9 % bloqueado): mantenerlas fuera del camino crítico del hilo de GPU,
   p. ej. dejando que los workers ociosos de preparación de draws vacíen una cola de copias.

## Pipeline de draws en dos etapas (2026-09-30)

`vk_draw_pipe.h`. El hilo de comandos de GPU (etapa A) conserva la decodificación PM4, el archivo de registros, el
constant engine y la selección de pipeline. Un draw o dispatch directo se convierte en un paquete en un ring
de 16 MiB: los bloques de registros de 32 palabras escritos desde el paquete anterior (`Liverpool::pipe_dirty`,
marcados por `ApplyGraphicsRegisterPacket`), las pistas de tamaño CB/DB, el user data de cada stage, el user data
aplanado y la base del programa, y los parámetros del draw (para un dispatch el `ComputeProgram`).
El hilo de grabación de draws `bb:DrawRec` (etapa B) aplica los bloques a su propia copia de registros y
ejecuta el resto del draw (`DrawRecord`, `DispatchRecord`): texturas, buffers, render targets,
barreras, descriptores, estado dinámico, grabación. Mientras hay paquetes en vuelo, la etapa B es dueña de las
cachés de texturas/buffers, el runtime, el scheduler, los scene targets, el upscaler y el estado
de movimiento.

- La etapa B lee los registros mediante `Rasterizer::Regs()/CbExtent()/CsRegs()` (su copia allí, la de
  Liverpool en el resto) y el user data de shaders mediante `Shader::Info::UserData()/FlatUserData()/
  ProgramBase()`, que devuelven el snapshot que la etapa B instaló para los stages del draw actual
  (`Info::ud_snapshots`, thread-local).
- Todo lo demás que la etapa A hace sobre el estado de la etapa B primero espera a que se vacíe
  (`Rasterizer::DrainDrawPipe`): cada punto de entrada público del rasterizer en el hilo de GPU, los paquetes PM4
  distintos de escrituras de registros/draws/dispatches/fences (`PipelinedOpcode`), los comandos pendientes
  (`ProcessCommands`), los paquetes de la cola compute, `DumpConstRam`.
- Los eventos end-of-pipe/-shader se ejecutan en orden en la etapa B (`Rasterizer::RunInOrder`). Un
  `WaitRegMem` sobre un valor de fence entregado a la etapa B se cuenta como cumplido (`Liverpool::pending_fences`):
  la etapa B ejecuta todo en el orden del stream de todos modos. Las esperas no cumplidas vacían la etapa B y luego ceden.
- El fence EOP lo señaliza el hilo de grabación Vulkan después de las copias de memoria del guest encoladas
  antes (`Scheduler::SignalAfterHostCopies`), de modo que la etapa B no las espera.
- Los draws preparados de un envío siguen vivos hasta que la etapa B los ha superado
  (`RetireSubmission`) en lugar de un vaciado al final.
- Faults: la etapa B maneja los suyos en línea, como el hilo de GPU (`IsGpuSideThread`); el hilo
  de GPU vacía la etapa B antes de manejar uno en línea. Con userfaultfd, los faults del hilo de GPU
  toman la ruta con lock de los hilos del guest.
- `DmaData` a 0x3022C (omitido por el manejador; ~70k/s) no vacía.
- `TSUKI_PIPE_VERIFY=N`: cada N-ésimo paquete lleva también el archivo de registros completo y la etapa B informa de
  las palabras en que su copia difiere (ninguna vista en el juego, menús incluidos).
- `TSUKI_DRAW_PIPE=0/1` anula el valor predeterminado (activado con 8+ hilos de hardware). La máscara de toggles es ahora de 64
  bits (los bits 20-29 son toggles de depuración sin procesar de los vectores de movimiento y el upscaler, que las
  primeras mediciones de abajo también alternaron): 1 << 37 todo el pipeline, 1 << 38 fences en la etapa B,
  1 << 39 WaitRegMem sobre fences pendientes, 1 << 40 dispatches, 1 << 41 fences señalizados por el hilo de
  grabación, 1 << 42 WriteData/DmaData/draws especiales/flip IRQ en la etapa B, 1 << 36 constant
  ring. `Frame stats` añade una línea `Draw pipe`: draws, vaciados que esperaron y por qué, etapa A esperando,
  etapa B ocupada.

Resultados (escena de prueba, quieto, FSR 4, `TSUKI_FPS_LIMIT=0`, A/B en una misma ejecución):

| | pipeline activado | desactivado |
|---|---|---|
| 16 hilos | 96.1 FPS | 80.6 FPS (+19 %) |
| 4 núcleos / 8 hilos (`taskset -c 0-3,8-11`) | 83.8 FPS | 71.0 FPS (+18 %) |

Pasos intermedios (FPS en la misma escena): primera versión, vaciados en cada paquete que no es draw —
79.8 (vaciaba ~80k/s, casi todos `DmaData` sin efecto); omitiéndolos — 89; fences en la etapa B y
`WaitRegMem` perezoso — 92; dispatches entregados — 93; fences señalizados por el hilo de grabación —
103.5 (A/B de ese paso solo: 103.5 frente a 94.9).

Ahora la etapa B es el límite (~85-90 % ocupada, el camino del draw como se perfiló arriba), la etapa A espera la mayor parte del
tiempo, y la GPU está ~80 % ocupada con FSR 4. Vaciados restantes: `WriteData` (~30/fotograma: 192
bytes a cero a un buffer de fotograma y una etiqueta de 4 bytes, cuyos lectores se desconocen), `DmaData` no
trivial, draws indirectos. Siguiente: mover trabajo de la etapa B a la A — el lado de buffers de un draw
(ObtainBuffer, subidas) requiere que la etapa A sea dueña de la caché de buffers y entregue sus comandos a la etapa B.

### Segunda ronda (2026-09-30)

- `WriteData`, `DmaData`, el flip IRQ posterior (la etiqueta del buffer es un `WriteData`) y los draws que
  `FilterDraw` maneja por sí mismo (eliminación de fast clear, resolve, copia de depth) se ejecutan en orden en
  la etapa B. Vaciados: ~7 por fotograma (DumpConstRam, draws/dispatches indirectos).
- **Constant ring** (`vk_constant_ring.h`): la etapa A copia los buffers del guest pequeños de solo lectura (la
  ruta stream de `ObtainBuffer`, y el user data aplanado) a un ring propio de 32 MiB;
  la etapa B solo los enlaza. Una región se reutiliza cuando el envío en el que la etapa B grabó su último
  draw ha terminado (la etapa B sella los paquetes con el tick del envío). Los buffers que se solapan con
  memoria del guest que el trabajo encolado escribirá (storage buffers, DMA, WriteData, fences:
  `NotePendingGpuWrite`) o con memoria modificada por la GPU se quedan en la etapa B.
- `TSUKI_PIPE_VERIFY` también recorre de nuevo las tablas de recursos de cada stage en la etapa B y las compara con
  el snapshot de la etapa A (protegido contra faults: los punteros pueden estar obsoletos para entonces). Se omiten los pixel shaders
  cuyo `Info` la selección de pipeline no refresca (sin user data).

A/B en una misma ejecución (16 hilos, FSR 4 Ultra Performance, bits de toggle limpios): todo el pipeline
110.9 frente a 83.9 FPS (+32 %); constant ring 114.2 frente a 102.5 FPS (+11 %).

### Tiempo de GPU por fotograma frente al preset del upscaler

La GPU (RX 7800 XT) ahora limita más que la CPU. Los presets lo cambian poco:

| modo | FPS | GPU ocupada | GPU ms/fotograma |
|---|---|---|---|
| upscaler apagado | 132 | 76 % | 5.8 |
| FSR 3 Native AA | 116 | 76 % | 6.6 |
| FSR 3 Ultra Performance (escena 640x360) | 114 | 71 % | 6.3 |
| FSR 4 Quality | 115 | 88 % | 7.7 |
| FSR 4 Ultra Performance | 114 | 84 % | 7.4 |

Se usan los scene targets reducidos (1160 de ~1530 draws de escena por fotograma), pero rasterizar la
escena cuesta poco en esta GPU: una novena parte de los píxeles ahorra ~0.3 ms. El resto no depende del
preset (shadow maps, postprocesado y UI a resolución completa, el propio FSR — FSR 4 cuesta
~1.1-1.4 ms más que FSR 3 — y la sobrecarga de emulación: barreras, copias, remuestreo). FSR 4 en
Native AA no arranca ("no free provider frame").

### Perfil de GPU (`TSUKI_GPU_PROFILE=1`, `vk_gpu_profiler.h`)

Timestamps donde empieza cada render pass, dispatch, ejecución del upscaler y fin de envío; el tiempo hasta
el siguiente se le carga a él (incluidas las barreras y copias intermedias; "between submissions"
es sobre todo la GPU esperando a la CPU). Se imprime cada 5 s, ms de GPU por fotograma por etiqueta. Los timestamps
se escriben fuera de los render passes (`radv_CmdWriteTimestamp2` fallaba dentro de algunos) y solo en el
scheduler del rasterizer (el presenter tiene el suyo).

escena de prueba, FSR 4 Ultra Performance, ~110 FPS (8.8 ms/fotograma):

| etiqueta | ms/fotograma |
|---|---|
| GPU ociosa entre envíos | 2.1 |
| copy shader del guest `fefebf9f`, 57 dispatches (HLE) | 2.1 |
| FSR 4 | 1.5 |
| compute del guest `3d5ebf4e`, 8 dispatches | 0.5 |
| pases del G-buffer a 640x360 | ~0.8 |
| el resto (postprocesado, UI, pases menores) | ~1.8 |

Por eso los presets apenas cambian la carga de GPU: solo escalan los pases de escena.

El copy shader tiene HLE (`vk_shader_hle.cpp`) como `vkCmdCopyBuffer` con ~1024 regiones pequeñas
por dispatch. `buffer_multi_copy.comp` ahora hace un lote en un dispatch (toggle 1 << 43):
las copias en sí 0.4 ms/fotograma; con la preparación de buffers previa la etiqueta bajó
de 2.1 a 1.6 ms/fotograma. La mayor parte del resto es `ObtainBuffer` sobre los rangos fusionados (copias de unos pocos
KiB repartidas por hasta 57 MiB de destino): sincronizar solo las partes copiadas ahorró otros
~0.75 ms de GPU pero costó más en la CPU (protección de páginas por parte, sin ruta stream para
fuentes pequeñas: 108.7 frente a 114.3 FPS, solo fuentes 105.7 frente a 111.9), así que se descartó. La tasa de
fotogramas no cambió con el multi-copy shader (aquí limitada por CPU); ayuda donde limita la GPU.

### Memo del estado de render (toggle 1 << 44)

~90 % de los draws continúan el render pass que abrió el draw anterior, y aun así `BeginRendering` rehacía
las búsquedas de targets, transiciones, proxies de scene targets y redirecciones del upscaler en cada uno. Ahora un
draw reutiliza el estado de render anterior cuando el scheduler todavía tiene abierto ese mismo pass (nada
lo rompió: las barreras, copias y dispatches terminan los passes), las entradas coinciden (ids y views de targets,
clave de attachments del pipeline, estado de escalado de escena/raster y redirección del upscaler, generación del
registro de imágenes, control de profundidad), no se pide ningún clear y ningún target es también muestreado por el draw.
Los estados con clears no se recuerdan (el del siguiente draw difiere). 90 % de aciertos; 122.1 frente a 111.8 FPS
(+9 %); las capturas con/sin difieren como mucho lo que dos tomadas en el mismo modo (escena animada).

### Memo de sets de texturas (toggle 1 << 45)

Las texturas resueltas de un stage (imagen tras la redirección de depth, view, backing, rango de subrecursos)
recordadas por los hashes T# preparados, 8192 slots, sets de hasta 16 imágenes muestreadas. Un acierto
requiere la misma generación del registro de imágenes, los mismos backings, ningún rebind, imágenes al día,
sin retroalimentación de render targets y sin redirección del upscaler; entonces solo rehace los efectos por draw
(tick de encontrado, flags de binding, lista de enlazados, transición de layout, uso) y escribe los descriptores.
84 % de aciertos; 128.6 frente a 121.8 FPS (+5.6 %); capturas con/sin dentro del ruido propio de la escena.

### Distancia de fusión del copy shader

El HLE fusionaba copias cuyos rangos cabían en 64 MiB, y cada lote fusionado sincroniza todo su rango de
origen y destino. Con 64 KiB (`TSUKI_COPY_MERGE_KB`): el tiempo de GPU del copy shader
1.5 -> 0.6 ms/fotograma (profiler), GPU ocupada 85 % -> ~77 %, tasa de fotogramas no inferior
(los reinicios varían 125-142 FPS; 16 KiB y 256 KiB similares).

## Tercera ronda (2026-09-30, tarde)

Tasa de fotogramas en la entrada del nivel, 16 hilos, FSR 4 Ultra Performance: ~134 -> ~145-150 FPS
(los reinicios varían unos pocos FPS; los números A/B de abajo son de una ejecución cada uno).

### Dónde espera la etapa A

`Frame stats` ahora imprime, junto a los recuentos de vaciados, la parte del tiempo de la etapa A que esperó cada motivo
de vaciado y cada punto de llamada (`function:line`, `DrainDrawPipe` registra `__builtin_LINE`).
La etapa A espera ~25 % de su tiempo, casi todo en un vaciado por fotograma: el primero que llega
tras un largo tramo de draws espera a que la etapa B los termine. Era `DrawIndirect` (21 %),
luego `DispatchIndirect`, luego un `DumpConstRam` una vez que aquellos se encadenaron en pipeline. Así que la etapa A es más rápida
que la B, y quitar vaciados ayuda solo donde rompe el paso sincronizado; la etapa B (85 % ocupada,
~2.8 µs por paquete, perfil plano) es el límite.

- Los draws y dispatches indirectos se encadenan en pipeline como los directos (toggle 1 << 46): el pipeline se
  selecciona en la etapa A, la búsqueda del buffer de argumentos y el comando indirecto se graban en B.
  +1.4 %.
- Las escrituras pendientes de la GPU (la protección del constant ring) fusionan rangos solapados y se podan cada
  64.ª comprobación en lugar de en cada buffer: `PendingWriteOverlaps` 7.5 % -> 3.6 % de la etapa A; en conjunto
  ~+4 %.
- Memo de sets de texturas: 32768 slots; una cuarta parte de los fallos eran colisiones de slots (37k -> 11k por
  5 s): +1.3 %.
- Tablas por fotograma de la historia de movimiento: direccionamiento abierto en lugar de `std::unordered_map` (cuatro
  reservas de nodos por draw almacenado, liberadas en cada fotograma). Neutral en FPS.
- Probado: un release store en lugar del secuencialmente consistente en `DrawPipe::Commit` con un
  sleep de futex temporizado (+0.4 %, no compensa un posible despertar perdido; revertido).

Los object motion vectors cuestan ~10 % de FPS (155 frente a 141 FPS con `object_motion=0`): la etapa B gasta
~0.28 µs más por paquete, repartidos en DrawRecord (hashes de paletas de huesos, caché de rangos de índices, variantes
de pipeline de movimiento).

### Resolución de escena y la cuestión preset/carga de GPU

Tiempo de GPU ocupada por fotograma (total del perfil menos el tiempo ocioso entre envíos):

| modo | GPU ocupada |
|---|---|
| upscaler apagado (1080p nativo) | 3.67 ms |
| FSR 4 Quality | 5.64 -> 5.50 ms |
| FSR 4 Ultra Performance | 5.11 -> 4.98 -> 4.86 ms |

Rasterizar la escena cuesta poco en esta GPU, así que un preset más bajo ahorra ~0.5 ms, mientras que el propio FSR 4
cuesta ~1.4 ms con salida 1080p; FSR 4 Ultra Performance toma más tiempo de GPU que el
render nativo sin upscaler. Hallado de paso:

- La decisión de tamaño reducido miraba los ocho slots de color, pero los slots más allá del ancho de la máscara conservan
  los targets de pases anteriores — en los pases de iluminación las imágenes del G-buffer que muestrean. Los pases de acumulación de
  luz se quedaban a 1920x1080 en cualquier preset. Corregido (el toggle 1 << 47 lo restaura).
- Cada pase que muestreaba un target reducido hacía que el proxy se remuestreara primero al tamaño nativo
  (~25 resolves por fotograma, 0.4 ms; `TSUKI_GPU_PROFILE` ahora etiqueta resolves, fills, subidas y
  descargas de imágenes). El recompiler marca las imágenes leídas por cualquier cosa distinta del muestreo normalizado
  sin offsets (`ImageResource::needs_native`); los demás bindings muestreados leen el proxy directamente
  (toggle 1 << 48): GPU ocupada 5.02 -> 4.86 ms, capturas idénticas. Cambia el layout de `Info`:
  versión de meta de shader 7, versión de clave de pipeline 5 (las cachés se reconstruyen una vez).
- `TSUKI_SCENE_DEBUG=<archivo>`: al tocar el archivo se imprimen los pases de escena del siguiente fotograma con el motivo por el que
  cada uno conserva el tamaño nativo. Lo que queda nativo antes del upscaler: los pases a media resolución
  (960x540), que `SceneTargets::Eligible` no maneja.

Siguiente punto de GPU (importa sobre todo en la Steam Deck): el compute del guest `3d5ebf4e` es un memcpy de dwords que
copia memoria de render targets (el buffer de profundidad 1080p, 12 MB, muestreado después como R32F; un
target del G-buffer; dos targets de 960x540): 8 dispatches, y cada uno requiere descargar la imagen al
buffer, resolver el proxy y volver a subir la imagen de destino. Reconocer las copias
cuya fuente es una imagen y convertirlas en copias de imagen (o una copia del tamaño del proxy) eliminaría
la mayor parte. Omitir los dispatches deja la escena en negro, así que las copias son necesarias.

### Corregido: corrupción del heap del guest con el pipeline de draws (2026-10-01)

Causa: el GPU idle (`IrqC GpuIdle`, que libera `sceGnmSubmitDone`) se señalizaba una vez que la etapa A había
decodificado todos los envíos, mientras los fences end-of-pipe diferidos al hilo de grabación Vulkan
(RecorderFences) seguían pendientes; el guest liberaba los objetos que contenían esas etiquetas mientras
otro de sus hilos aún las actualizaba. Ahora la etapa A vacía el pipe y espera las señales
diferidas antes del GPU idle (y antes de WriteData/ReleaseMem de la cola compute). Encontrado con
`TSUKI_WRITE_LOG=2` (objetivos de fences registrados en el momento de la decodificación, fuera de la ruta de la carrera). Las notas de abajo
son la investigación.

#### Notas de investigación

Tras ~2-15 min en el nivel (cámara girando, nadie moviéndose) el guest falla en el offset del guest
`0x263b8e7`: un pop de la free-list en un allocator del guest lee el siguiente puntero `0x0000005300000000`
de un bloque liberado, así que algo escribió en memoria que el juego ya había liberado. Ejecuciones de resistencia de
15-20 min: con el pipeline de draws 3 de 4 se cayeron (a los 863, 844 s y una antes), con
`TSUKI_DRAW_PIPE=0` 0 de 2. Con `TSUKI_WRITE_LOG=1` (descargas más lentas) una ejecución sobrevivió 20 min.

Descartado / hecho hasta ahora:
- escrituras visibles para el guest que adelantan a los fences diferidos (ahora ordenadas: `WaitDeferredSignals`, toggle
  1 << 49) — el fallo persiste;
- los cambios de proxies de escena y de sets de texturas: el fallo ocurrió también antes de ellos.

Candidatos: escrituras tardías en memoria del guest que el pipeline retrasa aún más — descargas asíncronas
de imágenes (`TextureCache::DownloadImageMemory`, diferidas hasta que la GPU termina, imagen completa),
descargas de buffers en page faults, y el manejo de faults cuando el hilo de comandos de GPU (etapa A), que
ahora lee memoria del guest para el constant ring, no se trata como un hilo del lado de la GPU
(`IsGpuSideThreadId` acepta solo la etapa B). Siguiente: una ejecución con `TSUKI_WRITE_LOG=1` que falle
imprime qué escritura registrada cayó cerca del bloque corrupto; bisecar los toggles del pipeline
(38 tareas, 39 esperas de fences pendientes, 41 fences del recorder, 42 escrituras de memoria, 36 constant ring).
