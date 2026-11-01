# Project Blue Moon

[English](README.md) | **Español**

Un port nativo para PC de **TSUKIHIME -A piece of blue glass moon-** (PlayStation 4, CUSA47999, versión 01.00)
para Linux y Windows. Necesita tu propio dump del juego: no se incluye ningún archivo del juego.

La parte de CPU es nativa: el ejecutable original del juego corre directamente en el procesador del PC (la PS4 usa
la misma arquitectura x86-64), y un runtime escrito para este port sustituye a las bibliotecas del sistema de la
PS4, así que no hay emulador de CPU. Los gráficos se traducen: el núcleo de vídeo de
[shadPS4](https://github.com/shadps4-emu/shadPS4) convierte los comandos y los shaders de la GPU de la PS4 a
Vulkan. En [Créditos](#créditos) están los proyectos de los que viene este.

> **No se incluye ningún archivo del juego.** Necesitas un dump de tu propia copia del juego.
> Este proyecto no está afiliado a TYPE-MOON, Aniplex ni Sony Interactive Entertainment.
> TSUKIHIME es una obra de TYPE-MOON; la edición de PlayStation 4 la publica Aniplex.

El avance se sigue en [docs/tsuki/ROADMAP.md](docs/tsuki/ROADMAP.md).

## Estado de la Alpha

Esto es una **Alpha**: el juego se puede jugar desde el principio hasta el **título del capítulo 1**, en inglés.

- Logos, el opening y los vídeos del prólogo (H.264), con sonido.
- Texto, fondos, sprites y transiciones.
- Audio: música, voces y efectos (ATRAC9 y HEVAG).
- Ratón, mando (DualSense, Xbox y otros, a través de SDL3) y teclado, a la vez.
- Guardar y cargar. El launcher hace una copia de seguridad de los saves antes de cada inicio.

Solo se ha probado el inglés. El **Game language** del launcher ofrece también el japonés y el chino, los otros
idiomas del juego, pero nadie los ha jugado con este port. Las rutas después del capítulo 1 todavía no se han
comprobado.

**Plataformas**
- **Linux:** probado en Bazzite 44 (basado en Fedora), KDE Plasma 6 en Wayland, AMD Radeon RX 6650 XT (RADV),
  Ryzen 5 7600.
- **Windows 10 y 11:** se compila con el mismo código y se ha probado en Windows. Si algo falla en tu PC,
  abre un issue con tu `last_run.log` (ver abajo).

## Requisitos

- Linux, o Windows 10 (1903 o más nuevo) / Windows 11, de 64 bits.
- Un procesador de 64 bits con AVX: Intel Sandy Bridge (2011) o más nuevo, AMD FX o más nuevo. En las CPU sin
  MOVBE, BMI1 o LZCNT (Sandy/Ivy Bridge, AMD FX) las instrucciones del juego que las usan se reescriben a stubs
  emulados y el log muestra una línea `ISA:`; la velocidad en esas CPU no se ha medido. Las CPU más antiguas se
  paran al empezar con una línea `STOP: this CPU lacks` en el log.
- Una tarjeta gráfica con **Vulkan 1.3** y el driver al día.
- Tu dump de TSUKIHIME -A piece of blue glass moon- (**CUSA47999, versión 01.00**), desencriptado: una carpeta
  que contiene `eboot.bin`, `sce_module` y `sce_sys`. El launcher comprueba el serial y rechaza cualquier otro
  juego.

## Descargar y jugar

Descarga el paquete de tu sistema desde la [página de Releases](https://github.com/Waffle631/Project-Blue-Moon/releases).

### Windows

1. Descarga `project-blue-moon-windows.zip` y descomprímelo en una carpeta donde puedas escribir, por ejemplo
   `C:\Games\project-blue-moon-windows`. No lo ejecutes desde dentro del zip y evita `Program Files`: los saves y
   los ajustes se guardan junto al programa.
2. Abre **`Project Blue Moon.exe`**. Si Windows muestra «Windows protegió su PC», pulsa **Más información** y
   luego **Ejecutar de todas formas**: el programa no está firmado digitalmente.
3. Pulsa **Choose the game folder**. Te lleva a la página **Game**: junto a **Game folder**, pulsa **Browse…** y
   elige la carpeta con `eboot.bin`. Una carpeta llamada `CUSA47999` al lado de la carpeta descomprimida se
   encuentra sola.
4. De vuelta en **Home**, aparecen la portada del juego y «CUSA47999 · version 01.00». Pulsa **PLAY**. El primer
   inicio tarda un poco más mientras se prepara la imagen del juego.

**La próxima vez:** **`Play Project Blue Moon.exe`** arranca el juego directamente con tus ajustes guardados.
En **Advanced** hay botones para crear un acceso directo en el escritorio y para añadir el juego a Steam (cierra
Steam antes).

**Archivos:** los saves, la caché de shaders y `last_run.log` están en la carpeta `user`, y los ajustes del juego
en `tsuki.ini`, los dos junto a `Project Blue Moon.exe`. Consérvalos al actualizar. Los ajustes del propio
launcher están en `%APPDATA%\tsuki-launcher`.

### Linux

1. Descarga `project-blue-moon-x86_64.AppImage`, hazlo ejecutable (`chmod +x`) y ábrelo.
2. Pulsa **Choose the game folder**, elige la carpeta con `eboot.bin` y pulsa **PLAY**.

`project-blue-moon-x86_64.AppImage --play` arranca el juego con los ajustes guardados y sin la ventana del
launcher.

- **Archivos:** los saves, los ajustes, la caché de shaders y el log están en `~/.local/share/tsuki`
  (`last_run.log` está en su carpeta `user`); los ajustes del propio launcher, en `~/.config/tsuki-launcher`.
- **Entrada de menú:** el AppImage escribe `~/.local/share/applications/tsuki.desktop` al arrancar, para que el
  launcher y la ventana del juego muestren su icono; **Advanced → Linux** la desactiva y borra la entrada.
- **Advanced → Linux** también tiene el sistema de ventanas (automático, Wayland o X11), GameMode y MangoHud.
  Solo se pueden activar las herramientas instaladas en el sistema.
- **Steam:** **Advanced → Add to Steam** añade Project Blue Moon como juego que no es de Steam, con sus imágenes
  hechas a partir del dump. Cierra Steam antes (reescribe sus accesos directos al salir).

### Antes del primer inicio

Sin save, el juego empieza con el vídeo del prólogo (unos 6 minutos y medio), que no se salta con los botones.
Después del primer guardado, el juego arranca en el menú de título.

### Si algo falla

- Pantalla negra al empezar: **Advanced → Clear shader cache** y vuelve a empezar.
- El juego no abre o se para: mira la sección **Log** de **Advanced** y adjunta `last_run.log` a tu informe en la
  [página de Issues](https://github.com/Waffle631/Project-Blue-Moon/issues).

## Mientras juegas

- **Insert** en el teclado, o **R3 + L2** en el mando, abre el menú del port, con tres pestañas:
  - **Image:** modo de pantalla, VSync, escalado FSR 1 y nitidez RCAS, con los tamaños de la imagen del juego y
    de la ventana.
  - **Performance:** fotogramas en cola y compilación de shaders en segundo plano.
  - **Overlay:** un overlay de rendimiento con FPS, tiempo de fotograma, GPU (nombre y uso), CPU, RAM y VRAM, cada
    uno opcional, con tamaño de letra, esquina, opacidad del fondo y disposición. Los números son el uso del propio
    juego, no el de todo el sistema. **Graphics → Show performance overlay** en el launcher también lo activa.
- En el menú: L1/R1 o Q/E cambian de pestaña.
- **Ratón:** el cursor del propio juego sigue al puntero.

### Controles de teclado

Valores por defecto; se cambian en **Game → Controls** del launcher.

| Tecla | Botón |
|---|---|
| WASD | stick izquierdo |
| Flechas | stick derecho |
| Espacio | Cruz |
| Shift izquierdo | Círculo |
| E | Cuadrado |
| Q | Triángulo |
| 1 y 3 | L1 y R1 |
| R y F | L2 y R2 |
| Z y C | L3 y R3 |
| I, K, J, L | cruceta |
| Enter | Options |
| Tab | clic del touchpad |
| Insert | el menú del port (R3+L2 en el mando) |

## Compilar desde el código

La rama principal es `tsuki/main`. Clona con los submódulos:

```bash
git clone --recursive https://github.com/Waffle631/Project-Blue-Moon
```

### Linux

Dependencias a través de pkg-config: Vulkan, SDL3 y FFmpeg (con un decodificador H.264: los vídeos lo necesitan),
más CMake, Ninja, clang y Python 3 con Tk y Pillow. La lista completa de paquetes de Fedora está en
[docs/ENTORNO.md](docs/ENTORNO.md); en NixOS los da `shell.nix`.

```bash
bash build.sh                                         # out/gpu/libtsukigpu.so y out/tsuki-probe
TSUKI_GAME_DIR=/ruta/a/CUSA47999 python3 run.py       # el juego, sin el launcher
python3 launcher/tsuki_launcher_win.py                # el launcher
bash packaging/linux/build.sh                         # dist/project-blue-moon-x86_64.AppImage
```

El AppImage necesita PyInstaller, iced-x86, patchelf y `file` ([docs/ENTORNO.md](docs/ENTORNO.md) §9).

### Windows

En la shell **MSYS2 CLANG64**, con los paquetes de
[packaging/windows/README.md](packaging/windows/README.md):

```bash
bash build.sh                       # out/tsuki-probe.exe, out/tsuki-gpu-capabilities.exe, out/tsuki-play.exe
bash packaging/windows/package.sh   # dist/project-blue-moon-windows/ y su .zip
```

### Windows, compilado desde Linux

`bash build.sh --windows` descarga [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) y los paquetes MSYS2
CLANG64 en `out/cross/` (versiones fijas con su SHA256) y compila los mismos tres `.exe`.
`bash packaging/windows/package.sh` hace después el `.zip` también en Linux: instala un Python de Windows en un
prefijo de Wine y congela allí el launcher. Detalles en [packaging/windows/README.md](packaging/windows/README.md)
y en [docs/ENTORNO.md](docs/ENTORNO.md) §8.

## Tests

```bash
python3 -m unittest discover -s tests
bash build.sh --test        # Linux: los tests de C y después tools/check_rename.sh
ninja -C out/gpu graphics-settings-test && out/gpu/graphics-settings-test
```

Los tests de C++ son targets de [gpu/CMakeLists.txt](gpu/CMakeLists.txt). Para comprobar que todas las funciones
del sistema que usa el juego están implementadas, sin arrancarlo:

```bash
out/tsuki-probe out/boot-linked.bin --app0 /ruta/a/CUSA47999 --check-imports
```

Lista tres funciones a propósito (`scePthreadCancel`, `sceKernelGetModuleInfoForUnwind`,
`sceKernelInternalMemoryGetModuleSegmentInfo`): pararían el juego con una línea `STOP:` si las llamara, y en la
ruta probada no las ha llamado.

## Cómo funciona

| Parte | Dónde | Qué hace |
|---|---|---|
| Preparación | `scripts/` | Lee `eboot.bin`, aplica sus relocalizaciones y enlaza en una sola imagen la `libc` y la `libSceFios2` del propio juego |
| Loader | `src/probe.c` | Carga la imagen por debajo de 1 TiB, resuelve cada import de la PS4 y arranca el código original del juego. Un import sin implementación para el juego con una línea `STOP:` |
| Runtime | `src/runtime_*.c` | Reimplementa las funciones del sistema de la PS4: memoria, hilos, archivos, decodificación de audio, mando, ratón, saves y servicios, como una consola sin conexión y de un solo usuario |
| GPU | `gpu/shadps4/` | El núcleo de vídeo de shadPS4: comandos y shaders de la GPU a Vulkan, y el reproductor de vídeo (`sceAvPlayer`) |
| Menú del juego | `gpu/shim/tsuki_overlay.cpp` | El menú y el overlay de rendimiento con Dear ImGui; ajustes en `tsuki.ini` |
| Launcher | `launcher/tsuki_launcher_win.py` | Carpeta del juego, ajustes, saves, controles y el botón de jugar |

Más documentación: [decodificación de audio](docs/audiodec.md), [reproductor de vídeo](docs/avplayer.md),
[entorno de compilación](docs/ENTORNO.md) y el [roadmap](docs/tsuki/ROADMAP.md).

## Créditos

- **deadinside28**: [bloodborne_pc](https://github.com/deadinside28/bloodborne_pc), el port nativo original.
- **Supermedo** (Mohammed Albarghouthi): [bloodborne_pc para Windows](https://github.com/Supermedo/bloodborne_pc).
- **Felipe Prado**: [el port de God of War III Remastered](https://github.com/FelipePrado0/gow3_pc), basado en el de Supermedo, del que se adaptó este port. <!-- rename-check: ok -->
- **El equipo de shadPS4**: el núcleo de vídeo de [shadPS4](https://github.com/shadps4-emu/shadPS4), que hace toda
  la traducción de GPU y shaders y la reproducción de vídeo.
- **cuesta4**: los arreglos de render de shadPS4 de [la rama `eltutz`](https://github.com/cuesta4/shadPS4/tree/eltutz)
  (reescritura de blends de GNM, estado de depth/stencil, aliasing de imágenes), que el núcleo incluido conserva.
- [LibAtrac9](https://github.com/Thealexbarney/LibAtrac9) (audio ATRAC9) y [vgmstream](https://github.com/vgmstream/vgmstream)
  (el decodificador HEVAG de `third_party/vgmstream_hevag`).

También se usan: [FFmpeg](https://ffmpeg.org), [SDL3](https://github.com/libsdl-org/SDL),
[Dear ImGui](https://github.com/ocornut/imgui), [sirit](https://github.com/shadps4-emu/sirit),
[magic_enum](https://github.com/Neargye/magic_enum), [miniz](https://github.com/richgel999/miniz),
[xbyak](https://github.com/herumi/xbyak),
[Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator),
[MSYS2](https://www.msys2.org), [LLVM](https://llvm.org), [llvm-mingw](https://github.com/mstorsjo/llvm-mingw),
[PyInstaller](https://pyinstaller.org) y [Pillow](https://python-pillow.org).

## Licencia

GNU GPL v2 o posterior ([LICENSE](LICENSE)). Los componentes de terceros conservan sus propias licencias.
