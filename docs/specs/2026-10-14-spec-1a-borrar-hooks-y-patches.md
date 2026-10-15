# Spec 1a — Eliminar hooks, patches, touchpad, tests e identidad de GoW3

Fecha: 2026-10-14. Estado: **borrador para aprobación**. Roadmap: `.waffle/ROADMAP.md`, sección "Spec 1a".

Marcas: **[V]** comprobado en el repo (comando indicado); **[D]** decisión que debe tomar el usuario.

## 1. Objetivo

Quitar del árbol todo el código de God of War III que **no** toca el renderizador: hooks de
orbes/daño/cheats/actores, el perfil de parches, la semántica del touchpad, los tests que solo
inspeccionan código de GoW3 y los valores por defecto de identidad. Sin dejarlo desactivado.

No se renombra nada (eso es la Spec 7) ni se tocan las heurísticas del renderizador (Spec 1b).
Se puede hacer en paralelo con la Spec 2 porque no depende del juego.

## 2. Inventario (hechos comprobados el 2026-10-14)

- **[V]** Hooks en `src/probe.c`: `install_cheats` (`:382`), `install_actor_watch` (`:431`),
  `install_orb_multiplier` (`:454`), `install_jump` (`:488`, solo lo usan los hooks),
  `install_stat_multipliers` (`:500`). Se invocan en `:886-889` bajo `if (gpu_enabled)`.
- **[V]** Cabeceras de hooks: `src/actor_hook.h`, `src/cheat_hook.h`, `src/damage_hook.h`,
  `src/orb_hook.h`. `src/signature.h` solo la usan esas cuatro, `probe.c` y
  `tests/test_signatures.cpp` (`git grep -l signature.h`): se borra con ellas.
- **[V]** Backends GPU de los hooks: `gpu/shim/gow3_orbs.{cpp,h}` y `gpu/shim/gow3_actors.{cpp,h}`.
  Los referencian `gow3_overlay.cpp`, `gow3_settings.{cpp,h}`, `gpu/CMakeLists.txt` y
  `tests/test_graphics_settings.cpp`.
- **[V]** Tests de GoW3: `tests/test_actor_hook.cpp`, `test_cheat_hook.cpp`, `test_cheat_hooks.cpp`,
  `test_orb_multiplier.cpp`, `test_stat_multipliers.cpp`, `test_signatures.cpp`,
  `tests/data/eboot_6e72_sites.json`. Sus destinos en `gpu/CMakeLists.txt`: `orb-multiplier-test`
  (`:387`), `stat-multipliers-test` (`:391`), `cheat-hooks-test` (`:399`), `signatures-test` (`:401`),
  `actor-hook-test` (`:413`), `cheat-hook-test` (`:429`).
- **[V]** Parches: `patches/God_of_War_III_Remastered.xml`; `scripts/patches.py` define
  `GAMES=[({'CUSA01623','CUSA01715','CUSA01720'},…,'01.02',…)]` (`:160`), `REFERENCE_SHA256` (`:163`)
  y la usa en `:185` y `:340`. `tools/make_signatures.py` genera las firmas del XML.
- **[V]** Identidad en código: `src/probe.c:826` (`title[128]="God of War III"`, serial `UNKNOWN`
  y lectura del `param.sfo`), `run.py`, `scripts/steam_shortcut.py`, `src/runtime_pad.c`,
  `src/vulkan_smoke.c`, `gpu/shim/gow3gpu.cpp`, `gpu/shim/gow3_overlay.cpp`.
- **[V]** Touchpad: `src/runtime_pad.c` (comentario `:7`, `CLICK_LEFT/RIGHT` `:30-32`, función de
  clics `:77-103`). `tests/test_pad.c` y `tests/test_pad_map.c` lo ejercitan.
- **[V]** Tests Python que citan identidad o perfil de GoW3: `test_patches.py`, `test_game_update.py`,
  `test_mods.py`, `test_save_backup.py`, `test_steam_shortcut.py`, `test_launcher_controls.py`,
  `test_launcher_game_files.py`, `test_launcher_run_log.py`, `test_launcher_stream_memory.py`.
- **[V]** Otros archivos con identidad de GoW3 que **no** son de esta spec porque son del renderizador
  (Spec 1b): `liverpool.cpp`, `texture_cache.cpp`, `vk_frame_capture.cpp`, `camera_motion.comp`,
  `motion_history.h`, `vk_lighting_test.h`, `vk_temporal_upscaler.h`.

## 3. Decisiones tomadas (2026-10-14)

- **`tools/gow3_frame_map.py`: se borra.** Es específico de GoW3 y no se va a usar para nada.
- **Launcher, paquetes, README, `.github` y acceso de Steam: se dejan para el final**, después de la
  Alpha (ver "Después de la Alpha" en el roadmap). Esta spec **no los toca**, ni siquiera para quitar
  las opciones de orbes o cheats que muestra el launcher. Consecuencias a vigilar: (a) el launcher es
  solo de Windows/Tk y en Linux no se usa; (b) los tests de Python del launcher no se modifican, y si
  alguno falla porque una función ya no existe, se anota en el cambio en vez de arreglarlo aquí.
- **`docs/plans/*.md` y `docs/gow3/ROADMAP.md`: se excluyen** del script de comprobación del
  renombrado (Spec 7). No se borran en esta spec.

## 4. Diseño

### Paso 1 — Hooks y backends
Borrar `install_*` de `probe.c` y su invocación, `src/*_hook.h`, `src/signature.h`,
`gow3_orbs.*`, `gow3_actors.*` y sus llamadas desde `gow3_overlay.cpp` y `gow3_settings.*` (menús y
claves de `gow3.ini`). El launcher no se toca (queda para el final). Quitar los fuentes y los tests de `gpu/CMakeLists.txt` y de
`build.sh`. El menú Insert pierde las secciones correspondientes; el resto no cambia.

### Paso 2 — Parches
Borrar `patches/God_of_War_III_Remastered.xml`, `GAMES` y `REFERENCE_SHA256`. El cargador de parches
queda **genérico**: sin perfil para el título, no aplica ninguno y lo dice en el log (hoy ya responde
"no patch profile for CUSA47999"). `tools/make_signatures.py` se borra si solo genera firmas del XML.
`mods.py` y `game_update.py` no cambian.

### Paso 3 — Touchpad
Quitar de `runtime_pad.c` la traducción de clics laterales del touchpad a modo foto / pausa y las
constantes `CLICK_LEFT/RIGHT`. Si Tsukihime usa el touchpad para algo, se verá en el recorrido;
mientras no, un `BTN_TOUCHPAD` sencillo (botón del mando) se conserva porque es el mapa de PS4.

### Paso 4 — Tests
Borrar los de la sección 2. Reescribir los genéricos que hoy dependen del perfil de GoW3, sobre todo
`test_patches.py`: pasan a usar un **perfil sintético** definido en el propio test, para seguir
cubriendo el motor de parches y mods sin un juego concreto. Listar en el cambio los tests borrados.

### Paso 5 — Identidad
`probe.c`: título y serial vacíos por defecto, leídos siempre del `param.sfo`; si el `TITLE_ID` no es
`CUSA47999`, **rechazar** con un mensaje claro y un código de salida distinto de cero. `run.py` y
`scripts/patches.py`: sin referencias a CUSA01623/CUSA01715/CUSA01720. `scripts/steam_shortcut.py`
queda para el final junto con el launcher.

## 5. Requisitos

- Ni hooks, ni parches, ni firmas de God of War III en el árbol.
- Otro juego distinto de CUSA47999 se rechaza con mensaje claro (el roadmap lo exige).
- Lo genérico (loader, runtime, núcleo de shadPS4, upscalers) no cambia de comportamiento.

## 6. Pruebas y criterios de aceptación

- [ ] `bash build.sh --test` y `python3 -m unittest discover -s tests` pasan. Se compara contra la
  línea base de la etapa 0: la diferencia es exactamente la lista de tests borrados.
- [ ] **Comprobación por lista cerrada de símbolos**, con palabra completa:
  `git grep -nwE 'CUSA01623|CUSA01715|CUSA01720|God_of_War_III_Remastered|REFERENCE_SHA256|install_cheats|install_actor_watch|install_orb_multiplier|install_stat_multipliers|GOW3_ORB_PICKUP_LENGTH|GOW3_ORB_PICKUP_OFFSET|GOW3_DAMAGE_HOOK_LENGTH'`
  (cada patrón se comprobó el 2026-10-14: da resultados hoy, así que se puede verificar que baja a 0;
  un primer borrador usaba `ORB_PICKUP`, que con `-w` no coincide con nada porque el símbolo real
  es `GOW3_ORB_PICKUP_LENGTH`)
  sin resultados, **excluyendo `docs/`**. La lista se amplía con los nombres que el inventario del paso 1
  encuentre. No se usan `orb` ni `damage` sueltos: coinciden con `LICENSE` y cabeceras de FSR.
- [ ] `out/first-run.log` antes y después: sin las diez líneas de hooks/cheats "code not found" ni
  las de parches; por lo demás, el mismo punto de parada.
- [ ] Un juego distinto de CUSA47999 (se simula con un `param.sfo` de prueba) se rechaza con mensaje.
- [ ] `python3 -m unittest discover -s tests` incluye un test del cargador de parches con perfil sintético.

## 7. Riesgos

- Borrar un archivo compartido con lo genérico: se borra paso a paso, compilando y probando tras cada
  uno; la historia queda en git y en la rama `gow3`.
- `gow3_overlay.cpp` y `gow3_settings.*` mezclan opciones de GoW3 con las genéricas (resolución,
  ventana, upscalers). Hay que revisarlas línea a línea y no borrar de más.
- Los `.ini` y los saves antiguos no se migran (ya está dicho en el roadmap).
