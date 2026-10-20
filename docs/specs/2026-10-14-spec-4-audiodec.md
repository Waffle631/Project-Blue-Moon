# Spec 4 — Audio: ATRAC9 por Audiodec (4a) y HEVAG por AudiodecCpu (4b)

Fecha: 2026-10-14, **replanteada el 2026-10-17** con los datos de la fase 2A de la Spec 2 y del
dump. Estado: **borrador para aprobación** (el usuario aprobó la dirección del replanteo en el chat
el 2026-10-17). **En curso desde el 2026-10-19:** el plan de implementación está aprobado; hechas la
tarea 1 (clips de referencia, `07800f5`), la 2 (fase A estática, `docs/audiodec.md`, `084d16a`) y la 3
(las seis funciones y la tabla de decodificadores, `731f3b6`). Roadmap:
`docs/tsuki/ROADMAP.md`, sección "Spec 4".

Marcas: **[V]** comprobado (con el comando, la ruta o el log); **[H]** hipótesis que se confirma en
la fase A; **[D]** decisión del usuario.

## 1. Objetivo

Que el audio del juego suene sin chirridos ni cortes, **en este orden de prioridad [D]
(2026-10-17): música, voces, efectos**.

Casi todo el audio es ATRAC9, y LibAtrac9 ya está en el repo. Solo 19 archivos de efectos son HEVAG.
Por eso la spec tiene dos partes que se hacen en orden:

- **4a — ATRAC9 por `libSceAudiodec`:** música (`bgm`), voces (`voice`) y efectos `vse`.
- **4b — HEVAG por `libSceAudiodecCpu`:** los efectos de `vag.mrg`.

**Sin tope de tiempo [D] (2026-10-14):** el proyecto es un experimento y un hobby; si la investigación
se alarga, se alarga.

Fuera de alcance: el audio de los vídeos (`image/movie/*.mp4`), que va por AvPlayer (Spec 5).

## 2. Hechos comprobados

### 2.1 Imports

**[V]** El eboot importa nueve funciones de Audiodec (6 de `libSceAudiodec`, 3 de `libSceAudiodecCpu`).
El juego no importa AJM. `libSceAudiodec.prx` y `libSceAudiodecCpu.prx` no están en `sce_module/`:
son módulos del sistema, que el juego carga con `sceSysmoduleLoadModule` (`src/runtime_content.c`).

| NID | Nombre | Parte | Estado |
|---|---|---|---|
| `VjhsmxpcezI` | `sceAudiodecInitLibrary` | 4a | hecha (`731f3b6`): 0; la segunda vez, `ALREADY_INITIALIZED` |
| `h5jSB2QIDV0` | `sceAudiodecTermLibrary` | 4a | hecha (`731f3b6`): libera todo; los ids anteriores dan error |
| `O3f1sLMWRvs` | `sceAudiodecCreateDecoder` | 4a | hecha (`731f3b6`): valida el bloque, crea el handle de LibAtrac9 y escribe bsi `+4` |
| `Tp+ZEy69mLk` | `sceAudiodecDeleteDecoder` | 4a | hecha (`731f3b6`) |
| `KHXHMDLkILw` | `sceAudiodecDecode` | 4a | resuelve; error provisional `0x807F0001` hasta la tarea 4 |
| `6Vf9WTLDoss` | `sceAudiodecClearContext` | 4a | resuelve; error provisional `0x807F0001` hasta la tarea 4 |
| `ktD2w3D4G2U` | `sceAudiodecCpuQueryMemSize` | 4b | falta |
| `hdFsxo3MFu8` | `sceAudiodecCpuInitDecoder` | 4b | falta |
| `lSVTiWV5wLc` | `sceAudiodecCpuDecode` | 4b | falta |

**[V]** Sitios de llamada en el eboot (2026-10-17, desensamblado de `out/boot-linked.bin`, todas las
llamadas directas a cada PLT):

- `libSceAudiodec`:
  - `InitLibrary` en `+0x10e20c`;
  - `TermLibrary` en `+0x10df70` y, como `jmp`, en `+0x10e864` *(2026-10-20: el segundo sitio salió
    en la revisión de `docs/audiodec.md`; esta lista solo buscó `call`)*;
  - `CreateDecoder` en `+0x113172`;
  - `Decode` en `+0x113398`;
  - `ClearContext` en `+0x11353f`;
  - `DeleteDecoder` en `+0x112d28` y `+0x1136ff`.
- `libSceAudiodecCpu`:
  - `QueryMemSize` en `+0x115788`;
  - `InitDecoder` en `+0x115811`;
  - `Decode` en `+0x11590c`.

Todo el ATRAC9 pasa por una sola rutina de preparación (2.5) y una sola de decodificación.

### 2.2 El audio del juego (dump, 2026-10-17)

**[V]** Formato de los archivos empaquetados: cada entrada de un `.hed` ocupa 8 bytes,
`{u32 sector; u16 sectores; u16 tamaño & 0xffff}`, con sectores de 0x800 bytes. Los 4 bits altos de
`sector` son los bits 16–19 del número de sector: por ejemplo, la entrada 31 de `bgm.hed` acaba en el
sector `0x10581` y la 32 vale `0x1_0000581`. `voice.hed` tiene otra forma, todavía sin descifrar.

**[V]** (2026-10-19, `tools/extract_audio_clips.py`) Más detalles del formato:
- El tercer campo son los 16 bits bajos del tamaño real: `riff_size + 8` en los RIFF y
  `0x30 + datos` en los `VAGp` (por ejemplo, `0xb4a8` para el RIFF de `0x4eb4a8` bytes de la entrada 1
  de `bgm`).
- Las entradas se encadenan sin huecos (sector + sectores = sector de la siguiente) hasta el final
  del `.mrg`.
- `vse.hed` tiene 1329 entradas y `vag.hed` 21. En las dos, las dos últimas son centinelas con los
  8 bytes a `0xFF`, sin datos detrás. `bgm.hed` no tiene centinelas.
- `vag.hed` tiene la misma forma que `bgm.hed`. Su entrada 0 se llama `dummy` y son 1984 bytes de
  silencio.

**[V]** Inventario (todas las entradas de `bgm.hed` y `vse.hed`, todas las cabeceras `VAGp` de
`vag.mrg` y los primeros 200 MB de `voice.mrg`):

| Audio | Archivo | Formato | Entradas | Parte |
|---|---|---|---|---|
| Música | `bgm.mrg` (536 MB) | RIFF ATRAC9, estéreo, 48 kHz | 132 de 132 | 4a |
| Voces | `voice.mrg` (1,3 GB) | RIFF ATRAC9, mono, 48 kHz, comprimido con MZX (`LV`…`MZX0`) | 3242 en la muestra | 4a |
| Efectos | `vse.mrg` (138 MB) | RIFF ATRAC9: 1316 estéreo y 11 mono, 48 kHz | 1327 de 1327 | 4a |
| Efectos | `vag.mrg` (1,8 MB) | `VAGp` versión `0x00020001` = **HEVAG**, 48 kHz, mono, en parejas `moon_seNNNNN_L`/`_R` (más una entrada `dummy`) | 19 cabeceras | 4b |

Los RIFF ATRAC9 son `WAVE_FORMAT_EXTENSIBLE` (`0xFFFE`) con el GUID
`47E142D2-36BA-4D8D-88FC-61654F8C836C` y llevan 4 bytes de configuración ATRAC9 en `+0x40`, que
empiezan por la marca `0xFE`. **[V]** (2026-10-19) Hay cinco combinaciones de configuración y tamaño
de bloque (`+0x20`), todas a 48 kHz:

| Configuración | Bloque | Canales | Entradas | Primera |
|---|---|---|---|---|
| `fe740ff0` | 512 | 2 | 131 de `bgm` | `bgm` 1 |
| `fe7409f0` | 320 | 2 | 1 de `bgm` | `bgm` 0 |
| `fe740bf0` | 384 | 2 | 1316 de `vse` | `vse` 1 |
| `fe700bf0` | 384 | 1 | 9 de `vse` | `vse` 775 |
| `fe7004f0` | 160 | 1 | 2 de `vse` | `vse` 0 | `se_nam.mrg` es un
archivo `mrgd00` de nombres (`S00`, `S01`…), no audio. **Ningún archivo de audio es MP3 ni AAC**, así
que FFmpeg no hace falta en esta spec.

Que `vse` sean efectos es **[H]**: se deduce del nombre.

### 2.3 Lo que hace el juego (fase 2A de la Spec 2, `out/run-2a-3.log` … `run-2a-5.log`)

- **[V]** Al arrancar carga los módulos del sistema `0x88`, `0xbd`, `0xe1`, `0xa5`, `0x8a`, `0xa4`,
  `0xa0`, `0xad`, `0xa9`, `0x9c`, `0x85`, `0x81`, `0xd3` y `0x83`. Que `0x88` sea Audiodec es **[H]**
  (valor de la tabla pública de módulos).
- **[V]** Abre dos puertos de `AudioOut` (2 y 8 canales; float, 1024 tramas), cada uno con un hilo
  `waveout thread` (prioridad 715).
- **[V]** **Llama a `sceAudiodecInitLibrary(1)` al arrancar, antes de cualquier draw** (eboot
  `+0x10e20c`) e ignora el valor devuelto: justo después reserva un objeto decodificador de 0x30
  bytes (vtable en `0x4324b0`).
- **[V]** Después crea **32 hilos `at9 thread`** (prioridad 717), cada uno con dos event flags
  (`at9 event` y `at9 thread`, attr `0x22`). Abre un tercer puerto (2 canales, float, **256 tramas**) y
  crea **`hevag decode thread`** y **`hevag waveout thread`** (prioridad 716).
- **[H]** ATRAC9 (`InitLibrary(1)`, los hilos `at9`) va por `libSceAudiodec` y HEVAG (el hilo
  `hevag`) por `libSceAudiodecCpu`. Hasta el último `STOP:` de la fase 2A, el juego aún no había
  llamado a `CreateDecoder` ni a ninguna función de `AudiodecCpu`.
- **[H]** El juego descomprime él mismo el MZX de las voces antes de pasarlas a Audiodec.

### 2.4 Decodificadores y referencias

- **[V]** **ATRAC9:** LibAtrac9 (`third_party/LibAtrac9`, `out/libatrac9.a`), con
  `Atrac9InitDecoder(handle, config[4])`, `Atrac9Decode(handle, in, pcm16, &used)` y
  `Atrac9GetCodecInfo`. `src/runtime_ajm.c` ya lo usa con la API AJM de GoW3: es la plantilla para
  guardar el estado del decodificador, medir tramas y devolver PCM.
- **[V]** **HEVAG:** vgmstream (`src/coding/psv_decoder.c`, `decode_hevag`, revisado en el commit
  `db77f98` del 2026-10-16). Usa tramas de 16 bytes con 28 muestras por canal y dos bytes de
  cabecera: un índice de tabla de 7 bits repartido entre los dos, el desplazamiento y unas banderas
  compatibles con el VAG clásico. Tiene una tabla de 128×4 coeficientes y 4 muestras de historia, y
  decodifica cada canal por separado. El propio código avisa de
  que se obtuvo por ingeniería inversa y puede tener diferencias de redondeo con el de Sony. Licencia
  **tipo ISC** (`COPYING`): se puede copiar y modificar conservando el aviso de copyright, y es
  compatible con la GPL-2.0 de este repo. vgmstream detecta HEVAG por la versión `0x00020001` o
  `0x00030000` de `VAGp` (`src/meta/vag.c`), con los datos en `+0x30`.
- **[V]** El shadPS4 fusionado no tiene HLE de Audiodec: carga el firmware real. Hay un **PR abierto,
  el #5289** (kiwakiwaa, 2026-10-06), que implementa `libSceAudiodecCpu` reenviando a los módulos
  de códec del firmware. Su autor lo probó con CUSA47999 y dice que HEVAG es el códec que usa
  Tsukihime. Está escribiendo un decodificador HEVAG en HLE. Datos del diff, **[H] hasta contrastarlos
  con el juego**:
  - ids de códec: `M4Aac 0x1001`, `Dts 0x1002`, `LpcmBd2 0x1003`, `LpcmDvd2 0x1004`, `DtsHdMa 0x1005`,
    `DtsHdLbr 0x1006`, `Ddp 0x1007`, **`Hevag 0x1008`**, `Alac2 0x1009`, `Flac2 0x100A`;
  - descriptor de memoria de 0x18 bytes, el mismo para el recurso, el AU y el PCM:
    `{u32 this_size; u32 reserved; void *data /*+0x8*/; u32 data_size /*+0x10*/; u32 reserved2}`.
    `QueryMemSize` escribe en `data_size` el tamaño de la memoria de trabajo, y `Decode` cambia la
    capacidad del AU y del PCM por los bytes consumidos y producidos;
  - controles: `QueryCtrl {param}` (8 bytes), `InitCtrl {param, bsi_info}` (0x10),
    `DecodeCtrl {param, bsi_info, au_info*, pcm_item*}` (0x20);
  - firmas `QueryMemSize/InitDecoder/Decode(ctrl, resource, codec_id)`;
  - errores `0x80F70000` (`API_FAIL`) … `0x80F70015` (`INVALID_WORK_MEM_SIZE`), por ejemplo
    `INVALID_TYPE 0x80F70001` y `ARG 0x80F70002`.
- **[V]** No hay ninguna fuente pública de las estructuras ni de los errores de `libSceAudiodec` de PS4.
  La respuesta provisional usa `0x807F0001` (`INVALID_TYPE` del `libSceAudiodec` de Vita, vitasdk), que
  para PS4 es **[H]**.
  *(2026-10-20, tarea 3: el runtime usa los de `psp2/audiodec.h` de vitasdk, todos **[H]** para PS4:
  `INVALID_INIT_PARAM 0x807F0002` (configuración), `ALREADY_INITIALIZED 0x807F0003`, `OUT_OF_MEMORY
  0x807F0004`, `NOT_INITIALIZED 0x807F0005`, `ALL_HANDLES_IN_USE 0x807F0007`, `INVALID_PTR 0x807F0008`,
  `INVALID_HANDLE 0x807F0009`, `INVALID_WORD_LENGTH 0x807F000C` e `INVALID_SIZE 0x807F000D`. El juego
  no distingue códigos, `docs/audiodec.md` 1.6.)*
- **[V]** `GOW3_AUDIO_TRACE` solo traza aperturas bajo `sound/` (`src/runtime_file.c`), una ruta de
  GoW3: no sirve para Tsukihime. La Spec 6 lo generaliza.

### 2.5 Primer `CreateDecoder`: al final del prólogo (2026-10-17, `out/run-prologo.log`)

Ejecución sin watchdog, con la partida de la iteración 9 de la Spec 2. El juego reproduce el vídeo del
prólogo y, según el usuario, el `STOP:` llega después del vídeo:

```
STOP: first unsupported PS4 import: O3f1sLMWRvs#libSceAudiodec (index 123)
Caller return offset: 0x113177; first argument: 0x1006eb2048
```

No se implementó (decisión del usuario, 2026-10-17: solo documentar). Todo lo que sigue sale del
desensamblado del sitio de llamada; el trazador de la fase A tiene que confirmarlo.

- **[V] Rutina de preparación** (`+0x1130b0`), llamada desde `+0x11276b` y `+0x1128ac`, dos objetos
  de flujo distintos *(2026-10-20: `+0x1128ac` está en `+0x112870`, código muerto; solo cuenta
  `+0x11276b`, `docs/audiodec.md` 1.1)*:
  - Recibe la cabecera RIFF ATRAC9 de la pista (2.2).
  - Solo acepta 12000, 24000 y 48000 Hz (RIFF `+0x18`), con 64, 128 y 256 muestras por trama. Con
    otra frecuencia devuelve "falso" sin llamar a Audiodec.
  - También lee los canales (`+0x16`), el tamaño de bloque (`+0x20`), las muestras por bloque (`+0x26`)
    y la configuración (`+0x40`).
- **[V] Bloque de control**, que reserva el juego (puntero en el objeto de flujo `+0x70`). `ctrl`, el
  puntero que reciben `CreateDecoder` y `Decode`, es el bloque `+0x08` (el primer argumento del log,
  `0x1006eb2048`, es bloque + 8):

  | Desplazamiento | Contenido | Lo que escribe el juego |
  |---|---|---|
  | `+0x08` | `ctrl`: puntero a los parámetros | → `+0x58` |
  | `+0x10` | puntero a la info del flujo (bsi) | → `+0x64` |
  | `+0x18` | puntero a la entrada (AU) | → `+0x28` |
  | `+0x20` | puntero a la salida (PCM) | → `+0x40` |
  | `+0x28` | AU, 0x18 bytes: `{u32 tamaño; u32; void *datos /*+0x8*/; u32 bytes /*+0x10*/; u32}` | tamaño `0x18` |
  | `+0x40` | PCM, la misma forma | tamaño `0x18` |
  | `+0x58` | parámetros ATRAC9: `{u32 tamaño; u32; u32 config}` | `0xC`, `1` y los 4 bytes del RIFF `+0x40` (`0xFE…`) |
  | `+0x64` | info del flujo | tamaño `0x24`; el resto lo rellena la biblioteca **[H]** |

  Es la misma forma que las estructuras de `AudiodecCpu` del PR #5289 (2.4): descriptor de 0x18 bytes
  con los datos en `+0x8` y el tamaño en `+0x10`, y un `ctrl` con parámetros, bsi, AU y PCM. Para
  `libSceAudiodec` ya no es una hipótesis: lo construye el propio juego.
- **[V] La llamada** es `sceAudiodecCreateDecoder(ctrl, 1)`, con códec 1 = ATRAC9, el mismo valor que
  en `InitLibrary`. El valor devuelto se guarda como identificador en el objeto de flujo `+0x0`. La
  rutina **no lo comprueba** y devuelve "verdadero" de todas formas.
- **[V] La rutina de decodificación** (`+0x1132b0`) hace lo siguiente:
  - **Si el identificador es negativo, devuelve "falso" sin llamar a `sceAudiodecDecode`.**
  - Si no, prepara la entrada y la salida:
    - AU: el resto de la supertrama, con el puntero en base + tamaño de bloque − bytes pendientes y el
      tamaño en los bytes pendientes;
    - PCM: un búfer de `canales × 512` bytes.
  - Llama a `sceAudiodecDecode(id, ctrl)`. Con un resultado ≠ 0 devuelve "falso"; con 0 resta de los
    pendientes el campo `bytes` del AU. Es decir, **la biblioteca sobrescribe el tamaño del AU con los
    bytes consumidos**.
  - Una función auxiliar (`+0x1132a0`) devuelve el `u32` de bsi `+0x4`. *(2026-10-19: es código
    muerto. El juego lee ese campo en línea en otros tres sitios, y es el número de canales:
    `docs/audiodec.md` 1.4.)*
- **[H]** Quedan para el trazador (*2026-10-19: resueltas con el desensamblado, salvo la primera;
  detalle en `docs/audiodec.md`*):
  - El `1` del segundo campo de los parámetros podría ser el formato del PCM; el juego abre sus
    puertos de `AudioOut` en float. *Sigue [H]: el juego no lo vuelve a leer.*
  - `canales × 512` bytes caben en una trama de 256 muestras en 16 bits o en una de 128 en float.
    ***[V]** 16 bits entrelazado: el juego divide `pcm.bytes` entre `canales × 2` (1.5).*
  - El contenido de la info del flujo. ***[V]** El juego solo lee `+4`, los canales, y lo lee antes del
    primer `Decode` (1.4).*
  - Qué hace el nivel superior cuando la rutina de decodificación devuelve "falso": silencio,
    reintento o espera. ***[V]** Corta la pista: vuelca lo acumulado y borra el decodificador. Si la
    petición pide repetir, vuelve a llamar a `CreateDecoder` (1.6).*
- **Consecuencia para la respuesta provisional.** Si `CreateDecoder` devuelve un error, el juego no
  llama a `Decode` para ese flujo. Con la regla "función no llamada = sin resolver", la siguiente
  ejecución no pararía en `Decode` sino en lo que haga el juego con un flujo que no produce nada.
  *(2026-10-19: con una petición de repetir, eso es un bucle sin espera sobre `CreateDecoder`. Lo
  mismo pasa con un `Decode` que siempre falla. Por eso la fase A ya no ejecuta el juego con
  respuestas de error: 4.1.)*

## 3. Reglas comunes a 4a y 4b

- **Nunca éxito simulado.** Hasta que una función esté implementada de verdad, responde con un error
  explícito y lo registra (como `sceAudiodecInitLibrary` ahora). Una función que el juego no ha
  llamado sigue sin resolver y da `STOP:`.
- **Trazador `GOW3_TRACE_AUDIODEC=1`:** por cada llamada, el nombre, los argumentos y un volcado
  hexadecimal acotado (primeros 256 bytes) de cada estructura que el guest pasa por puntero, antes y
  después de la llamada. Sin la variable, se registra solo la primera llamada de cada función.
  *(2026-10-20, tarea 3: también el primer error de cada función, con su causa, porque un error corta
  la pista sin avisar. Por ahora es una línea por llamada; el volcado hexadecimal, el tope de 400 líneas
  y el desfase llegan con `Decode` en la tarea 4.)*
- **Códec no soportado → `STOP:` con el número**, nunca silencio. **Flujo no válido → código de error
  de la PS4**, sin cerrar el juego.
- **Memoria del guest:** antes de leer o escribir un búfer del guest, comprobar que es memoria
  mapeada (`runtime_memory_is_mapped`).
- **Concurrencia:** el juego tiene 32 hilos `at9` y uno `hevag`, así que la tabla de decodificadores y
  los contadores van protegidos por un mutex. Cada decodificador puede usarse desde cualquier hilo.
- **Contadores en `runtime_report`:** `Runtime: Audiodec decoders=…, frames=…, errors=…` (y su
  equivalente de AudiodecCpu).
- **Archivos:** `src/runtime_audiodec.c` (ya existe) para `libSceAudiodec`, y uno nuevo,
  `src/runtime_audiodec_cpu.c`, para `libSceAudiodecCpu`, porque son bibliotecas con espacios de error
  distintos (`0x807F…` y `0x80F7…`). *(Decisión del redactor: también podrían ir en un solo archivo.)*

## 4. Spec 4a — ATRAC9 (`libSceAudiodec`): música, voces y efectos `vse`

### 4.1 Fase A — Investigación (primero)

Entregable: `docs/audiodec.md`, apartado ATRAC9. Sin él no se pasa a la fase B.

**Orden de la fase A [D] (2026-10-19, aprobado con el plan).** Sustituye al punto 1 de abajo. Cada
ejecución hasta `CreateDecoder` cuesta 6 min 29 s, y una respuesta de error corta la pista (2.5).
Por eso el orden es:

1. **Desensamblado estático** del código de llamada. *Hecho el 2026-10-19:* `docs/audiodec.md`
   (estructuras, consumo por trama, bsi, PCM, errores, bucles y el contrato que se deduce).
2. **`Decode` real provisional** sobre LibAtrac9. Se apoya en las suposiciones del punto 1 y comprueba
   cada una en cada llamada, con el trazador de la sección 3; si alguna no se cumple, error y traza.
3. **Una sola ejecución de confirmación**, que contesta las preguntas de `docs/audiodec.md` 1.9.

Lista original:

1. **Instrumentación:** el trazador de la sección 3 sobre las seis funciones de `libSceAudiodec`, con
   respuestas provisionales de error.
2. **Qué hay que averiguar (todo [H] hasta entonces):**
   - Layout de la estructura de control (`SceAudiodecCtrl` en la API pública) y de los parámetros de
     ATRAC9: dónde van el puntero de entrada, su tamaño, el búfer de salida y dónde se escriben los
     bytes consumidos y producidos. Contraste: las estructuras de `AudiodecCpu` del PR #5289 (2.4)
     podrían compartir forma, pero es una hipótesis.
   - Cómo recibe `CreateDecoder` la configuración: se espera que lleve los 4 bytes de `+0x40` del
     RIFF (`0xFE…`), que es lo que pide `Atrac9InitDecoder`.
   - Qué entrega `Decode` por llamada (una supertrama, varias o un búfer arbitrario) y en qué formato
     quiere el PCM (16 bits o float, entrelazado o no).
   - La secuencia por pista: `InitLibrary` → `CreateDecoder` → `Decode`* → `ClearContext` (en un salto
     o un bucle) → `DeleteDecoder` → `TermLibrary`.
   - Los códigos de error que el juego distingue (fin de flujo, trama corrupta).
   - Qué hace el juego si `CreateDecoder` falla, para saber si la respuesta provisional deja pasar el
     arranque (sección 6).

   *Avance (2026-10-17, solo desensamblado, 2.5):*
   - el layout del control y de los parámetros, el paso de la configuración y los bytes consumidos ya
     están vistos;
   - que el juego no llama a `Decode` si `CreateDecoder` falla también está visto;
   - falta todo lo que solo da el trazador: el formato del PCM, la info del flujo, los errores y la
     secuencia completa por pista.

   *Avance (2026-10-19, desensamblado completo, `docs/audiodec.md`):*
   - **PCM:** 16 bits entrelazado, una trama de 256 muestras por llamada.
   - **Bytes consumidos:** `au.bytes` es lo que usó la trama, salvo en la última de la supertrama, que
     consume todo lo que queda, relleno incluido **[D]**. En los clips, el relleno aparece en el 42 %
     de las supertramas de `bgm_typ` y en casi todas las de `bgm_rare`.
   - **bsi:** `CreateDecoder` escribe `+4` = canales.
   - **Errores:** el juego no distingue códigos; cualquier error corta la pista.
   - **Secuencia por pista:** `CreateDecoder` → `Decode`* → (`ClearContext` en cada vuelta del bucle
     `smpl`) → `DeleteDecoder`. Repetir o empezar en otra posición es borrar y crear de nuevo.
     `TermLibrary` está en el destructor y en otro método del mismo objeto al que no se ha visto
     llamar *(corregido el 2026-10-20; antes decía "solo al cerrar")*.
   - **`ClearContext`** tiene que reiniciar el índice de trama de LibAtrac9: si no, el siguiente
     `Decode` da `0x82000000`. *Revisión (2026-10-20):* volver a llamar a `Atrac9InitDecoder` sobre el
     mismo handle no lo reinicia; se libera el handle y se crea otro, como `at9_reset` en
     `src/runtime_ajm.c` **[D]**. El salto cae a mitad de supertrama en 93 de las 132 entradas de
     `bgm` y en 1 040 de las 1 327 de `vse`.
   - Queda para la ejecución: qué pistas hacen bucle, cuántos decodificadores hay vivos a la vez y el
     desfase real.
3. **Orden de observación:** la música primero (lo primero que suene en la pantalla de título o en el
   prólogo), luego las voces y luego `vse`.
4. **Fuentes:** el código de llamada del propio juego (desensamblado de `eboot.bin`) y el volcado del
   trazador. No se cita ninguna estructura sin verificarla con el comportamiento del juego.

Si la fase A muestra que hace falta otra estrategia, se vuelve a hablar con el usuario.

### 4.2 Fase B — Implementación

En `src/runtime_audiodec.c`, una tabla de decodificadores por identificador. Cada uno guarda el
códec, los parámetros, un contexto de LibAtrac9 y un búfer de salida.

- `CreateDecoder`: valida el códec (solo ATRAC9; otro → `STOP:`) y la configuración, y crea el
  contexto con `Atrac9InitDecoder`.
- `Decode`: lee la entrada del guest, la decodifica, escribe el PCM en el formato y tamaño que el
  juego pidió y rellena los bytes consumidos y producidos.
- `ClearContext`: reinicia el estado del decodificador (para saltar dentro de una pista).
  *(2026-10-20: con un handle de LibAtrac9 nuevo, `docs/audiodec.md` 1.7.)*
- `DeleteDecoder` y `TermLibrary`: liberan; un identificador viejo devuelve error, no falla.
- `InitLibrary` deja de devolver el error provisional cuando `CreateDecoder` existe de verdad.

Los hitos internos siguen la prioridad: **música** (estéreo) → **voces** (mono) → **`vse`**.

## 5. Spec 4b — HEVAG (`libSceAudiodecCpu`): efectos de `vag.mrg`

Empieza cuando la música y las voces de 4a suenan.

### 5.1 Fase A — Investigación

Entregable: `docs/audiodec.md`, apartado HEVAG.

1. El trazador de la sección 3 sobre `QueryMemSize`, `InitDecoder` y `Decode`, con respuestas
   provisionales de error (`0x80F70000`).
2. Contrastar con el volcado las estructuras, ids y errores del PR #5289 (2.4): el códec esperado es
   `0x1008`. Averiguar también cuánta memoria de trabajo hay que pedir en `QueryMemSize` (la memoria
   la pone el guest), qué entrega cada `Decode` y en qué formato quiere el PCM.
3. Ver cómo junta el juego las parejas `_L`/`_R`. **[H]**: decodifica cada mono por separado y las
   mezcla él.

### 5.2 Fase B — Implementación

- El decodificador HEVAG se toma de vgmstream (`decode_hevag` y su tabla) y se adapta al estilo del
  repo, **conservando su aviso de copyright y su licencia** en el archivo copiado. Dónde va (por
  ejemplo, `third_party/` o un archivo propio en `src/`) lo decide el implementador.
- El estado del decodificador (las 4 muestras de historia) va en la memoria de trabajo que entrega el
  guest o en una tabla del runtime indexada por ella, según lo que muestre la fase A.
- `InitDecoder` y `Decode` sobre ese decodificador; otro códec → `STOP:`.

## 6. Relación con el hito de primer fotograma

Audiodec se llama **antes del primer draw** (2.3). Mientras el juego ignore lo que devuelven las
respuestas provisionales, el hito no depende de esta spec, y la fase 2A de la Spec 2 sigue con ellas.
Si un `CreateDecoder` (o `InitDecoder`) fallido cuelga el arranque o lo desvía, la fase A de 4a (o de
4b) pasa a formar parte del camino hacia el hito, y se avisa al usuario.

**Resultado (2026-10-17).** `InitLibrary` llega antes del primer draw, pero `CreateDecoder` no llega
hasta el final del vídeo del prólogo (2.5), cuando el H1 del hito ya está cumplido. Por tanto:

- Audiodec **no bloquea el hito**.
- Audiodec **sí bloquea pasar del prólogo**: lo que viene después (**[H]** la música de la pantalla
  siguiente) necesita al menos la respuesta de la fase A para `CreateDecoder`.
- Cada prueba cuesta los 6 min 29 s del vídeo, porque el prólogo no se ha podido saltar (Spec 2,
  sección 4.1).

**Resultado (2026-10-20, tarea 3, `731f3b6`).** El juego ya no pararía en `CreateDecoder`: las seis
funciones resuelven y `--check-imports` no lista ninguna de `libSceAudiodec`. Pero `Decode` responde
con error hasta la tarea 4, y con eso el juego corta cada pista y, si pide repetirla, entra en un bucle
sin espera sobre `CreateDecoder` (`docs/audiodec.md` 1.6). **No se ejecuta el juego entre la tarea 3
y la 4.**

## 7. Requisitos

- Cuando el juego envíe un bloque válido, el sistema debe devolver el PCM en el formato y tamaño que
  el juego pidió, con los bytes consumidos correctos.
- Si el flujo no es válido, el sistema debe devolver el código de error de la PS4 sin cerrar el juego.
- Códec no soportado → `STOP:` con el número.

## 8. Pruebas

- *(2026-10-20, tarea 3: `tests/test_audiodec.c` existe y cubre ya el ciclo de vida: resolución,
  las cinco configuraciones, bsi `+4`, configuración, tamaños y punteros no válidos, ids viejos o nunca
  creados, ids no reutilizados, tabla llena, `Delete`/`Term` con una referencia tomada, `Term` → `Init`
  → `Create`, códec desconocido (`STOP:` en `Init`, `Create` y `Term`) y el registro de la primera
  llamada. Se compila con `build.sh --test` como `out/audiodec-test`.)*
- `tests/test_audiodec.c` (esquema de `tests/test_mp3.c`) para 4a: decodifica un clip ATRAC9 de
  referencia por la API del runtime y lo compara, muestra a muestra, con LibAtrac9 llamada
  directamente. Casos: tamaño de salida, bytes consumidos, flujo corrupto (error sin cerrar),
  `ClearContext` seguido de otra decodificación, identificador viejo y códec desconocido (`STOP:`).
  *(2026-10-20: `ClearContext` después de 1, 2 y 3 tramas de una supertrama, con los clips `vse`; los
  dos de música vuelven en frontera y no ejercitan el reinicio.)*
- Para 4b, un test del decodificador HEVAG: tramas sintéticas con índices de tabla bajos (≤ `0x1c`,
  donde HEVAG se comporta como el VAG clásico) y altos, con el resultado calculado aparte. Además, si
  hay un fragmento local, se compara con `vgmstream-cli` cuando esté instalado; si no, se omite.
- **Clips de referencia:** no hay ATRAC9 ni HEVAG pequeños en el repo, y el contenido del juego tiene
  derechos. Se extraen en local del dump con el formato de la sección 2.2, **sin versionarlos**: una
  pista de `bgm.mrg` (estéreo), una de las 11 entradas mono de `vse.mrg` (mono, sin MZX: las voces
  necesitarían descomprimir MZX, que el runtime no hace) y un efecto de `vag.mrg` (HEVAG). Los tests
  se omiten si faltan, como los que necesitan el dump. Se documenta el comando que los genera.

  *Hecho (2026-10-19, `07800f5`).* El comando es
  `python3 tools/extract_audio_clips.py --image Tsukihime/CUSA47999/image --out out/audiodec-clips`.
  Escribe un clip por cada una de las cinco configuraciones de 2.2, la primera entrada de cada una:

  | Clip | Origen | Configuración | Bloque | Canales | Tamaño |
  |---|---|---|---|---|---|
  | `bgm_typ.at9` | `bgm` 1 | `fe740ff0` | 512 | 2 | 5 158 056 B |
  | `bgm_rare.at9` | `bgm` 0 | `fe7409f0` | 320 | 2 | 30 248 B |
  | `vse_stereo.at9` | `vse` 1 | `fe740bf0` | 384 | 2 | 951 336 B |
  | `vse_mono.at9` | `vse` 775 | `fe700bf0` | 384 | 1 | 124 968 B |
  | `vse_mono_small.at9` | `vse` 0 | `fe7004f0` | 160 | 1 | 7 848 B |

  Además escribe `vag0.vagp`, el primer efecto real de `vag.mrg`: la entrada 1, `moon_se00001_L`
  (HEVAG `0x00020001`, 48 kHz, 18 800 B). Se salta la entrada 0, `dummy`, porque es silencio.

  Si falta algún clip, la herramienta lo dice, escribe los demás y sale con 1. Sus tests
  (`tests/test_extract_audio_clips.py`, 9 casos) usan archivos sintéticos y no necesitan el dump.
- Ejecución real: arrancar con `GOW3_TRACE_AUDIODEC` y escuchar la música, las voces y los efectos del
  prólogo.

## 9. Archivos

Nuevos: `src/runtime_audiodec_cpu.c`, el decodificador HEVAG (adaptado de vgmstream),
`docs/audiodec.md`, `tests/test_audiodec.c`, `tests/test_audiodec.py` y la herramienta que extrae los
clips de referencia. Modificados: `src/runtime_audiodec.c`, `src/runtime.h` y `src/runtime.c`
(resolver y `runtime_report`), `src/import_names.inc` (las 9 entradas), `build.sh` y, si hace falta
aceptar módulos de sistema nuevos, `src/runtime_content.c`.

## 10. Criterios de aceptación

**4a**

- [ ] `docs/audiodec.md` describe el ATRAC9 observado: estructuras, secuencia y errores. *(La parte
  estática está hecha desde el 2026-10-19; falta lo observado en la ejecución de confirmación.)*
- [ ] `out/audiodec-test` decodifica el clip ATRAC9 de referencia igual que LibAtrac9.
- [ ] En el juego suena la música; después, las voces; después, los efectos `vse` (lo juzga el usuario).
- [x] `--check-imports` no lista ninguna función de `libSceAudiodec`. *(2026-10-20, tarea 3: quedan 6,
  los 3 de `libSceAudiodecCpu` y las 3 trampas de la Spec 2; `out/spec4-t3-check-imports.log`. Ojo:
  `Decode` y `ClearContext` resuelven, pero con error provisional hasta la tarea 4.)*

**4b**

- [ ] `docs/audiodec.md` describe el HEVAG observado, contrastado con el PR #5289.
- [ ] El test del decodificador HEVAG pasa.
- [ ] En el juego suenan los efectos de `vag.mrg` (lo juzga el usuario).
- [ ] `--check-imports` no lista ninguna función de `libSceAudiodecCpu`.

**Común:** un códec no soportado detiene con `STOP:` y el número.

## 11. Riesgos

- **Las estructuras de `libSceAudiodec` no tienen referencia** (sigue siendo el riesgo "Alto" del
  roadmap, ahora solo para 4a). La fase A existe para convertir la incertidumbre en datos.
- El decodificador HEVAG de vgmstream salió de ingeniería inversa y puede redondear distinto que el de
  Sony. El impacto es bajo: son 19 efectos cortos.
- 32 hilos `at9` decodificando a la vez: una tabla mal protegida da fallos aleatorios. Se mitiga con
  el mutex de la sección 3 y un caso de varios hilos en el test.
- La salida depende de la cola de `AudioOut` (`src/runtime_audio.c`). Si los chirridos vienen de allí
  y no de Audiodec, se separa el diagnóstico para no culpar a esta spec.
- **Un error de `Decode` corta la pista sin avisar** (`docs/audiodec.md` 1.6). Una suposición mal
  validada da silencio, no un mensaje. Se mitiga trazando cada error con su causa y con los tests
  bit-exactos contra LibAtrac9 sobre las cinco configuraciones.
- **Un `ClearContext` que no reinicia LibAtrac9 pasa las pruebas con música** (los dos clips `bgm`
  vuelven en frontera de supertrama) y en el juego corta la pista en el primer bucle que acabe a
  mitad. Se mitiga probando con los tres clips `vse` (`docs/audiodec.md` 1.7).

## 12. Historial

- 2026-10-14: primera versión (una sola spec, códecs desconocidos).
- 2026-10-17: la fase 2A de la Spec 2 llega a `sceAudiodecInitLibrary(1)` antes del primer fotograma
  (respuesta provisional en `18ad4dc`). El inventario del dump (2.2) y el PR #5289 muestran que todo
  es ATRAC9 salvo 19 efectos HEVAG. El usuario fija la prioridad (música, voces, efectos) y aprueba
  dividir la spec en 4a y 4b.
- 2026-10-17 (después): sin watchdog, el juego reproduce el prólogo y para en
  `sceAudiodecCreateDecoder`. El sitio de llamada da el bloque de control y la secuencia
  `CreateDecoder` → `Decode` (2.5, sección 6). No se implementa nada (decisión del usuario).
- 2026-10-19: plan de implementación aprobado. Tarea 1 hecha (`07800f5`): la herramienta de clips de
  referencia (8). Con ella se ven las cinco configuraciones reales de ATRAC9 y los centinelas de los
  `.hed` (2.2).
- 2026-10-19 (después): tarea 2 hecha (`084d16a`). La fase A empieza por el desensamblado (nuevo orden en 4.1).
  `docs/audiodec.md` documenta el uso de ATRAC9 entero, y con él se resuelven tres de las cuatro
  hipótesis de 2.5 (PCM, bsi y qué pasa ante un error). También se fija qué devuelve la última trama
  de una supertrama.
- 2026-10-20: revisión adversarial de `docs/audiodec.md`, comprobada contra el desensamblado,
  LibAtrac9 y una emulación de la rutina de decodificación sobre los cinco clips. Las tres decisiones
  de la tarea 2 se sostienen. Cambia `ClearContext`: volver a llamar a `Atrac9InitDecoder` no reinicia
  LibAtrac9, así que se crea un handle nuevo (4.1, 4.2, 8, 11). Además, `TermLibrary` tiene un segundo
  sitio (2.1), `+0x1128ac` es código muerto (2.5) y el documento añade el contador de vueltas del
  bucle, la lectura anticipada de LibAtrac9 y la marca [H] de `pcm.bytes`.
- 2026-10-20 (después): tarea 3 hecha (`731f3b6`). Resuelven las seis funciones de `libSceAudiodec`:
  `InitLibrary`, `TermLibrary`, `CreateDecoder` (valida el bloque, crea el handle de LibAtrac9 y escribe
  bsi `+4` = canales) y `DeleteDecoder` hacen su trabajo; `Decode` y `ClearContext` dan `0x807F0001`
  hasta la tarea 4. Tabla de 64 decodificadores con ids crecientes desde 1 y contador de referencias.
  Códec distinto de 1 → `STOP:` también en `Init` y `Term`. Errores de vitasdk (2.4). `TermLibrary`
  recibe el códec (`TermLibrary(1)`, 2.1).
