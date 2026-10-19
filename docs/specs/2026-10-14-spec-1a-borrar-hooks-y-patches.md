# Spec 1a — Eliminar hooks, patches, touchpad, tests e identidad de GoW3

Fecha: 2026-10-14. Revisada el 2026-10-18 tras auditar el repo (la spec seguía sin implementar).
Estado: **en curso**: pasos 0, 1, 4, 2 y 3 hechos el 2026-10-19, en ese orden (commits `9f86fc1`, `f90d6ac`, `ad33479`, `2e8474d`); falta el 5. Desde el paso 2 fallan 4 tests del launcher (ver su resultado). Roadmap: `docs/tsuki/ROADMAP.md`, sección "Spec 1a".

Marcas: **[V]** comprobado en el repo (comando indicado); **[D]** decisión que debe tomar el usuario.

## 1. Objetivo

Quitar del árbol todo el código de God of War III que **no** toca el renderizador: hooks de
orbes/daño/cheats/actores, el perfil de parches, la semántica del touchpad, los tests que solo
inspeccionan código de GoW3 y los valores por defecto de identidad. Sin dejarlo desactivado.

No se renombra nada (eso es la Spec 7) ni se tocan las heurísticas del renderizador (Spec 1b).
Se puede hacer en paralelo con la Spec 2 porque no depende del juego.

## 2. Inventario (hechos comprobados el 2026-10-18)

- **[V]** Hooks en `src/probe.c`: `install_cheats` (`:384`), `install_actor_watch` (`:433`),
  `install_orb_multiplier` (`:456`), `install_jump` (`:490`, solo lo usan los hooks),
  `install_stat_multipliers` (`:502`). Se invocan en `:888-891` bajo `if (gpu_enabled)`.
- **[V]** Cabeceras de hooks: `src/actor_hook.h`, `src/cheat_hook.h`, `src/damage_hook.h`,
  `src/orb_hook.h`. `src/signature.h` solo la usan esas cuatro, `probe.c` y
  `tests/test_signatures.cpp` (`git grep -l signature.h`): se borra con ellas.
- **[V]** Backends GPU de los hooks: `gpu/shim/gow3_orbs.{cpp,h}` y `gpu/shim/gow3_actors.{cpp,h}`.
  Los referencian `gow3_overlay.cpp` (52 líneas con orb/actor/cheat/damage/enemy),
  `gow3_settings.{cpp,h}` (claves `cheat_*`, `*_orb_multiplier`, daño, `GOW3_CHEAT_COUNT`),
  `gpu/CMakeLists.txt` y `tests/test_graphics_settings.cpp` (`:84-90` escribe y lee claves `cheat_*`).
- **[V]** `build.sh` **no** menciona ninguno de estos tests ni fuentes (`grep` vacío): no hay nada que
  quitar ahí. Los tests C se descubren por CMake.
- **[V]** Tests de GoW3: `tests/test_actor_hook.cpp`, `test_cheat_hook.cpp`, `test_cheat_hooks.cpp`,
  `test_orb_multiplier.cpp`, `test_stat_multipliers.cpp`, `test_signatures.cpp`,
  `tests/data/eboot_6e72_sites.json`. Sus destinos en `gpu/CMakeLists.txt`: `orb-multiplier-test`
  (`:391`), `stat-multipliers-test` (`:395`), `cheat-hooks-test` (`:403`), `signatures-test` (`:405`),
  `actor-hook-test` (`:417`), `cheat-hook-test` (`:433`).
- **[V]** `tests/test_patches.py:153` lee `tests/data/eboot_6e72_sites.json` y `:11` apunta al XML de
  GoW3: **el JSON no puede borrarse antes de reescribir ese test** (orden en el paso 4).
- **[V]** Parches: `patches/God_of_War_III_Remastered.xml`; `scripts/patches.py` define
  `GAMES=[({'CUSA01623','CUSA01715','CUSA01720'},…,'01.02',…)]` (`:160`), `REFERENCE_SHA256` (`:163`)
  y los usa en `:185`, `:262` y `:340`. `tools/make_signatures.py` genera las firmas del XML.
  `scripts/patches.py:322` y `run.py:160` tienen además `CUSA01623` como carpeta por defecto.
- **[V]** Identidad en código: `src/probe.c:828` (`title[128]="God of War III"`, serial `UNKNOWN`
  y lectura del `param.sfo`), `run.py`, `scripts/steam_shortcut.py`, `src/runtime_pad.c`,
  `src/vulkan_smoke.c`, `gpu/shim/gow3gpu.cpp`, `gpu/shim/gow3_overlay.cpp`.
- **[V]** No existe hoy ninguna comprobación del `TITLE_ID`. `probe.c:686-688` lo lee solo para
  configurar el directorio de savedata (`runtime_savedata_configure`).
- **[V]** Touchpad en `src/runtime_pad.c`: comentario de cabecera (`:7`), `CLICK_LEFT/RIGHT` (`:32`),
  `play_click` y sus tiempos (`:70-104`, se llama en `:378`), tabla `ps_names` (`:131-135`: `touchpad`
  y `touchpad_left` → `CLICK_LEFT`, `touchpad_right` → `CLICK_RIGHT`), mapas por defecto
  (`BACK` → `CLICK_RIGHT` en `:143`, `Tab` → `CLICK_LEFT` y `Backspace` → `CLICK_RIGHT` en `:152`) y el
  comentario del archivo de pad (`:258`).
  Lo ejercitan `tests/test_pad.c` (`:28-113` y `:150-164`, toda la parte de clics sintéticos con toques
  en x=480/1440) y `tests/test_pad_map.c` (`:41-58`, `pressed_by(...)==CLICK_LEFT/RIGHT`).
  El launcher (`launcher/gow3_launcher_win.py:321-357`, `button_icons.py`) escribe `touchpad_left` y
  `touchpad_right` en `GOW3_KEY_MAP`; es otra razón para conservar los nombres (ver paso 3).
- **[V]** Tests Python con identificadores de la lista cerrada de GoW3:
  `test_patches.py` (6), `test_game_update.py` (3), `test_launcher_controls.py` (3),
  `test_launcher_game_files.py` (1), `test_mods.py` (1), `test_save_backup.py` (1). Otros tests Python
  citan "God of War" en datos de ejemplo (`test_launcher_font`, `test_launcher_run_log`,
  `test_launcher_stream_memory`, `test_steam_shortcut`) sin usar los identificadores.
- **[V]** Otros archivos con identidad de GoW3 que **no** son de esta spec porque son del renderizador
  (Spec 1b): `liverpool.cpp`, `texture_cache.cpp`, `vk_frame_capture.cpp`, `camera_motion.comp`,
  `motion_history.h`, `vk_lighting_test.h`, `vk_temporal_upscaler.h`.
- **[V]** Línea base de Python (2026-10-18, `out/baseline-py-1a.log`, dentro del distrobox):
  `python3 -m unittest discover -s tests` → **254 tests, OK, 37 omitidos**. La línea base de los tests C
  (`bash build.sh --test`) se captura en el paso 0.

## 3. Decisiones

Tomadas el 2026-10-14:

- **`tools/gow3_frame_map.py`: se borra.** Es específico de GoW3 y no se va a usar para nada.
- **Launcher, paquetes, README, `.github` y acceso de Steam: se dejan para el final**, después de la
  Alpha (ver "Después de la Alpha" en el roadmap). Esta spec **no los toca**, ni siquiera para quitar
  las opciones de orbes o cheats que muestra el launcher. Consecuencias a vigilar: (a) el launcher es
  solo de Windows/Tk y en Linux no se usa; (b) los tests de Python del launcher no se modifican, y si
  alguno falla porque una función ya no existe, se anota en el cambio en vez de arreglarlo aquí.
- **`docs/plans/*.md` y `docs/gow3/ROADMAP.md`: se excluyen** del script de comprobación del
  renombrado (Spec 7). No se borran en esta spec.

Tomadas el 2026-10-18:

- **El ámbito de la comprobación por `git grep` se limita a** `src/ gpu/ scripts/ tests/ run.py
  patches/ tools/`. Lo demás (`launcher/`, `README*`, `packaging/`, `.github/`, `CLAUDE.md`, `docs/`) se
  deja para después de la Alpha, de acuerdo con la decisión anterior. Dentro de `tests/` se excluyen
  `tests/test_launcher_*.py`, que no se modifican y usan `CUSA01623` como dato de ejemplo.
- **Touchpad: los nombres `touchpad`, `touchpad_left` y `touchpad_right` se conservan como alias del
  mismo botón** (`BTN_TOUCHPAD`) y su comportamiento se documenta (paso 3). Así el launcher y los
  `GOW3_KEY_MAP`/`GOW3_PAD_MAP` ya guardados siguen siendo válidos.
- **Se ejecutan los tests antes y después de cada paso** y la diferencia con la línea base se anota en
  el cambio.

## 4. Diseño

Orden: paso 0, y luego 1 a 5 en ese orden; un commit por paso, compilando y probando tras cada uno.

### Paso 0 — Línea base
`bash build.sh --test` (dentro del distrobox `ps4dev`), guardando la salida en `out/baseline-c-1a.log`
con el recuento de tests C. Junto con los 254 de Python, es la referencia del criterio de aceptación.
Guardar también `out/first-run.log` actual (arranque con `GOW3_GAME_DIR=Tsukihime/CUSA47999`) para
compararlo al final.

**Resultado (2026-10-19).** Todos los logs están en `out/` (sin versionar).

- Python (`out/baseline-py-1a.log`): 254 tests, OK, 37 omitidos.
- `bash build.sh --test` (`out/baseline-c-1a.log`): `exit=0`; 11 binarios y 14 ejecuciones, todas PASS
  (pad, runtime, file-mods, sema, eventflag ×4, content, mp3, videodec, services, isa-emulate, isa-stub).
- Tests C++ de `gpu/CMakeLists.txt` (`out/baseline-cpp-1a.log`; `build.sh --test` no los ejecuta). De 45
  destinos `*-test`, 24 pasan. 9 solo existen en Windows (bloque `if (WIN32)`: mp3, videodec, services,
  pad-map, audio-downmix, cpu-check, isa-emulate, isa-stub, tsc-clock). 6 no enlazan en Linux
  (`libgow3gpu.so: undefined reference to runtime_report`: pipeline-binary-cache,
  pipeline-binary-initialization, motion-shader, scene-resolution, taa-shader, camera-motion). 5 no
  compilan en Linux: graphics-settings y perf-overlay (`_putenv_s`), orb-multiplier (`_putenv_s` y
  `std::memcpy` sin `<cstring>`), actor-hook y cheat-hook (`windows.h`). Y `cheat-hooks-test` da 139 sin el
  `eboot.elf` como argumento. Todo eso fallaba ya antes de esta spec.
- Arranque (`out/first-run-before-1a.log`, `GOW3_TIMEOUT=25`): 10 líneas de hooks "code not found" o
  "unavailable" (orbes rojos, siete cheats, barra de vida, multiplicadores) y 2 de parches
  (`no patch profile for CUSA47999`, `0 writes`). Para el watchdog a los 25 s (`flips=1438`, código 142),
  sin `STOP:` de import ni `Fault:`.

### Paso 1 — Hooks y backends
Borrar `install_*` de `probe.c` y su invocación, `src/*_hook.h`, `src/signature.h`,
`gow3_orbs.*`, `gow3_actors.*` y sus llamadas desde `gow3_overlay.cpp` y `gow3_settings.*` (menús y
claves de `gow3.ini`). Quitar los fuentes y los tests de `gpu/CMakeLists.txt` (`build.sh` no cambia).
Adaptar `tests/test_graphics_settings.cpp` para que no use claves `cheat_*`. El menú Insert pierde las
secciones correspondientes; el resto no cambia. Se borran en este paso los seis tests de GoW3 que
inspeccionan hooks (`test_actor_hook`, `test_cheat_hook`, `test_cheat_hooks`, `test_orb_multiplier`,
`test_stat_multipliers`, `test_signatures`), pero **no** `tests/data/eboot_6e72_sites.json` (ver paso 4).

**Resultado (2026-10-19, commit `9f86fc1`).**

- Borrados: las cinco funciones `install_*` de `probe.c` (con `install_jump`), sus `extern` y sus
  llamadas; `src/{actor,cheat,damage,orb}_hook.h`, `src/signature.h`, `gpu/shim/gow3_orbs.*`,
  `gpu/shim/gow3_actors.*`; los seis tests y sus destinos de CMake. `allocate`, `protect` y `mapped`
  se quedan porque los usa el cargador.
- `gow3_settings`: sin campos ni claves de cheats, multiplicadores, daño y barra de vida. Un `gow3.ini`
  antiguo con esas claves se carga sin error y `Save` ya no las escribe. `menu_tab` queda en 0..2.
- Menú Insert: se quita entera la pestaña "Game", que solo tenía cheats, multiplicadores y la barra de
  vida. Quedan Image, Performance y Overlay. Tampoco están ya la fila "Game hooks" de About ni el dibujo
  de la barra de vida. Un `menu_tab` guardado se acota y no se reasigna (el 3 antiguo pasa a 0).
- Tests adaptados:
  - `test_graphics_settings.cpp` comprueba que las claves antiguas se ignoran y no se reescriben.
  - `test_perf_overlay.cpp` cuenta tres pestañas.
  - Los dos usaban `_putenv_s`, que solo existe en Windows, y nunca habían compilado en Linux. Ahora lo
    definen con `setenv` fuera de Windows y pasan.
- Hallazgo: `tests/test_patches.py::test_signatures_match_the_full_update_eboot` también leía
  `src/cheat_hook.h`. Pierde solo esa mitad; la de los parches sigue hasta el paso 4, que borra el test.
- Comparación con el paso 0:
  - Python: 254 OK y 37 omitidos, igual.
  - `build.sh --test`: igual, todo PASS.
  - C++ (`out/t1-cpp.log`): de 45 destinos y 24 PASS se pasa a 39 y 24. La diferencia son exactamente
    los seis destinos borrados (dos de ellos pasaban: signatures y stat-multipliers), más
    graphics-settings y perf-overlay, que pasan de no compilar a PASS.
- Arranque (`out/first-run-after-t1.log`): faltan las 10 líneas de hooks y el resto del log es igual.
  Las 2 de parches siguen (son del paso 2). Misma parada: watchdog a los 25 s, `flips=1442`.
- `git grep` de `gow3_orbs|gow3_actors|orb_hook|actor_hook|cheat_hook|damage_hook|signature\.h|Gow3Orbs|Gow3Actors`
  y, con `-w`, de `install_cheats|install_actor_watch|install_orb_multiplier|install_stat_multipliers|install_jump|GOW3_ORB_PICKUP_LENGTH|GOW3_ORB_PICKUP_OFFSET|GOW3_DAMAGE_HOOK_LENGTH|GOW3_CHEAT_COUNT`
  en el ámbito de la sección 3: 0 resultados.

### Paso 2 — Parches
Borrar `patches/God_of_War_III_Remastered.xml`, `GAMES` y `REFERENCE_SHA256` (y su uso en `:185`, `:262`
y `:340`). El cargador de parches queda **genérico**: sin perfil para el título, no aplica ninguno y lo
dice en el log (hoy ya responde "no patch profile for CUSA47999"). `tools/make_signatures.py` se borra
si solo genera firmas del XML. `mods.py` y `game_update.py` no cambian. La carpeta `patches/` queda
vacía (git no la conserva); el código no debe fallar si no existe.

**Resultado (2026-10-19, commit `ad33479`).** Se hizo después del paso 4, que es su precondición (sin él,
`test_patches.py` fallaba al borrar el XML).

- Borrados:
  - `patches/God_of_War_III_Remastered.xml`; la carpeta `patches/` desaparece.
  - La entrada de GoW3 en `GAMES`, que queda como tabla vacía.
  - `REFERENCE_SHA256`. Los parches externos ya no dependen de él; sin perfil, ese bloque no se alcanza.
- `tools/make_signatures.py` **se conserva**. Es genérico: recibe cualquier eboot y los sitios por
  argumento, no lleva datos de GoW3 y genera las firmas `Pattern`/`At` que el motor sigue leyendo. Solo
  pierde "hook" en su descripción.
- Carpeta de juego por defecto en `run.py` y `patches.py`: `<repo>/Tsukihime/CUSA47999`, donde está el
  dump (ignorado en `.gitignore`).
- Tests nuevos en `test_patches.py`, tres en rojo antes del cambio:
  - `GAMES` vacío.
  - La carpeta por defecto.
  - Parches externos sin build de referencia.
  - Sin `patches/` ni carpeta de datos (es una guarda y ya pasaba).
- `patches.py`, con `--game-dir Tsukihime/CUSA47999` y sin él: `Patches: no patch profile for CUSA47999: none applied`.
- Python: 254 ejecutados y 35 omitidos. **Fallan 4 tests del launcher** (6 fallos y 5 errores contando
  subtests):
  - `test_launcher_controls`: `GameFilesTest.test_play_offers_to_adopt_the_system_files` y
    `.test_system_files_beside_a_pkg_extraction`.
  - `test_launcher_game_files`: `LauncherGameFilesTests.test_cancel_then_copy_keeps_existing_files_and_reports_missing_module`
    y `.test_slow_copy_blocks_reentry_and_failure_allows_retry`.

  El launcher reconoce un dump de GoW3 con `patches.game_profile`. Con el perfil y el XML antiguos
  inyectados en memoria, los 24 tests del launcher pasan, así que esa es la única causa. Según las
  secciones 3 y 6, se anota y no se arregla.
- También fuera del ámbito: `packaging/linux/build.sh` y `packaging/windows/package.sh` copian
  `patches/` con `cp -r` y fallarán hasta que se limpie el empaquetado.
- Arranque sin `GOW3_GAME_DIR` (`out/first-run-after-t2.log`): usa `Tsukihime/CUSA47999`, y el log es
  igual al del paso 1 hasta la parada del watchdog.
  - Las 2 líneas de parches siguen ("no patch profile …" y "0 writes"). Son las de un juego sin perfil,
    que este paso pide decir en el log.
  - Para el cierre: esto choca con "ni las de parches" del criterio de la sección 6.

### Paso 3 — Touchpad
Se elimina la **semántica de GoW3**: la traducción de clics laterales a modo foto / pausa. Se conserva
un único botón.

Comportamiento tras el cambio (es el que se documenta en el comentario de cabecera de `runtime_pad.c`):

- `touchpad`, `touchpad_left` y `touchpad_right` son tres nombres del **mismo** botón, `BTN_TOUCHPAD`
  (clic del touchpad de PS4). Sirven en `GOW3_KEY_MAP`, `GOW3_PAD_MAP` y en el archivo de `GOW3_PAD_FILE`.
- Valores por defecto: teclado solo `Tab` → `BTN_TOUCHPAD`; mando solo el clic del touchpad real
  (`SDL_GAMEPAD_BUTTON_TOUCHPAD`). `Backspace` y `Back/View` (Share en DualSense) **dejan de tener
  fuente por defecto**: se quitan de los mapas (`:143` y `:152`) y el usuario puede asignarlas con
  `GOW3_KEY_MAP`/`GOW3_PAD_MAP`. El overlay ImGui las sigue usando para su propia navegación, sin cambios.
- Se pasa al juego **solo el botón**. No hay toque sintético, ni retardo de entrada/salida, ni la
  posición fija x=480/1440, ni el intercambio con L1.
- Un touchpad real sigue enviando sus dedos y su clic tal cual, como hoy.
- Se borran `CLICK_LEFT`, `CLICK_RIGHT`, `play_click`, `CLICK_LEAD_US/HOLD_US/TAIL_US` y la rama de
  "replay sin dedos" que convertía `BTN_TOUCHPAD` en clic izquierdo.
- Si dos fuentes del mismo botón están pulsadas a la vez, el botón sigue pulsado hasta soltarlas todas
  (es un OR, como el resto de botones).
- Si el recorrido de Tsukihime demuestra que usa posición del toque, se añadirá en una spec propia.

Tests: `tests/test_pad.c` pierde la parte de clics sintéticos (`:28-113` y `:150-164`) y gana una
comprobación de que `touchpad_left` y `touchpad_right` del archivo de pad dan `BTN_TOUCHPAD` sin toques
(`touch_count==0`); se conserva la parte del touchpad real (`:115-143`). `tests/test_pad_map.c` pasa a
afirmar que los tres nombres aceptan el mismo botón del host y que las únicas fuentes por defecto
(`Tab` y el clic del touchpad real) dan `BTN_TOUCHPAD`, y que `Backspace` y `Back` ya no pulsan nada.

**Resultado (2026-10-19, commit `2e8474d`).**

- Hecho como se describe arriba:
  - Fuera `CLICK_LEFT/RIGHT`, `play_click` y sus tiempos.
  - Los tres nombres dan `BTN_TOUCHPAD`.
  - Por defecto, `Tab` en el teclado y el clic real en el mando. `Backspace` y `Back/View` no tienen fuente.
  - El comportamiento queda en el comentario de cabecera de `runtime_pad.c`.
- Dos cambios que este paso no preveía, necesarios para el OR y para "tal cual, como hoy":
  - `apply_button_map` aplica juntas las entradas de un valor. De un mismo nombre cuenta la última,
    como antes, y nombres distintos del mismo botón se suman. Antes, un botón asignado perdía todas
    sus fuentes anteriores. Sin el cambio, `touchpad_left=X,touchpad_right=Y` (lo que escribe el
    launcher si se cambian los dos lados) dejaría solo `Y`.
  - El clic del touchpad real se lee junto a sus dedos, fuera del mapa del mando (que pasa de 15 a 13
    entradas). Así, asignar un nombre del touchpad en `GOW3_PAD_MAP` no lo quita; antes tampoco se podía.
- `pad-map-test` entra en `build.sh --test`. En CMake solo existe para Windows, así que en Linux no se
  ejecutaba.
- Tests. La parte de clics sintéticos de `test_pad.c` ocupaba `:28-113` y `:145-186`, no solo
  `:150-164`. En su lugar se comprueba que:
  - Los tres nombres en el archivo de pad dan `BTN_TOUCHPAD` sin toque y sin retardo.
  - Un clic real sin dedos da solo el botón.
  - `Back` no pulsa nada, tampoco con L1.
  - Con `GOW3_PAD_MAP=touchpad=misc1`, el clic real sigue funcionando y `misc1` también pulsa.

  `test_pad_map.c` cubre los alias que se suman, el mismo nombre dos veces y las fuentes por defecto.
- Rojo visto:
  - `pad-test`: el botón llegaba con retardo.
  - `pad-map-test`: `Back` seguía como fuente.
  - Con solo el cambio literal del plan, `pad-map-test` seguía en rojo: `touchpad=misc1` quitaba el clic real.
- Verde:
  - `build.sh --test` sale con 0.
  - `git grep -nE 'CLICK_LEFT|CLICK_RIGHT|play_click' -- src tests` no da resultados.
  - Python, igual que tras el paso 2: 254 ejecutados y 35 omitidos; solo fallan los 4 tests del launcher.
  - Arranque (`out/first-run-after-t3.log`): el mismo log y la misma parada que tras el paso 2.
- No se migran las grabaciones antiguas de `GOW3_PAD_RECORD` con los bits `0x40000000`/`0x80000000`:
  ahora llegan al juego tal cual.
- Para la limpieza del launcher: sigue mostrando `Backspace` y `back` como valores por defecto de
  "Touchpad right" y solo escribe los botones cambiados, así que quien no los cambie no tiene esa fuente.

### Paso 4 — Tests
Reescribir `tests/test_patches.py` con un **perfil sintético** definido en el propio test (un XML
mínimo, un `TitleID` ficticio y una tabla `GAMES` inyectada), para seguir cubriendo el motor de
parches y mods sin un juego concreto; deja de leer el XML de GoW3 y el JSON de sitios. Después, borrar
`tests/data/eboot_6e72_sites.json`. En `test_game_update.py`, `test_mods.py` y `test_save_backup.py`,
sustituir `CUSA01623`/`CUSA01715` por IDs ficticios (`CUSA99991`, `CUSA99992`; son datos de prueba, no
perfiles). Los `test_launcher_*.py` no se tocan. Listar en el cambio los tests borrados y reescritos.

**Resultado (2026-10-19, commit `f90d6ac`).**

- `test_patches.py` usa un perfil sintético: `CUSA99990`, un XML mínimo en un directorio temporal y
  `GAMES` inyectado con `mock`.
- Reescritos con ese perfil:
  - Los 2 tests de `ExternalPatchTests`. El de "el archivo integrado no es externo" ahora también cubre
    la exclusión por `GAMES`.
  - 4 de `GameProfileTests`.
  - `test_requirements_from_patch_notes`, que pasa de `SignatureTests` a `GameProfileTests`.
  - `test_profile_patches_compile`, que sustituye a `test_gow3_patches_compile`.
- Nuevo: `test_without_profile_nothing_applies`.
- Borrados:
  - `test_signatures_match_the_full_update_eboot`.
  - `test_gow3_signatures_in_the_reference_and_a_moved_build` y `test_gow3_skip_intro_original_bytes`,
    que se omitían sin `GOW3_GAME`.
  - `test_gow3_skip_intro_targets`.
  - `tests/data/eboot_6e72_sites.json`.
- `test_game_update.py`, `test_mods.py` y `test_save_backup.py` usan `CUSA99991` y `CUSA99992`.
- Python: 254 OK y 37 omitidos → **251 OK y 35 omitidos**.

### Paso 5 — Identidad
`probe.c`: título y serial vacíos por defecto, leídos siempre del `param.sfo`. Comprobación de juego
soportado:

- **Cuándo se comprueba:** siempre que se pase `--app0`, en cualquier modo (arranque normal,
  `--check-imports`, `--cpu-only`). Va antes de `runtime_file_configure` y de crear la GPU, para no abrir
  ventana ni montar nada de un juego ajeno. No se comprueba en `--vulkan-only` ni `--cpu-features` (no
  usan juego), ni cuando no hay `--app0` (así `tests/test_probe.py`, que arranca imágenes sintéticas sin
  juego, no cambia; hoy no usa `--app0`).
- **Qué se exige:** `TITLE_ID == CUSA47999` en `sce_sys/param.sfo`.
- **Si no coincide, o falta el `param.sfo` o el `TITLE_ID`:** sale con código **30** y el mensaje
  `STOP: unsupported title <TITLE_ID|desconocido>: this build only runs CUSA47999`. Una variable de
  entorno no la salta.
- El `CUSA47999` queda en **una sola constante** de `probe.c`.

`run.py` y `scripts/patches.py`: sin referencias a CUSA01623/CUSA01715/CUSA01720; la carpeta por defecto
pasa a ser `Tsukihime/CUSA47999` (o ninguna). `scripts/steam_shortcut.py` queda para el final junto con el
launcher.

## 5. Requisitos

- Ni hooks, ni parches, ni firmas de God of War III en el árbol (en el ámbito de la sección 3).
- Otro juego distinto de CUSA47999 se rechaza con mensaje claro y código de salida 30 (el roadmap lo exige).
- Lo genérico (loader, runtime, núcleo de shadPS4, upscalers) no cambia de comportamiento.
- El comportamiento del touchpad queda documentado en el código (paso 3).

## 6. Pruebas y criterios de aceptación

- [ ] `bash build.sh --test` y `python3 -m unittest discover -s tests` pasan. Se compara contra la
  línea base del paso 0 (Python: 254 OK, 37 omitidos; C: `out/baseline-c-1a.log`): la diferencia es
  exactamente la lista de tests borrados o reescritos, anotada en el cambio. Si falla algún test del
  launcher por una función que ya no existe, se anota y no se arregla aquí.
- [ ] **Comprobación por lista cerrada de símbolos**, con palabra completa:
  `git grep -nwE 'CUSA01623|CUSA01715|CUSA01720|God_of_War_III_Remastered|REFERENCE_SHA256|install_cheats|install_actor_watch|install_orb_multiplier|install_stat_multipliers|install_jump|GOW3_CHEAT_COUNT|GOW3_ORB_PICKUP_LENGTH|GOW3_ORB_PICKUP_OFFSET|GOW3_DAMAGE_HOOK_LENGTH' -- src gpu scripts tests run.py patches tools ':!tests/test_launcher_*.py'`
  sin resultados. Cada patrón da resultados hoy (comprobado el 2026-10-18: 14 en `probe.c`, 6 en
  `damage_hook.h`, etc.), así que se puede verificar que baja a 0. La lista se amplía con los nombres
  que el inventario del paso 1 encuentre. No se usan `orb` ni `damage` sueltos: coinciden con `LICENSE` y
  cabeceras de FSR. Una segunda búsqueda sin `-w` sobre los mismos directorios para `gow3_orbs`,
  `gow3_actors`, `orb_hook`, `actor_hook`, `cheat_hook`, `damage_hook`, `signature.h`, `Gow3Orbs` y
  `Gow3Actors` también debe dar 0.
- [ ] `git grep -nE 'CLICK_LEFT|CLICK_RIGHT|play_click' -- src tests` sin resultados.
- [ ] `out/first-run.log` antes y después: sin las diez líneas de hooks/cheats "code not found" ni
  las de parches; por lo demás, el mismo punto de parada.
- [ ] Un juego distinto de CUSA47999 (se simula con un `param.sfo` de prueba) se rechaza con el mensaje
  de la sección 4 y código 30; un `param.sfo` sin `TITLE_ID` y una carpeta sin `param.sfo` también.
  Hay un test en `tests/test_probe.py` que lo cubre, y otro que comprueba que sin `--app0` no se rechaza.
- [ ] `python3 -m unittest discover -s tests` incluye un test del cargador de parches con perfil sintético.
- [ ] `tests/test_pad.c` y `tests/test_pad_map.c` cubren que los tres nombres del touchpad dan
  `BTN_TOUCHPAD` sin toque sintético.

## 7. Riesgos

- Borrar un archivo compartido con lo genérico: se borra paso a paso, compilando y probando tras cada
  uno; la historia queda en git y en la rama `gow3`.
- `gow3_overlay.cpp` y `gow3_settings.*` mezclan opciones de GoW3 con las genéricas (resolución,
  ventana, upscalers). Hay que revisarlas línea a línea y no borrar de más.
- Al quitar el toque sintético, un juego que dependa de la posición del dedo no verá toque alguno. Para
  Tsukihime no se ha visto que lo use; se revisará en el recorrido y, si hace falta, tendrá su spec.
- Los `.ini` y los saves antiguos no se migran (ya está dicho en el roadmap). Un `gow3.ini` con claves
  `cheat_*` u `*_orb_multiplier` debe ignorarse sin error: se comprueba en `test_graphics_settings.cpp`.
- El launcher seguirá escribiendo claves de GoW3 (cheats, orbes) en `gow3.ini` hasta que se limpie al
  final; el cargador de ajustes las ignora.
