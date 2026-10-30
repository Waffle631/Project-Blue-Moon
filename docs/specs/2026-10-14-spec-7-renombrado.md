# Spec 7 — Renombrar `gow3` a `tsuki`

Fecha: 2026-10-14, ampliada el 2026-10-25 (§9), el 2026-10-28 (§10) y el 2026-10-30 (§11). Estado: **borrador para aprobación**; tareas 0 (rama
`tsuki/spec-7` y línea base, §4), 1 (`tools/check_rename.sh`, §6), 2 y 3 (§10 completa), 4 (inventario, prosa y
§11) y 5 (variables `TSUKI_*`, §4 paso 2) del plan hechas el 2026-10-30. Roadmap: `docs/tsuki/ROADMAP.md`, sección "Spec 7".
Depende de: Spec 6 (el recorrido grabado y las capturas son el oráculo del renombrado) y Spec 1b. El renombrado va
al final. Antes de que la Spec 6 se cerrara sin la 1b (2026-10-28), esta dependencia llegaba a través de la 6.

Marcas: **[V]** comprobado el 2026-10-14; **[D]** decisión del usuario.

## 1. Objetivo

Pasar todos los nombres estáticos `gow3` / `GOW3` del proyecto al prefijo `tsuki` / `TSUKI`, por capas,
sin alias ni lectura de los nombres antiguos, sin cambiar el comportamiento. Va al final porque es
mecánico, no reduce riesgo técnico y mezclaría sus errores con los de las specs nuevas si se hiciera antes.

## 2. Inventario (hechos comprobados)

- **[V]** `git grep -i gow3`: 2551 líneas en 290 archivos. `git ls-files | grep -i gow3`: 40 archivos
  con `gow3` en el nombre. 194 variables `GOW3_*` distintas.
- **[V]** No hay nombres de variable construidos dinámicamente en C, C++, Python o shell
  (patrones `GOW3_%s`, `"GOW3_"+…`, `startswith('GOW3')`, `GOW3_$…`): sin resultados el
  2026-10-14. Se repite al empezar.
- **[V]** Repetido el 2026-10-30 en `c75138c` (tarea 0): 2913 líneas en 308 archivos, 185 variables `GOW3_*`
  distintas y 38 archivos con `gow3` en el nombre (la Spec 1a borró `gpu/shim/gow3_{actors,orbs}.*` y
  `tools/gow3_frame_map.py`; después llegaron `gpu/shim/gow3_mouse.h` y `gow3_screenshot.*`). Los patrones dinámicos de arriba, ampliados a literales `"GOW3_"`/`'GOW3'` sueltos y a
  concatenación de macros, solo aparecen en esta spec y en el roadmap, como texto.
- **[V]** `GOW3_*` aparece también como **variables de compilación**, no solo de ejecución:
  `GOW3_LTO`, `GOW3_PGO`, `GOW3_PGO_DIR`, `GOW3_IN_NIX_SHELL` en `build.sh` y `gpu/CMakeLists.txt`
  (`-DGOW3_LTO`, `-DGOW3_PGO_DIR`); `docs/ENTORNO.md` las documenta.
- **[V]** Archivos con `gow3` en el nombre (además de los que ya habrá borrado la Spec 1a):
  `gpu/gow3gpu.h`, `gpu/shim/gow3gpu.cpp`, `gpu/shim/gow3_{copy,graphics,overlay,perf_stats,png,settings,threads,toggles,win_compat,write_log}.*`,
  `gpu/shadps4/video_core/amdgpu/gow3_msaa*.h` (según la Spec 1b), `gpu/dlss_bridge/gow3_dlss_bridge.*`,
  `launcher/gow3*`, `packaging/gow3-entry.c`, `packaging/linux/gow3.desktop`,
  `packaging/windows/gow3-{probe,gpu-capabilities}.*`, `tools/gow3_frame_map.py` (se borra en la Spec 1a),
  `docs/gow3/ROADMAP.md`.
- **[V]** El parche de FSR (`gpu/patches/fsr-vulkan/0001-FSR-4-v07-provider-GOW3_FSR4_PROFILE-pass-timings-GOW3_F.patch`)
  lleva variables `GOW3_*` en su nombre y en su contenido. Se aplica al submódulo `gpu/third_party/fsr-vulkan`
  (`build.sh` lo hace), que no se modifica. **[D] 2026-10-30:** ese parche solo añade `GOW3_FSR4_PROFILE` y
  `GOW3_FSR4_STATS`, que miden un FSR 4 que con este juego ya no se ejecuta (§10). **Se borra en vez de
  renombrarse** (paso 6 del §4). Los parches `0002` y `0003` se quedan: añaden los `#include` que libc++/MinGW
  necesitan para compilar en Windows el SDK FFX 1.1.4, que `libgow3gpu` sigue enlazando (`ffx-vulkan::portable`,
  `gpu/CMakeLists.txt:181`) mientras se conserve el upscaler temporal. Nombran `gow3` en los siete comentarios
  `// gow3:` de esos `#include`, que pasan a `tsuki:`.
- **[V]** `pgo/` no existe en el árbol: renombrar funciones no invalida ningún perfil de PGO hoy. Si se
  añade uno antes del renombrado, hay que regenerarlo.

## 3. Tabla de renombrado

(Primera propuesta; se revisa antes de ejecutar.)

| Hoy | Pasa a |
|---|---|
| Variables de entorno y de compilación `GOW3_*` (194) | `TSUKI_*` |
| Binarios `gow3-probe`, `gow3-gpu-capabilities` (y `.exe`), pruebas `gow3-*` | `tsuki-probe`, `tsuki-gpu-capabilities`, … |
| `libgow3gpu.so`, `gow3gpu.h`, `gow3gpu_resolve`, `gow3gpu_register_*`, opción CMake `gow3gpu` | `libtsukigpu.so`, `tsukigpu.h`, `tsukigpu_*` |
| `gpu/shim/gow3_*.cpp/.h`, `gpu/dlss_bridge/gow3_dlss_bridge.*` | `tsuki_*` |
| `Gow3Settings`, `Gow3Overlay`, `Gow3Graphics`, `Gow3Png`, `gow3_longjmp`, … | `TsukiSettings`, `TsukiOverlay`, … / `tsuki_*` |
| `gow3.ini`, `%APPDATA%\gow3-launcher`, `launcher/gow3.png/.ico`, `God of War III.exe` | `tsuki.ini`, `%APPDATA%\tsuki-launcher`, … |
| Marca `gow3:` en los cambios de shadPS4 | `tsuki:` |
| `GOW3_RESTART_COMMAND` y similares del "Apply and restart" | `TSUKI_*` |
| Nombres del proyecto: `gow3_pc`, `gow3-windows`, `gow3_launcher_win`, `.gitignore`, `shell.nix`, empaquetado | `tsuki_pc`, … |

Configuraciones y saves antiguos no se migran (no hay usuarios de los nombres viejos).

`launcher/` y `packaging/`: se renombran con el resto (solo nombres, para que `tools/check_rename.sh`
pase), pero su contenido (funciones, iconos, textos de identidad) se deja para el final, después de la
Alpha, como se decidió el 2026-10-14. **[D]** 2026-10-30: los textos que nombran God of War III sí se cambian
en esta spec, y el actualizador del launcher se borra (§11); el icono, la fuente y la paleta siguen para la Alpha.

## 4. Orden de trabajo (por capas; compilar y pasar tests tras cada una)

Antes de empezar: una captura de la línea base de las pruebas (`bash build.sh --test`, Python), el
log del recorrido grabado (`user/last_run.log`) y las capturas de píxeles (`out/baseline/`). Son el
oráculo de que nada cambió.

Línea base tomada el 2026-10-30 en `c75138c` (`out/baseline/7/`, sin versionar): `bash build.sh --test` verde
(`tests-c.log`); Python, 273 tests, 35 omitidos y 4 fallos esperados (`tests-py.log`); `--check-imports`, las 3
trampas de libkernel (`imports.log`); el recorrido grabado (`antes/`, `antes.log`) da idénticos los 6 fotogramas
`exact`/`mask` frente a `out/baseline/6a/`, sin `STOP:` ni `Fault:`, con `Settings: out/route/gow3.ini`. Los 2
fotogramas `eye` (2700, 2890: el menú de carga sobre `titlebg1.mp4`) quedan a ojo.

1. **Inventario.** `git grep -ohiE '[a-z0-9_.-]*gow3[a-z0-9_.-]*' | sort -u` y `git ls-files | grep -i gow3`,
   más strings, `CMakeLists.txt`, `build.sh`, `run.py`, `run.sh`, `shell.nix`, `.gitignore`, empaquetado y
   documentación. Resultado: una lista cerrada de nombres antiguos → nuevos (`tools/rename_map.txt`,
   solo local, no versionado).
2. **Variables de entorno y de compilación** `GOW3_*` → `TSUKI_*`, en C, C++, Python, shell, CMake,
   `docs/ENTORNO.md` y `CLAUDE.md` a la vez (no los README del jugador, §11 punto 6), **sin alias** y sin leer los nombres viejos.
   Una variable que quede a medias haría que el juego ignorara la opción sin avisar.
3. **Archivos y directorios** con `git mv` (conserva la historia); después `#include`, listas de
   fuentes de CMake y `build.sh`.
4. **Símbolos**, clases, binarios, la biblioteca de GPU, `.ini`, rutas de usuario y la marca `gow3:`
   → `tsuki:` en el código de shadPS4 (es un cambio de comentario, no de lógica).
5. **Documentación.** `CLAUDE.md`, este roadmap, los README de desarrollo y la memoria del proyecto (los
   README del jugador esperan a la Alpha, §11). El historial
   (`docs/gow3/`) no se renombra.
6. **Parche de FSR.** Borrar `0001-…GOW3_FSR4_PROFILE….patch` (**[D]** 2026-10-30, §2) y quitarlo de su
   `README.md`; en `0002` y `0003`, los comentarios `gow3:` pasan a `tsuki:`. `build.sh` aplica los parches y
   nunca los quita, así que antes de tocarlos se revierten los tres del árbol de trabajo del submódulo con
   `git apply --reverse`. Hasta este paso, los pasos 2 a 4 no tocan `gpu/patches/`: cambiar un parche con su
   versión vieja aplicada hace fallar `git apply` en `build.sh`. Comprobar que `build.sh` aplica `0002` y `0003`
   al submódulo limpio.
7. **Métricas del overlay en Linux** (§9), con el renombrado ya comprobado por el §7.

**Hecho, paso 2 (tarea 5 del plan, 2026-10-30):**
- Cambio mecánico de `GOW3_` a `TSUKI_` en 143 archivos versionados, sin alias. Quedan fuera los de la lista de
  excepciones del §6, `gpu/patches/` (paso 6) y los README del jugador. Los dos nombres de CMake sin guion bajo
  (`GOW3GPU_KIND` y la guarda `GOW3GPU_H`) se cambiaron a mano. El mismo cambio se aplicó a los 19 scripts de `out/`
  que usaban esas variables (oráculos, recorridos y análisis de specs anteriores).
- Solo quedan `GOW3_` en las cuatro líneas marcadas de los tests nuevos y en el parche `0001`, que se borra en el
  paso 6. El nombre `X-GOW3-Integration` del `.desktop` del launcher no es una variable y cambia en el paso 4.
- **Corrección del inventario.** La afirmación de que `GOW3_FSR4_PROFILE` solo se leía en el parche `0001` era falsa:
  `fsr411/fsr411.cpp` también la lee, como perfilador propio del FSR 4.1.1 conservado. Se aplicó la regla de los
  diagnósticos del código que se queda (§10): pasó a `TSUKI_FSR4_PROFILE`. La del parche se borra con él.
- Tests de que la variable vieja no tiene efecto, rojos antes del cambio y verdes después:
  - `test_services.c`: `GOW3_LANGUAGE=8` deja el idioma en 1, y `TSUKI_LANGUAGE=8` lo cambia a 8;
  - `test_graphics_settings.cpp`: `GOW3_FSR1=1` no enciende `fsr1`, y `TSUKI_FSR1=1` sí.
- Se borró `out/gpu`, porque la caché de CMake guardaba `GOW3_LTO` y `GOW3_PGO`, y se recompiló desde cero.
  - `build.sh --test` sale verde (12 PASS) y `graphics-settings-test` pasa.
  - Python: 284 tests OK, 35 omitidos, 4 fallos esperados.
  - El recorrido grabado (`out/route/spec7-t5/`) coincide con `out/baseline/6a/`: 6 fotogramas idénticos y los
    dos del menú de carga, revisados a ojo. El log muestra que el juego leyó `out/route/gow3.ini` por `TSUKI_CONFIG`.
- `README.md` (5 menciones) y `README.pt-BR.md` (3) siguen nombrando las variables `GOW3_*`, que ya no funcionan,
  hasta que se reescriban para la Alpha (§11).

## 5. Requisitos

- Ninguna variable `GOW3_*` debe seguir leyéndose.
- El comportamiento no cambia: el recorrido grabado y las capturas dan el mismo resultado.
- Los submódulos `gpu/third_party/*` no se tocan.

## 6. Comprobación

`tools/check_rename.sh` (nuevo): ejecuta `git grep -i gow3` y falla si encuentra algo fuera de estas
**excepciones explícitas**:

1. `docs/gow3/` (histórico de GoW3);
2. `docs/plans/` (diagnósticos históricos de GoW3; **excluido**, decidido 2026-10-14);
3. `docs/specs/` (las specs cuentan lo que se hizo con los nombres de entonces; **[D]** 2026-10-28);
4. el nombre de la **rama** `gow3` cuando aparece como `rama \`gow3\`` o `branch gow3`, y la ruta `docs/gow3/`
   (también como enlace relativo `../gow3/`), en `CLAUDE.md` y en el roadmap;
5. los submódulos y `pgo/`;
6. el propio script y su test, y las líneas marcadas con `rename-check: ok` (el test del §7 que comprueba que
   una variable `GOW3_*` ya no tiene efecto tiene que nombrarla);
7. **[D]** 2026-10-30 (§11): `README.md`, `README.pt-BR.md`, `packaging/windows/README-Windows.txt` y las líneas
   `Copyright … gow3 contributors`. Desde entonces busca también `god of war`, `CUSA01623` y `CUSA01715`;
8. desde el 2026-10-30, la ruta `docs/gow3/` en cualquier archivo: el usuario movió allí `docs/plans/` y los tres
   documentos del upscaler (§11), y el código los cita. El `sed` de las tareas 5 a 8 no debe tocarla.

El script se ejecuta en `bash build.sh --test` para que no regrese ningún nombre antiguo.

Hecho el 2026-10-30 (tarea 1 del plan): `tools/check_rename.sh [repo_dir]` y `tests/test_check_rename.py` (8 casos
sobre un repositorio temporal). Sale 0 sin salida si no queda nada, 1 con cada resto (`ruta:línea:texto` y luego cada
ruta con `gow3`), 2 si `repo_dir` no es un repositorio. Busca sin distinguir mayúsculas y solo en archivos versionados.
En `CLAUDE.md` y el roadmap quita las frases permitidas del punto 4 antes de buscar otra vez, así que otro `gow3` en la
misma línea sigue contando. Punto de partida en `65b6c13`: 2504 restos, 2467 líneas en 295 archivos y 37 rutas. Aún no
se engancha a `build.sh --test`, porque quedaría en rojo hasta el final; se engancha en la tarea 9 del plan. «the `gow3`
branch» (`CLAUDE.md:25`) no es una de las formas del punto 4: hay que escribirla como «branch `gow3`» en el paso 5.

## 7. Pruebas y criterios de aceptación

- [ ] `bash build.sh --test` y `python3 -m unittest discover -s tests` pasan, con la línea base como
  referencia.
- [ ] `tools/check_rename.sh` termina con éxito.
- [ ] `bash build.sh` produce `out/tsuki-probe` y `out/gpu/libtsukigpu.so`, y
  `TSUKI_GAME_DIR=Tsukihime/CUSA47999 python3 run.py` repite el recorrido grabado con el mismo log (sin
  `STOP:` ni `Fault:`).
- [ ] Las capturas de píxeles de la línea base coinciden antes y después (`tools/compare_captures.sh`).
- [ ] La ventana y el menú Insert muestran el título leído del `param.sfo`.
- [x] Un test comprueba que una variable `GOW3_*` ya no tiene efecto (por ejemplo, `GOW3_LANGUAGE=8`
  no cambia el idioma, `TSUKI_LANGUAGE=8` sí).

## 8. Riesgos

- Un renombrado a medias deja opciones ignoradas sin aviso (riesgo "Medio" del roadmap). Mitigación:
  capas pequeñas, `check_rename.sh`, el test de variables antiguas y el recorrido como oráculo.
- Windows: `packaging/windows/*` y el lanzador de Tk se renombran sin poder probarse en esta máquina;
  se listan como pendientes de verificar cuando se aborde Windows.
- Los hashes de PGO (si algún día se generan) y las cachés de pipelines/shaders con rutas o
  nombres antiguos quedan obsoletas; se regeneran.

## 9. Añadido: CPU, RAM, GPU y VRAM del overlay de rendimiento en Linux

**[D]** 2026-10-25: el usuario lo añade a esta spec. Lo vio al probar la Tarea 6 de la Spec 6: en el overlay
de rendimiento, CPU, RAM y VRAM salen "n/a". Es el único cambio de comportamiento de la spec. Va **después**
del renombrado ya comprobado (paso 7 del §4), en su propio commit, para que el oráculo de "nada cambió" (§5)
siga valiendo para el renombrado. Para entonces el archivo se llamará `tsuki_perf_stats.*`.

Hechos (**[V]** comprobados el 2026-10-25):

- `gpu/shim/gow3_perf_stats.cpp:121-131`: en Linux, `Run()` solo rellena `gpu_name`. CPU, RAM, VRAM y GPU %
  se quedan negativos, que `Format` muestra como "n/a". La medición solo existe en la rama `_WIN32` (desde
  `d9358cd`).
- La VRAM no depende del sistema operativo: `Instance::CanReportMemoryUsage()`, `GetDeviceMemoryUsage()` y
  `GetDeviceMemoryBudgetNow()` (VK_EXT_memory_budget, `vk_instance.cpp:932-957`). Solo está metida dentro del
  `#ifdef _WIN32`.
- `CpuPercent` (`gow3_perf_stats.h`) ya es común y tiene test (`tests/test_perf_overlay.cpp`, objetivo
  `perf-overlay-test`).
- El equipo tiene dos GPU AMD: `card0` (PCI `0000:12:00.0`, la integrada del Ryzen) y `card1` (`0000:03:00.0`,
  la RX 6650 XT). `gpu_busy_percent` existe en las dos, pero mide la tarjeta entera con todos los procesos.
- El kernel (7.2) publica datos por proceso en `/proc/<pid>/fdinfo/<fd>` de cada descriptor DRM abierto:
  `drm-pdev`, `drm-client-id`, `drm-engine-gfx` (ns) y `drm-resident-vram`. Equivale a los contadores
  "GPU Engine" por proceso que usa la rama de Windows.

Diseño propuesto:

- **CPU:** tiempo de CPU del proceso (`CLOCK_PROCESS_CPUTIME_ID`) sobre tiempo de pared (`CLOCK_MONOTONIC`),
  pasado por `CpuPercent` con el número de núcleos, como en Windows: 100 % son todos los núcleos.
- **RAM:**
  - Usada: el residente de `/proc/self/statm` (segunda columna × `sysconf(_SC_PAGESIZE)`), el equivalente
    del `WorkingSetSize`. Incluye las páginas tocadas del pool de memoria directa (memfd), igual que en Windows
    cuenta la sección.
  - Total: `_SC_PHYS_PAGES` × página.
- **VRAM:** el bloque de `CanReportMemoryUsage` pasa a código común a los dos sistemas.
- **GPU %:**
  - Fórmula: el delta de `drm-engine-gfx` entre dos muestras, dividido por el tiempo de pared.
  - Clientes: se suman los descriptores DRM del proceso, sin repetir un `drm-client-id`. RADV puede abrir
    también la integrada; su tiempo es casi cero y no estorba.
  - Motor: solo `gfx`, como Windows, que solo cuenta `engtype_3D`.
  - Sin esas claves (otro driver o un kernel viejo) sale "n/a".
- **Pruebas:** las funciones que interpretan texto (la línea de `statm` y un `fdinfo`) son puras, viven en
  `gow3_perf_stats.h` y se prueban en `tests/test_perf_overlay.cpp` con textos fijos. Leer `/proc` y el hilo
  de muestreo no tienen test, porque dependen de la máquina.

Criterios de aceptación:

- [ ] `perf-overlay-test` cubre `statm`, un `fdinfo` de amdgpu con dos clientes y un `fdinfo` sin
  `drm-engine-gfx` (resultado: "n/a").
- [ ] En el juego, con CPU, RAM, GPU y VRAM activados en el overlay, las cuatro muestran cifras (RADV, este
  equipo). CPU y RAM se comparan a mano con `top`, y VRAM con `mem_info_vram_used` de `card1`.
- [ ] Las capturas de la línea base no cambian: CPU, RAM, GPU y VRAM no están en `DefaultItems`.
- [ ] Windows: solo cambia que la VRAM pasa a código común. Queda pendiente de compilar en MSYS2 cuando se
  aborde Windows (como el resto del §8).

## 10. Añadido: quitar las opciones del upscaler temporal, que ya no hacen nada

**[D]** 2026-10-28: el usuario lo añade a esta spec, al cerrar la tarea 3 de la Spec 1b. El código del upscaler
temporal **se queda**. Lo que se quita es todo lo que permite encenderlo, porque encendido ya no hace nada y una
opción que no hace nada engaña a quien la toca.

Hechos (**[V]** comprobados el 2026-10-28, en `c6f5fd4`):

- La Spec 1b (tarea 3) borró `gbuffer_draw` y la lectura de la constante de 864 bytes, que eran las únicas entradas
  de `CameraMotion` (`OnGBufferPass`, `OnConstants`). Sin ellas `camera_motion.Ready()` nunca es verdadero y
  `camera_motion->Depth()` siempre es nulo.
- El upscaler solo escala desde `TemporalUpscaler::OnDispatch`, que pide `camera_motion.Ready()` (y un compute shader,
  que Tsukihime no lanza), o desde el color de escena, que pide `Depth()`. FSR 3.1, FSR 4, FSR 4.1.1, DLSS y TAA no
  pueden ejecutarse con este juego, tampoco con `GOW3_EXPERIMENTAL_UPSCALER=1`.
- **No es del upscaler temporal y se queda:** FSR 1 y el afilado RCAS (`fsr1`, `rcas`, `rcas_strength`, menú Insert y
  `GOW3_FSR1`). Son espaciales, al presentar, y funcionan.

Lo que hay que quitar (punto de partida; se rehace el inventario al empezar):

- `run.py:211-214`: el interruptor `GOW3_EXPERIMENTAL_UPSCALER`. Sin él, `GOW3_UPSCALER=none` deja de hacer falta: el
  upscaler queda apagado siempre.
- Launcher (`launcher/gow3_launcher_win.py`): el bloque comentado de «Experimental»/«Upscaling» de `build_graphics`
  (~`:1177`), el envío de `experimental_upscaler` (~`:703`), las claves `upscaler`, `preset`, `output_res`,
  `live_resolution` y `object_motion` de `INI_DEFAULTS` si nada vivo las usa, y sus textos en `gow3_lang.py`. Los
  tests del launcher que las cubren (`test_launcher_upscale.py`, `test_upscaler_assets.py`, partes de
  `test_launcher_save.py`) se ajustan o se borran con ellas.
- `gow3.ini` y `Gow3Settings`: dejar de leer y escribir las claves del upscaler temporal (`upscaler`, `preset`,
  `jitter`, `reactive*`, `object_motion`, `debug_view`, `output_res`), con el valor fijo «apagado». El elemento
  «Upscaler» del overlay de rendimiento sale de la lista.
- Variables de entorno que lo encienden o lo configuran: `GOW3_UPSCALER`, `GOW3_UPSCALE_PRESET`,
  `GOW3_UPSCALE_BEFORE_CS`, `GOW3_UI_TRIGGER_VS`, `GOW3_PRESET_FILE`, `GOW3_JITTER`, `GOW3_OBJECT_MOTION*`,
  `GOW3_DEBUG_MOTION`, `GOW3_TAA_DIAGNOSTICS`, `GOW3_DLSS*`, `GOW3_FSR4_*`, `GOW3_FSR411_DIR`. `GOW3_RENDER_RES` se mira
  aparte, porque también la usa el escalado de escena. `GOW3_FSR4_PROFILE` y `GOW3_FSR4_STATS` solo se leen dentro
  del parche `0001` de FSR-Vulkan: se van con él en el paso 6 del §4 (**[D]** 2026-10-30, §2), nunca se renombran.
  (*Corregido en la tarea 5: `fsr411/fsr411.cpp` también lee `GOW3_FSR4_PROFILE`. Es un diagnóstico del código
  conservado y pasó a `TSUKI_FSR4_PROFILE`; ver §4.*)
- El ítem «Upscaler» del overlay sale de `DefaultItems` **y** del valor inicial `overlay_items{7}` de
  `Gow3Settings` (`gow3_settings.h:44`), que repite a mano los bits 0-2. Si no, el bit 2 sigue encendido por
  defecto (**[D]** 2026-10-30).
- Empaquetado y documentación: los assets de FSR 4 / 4.1.1 (`packaging/`), y `docs/upscaler.md`, que pasa a decir
  que el upscaler está aparcado y sin forma de encenderlo.

Orden: **antes** del renombrado (paso 1 del §4), en su propio commit, para no renombrar a `TSUKI_*` variables que se
van a quitar. El oráculo no cambia: `run.py` ya fuerza `GOW3_UPSCALER=none`, así que las capturas y el recorrido
grabado tienen que salir iguales.

Fuera de esta spec: borrar el código del upscaler temporal, `CameraMotion`, `ObjectMotion`, los shaders de movimiento
y el submódulo `fsr-vulkan`. El usuario decidió conservarlo (2026-10-28).

**Hecho, núcleo (tarea 2 del plan, 2026-10-30):** el juego ya no tiene forma de encender el upscaler temporal.

- `run.py` ya no lee `GOW3_EXPERIMENTAL_UPSCALER` ni pone `GOW3_UPSCALER=none` ni `GOW3_FSR411_DIR`. `run.sh` ya no
  compila los pases optimizados de FSR 4 (`tools/fsr4_optimize.sh`) en cada arranque.
- `GOW3_UPSCALER=none` no solo apagaba el ajuste: el renderer lo leía para dejar `TemporalUpscaler::enabled` y
  `CameraMotion::for_upscaler` en falso, y el rasterizer mira los dos. Quitar la variable sin más los habría encendido.
  Ahora valen la constante `Gow3Settings::TemporalUpscalerAvailable` (`false`). Con ella, `Dlss::Get()` ya no carga
  las DLL (sustituye a `GOW3_DLSS=0`).
- `Gow3Settings` ni lee ni escribe las claves del upscaler temporal. El inventario de la tarea añadió `sharpen`,
  `sharpness`, `fsr4_auto_exposure` y `fsr4_invert_jitter`, que solo lee el upscaler temporal, y `live_resolution`,
  que no leía nadie (campo borrado). `Values::upscaler` empieza en `UpscalerOff`, y los demás campos se quedan en su
  valor por defecto porque el código conservado los lee.
- Variables que ya no se leen: `GOW3_UPSCALER`, `GOW3_UPSCALE_PRESET`, `GOW3_JITTER`, `GOW3_REACTIVE*`,
  `GOW3_OBJECT_MOTION`, `GOW3_OBJECT_MOTION_ALL`, `GOW3_FSR_SHARPNESS`, `GOW3_UPSCALE_BEFORE_CS`,
  `GOW3_UI_TRIGGER_VS`, `GOW3_OUTPUT_RES`, `GOW3_PRESET_FILE` (también podía cambiar el upscaler), `GOW3_DEBUG_MOTION`,
  `GOW3_TAA_DIAGNOSTICS` (con su volcado de imágenes), `GOW3_DLSS`, `GOW3_FSR4_DIR`, `GOW3_FSR4_OPT` y
  `GOW3_FSR411_DIR`.
- **Se quedan:**
  - `GOW3_RENDER_RES`, según la decisión del plan.
  - Los volcados de diagnóstico del código conservado (`GOW3_DUMP_*`, `GOW3_PRESENT_DUMP_*`, `GOW3_MOTION_SELECT_LOG`),
    que no lo encienden ni lo configuran.
  - Las variables propias de las herramientas sueltas de FSR 4 (`gpu/tools/fsr4_bench.cpp`, `tools/fsr4_*.sh`,
    `tools/fsr4cap/`, `tools/fetch_fsr4_assets.sh`). No las lee el juego.
  - `GOW3_FSR4_PROFILE`/`STATS` (parche `0001`), que se van en la tarea 8.
- Overlay:
  - El ítem «Upscaler» sale de `Gow3PerfStats` y del menú. El bit 2 queda libre, sin renumerar los demás.
  - `DefaultItems` pasa a FPS y frametime, y es también el valor inicial de `overlay_items`, que antes era `7` puesto
    a mano.
  - Una clave `overlay_upscaler` de un `gow3.ini` viejo se ignora.
- Pruebas:
  - `graphics-settings-test`: el `.ini` lleva `upscaler=fsr3` y todas las claves del upscaler temporal, y el entorno
    `GOW3_UPSCALER=fsr3` y otras seis variables. El resultado es el upscaler apagado y ninguna de esas claves en el
    `Save()`; `fsr1=1` se sigue leyendo.
  - `perf-overlay-test` se ajustó al overlay sin el ítem.
  - `upscaler-support-test` se ajustó: ya no prueba guardar y leer la elección, que se quitó; la vuelta a FSR 3.1 se
    sigue probando.
  - Nuevo `test_launch_has_no_temporal_upscaler_switch`, sobre `run.py` y `run.sh`.
  - Resultados: `build.sh --test` verde. En Python 282 tests (281 + 1), 35 omitidos y los 4 fallos esperados de la
    línea base. Siguen verdes `ui-composition-test`, `camera-motion-test`, `taa-shader-test` y
    `scene-resolution-test`.
  - Recorrido grabado `t2` (`out/route/t2/`), adelantado de la tarea 3 porque el cambio toca el rasterizer y la caché
    de pipelines: los 6 fotogramas `exact`/`mask` son idénticos a `out/baseline/6a/`, sin `STOP:`/`Fault:`. 2700 y
    2890 son `eye` (vídeo debajo) y no se comparan a nivel de píxel.
- Quedan para la tarea 3: el launcher, el empaquetado y la documentación (`docs/upscaler.md`,
  `docs/DEPTH_ADAPTIVE_TAA.md`, `docs/motion_vectors.md`, `packaging/windows/README.md`).

**Hecho, launcher, empaquetado y documentación (tarea 3 del plan, 2026-10-30):**

- **[D]** 2026-10-30: el usuario decide borrar también las herramientas sueltas de FSR 4, que la tarea 2 había dejado.
  No las usa el juego; solo descargaban, optimizaban, comprobaban y medían los assets de un FSR 4 que ya no se puede
  encender. Siguen en la historia de git. Se borraron `tools/fetch_fsr4_assets.sh`, `tools/fsr4_optimize.sh`,
  `tools/fsr4_verify.sh`, `tools/fsr4_post_lds.pl`, `tools/fsr4_pass11_guard.pl`, `tools/Fsr4SpirvCrossFixes.pm`,
  `tools/fsr4cap/` (12 archivos), `gpu/tools/fsr4_bench.cpp` con su target `fsr4-bench` y la línea de
  `tools/fsr4cap/*.exe` en `.gitignore`. También `tools/soak.sh`, un resto de GoW3 («entra en el nivel y gira la
  cámara»). El código del upscaler (`vk_fsr4.cpp`, `fsr411/`, el submódulo) se queda; sus comentarios ya no mandan a
  las herramientas borradas. `fsr4_shaders/` y `fsr4_411/` siguen en `.gitignore`, porque el código conservado los
  busca ahí y protegen copias locales.
- Launcher:
  - ya no envía `GOW3_EXPERIMENTAL_UPSCALER` ni `GOW3_FSR4_DIR` (con su `fsr4_folder()`);
  - `experimental_upscaler` sale de `APP_DEFAULTS`; un `settings.json` viejo que lo tenga se ignora;
  - `upscaler`, `preset`, `output_res`, `live_resolution` y `object_motion` salen de `INI_DEFAULTS`/`INI_FLAGS`.
    Ya no se escribían, porque el launcher solo guarda las claves con control, pero eran valores por defecto
    muertos;
  - se borran el bloque comentado de `build_graphics`, las listas `UPSCALERS`, `PRESETS`, `OUTPUTS` y `LIVE`, y
    `launcher/gow3_assets.py` (comprobaba los assets de FSR 4.1.1; solo lo usaban el empaquetado Nix y su test);
  - `gow3_lang.py`: salen los 26 textos del upscaler de `KEYS` y de las 12 listas por posición (156 → 130, con los
    mismos índices en todas; comprobado contra el archivo anterior), y 4 de `EXTRA['pt']`. Quedan otros textos
    huérfanos de la época de GoW3, que no son del upscaler y no se tocan aquí.
- `tools/gpu_capabilities.c` se queda solo con `--list`, la lista de GPUs del launcher. Sus otros dos modos no
  los llamaba nadie: `--live-resolution` (decidía `live_resolution=auto`) y la comprobación de blit de los presets
  en vivo. Sin `--list`, sale con el uso y código 2.
- Empaquetado: `packaging/default.nix` ya no copia `fsr4_shaders` ni `fsr4_411` (con `GOW3_PACKAGE_FSR411`).
  `package.sh` ya no los empaquetaba; su comentario y el de `packaging/windows/README.md` (que decía que
  `GOW3_DLSS=0` lo apagaba y que el paquete llevaba la DLL de DLSS) dicen ahora que no se puede encender.
  `build_dlss.sh` y `gpu/dlss_bridge` se quedan: construyen código conservado.
- Documentación: `docs/upscaler.md` abre con un aviso de que el upscaler está aparcado, sin forma de encenderlo y
  sin sus herramientas, y el resto queda como referencia. `docs/DEPTH_ADAPTIVE_TAA.md` y `docs/motion_vectors.md`
  llevan una nota igual. `README.md` y `README.pt-BR.md` (juntos) quitan el interruptor «experimental» y el ítem
  «upscaler» del overlay. `CLAUDE.md` dice que §10 está hecha. Las cabeceras no nombran variables quitadas, para que
  el `sed` de la tarea 5 no las convierta en nombres `TSUKI_*` que nunca existieron.
- Pruebas:
  - `test_launcher_upscale.py` se reescribe: sin claves ni valores por defecto del upscaler, sin las dos variables
    en el entorno de lanzamiento aunque `settings.json` traiga `experimental_upscaler`, y sin sus textos; cada tabla
    de traducción sigue cuadrando con `KEYS`. Falló por la razón esperada antes del cambio.
  - Se borra `test_upscaler_assets.py`, que solo probaba `gow3_assets.py`.
  - `HiddenUpscalingTest` (`test_launcher_save.py`, solo Windows) se queda: comprueba que las claves viejas de un
    `gow3.ini` se conservan sin migrar.
  - Resultados: `build.sh --test` verde (12 PASS) y sin avisos nuevos; Python 280 tests (282 − 3 + 1), 35 omitidos y
    los 4 fallos esperados; `graphics-settings-test`, `perf-overlay-test` y `upscaler-support-test` verdes.
    `gow3-gpu-capabilities --list` lista las 3 GPUs.
  - Recorrido grabado `tras10`: los 6 fotogramas `exact`/`mask` son idénticos a `out/baseline/6a/`, y 2700 y 2890
    coinciden a ojo. Sin `STOP:`/`Fault:`; `Object motion: off`.
- Revisión a ojo del usuario (2026-10-30, partida `out/run-audiodec-a1.log`): en el menú Insert, «Scaling and
  sharpening» muestra «Upscale with FSR 1», «Sharpening (RCAS)» y «Sharpening strength», y la pestaña Overlay ya no
  tiene «Upscaler». El `gow3.ini` que guardó el juego al cerrar lleva los ítems del overlay que se tocaron y
  `fsr1`/`rcas`/`rcas_strength`, sin ninguna clave del upscaler temporal.

Criterios de aceptación:

- [x] Ni el launcher, ni el menú Insert, ni `gow3.ini`, ni una variable de entorno pueden encender el upscaler temporal.
  Lo comprueba un test que ponga `upscaler=fsr3` en el `.ini` y `GOW3_UPSCALER=fsr3` en el entorno, y vea el
  upscaler apagado.
- [x] FSR 1 y RCAS siguen funcionando como antes. `graphics-settings-test` lee y guarda `fsr1`, `rcas` y
  `rcas_strength`, y el usuario vio sus controles en el menú Insert (2026-10-30).
- [x] Suites verdes; las capturas de la línea base y el recorrido grabado no cambian (`t2` y `tras10`).

## 11. Añadido: quitar «God of War III», no solo los nombres `gow3`

**[D]** 2026-10-30, al empezar la tarea 4 del plan: el usuario lo añade a esta spec. Todo lo que nombre a God of War
III se reemplaza o se quita, no solo los identificadores `gow3`. Esto cambia la decisión del 2026-10-14 (§3) para los
textos del launcher y del empaquetado, salvo lo que el punto 6 deja para la Alpha.

Hechos (**[V]** 2026-10-30, en `ef0c90c`, fuera de `docs/gow3/`, `docs/plans/`, `docs/specs/` y submódulos):

- 237 líneas en 40 archivos nombran el juego por su nombre, `GoW3` o sus seriales (`God of War`, `GoW3`,
  `CUSA01623`, `CUSA01715`, `Remastered`, `Kratos`). De ellas, 77 están en `README.md`, `README.pt-BR.md` y
  `packaging/windows/README-Windows.txt`.
- El `sed` de las tareas 5 y 7 haría falsas otras frases: «`bb`/`bbport` renamed to `gow3`», «GoW3-only features»,
  «el trabajo de rendimiento 3D de gow3», «gow3 contributors».
- `Kratos` en `sceUserServiceIsKratosUser`, `sceShellCoreUtilGoBackToKratosCurrentSessionGame` y parecidas
  (`gpu/shadps4/core/libraries/system/`) es el nombre interno de la consola en la API de PS4, no el personaje.
- El actualizador del launcher (`check_update`, `offer_update`, `install_update`, `--install-update`,
  `RELEASES_API`/`RELEASES_PAGE` a `FelipePrado0/gow3_pc`) descargaría las versiones del port de God of War III
  encima de este.

Reglas, por tipo de mención (**[D]** 2026-10-30):

1. **Identidad** (nombre de la aplicación, del `.exe` y del acceso directo, `.desktop`, acceso de Steam, textos del
   launcher en los 12 idiomas, datos de prueba): pasa a Tsukihime. Nombre corto `Tsukihime` (`Tsukihime.exe`,
   `Play Tsukihime.exe`, `APP_NAME`); título completo `TSUKIHIME -A piece of blue glass moon-` donde se muestra el
   título del juego. Los datos de prueba usan `CUSA47999` y nombres del juego (`Shiki` en vez de `Kratos`).
2. **Comentarios de código que explican algo con lo que hacía God of War III** (MSAA con dos muestras de
   profundidad, la página que reescribe cada fotograma, «L3+R3 is a game action in God of War III», los hashes de
   shaders, …): **se borran**, no se reescriben.
3. **Créditos de otros proyectos** (el fork de shadPS4 de cuesta4, `KrachDev/shadPS4_GOW3` en
   `vk_lighting_test.h`, el aviso de marca registrada, las líneas `Copyright … gow3 contributors`): se quedan como
   están. Las líneas con God of War llevan `rename-check: ok`; `gow3 contributors` es una excepción de
   `check_rename.sh`, para no meter el marcador en `LICENSE.txt`.
4. **El actualizador del launcher se borra** (funciones, `--install-update`, el ajuste `check_updates`, su casilla,
   su botón y sus textos). Es el único cambio de comportamiento de esta sección, solo en el launcher, y va en su
   propio commit. El recorrido grabado no lo usa.
5. **Historia** en `CLAUDE.md`, el roadmap y el resto de `docs/` (fuera de las excepciones del §6): dice «el juego
   anterior» / «the previous game», con un enlace a `docs/gow3/ROADMAP.md`, que lo nombra.
6. **Se deja para la Alpha, sin tocar:**
   - `README.md`, `README.pt-BR.md` y `packaging/windows/README-Windows.txt` (la guía del jugador que va en el
     paquete). Ni siquiera se renombran sus nombres `gow3`: se reescribirán enteros para la Alpha.
   - La identidad visual del launcher: el icono (`launcher/gow3.png`/`.ico`, el símbolo Omega, también dentro del
     `.exe` de Windows y en el `.desktop`), la fuente de títulos `launcher/gowfont.ttf` y la paleta. Los archivos
     cambian de nombre con el §3 (`tsuki.png`/`.ico`), no de contenido; las líneas que nombran God of War por ellos
     llevan `rename-check: ok`.
   - Los README de desarrollo (`packaging/windows/README.md`, `gpu/patches/fsr-vulkan/README.md`) **no** son de
     este punto: describen archivos que cambian de nombre y siguen el renombrado.
7. No se tocan los nombres de la API de PS4 con `Kratos` (hecho 3).

Comprobación: `tools/check_rename.sh` busca también `god of war`, `CUSA01623` y `CUSA01715` (sin distinguir
mayúsculas), con las excepciones del §6 más los tres README del punto 6 y las líneas `Copyright … gow3 contributors`.

Orden: la prosa, los comentarios y los textos de esta sección van en la tarea 4 del plan, **antes** de cualquier
`sed`, porque el `sed` no los arregla y algunos los volvería falsos.

**Hecho (tarea 4 del plan, 2026-10-30):**

- `check_rename.sh` (commit `5571780`): busca también el nombre y los seriales, con tres tests nuevos. Además acepta
  la ruta `docs/gow3/` escrita como enlace relativo (`../gow3/`) en `CLAUDE.md` y el roadmap, que es la misma
  excepción del §6 punto 4.
- Actualizador (commit `05e1692`): borrado con sus 12 textos en los 13 idiomas (`KEYS` y las 12 listas, 130 → 118,
  comprobado contra el archivo anterior) y un test que comprueba que no queda.
- Identidad: `APP_NAME` del launcher es `Tsukihime`, y el título completo va en el `.desktop`, en el acceso de Steam
  y en el texto que pide el dump. `Tsukihime.exe` y `Play Tsukihime.exe` aparecen en `package.sh`, `play.c`, el
  acceso directo y el comentario de CMake. El logo de Steam dice «TSUKIHIME / A PIECE OF BLUE GLASS MOON» (visto
  renderizado). `CUSA47999` sustituye al serial anterior en la carpeta por defecto, en el texto «Choose the game
  folder…» (13 idiomas), en `docs/MODS.md`, en la plantilla de incidencias y en los datos de prueba. `Kratos`
  pasa a `Shiki` en los tests. Se borra una entrada huérfana de `EXTRA['pt']`: su clave no era la que pide el
  launcher, y nombraba el juego.
- Comentarios borrados (regla 2): `gow3_msaa.h`, `liverpool.cpp`, `camera_motion.comp`, `motion_history.h`,
  `vk_frame_capture.cpp`, dos en `texture_cache.cpp`, `gow3_overlay.cpp`, `scripts/patches.py`,
  `test_blend_rewrite.cpp`, `test_graphics_settings.cpp` y `test_patches.py`. En el código del juego solo cambian
  comentarios (`git diff` sin otra línea), así que el recorrido grabado no se repite.
- Créditos (regla 3): el build de `KrachDev/shadPS4_GOW3` va en su propia línea con el marcador
  (`vk_lighting_test.h`). La fuente de títulos se explica en una línea marcada; los demás comentarios dicen «la
  fuente de títulos» y «los colores del juego anterior».
- Historia (regla 5): `CLAUDE.md`, el roadmap, `docs/upscaler.md`, `docs/ENTORNO.md` y los comentarios de cuatro
  tests. En `CLAUDE.md`, «Game-specific surface» pasa a ser la lista de lo borrado: nombraba archivos que la Spec
  1a ya había quitado. «the `gow3` branch» pasa a «branch `gow3`». La sección de la Spec 7 del roadmap ya no copia
  los nombres viejos (la tabla está en el §3), y lleva la §11. Las frases de `CLAUDE.md` y del roadmap que nombraban
  variables quitadas en la §10 ya no las nombran.
- Resultados: `check_rename.sh` no encuentra ninguna línea con el nombre o los seriales (2233 restos, todos
  nombres `gow3`). `build.sh --test` verde (12 PASS); Python 283 tests, 35 omitidos y los 4 fallos esperados.
- `docs/upscaler.md`, `docs/DEPTH_ADAPTIVE_TAA.md` y `docs/motion_vectors.md` nombraban variables quitadas en la §10,
  que el `sed` convertiría en nombres `TSUKI_*` que nunca existieron. **[D]** 2026-10-30: el usuario los movió a
  `docs/gow3/`, con `docs/plans/` (commit `c48d6c0`). Las referencias del código (siete comentarios) y del roadmap
  apuntan a la ruta nueva; `check_rename.sh` acepta `docs/gow3/` en todo el árbol (§6 punto 8, con test). Los
  README del jugador siguen enlazando `docs/upscaler.md`: se rehacen para la Alpha.

Criterios de aceptación:

- [x] `tools/check_rename.sh` no encuentra `God of War`, `CUSA01623` ni `CUSA01715` fuera de las excepciones.
- [x] El launcher no tiene actualizador: ni `RELEASES_API`, ni `--install-update`, ni el ajuste `check_updates`;
  un test lo comprueba (`test_no_updater`).
- [x] `README.md`, `README.pt-BR.md`, `packaging/windows/README-Windows.txt`, el icono y `launcher/gowfont.ttf` no
  cambian de contenido en esta spec (`git diff ef0c90c` vacío para ellos al cerrar la tarea 4).
