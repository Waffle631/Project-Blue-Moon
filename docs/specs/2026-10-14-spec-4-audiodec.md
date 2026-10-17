# Spec 4 — Audio: ATRAC9 por Audiodec (4a) y HEVAG por AudiodecCpu (4b)

Fecha: 2026-10-14, **replanteada el 2026-10-17** con los datos de la fase 2A de la Spec 2 y del
dump. Estado: **borrador para aprobación** (el usuario aprobó la dirección del replanteo en el chat
el 2026-10-17). Roadmap: `docs/tsuki/ROADMAP.md`, sección "Spec 4".

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
| `VjhsmxpcezI` | `sceAudiodecInitLibrary` | 4a | provisional (error explícito, fase 2A, `18ad4dc`) |
| `h5jSB2QIDV0` | `sceAudiodecTermLibrary` | 4a | falta |
| `O3f1sLMWRvs` | `sceAudiodecCreateDecoder` | 4a | falta |
| `Tp+ZEy69mLk` | `sceAudiodecDeleteDecoder` | 4a | falta |
| `KHXHMDLkILw` | `sceAudiodecDecode` | 4a | falta |
| `6Vf9WTLDoss` | `sceAudiodecClearContext` | 4a | falta |
| `ktD2w3D4G2U` | `sceAudiodecCpuQueryMemSize` | 4b | falta |
| `hdFsxo3MFu8` | `sceAudiodecCpuInitDecoder` | 4b | falta |
| `lSVTiWV5wLc` | `sceAudiodecCpuDecode` | 4b | falta |

### 2.2 El audio del juego (dump, 2026-10-17)

**[V]** Formato de los archivos empaquetados: cada entrada de un `.hed` ocupa 8 bytes,
`{u32 sector; u16 sectores; u16 tamaño & 0xffff}`, con sectores de 0x800 bytes. Los 4 bits altos de
`sector` son los bits 16–19 del número de sector: por ejemplo, la entrada 31 de `bgm.hed` acaba en el
sector `0x10581` y la 32 vale `0x1_0000581`. `voice.hed` tiene otra forma, todavía sin descifrar.

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
empiezan por la marca `0xFE` (`fe 74 09 f0` en la música, `fe 70 04 f0` en `vse`). `se_nam.mrg` es un
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
- **[V]** `GOW3_AUDIO_TRACE` solo traza aperturas bajo `sound/` (`src/runtime_file.c`), una ruta de
  GoW3: no sirve para Tsukihime. La Spec 6 lo generaliza.

## 3. Reglas comunes a 4a y 4b

- **Nunca éxito simulado.** Hasta que una función esté implementada de verdad, responde con un error
  explícito y lo registra (como `sceAudiodecInitLibrary` ahora). Una función que el juego no ha
  llamado sigue sin resolver y da `STOP:`.
- **Trazador `GOW3_TRACE_AUDIODEC=1`:** por cada llamada, el nombre, los argumentos y un volcado
  hexadecimal acotado (primeros 256 bytes) de cada estructura que el guest pasa por puntero, antes y
  después de la llamada. Sin la variable, se registra solo la primera llamada de cada función.
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

## 7. Requisitos

- Cuando el juego envíe un bloque válido, el sistema debe devolver el PCM en el formato y tamaño que
  el juego pidió, con los bytes consumidos correctos.
- Si el flujo no es válido, el sistema debe devolver el código de error de la PS4 sin cerrar el juego.
- Códec no soportado → `STOP:` con el número.

## 8. Pruebas

- `tests/test_audiodec.c` (esquema de `tests/test_mp3.c`) para 4a: decodifica un clip ATRAC9 de
  referencia por la API del runtime y lo compara, muestra a muestra, con LibAtrac9 llamada
  directamente. Casos: tamaño de salida, bytes consumidos, flujo corrupto (error sin cerrar),
  `ClearContext` seguido de otra decodificación, identificador viejo y códec desconocido (`STOP:`).
- Para 4b, un test del decodificador HEVAG: tramas sintéticas con índices de tabla bajos (≤ `0x1c`,
  donde HEVAG se comporta como el VAG clásico) y altos, con el resultado calculado aparte. Además, si
  hay un fragmento local, se compara con `vgmstream-cli` cuando esté instalado; si no, se omite.
- **Clips de referencia:** no hay ATRAC9 ni HEVAG pequeños en el repo, y el contenido del juego tiene
  derechos. Se extraen en local del dump con el formato de la sección 2.2, **sin versionarlos**: una
  pista de `bgm.mrg` (estéreo), una de las 11 entradas mono de `vse.mrg` (mono, sin MZX: las voces
  necesitarían descomprimir MZX, que el runtime no hace) y un efecto de `vag.mrg` (HEVAG). Los tests
  se omiten si faltan, como los que necesitan el dump. Se documenta el comando que los genera.
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

- [ ] `docs/audiodec.md` describe el ATRAC9 observado: estructuras, secuencia y errores.
- [ ] `out/audiodec-test` decodifica el clip ATRAC9 de referencia igual que LibAtrac9.
- [ ] En el juego suena la música; después, las voces; después, los efectos `vse` (lo juzga el usuario).
- [ ] `--check-imports` no lista ninguna función de `libSceAudiodec`.

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

## 12. Historial

- 2026-10-14: primera versión (una sola spec, códecs desconocidos).
- 2026-10-17: la fase 2A de la Spec 2 llega a `sceAudiodecInitLibrary(1)` antes del primer fotograma
  (respuesta provisional en `18ad4dc`). El inventario del dump (2.2) y el PR #5289 muestran que todo
  es ATRAC9 salvo 19 efectos HEVAG. El usuario fija la prioridad (música, voces, efectos) y aprueba
  dividir la spec en 4a y 4b.
