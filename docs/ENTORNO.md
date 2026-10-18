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

Fedora 44 trae Vulkan-Headers **1.4.341**, pero el shadPS4 vendorizado usa
`VK_VALVE_shader_mixed_float_dot_product`, que llega en **1.4.344**. Se instalan esos headers
(incluye `vulkan.hpp`) en `/usr/local` dentro del contenedor, que tiene prioridad sobre
`/usr/include`:

```bash
d=$(mktemp -d)
git clone --depth 1 --branch v1.4.344 https://github.com/KhronosGroup/Vulkan-Headers.git $d/vh
cmake -S $d/vh -B $d/build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr/local
sudo cmake --install $d/build && rm -rf $d
rm -f out/gpu/CMakeCache.txt     # si ya se había configurado con los headers del sistema
```

Cuando Fedora publique `vulkan-headers` ≥ 1.4.344 se puede borrar la copia de `/usr/local`
(`sudo rm -rf /usr/local/include/vulkan /usr/local/include/vk_video /usr/local/share/cmake/VulkanHeaders /usr/local/share/vulkan`).

Notas:

- `magic_enum` y `miniz` **no** se instalan: Fedora trae versiones distintas y `gpu/CMakeLists.txt`
  usa los submódulos (`gpu/third_party/`) cuando no encuentra el paquete del sistema. `xbyak`,
  `sirit`, `imgui` y `fsr-vulkan` también vienen como submódulo o vendorizados.
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
# volver a 1. (create) y 2. (dnf + Vulkan-Headers); después rm -rf out/gpu y bash build.sh
```

## 3. Build y tests (dentro del contenedor, desde el repo)

```bash
bash build.sh                      # out/gpu/libgow3gpu.so + out/gow3-probe (LTO activado)
bash build.sh --test               # además compila y ejecuta los tests en C
GOW3_LTO=OFF bash build.sh         # sin LTO (menos RAM y enlace más rápido)
ninja -C out/gpu <target>          # un target concreto, p. ej. frame-capture-test
ninja -C out/gpu -j6 gow3gpu       # limitar hilos si falta RAM
tail -40 out/gpu-build.log         # log completo del build de la biblioteca GPU

python3 -m unittest discover -s tests                         # todos los tests Python
python3 -m unittest discover -s tests -p test_patches.py      # un archivo
cd tests && python3 -m unittest test_patches.ExternalPatchTests.test_selection_and_unsupported_lines  # un test

ninja -C out/gpu frame-capture-test && out/gpu/frame-capture-test   # un test C++
```

Línea base (2026-10-14): 10/10 tests en C pasan; Python 238 tests OK con 37 saltados (17 piden
`pip install iced-x86`, 16 son solo de Windows/Tk, 2 piden el dump de GoW3, 2 son de fuentes GDI).

## 4. Loader y juego

```bash
out/gow3-probe --cpu-features                 # extensiones de CPU (MOVBE/BMI1/LZCNT)
out/gow3-probe --vulkan-only                  # comprueba Vulkan sin cargar el juego

# Preparar el eboot e inventariar los imports sin implementar (no arranca el juego)
python3 scripts/prepare.py Tsukihime/CUSA47999 --out out       # → out/boot.bin, out/analysis.json
python3 scripts/link_libc.py Tsukihime/CUSA47999 --out out
python3 scripts/link_modules.py Tsukihime/CUSA47999 --out out  # → out/boot-linked.bin
out/gow3-probe out/boot-linked.bin --app0 Tsukihime/CUSA47999 --check-imports   # una línea MISSING por import; sale con 1 si falta alguno

# Ejecutar el juego completo (run.py hace todo el pipeline)
GOW3_GAME_DIR=Tsukihime/CUSA47999 python3 run.py
GOW3_GAME_DIR=Tsukihime/CUSA47999 python3 run.py --software   # lavapipe (sin GPU)
out/gow3-probe out/boot-linked.bin --app0 Tsukihime/CUSA47999 --timeout 60   # watchdog: vuelca las pilas a los 60 s
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
- `GOW3_FRAME_CAPTURE_KEY=1` + F11 — escribe la lista de pasos de un fotograma en texto
  (`frame_<fecha>_<n>.txt`); no es una imagen.
- Capturas PNG del fotograma presentado (sin el overlay de ImGui): F12 captura el siguiente, y
  `GOW3_SCREENSHOT_AT=120,600` los fotogramas de presentación indicados (el primero es el 1). Van a
  `GOW3_SCREENSHOT_DIR` (por defecto `out/baseline/`, sin versionar) como
  `frame_<NNNNNN>_<ancho>x<alto>.png`, y cada una deja `Screenshot: <ruta> (frame N)` en el log. El
  tamaño es el de la ventana: para comparar ejecuciones, usar siempre `GOW3_FULLSCREEN=1`
  (1920×1080 en este monitor). Con `run.py`, `GOW3_TIMEOUT=N` arma el watchdog.

  ```bash
  GOW3_GAME_DIR=Tsukihime/CUSA47999 GOW3_FULLSCREEN=1 GOW3_TIMEOUT=40 \
    GOW3_SCREENSHOT_AT=300,1500 python3 run.py > out/run-capturas.log 2>&1
  ```
- `tools/compare_captures.sh a.png b.png` compara dos capturas píxel a píxel (filtro `psnr` de
  ffmpeg, sin tolerancia): `idéntico` (sale 0) o `diferente: PSNR=<dB>` (sale 1). Un archivo que
  falta, que no es una imagen o de otro tamaño es un error (sale 2).
- `GOW3_PAD_FILE`, `GOW3_PAD_RECORD` / `GOW3_PAD_REPLAY` — entrada de mando por guion.
- `gdb --args out/gow3-probe out/boot-linked.bin --app0 Tsukihime/CUSA47999` — `gdb` viene en el
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
