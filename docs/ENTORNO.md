# Entorno de desarrollo (Bazzite + distrobox Fedora)

Referencia de comandos para compilar, probar y depurar el port en la máquina de desarrollo
(Bazzite 44, inmutable). Todo se compila dentro de un contenedor **distrobox Fedora 44** llamado
`ps4dev`, con los paquetes `-devel` instalados con `dnf`; el sistema del host no se toca
(nada de `rpm-ostree`). Se eligió Fedora para que Mesa/RADV y glibc coincidan con el host.

`$HOME` se comparte con el contenedor, así que el repo
(`/var/home/wafflepc/Documentos/ps4_port`) y el dump (`Tsukihime/CUSA47999`) se ven en la misma
ruta dentro y fuera. `/dev/dri` (GPU) y `/dev/input` (mandos) también están disponibles.

## 1. Contenedor

Los comandos `distrobox` se lanzan **desde un terminal del host** (Konsole). El terminal integrado
de VS Code (Flatpak) no ve `distrobox`; ahí hay que anteponer `flatpak-spawn --host`.

```bash
# Crear (una vez)
distrobox create -n ps4dev -i registry.fedoraproject.org/fedora:44 \
  --additional-flags "--device /dev/input --security-opt seccomp=unconfined"

distrobox enter ps4dev                      # shell interactivo (entra en el directorio actual)
distrobox enter ps4dev -- bash build.sh     # un comando suelto, desde el repo
distrobox list                              # contenedores y su estado
distrobox stop ps4dev                       # pararlo
distrobox rm ps4dev                         # borrarlo (el repo y $HOME no se tocan)

# Desde el terminal de VS Code (Flatpak)
flatpak-spawn --host distrobox enter ps4dev -- bash build.sh

# Exportar un binario del contenedor al host (opcional, p. ej. vulkaninfo)
distrobox enter ps4dev -- distrobox-export --bin /usr/bin/vulkaninfo --export-path ~/.local/bin
```

`seccomp=unconfined` evita sorpresas con `ptrace`/`mmap` del loader (mapea todo por debajo de
1 TiB). Si se comprueba que no hace falta, se puede quitar al recrear el contenedor.

Atajo opcional en el shell del host (`~/.bashrc`):

```bash
alias ps4='distrobox enter ps4dev'
```

## 2. Dependencias (dentro del contenedor)

```bash
sudo dnf install -y gcc gcc-c++ clang make cmake ninja-build git pkgconf-pkg-config python3 \
  python3-tkinter binutils gdb \
  vulkan-headers vulkan-loader-devel mesa-vulkan-drivers vulkan-tools glslang \
  spirv-tools-devel spirv-headers-devel \
  SDL3-devel ffmpeg-free-devel boost-devel fmt-devel xxhash-devel \
  robin-map-devel zydis-devel VulkanMemoryAllocator-devel \
  libX11-devel libxcb-devel wayland-devel xorg-x11-proto-devel python3-pillow

# Repositorio RPM Fusion (free), para cambiar a ffmpeg completo cuando haga falta
sudo dnf install -y https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm
```

Los headers de Vulkan del sistema (Fedora 44: **1.4.341**) bastan desde el 2026-10-30. Antes hacía falta
1.4.344 en `/usr/local`, porque el FSR 4.1.1 usaba `VK_VALVE_shader_mixed_float_dot_product`. La Spec 7 borró el
FSR 4.1.1 y esa extensión. Un contenedor creado antes todavía tiene esa copia; no molesta, porque la 1.4.344 también
sirve. Para volver a los headers del sistema:

```bash
sudo rm -rf /usr/local/include/vulkan /usr/local/include/vk_video /usr/local/share/cmake/VulkanHeaders /usr/local/share/vulkan
rm -rf out/gpu     # la caché de CMake guarda la ruta de los headers
```

Notas:

- `magic_enum` y `miniz` **no** se instalan: Fedora trae versiones distintas y `gpu/CMakeLists.txt`
  usa los submódulos (`gpu/third_party/`) cuando no encuentra el paquete del sistema. `xbyak`,
  `sirit` e `imgui` también vienen como submódulo o vendorizados. (El submódulo `fsr-vulkan` se borró con el
  upscaler temporal, Spec 7 §12; un clon anterior puede conservar `.git/modules/gpu/third_party/fsr-vulkan`, que
  ya no se usa.)
- `zydis-devel` solo trae `zydis.pc`; `gpu/CMakeLists.txt` lo usa vía pkg-config cuando no hay
  paquete de CMake.
- `ffmpeg-free-devel` basta para compilar, pero no decodifica H.264 sin openh264 (falla
  `videodec-test`, y los `.mp4` de `image/movie/` lo necesitan). Después del repo de RPM Fusion,
  cambiar al ffmpeg completo:

  ```bash
  sudo dnf install -y --allowerasing ffmpeg ffmpeg-devel
  ffmpeg -hide_banner -decoders | grep ' h264 '    # debe aparecer el decodificador nativo
  ```

### Recrear el contenedor desde cero

```bash
distrobox stop ps4dev && distrobox rm ps4dev
# volver a 1. (create) y 2. (dnf); después rm -rf out/gpu y bash build.sh
```

## 3. Build y tests (dentro del contenedor, desde el repo)

```bash
bash build.sh                      # out/gpu/libtsukigpu.so + out/tsuki-probe (LTO activado)
bash build.sh --test               # además compila y ejecuta los tests en C
TSUKI_LTO=OFF bash build.sh         # sin LTO (menos RAM y enlace más rápido)
ninja -C out/gpu <target>          # un target concreto, p. ej. frame-capture-test
ninja -C out/gpu -j6 tsukigpu       # limitar hilos si falta RAM
tail -40 out/gpu-build.log         # log completo del build de la biblioteca GPU

python3 -m unittest discover -s tests                         # todos los tests Python
python3 -m unittest discover -s tests -p test_patches.py      # un archivo
cd tests && python3 -m unittest test_patches.ExternalPatchTests.test_selection_and_unsupported_lines  # un test

ninja -C out/gpu frame-capture-test && out/gpu/frame-capture-test   # un test C++
```

Línea base (2026-10-14): 10/10 tests en C pasan; Python 238 tests OK con 37 saltados (17 piden
`pip install iced-x86`, 16 son solo de Windows/Tk, 2 piden el dump del juego anterior, 2 son de fuentes GDI).

Tests que usan contenido del juego (sin versionar) escriben `SKIP: <motivo>` cuando les falta algo:
los clips de `python3 tools/extract_audio_clips.py --image Tsukihime/CUSA47999/image --out
out/audiodec-clips` (Audiodec y HEVAG) y, para `matches_vgmstream_cli` de `out/hevag-test`, un
`vgmstream-cli`, que Fedora no empaqueta. Se compila del mismo commit del que sale
`third_party/vgmstream_hevag` (`VENDOR.txt`), sin códecs opcionales, y el test lo busca en
`$VGMSTREAM_CLI`, `out/vgmstream-cli` y el `PATH`:

```bash
mkdir -p out/vgmstream-src && cd out/vgmstream-src
curl -L -o vgmstream.tar.gz https://codeload.github.com/vgmstream/vgmstream/tar.gz/db77f98f405a45ad08df98aa444cae3848f95e6c
tar xzf vgmstream.tar.gz
cmake -S vgmstream-db77f98f405a45ad08df98aa444cae3848f95e6c -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DUSE_MPEG=OFF -DUSE_VORBIS=OFF -DUSE_FFMPEG=OFF -DUSE_G7221=OFF -DUSE_G719=OFF -DUSE_ATRAC9=OFF \
    -DUSE_CELT=OFF -DUSE_SPEEX=OFF -DBUILD_V123=OFF -DBUILD_AUDACIOUS=OFF
ninja -C build vgmstream_cli
cd ../.. && ln -sf vgmstream-src/build/cli/vgmstream-cli out/vgmstream-cli
```

## 4. Loader y juego

```bash
out/tsuki-probe --cpu-features                 # extensiones de CPU (MOVBE/BMI1/LZCNT)
out/tsuki-probe --vulkan-only                  # comprueba Vulkan sin cargar el juego

# Preparar el eboot e inventariar los imports sin implementar (no arranca el juego)
python3 scripts/prepare.py Tsukihime/CUSA47999 --out out       # → out/boot.bin, out/analysis.json
python3 scripts/link_libc.py Tsukihime/CUSA47999 --out out
python3 scripts/link_modules.py Tsukihime/CUSA47999 --out out  # → out/boot-linked.bin
out/tsuki-probe out/boot-linked.bin --app0 Tsukihime/CUSA47999 --check-imports   # una línea MISSING por import; sale con 1 si falta alguno

# Ejecutar el juego completo (run.py hace todo el pipeline)
TSUKI_GAME_DIR=Tsukihime/CUSA47999 python3 run.py
TSUKI_GAME_DIR=Tsukihime/CUSA47999 python3 run.py --software   # lavapipe (sin GPU)
out/tsuki-probe out/boot-linked.bin --app0 Tsukihime/CUSA47999 --timeout 60   # watchdog: vuelca las pilas a los 60 s
```

Elegir GPU si alguna vez arranca en la iGPU: `MESA_VK_DEVICE_SELECT=list vulkaninfo` lista los
dispositivos y `MESA_VK_DEVICE_SELECT=<vendor:device>` fuerza uno.

## 5. Depuración

- `user/last_run.log` — log completo de la última ejecución desde el launcher.
- `STOP: first unsupported PS4 import: <nombre>` — la siguiente API que falta implementar.
- Los fallos del juego se reportan como offsets relativos a la imagen con backtrace por `rbp`;
  `python3 tools/got_import.py [--linked] <offset>` dice qué import está enlazado a un slot de la GOT
  (`--linked`: lee `out/boot-linked.bin`, que incluye `libc.prx`).
- `--timeout N` arma el watchdog; `kill -USR2 <tid>` vuelca la pila de un hilo.
- `TSUKI_FRAME_CAPTURE_KEY=1` + F11 — escribe la lista de pasos de un fotograma en texto
  (`frame_<fecha>_<n>.txt`); no es una imagen.
- Capturas PNG del fotograma presentado (sin el overlay de ImGui): F12 captura el siguiente, y
  `TSUKI_SCREENSHOT_AT=120,600` los fotogramas de presentación indicados (el primero es el 1). Van a
  `TSUKI_SCREENSHOT_DIR` (por defecto `out/baseline/`, sin versionar) como
  `frame_<NNNNNN>_<ancho>x<alto>.png`, y cada una deja `Screenshot: <ruta> (frame N)` en el log. El
  tamaño es el de la ventana: para comparar ejecuciones, usar siempre `TSUKI_FULLSCREEN=1`
  (1920×1080 en este monitor). Con `run.py`, `TSUKI_TIMEOUT=N` arma el watchdog.

  ```bash
  TSUKI_GAME_DIR=Tsukihime/CUSA47999 TSUKI_FULLSCREEN=1 TSUKI_TIMEOUT=40 \
    TSUKI_SCREENSHOT_AT=300,1500 python3 run.py > out/run-capturas.log 2>&1
  ```
- `tools/compare_captures.sh a.png b.png` compara dos capturas píxel a píxel (filtro `psnr` de
  ffmpeg, sin tolerancia): `idéntico` (sale 0) o `diferente: PSNR=<dB>` (sale 1). Un archivo que
  falta, que no es una imagen o de otro tamaño es un error (sale 2). Entre dos ejecuciones, el mismo
  número de fotograma da la misma imagen en el 2D del juego (el aviso de autoguardado, fotograma 300),
  pero no en los vídeos: AvPlayer sigue el reloj real.
- `tools/compare_route.sh a/ b/ frames.txt` compara dos carpetas de capturas según `frames.txt`: una línea por
  fotograma, `<N> exact`, `<N> mask <x> <y> <w> <h> […]` (esos recuadros en negro en las dos) o `<N> eye` (no se
  compara), y `#` para comentarios. Sale 0 si todo lo comparado es idéntico, 1 si algo difiere y 2 ante un error
  (captura que falta, línea mal formada, recuadro fuera de la imagen).
- `tools/route.sh record` / `tools/route.sh replay <nombre>` — el recorrido grabado de la Spec 6 (bloque A), todo en
  `out/route/`. Cada ejecución parte de la copia fija `out/route/save/` en un `out/route/user/` nuevo, con
  `out/route/tsuki.ini` (`async_shaders=0`), pantalla completa y sin ratón. `record` graba `pad.txt` y las capturas
  de F12, y escribe `frames.txt`; `replay` repite `pad.txt`, captura los fotogramas de `frames.txt` en
  `out/route/<nombre>/` y cierra el juego al acabar. Desde el host vuelve a entrar en el distrobox.
- Para ver qué teclas recibe el juego: `SDL_EVENT_LOGGING=1 SDL_LOGGING='*=info'` escribe cada evento
  de SDL en el log (`SDL EVENT: SDL_EVENT_KEY_DOWN …`).
- `TSUKI_PAD_FILE` (botones y ejes por guion), `TSUKI_PAD_RECORD=<archivo>` / `TSUKI_PAD_REPLAY=<archivo>`
  (graba y repite un recorrido). Las dos empiezan en la primera lectura del mando del juego, y F9 solo para la
  grabación. El reloj son las lecturas del juego, no los ms: el archivo empieza con la línea `reads` y sigue con
  `<lectura> <botones> <lx> <ly> <rx> <ry> <l2> <r2>`. Un archivo del formato antiguo (ms, sin cabecera) no se
  repite. La repetición acaba 30 lecturas después de la última línea (`Runtime: pad replay finished at read <r>`).
- `TSUKI_MOUSE=0` — el juego no ve ningún ratón; el recorrido no graba el ratón y el puntero movería su cursor.
- `gdb --args out/tsuki-probe out/boot-linked.bin --app0 Tsukihime/CUSA47999` — `gdb` viene en el
  bloque `dnf`.

## 6. Diagnóstico del entorno

```bash
vulkaninfo --summary          # debe listar la RX 6650 XT (RADV NAVI23) como GPU0
ls /dev/dri /dev/input        # GPU y dispositivos de entrada visibles en el contenedor
sysctl vm.mmap_min_addr       # 65536 en Bazzite
ulimit -a                     # memoria virtual sin límite (ulimit -v: unlimited)
nproc; free -h                # 12 hilos, ~14 GiB visibles
```

## 7. Mantenimiento

```bash
sudo dnf upgrade -y           # dentro del contenedor; no a mitad de una sesión de depuración
podman system df              # en el host: espacio usado por imágenes y contenedores
```

Tras actualizar el compilador o las bibliotecas, borrar `out/gpu` para forzar un build limpio.
