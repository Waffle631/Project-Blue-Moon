# Spec 7 — Renombrar `gow3` a `tsuki`

Fecha: 2026-10-14, ampliada el 2026-10-25 (§9). Estado: **borrador para aprobación**. Roadmap: `docs/tsuki/ROADMAP.md`, sección "Spec 7".
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
  (`build.sh` lo hace), que no se modifica.
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
Alpha, como se decidió el 2026-10-14.

## 4. Orden de trabajo (por capas; compilar y pasar tests tras cada una)

Antes de empezar: una captura de la línea base de las pruebas (`bash build.sh --test`, Python), el
log del recorrido grabado (`user/last_run.log`) y las capturas de píxeles (`out/baseline/`). Son el
oráculo de que nada cambió.

1. **Inventario.** `git grep -ohiE '[a-z0-9_.-]*gow3[a-z0-9_.-]*' | sort -u` y `git ls-files | grep -i gow3`,
   más strings, `CMakeLists.txt`, `build.sh`, `run.py`, `run.sh`, `shell.nix`, `.gitignore`, empaquetado y
   documentación. Resultado: una lista cerrada de nombres antiguos → nuevos (`tools/rename_map.txt`,
   solo local, no versionado).
2. **Variables de entorno y de compilación** `GOW3_*` → `TSUKI_*`, en C, C++, Python, shell, CMake,
   `docs/ENTORNO.md`, `README*.md` y `CLAUDE.md` a la vez, **sin alias** y sin leer los nombres viejos.
   Una variable que quede a medias haría que el juego ignorara la opción sin avisar.
3. **Archivos y directorios** con `git mv` (conserva la historia); después `#include`, listas de
   fuentes de CMake y `build.sh`.
4. **Símbolos**, clases, binarios, la biblioteca de GPU, `.ini`, rutas de usuario y la marca `gow3:`
   → `tsuki:` en el código de shadPS4 (es un cambio de comentario, no de lógica).
5. **Documentación.** `CLAUDE.md`, `README*.md`, este roadmap y la memoria del proyecto. El historial
   (`docs/gow3/`) no se renombra.
6. **Parche de FSR.** Renombrar el archivo y las variables internas; comprobar que `build.sh` lo aplica al
   submódulo limpio.
7. **Métricas del overlay en Linux** (§9), con el renombrado ya comprobado por el §7.

## 5. Requisitos

- Ninguna variable `GOW3_*` debe seguir leyéndose.
- El comportamiento no cambia: el recorrido grabado y las capturas dan el mismo resultado.
- Los submódulos `gpu/third_party/*` no se tocan.

## 6. Comprobación

`tools/check_rename.sh` (nuevo): ejecuta `git grep -i gow3` y falla si encuentra algo fuera de estas
**excepciones explícitas**:

1. `docs/gow3/` (histórico de GoW3);
2. `docs/plans/` (diagnósticos históricos de GoW3; **excluido**, decidido 2026-10-14);
3. el nombre de la **rama** `gow3` cuando aparece como `rama \`gow3\`` o `branch gow3` en `CLAUDE.md` y
   en este roadmap;
4. los submódulos y `pgo/`.

El script se ejecuta en `bash build.sh --test` para que no regrese ningún nombre antiguo.

## 7. Pruebas y criterios de aceptación

- [ ] `bash build.sh --test` y `python3 -m unittest discover -s tests` pasan, con la línea base como
  referencia.
- [ ] `tools/check_rename.sh` termina con éxito.
- [ ] `bash build.sh` produce `out/tsuki-probe` y `out/gpu/libtsukigpu.so`, y
  `TSUKI_GAME_DIR=Tsukihime/CUSA47999 python3 run.py` repite el recorrido grabado con el mismo log (sin
  `STOP:` ni `Fault:`).
- [ ] Las capturas de píxeles de la línea base coinciden antes y después (`tools/compare_captures.sh`).
- [ ] La ventana y el menú Insert muestran el título leído del `param.sfo`.
- [ ] Un test comprueba que una variable `GOW3_*` ya no tiene efecto (por ejemplo, `GOW3_LANGUAGE=8`
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
