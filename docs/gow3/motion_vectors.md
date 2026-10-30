# Vectores de movimiento: mejoras tras el vídeo del 2026-09-26

> **Estado (2026-10-30):** el upscaler temporal y los vectores de movimiento no se pueden encender
> desde la Spec 7 §10 (`docs/upscaler.md`); las opciones y variables que se nombran aquí ya no existen.

Material de origen: `video_2026-09-26_04-19-53.mp4`, FSR 3.1 Performance,
render 960×540 → salida 1920×1080, y `out/dev/t71.log`.
Esto describe los cambios actuales del árbol de trabajo, no una confirmación de la calidad en el juego.

El pase común para todos los draws del G-buffer resultó demasiado costoso: Performance funcionaba a unos
49 FPS con él y 77–78 FPS sin él. Ahora los vectores de objeto están activados por defecto solo
para los shaders con buffer de pose/huesos (tamaño 640–16384 bytes, salvo el buffer común de la escena
de 864 bytes; paso de 16 bytes). En una ejecución de prueba con un umbral más estrecho de 1024 bytes
esto redujo el número de draws procesados a unos 140 por fotograma, eliminó el desbordamiento de la
historia y dio unos 81–87 FPS. El resto de los draws conservan los vectores de cámara, que son baratos.
`GOW3_OBJECT_MOTION=0` desactiva toda la función, `GOW3_OBJECT_MOTION_ALL=1` devuelve el pase común
para A/B. El canal alfa del objetivo auxiliar guarda la profundidad: si un draw normal posterior tapó
el objeto, su vector no se usa. La calidad del personaje y del arma en movimiento tras la
selección aún debe comprobarse visualmente.

## Qué se descubrió

- Al final del log original, a los 600 fotogramas: 257400 draws del G-buffer, 256200 declarados
  en movimiento, y solo 30000 recibieron posiciones anteriores (~11,7 % de todos los draws).
  Es cobertura de dibujados, no porcentaje de píxeles. El contador antiguo no distinguía
  la causa de cada omisión.
- Para la historia se reservaba el tamaño de todo el vertex buffer multiplicado por el número de
  instancias **junto con firstInstance**. Para los submeshes de buffers compartidos son posiciones
  de más; el presupuesto era de 2097152 posiciones por fotograma.
- La historia no tenía una barrera separada entre escrituras/lecturas de los vertex shaders;
  la reutilización del anillo de parámetros del host no esperaba a la GPU.
- Las escrituras desactivadas de todos los vértices se dirigían a un único elemento scratch.
- `CameraMotion::OnDisplayPass` reiniciaba Depth() antes de calcular las jitter phases:
  Performance recibía 8 fases en lugar de 32. Además, la reconstrucción de la posición a partir de la
  profundidad no tenía en cuenta el desplazamiento del viewport.

## Cambios

- El rango de la historia se determina con el mín/máx de los índices reales, teniendo en cuenta baseVertex
  y primitive restart. FirstInstance se resta en el shader, en lugar de consumir memoria.
- Se guardan todos los draws directos del G-buffer que encajan, desde su primera aparición.
  Se eliminó la heurística de cambio de constantes: consideraba en movimiento a casi todos los draws
  y se saltaba el inicio del movimiento. La lectura solo es posible si coincide con el fotograma anterior.
- La clave incluye todas las descripciones de vertex streams, el shader, la dirección y el hash del contenido
  de los índices, el rango de vértices, las instancias y el número entre draws idénticos.
  Un cambio de topología o un fotograma omitido reinicia la historia correspondiente.
- El presupuesto es de 4194304 posiciones/fotograma; los dos arrays juntos ocupan 128 MiB en lugar de 64 MiB.
  Si falta memoria, se quedan los vectores de cámara. El log muestra por separado los guardados,
  las coincidencias, la falta de historia, la falta de memoria y los rangos no válidos.
- Se añadieron vertex-to-vertex barriers, espera de la GPU antes de reutilizar los
  parámetros del host, memoria del host coherent y un seguimiento independiente del layout de la imagen de movimiento.
  Releer la imagen no borra la marca de presencia de vectores.
- Los accesos desactivados se evitan con una bifurcación. Para las invocaciones repetidas de un mismo
  vértice indexado se usan escrituras atómicas de los componentes de la posición.
  No se usan las posiciones detrás de la cámara ni los vectores no numéricos/infinitos.
- Performance recibe 32 fases de jitter tras crear el FSR context. Los vectores de cámara
  se reconstruyen a partir de coordenadas unjittered; los vectores de objeto ya se obtenían
  de las clip positions antes del desplazamiento del viewport.

## Verificaciones

Compilación y tests de CPU:

```bash
cd native_probe
nix-shell shell.nix --run 'bash build.sh && ninja -C out/gpu motion-history-test motion-shader-test && out/gpu/motion-history-test && out/gpu/motion-shader-test out/motion-shaders'
```

`test_motion_history.cpp` comprueba los rangos u16/u32, restart, baseVertex negativo,
firstInstance, draws repetidos, cambio de topología, fotograma omitido, agotamiento del presupuesto,
desbordamiento y el número de fases. También se pasó una ejecución con AddressSanitizer/UBSan.

`test_motion_shaders.cpp` usa el verdadero backend SPIR-V y crea VS/PS con
vectores y sin ellos. Los cuatro resultados pasaron `spirv-val --target-env vulkan1.3`.
Para repetirlo hace falta `spirv-val` de SPIRV-Tools:

```bash
for shader in out/motion-shaders/*.spv; do
    spirv-val --target-env vulkan1.3 "$shader" || exit
done
```

Logs de compilación/tests: `out/dev/motion-validation.log`. En el entorno de verificación no hay
acceso a `/dev/dri`: la calidad en la RX 7800 XT, los FPS y el porcentaje de cobertura tras los cambios
aún no se han comprobado. Una validación SPIR-V correcta no demuestra que la imagen sea correcta.

## Próxima ejecución en el juego

Desde `game_files`:

```bash
GOW3_FRAME_STATS=1 GOW3_TOGGLE_FILE=out/dev/motion-toggles bash native_probe/run.sh 2>&1 | tee native_probe/out/dev/motion-run.log
```

El bit 29 del archivo toggle temporal desactiva solo la escritura de la historia. Para comparar
correctamente el rendimiento hay que ejecutar el juego con `GOW3_OBJECT_MOTION=0` y sin él:
la elección del pipeline y del objetivo adicional se fija al iniciar el proceso.
Tras el cambio, dar a FSR unos fotogramas para actualizar la historia.
Comparar una misma escena: personaje/arma al correr, giro de cámara, combate, cámara inmóvil.
Tras ~600 fotogramas, revisar `Object motion:`: `with history`, `capacity skips`, `unmatched`.

Limitaciones restantes: las mallas idénticas se emparejan por el orden de los draws, que
puede cambiar con el descarte de objetos; el cambio de direcciones de los vertex buffers dinámicos
pierde la coincidencia. Los indirect draws, los tessellation/geometry shaders, MSAA y los pases
fuera del G-buffer siguen con vectores de cámara. La transparencia/partículas, la UI borrosa y el traslado
del escalado al nivel del renderer no se resuelven con este cambio. El coste del escaneo de
índices y de las escrituras atómicas debe medirse en una ejecución real.

## 29 de septiembre: arma y esqueletos pequeños (ghosting de la sierra giratoria)

Vídeo `video_2026-09-29_23-08-26.mp4`: el personaje está quieto, el disco de la sierra gira y deja
una estela. El log `GOW3_MOTION_SELECT_LOG=1` mostró la causa: el segundo buffer del vertex shader es
una paleta de huesos de 48 bytes cada uno (3x4), y en armas/utilería mide 96–384 bytes (2–8 huesos).
La regla «desde 640 bytes» no incluía esos dibujados, el disco recibía solo vectores de cámara,
es decir, para FSR estaba inmóvil.

Cambios:

- `Motion::ClassifyBuffer` (`motion_history.h`): 864 — escena, ≥640 — esqueleto de personaje,
  96..639 múltiplo de 48 — esqueleto pequeño. Los pipelines con cualquier esqueleto reciben la variante
  con vectores.
- Hay unos mil esqueletos pequeños por fotograma, la mayoría partes estáticas del mapa.
  Por eso se «filtran»: el hash de la paleta se compara con el del fotograma anterior mediante una clave
  barata (`History::Moving`) antes de escanear los índices. Si coincide, el vector de cámara es exacto
  y no se escribe nada. Si cambió, se guardan las posiciones y desde el siguiente fotograma hay vector
  de objeto. Un hash con el buffer de 416 bytes no sirve: cambia junto con la cámara en casi todos
  (comprobado: desbordamiento de la historia y caída de FPS hasta ~40).
- Menú: «Mostrar vectores de movimiento (depuración)». Rojo/verde — |x|/|y|, azul —
  el píxel recibió vector de objeto. `GOW3_MOTION_SELECT_LOG=1` imprime los shaders del G-buffer
  con los tamaños de buffer y la decisión.

Medición `out/motion-gated.log`: ~225 dibujados guardados y ~600 «inmóviles» por fotograma,
sin desbordamiento, 58–80 FPS en escenas de juego. Aún debe confirmarse visualmente la desaparición de la
estela en la sierra.
