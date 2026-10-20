# Audiodec en Tsukihime (CUSA47999)

Cómo usa el juego `libSceAudiodec` (ATRAC9) y, más adelante, `libSceAudiodecCpu` (HEVAG), y qué
contrato tiene que cumplir el runtime (`src/runtime_audiodec.c`). La spec es
`docs/specs/2026-10-14-spec-4-audiodec.md`.

No existe ninguna fuente pública de las estructuras de `libSceAudiodec` de PS4, y la biblioteca real
no está en el dump. La única autoridad es el código del propio juego: lo que construye antes de cada
llamada y lo que hace después con el resultado.

Marcas: **[V]** comprobado, con la dirección en el eboot (desplazamientos de la imagen, los mismos que
da el loader en sus `STOP:`); **[H]** hipótesis pendiente; **[D]** decisión que se fija aquí.

**Cómo se reproduce.** `objdump` no reconoce el ELF de Sony (`e_type` `0xFE10`). Se extrae el primer
`PT_LOAD` (desplazamiento `0x4000` del archivo, `0x329dc8` bytes, dirección virtual 0) y se desensambla
como binario plano:

```bash
python3 -c "d=open('out/eboot.elf','rb').read(); open('out/text.bin','wb').write(d[0x4000:0x4000+0x329dc8])"
objdump -D -b binary -m i386:x86-64 -M intel --start-address=0x1130b0 --stop-address=0x1135ca out/text.bin
```

Las llamadas a imports son `call` a un stub de la PLT (`jmp [rip+X]`); `python3 tools/got_import.py
<ranura>` da el NID de la ranura `X`, que se busca en la tabla del §2.1 de la spec.

## 1. ATRAC9 (`libSceAudiodec`) — fase A estática (2026-10-19)

### 1.1 Quién llama y en qué orden

- **[V]** `sceAudiodecInitLibrary(1)` al crear el sistema de audio (`+0x10e20c`, método de la vtable
  `0x432328`); el valor devuelto se ignora.
- **[V]** `sceAudiodecTermLibrary(1)` solo en el destructor del sistema de audio (`+0x10df70`, dentro
  de `+0x10def0`, primera entrada de la vtable `0x432318`); el valor devuelto se ignora. No se llama
  durante la partida.
- **[V]** Todo lo demás corre en los **hilos `at9 thread`**: el nombre se carga en `+0x10e613` y la
  entrada `+0x112350` en `+0x10e61a`. La entrada llama al bucle del hilo `+0x112650`, que por cada
  petición hace esto:
  1. lee y analiza la cabecera RIFF (`+0x112d60`);
  2. prepara el flujo y llama a `CreateDecoder` (`+0x1130b0`, llamada en `+0x113172`);
  3. programa el bucle de la pista (`+0x10fcb0`);
  4. decodifica hasta el final (`+0x1128e0`, que llama a la rutina de decodificación `+0x1132b0`, donde
     está `Decode` en `+0x113398` y `ClearContext` en `+0x11353f`);
  5. al salir del bucle de decodificación, `DeleteDecoder` (`+0x112d28`).
- **[V]** Hay una sola llamada viva a `DeleteDecoder`, la de `+0x112d28`. La otra que da el §2.1 de la
  spec (`+0x1136ff`) está en `+0x1136f0`, una función que nadie llama: no hay ningún `call`/`jmp`
  directo, ninguna referencia relativa a `rip` y ninguna reubicación que apunte a ella. Lo mismo pasa
  con `+0x1132a0` (el auxiliar que devuelve bsi `+4`) y `+0x113660`: son copias fuera de línea de
  funciones que el compilador expandió en línea.
- **[V]** Valores devueltos: el de `CreateDecoder` se guarda como identificador (flujo `+0x00`,
  `+0x113177`) sin comprobarlo; el de `Decode` se compara con 0 (`+0x11339d`); los de `ClearContext`
  (`+0x113544`) y `DeleteDecoder` (`+0x112d2d`) se ignoran.
- **[H]** Un hilo, un flujo y como mucho un decodificador vivo: hasta 32 decodificadores a la vez.

### 1.2 Estructuras

**Bloque de control** (`NEXL_ATRAC9_CTRL`, `0x88` bytes alineados a `0x40`, reservado en `+0x113a57`;
el `+0x00` es la vtable `0x432450`). El juego pone a cero `+0x28`…`+0x87` antes de cada
`CreateDecoder` (`+0x113118`…`+0x113122`) y después escribe:

| Desplazamiento | Contenido | Escrito en |
|---|---|---|
| `+0x08` | `ctrl`, el puntero que reciben `CreateDecoder` y `Decode`: puntero a los parámetros (`+0x58`) | `+0x113127` |
| `+0x10` | puntero a la info del flujo, bsi (`+0x64`) | `+0x11312b` |
| `+0x18` | puntero al AU (`+0x28`) | `+0x11312f` |
| `+0x20` | puntero al PCM (`+0x40`) | `+0x11313d` |
| `+0x28` | AU: `{u32 tamaño = 0x18; u32; void *datos /*+0x30*/; u32 bytes /*+0x38*/; u32}` | `+0x113141`, `+0x113383`, `+0x113387` |
| `+0x40` | PCM, la misma forma: datos en `+0x48`, bytes en `+0x50` | `+0x113148`, `+0x11338b`, `+0x113392` |
| `+0x58` | parámetros: `{u32 tamaño = 0xC; u32 = 1; u8 config[4]}` (config = RIFF `+0x40`) | `+0x113154`, `+0x113167` |
| `+0x64` | bsi: `{u32 tamaño = 0x24; …}`, el resto a cero | `+0x113158` |

**Objeto de flujo** (objeto del hilo `+0x2b8`; constructor `+0x113a50`). Solo los campos que
importan para el contrato:

| Campo | Contenido |
|---|---|
| `+0x00` | identificador del decodificador; −1 sin decodificador (`+0x113a6e`, `+0x112d31`) |
| `+0x10` | posición de lectura en el archivo |
| `+0x18` | canales (RIFF `+0x16`) |
| `+0x1c` | tamaño de bloque, es decir, de supertrama (RIFF `+0x20`) |
| `+0x20` | tramas por supertrama (RIFF `+0x26` / muestras por trama, 256 a 48 kHz) |
| `+0x28` | capacidad del PCM por llamada: `canales × 512` (`+0x11318d`, `+0x1131b1`) |
| `+0x38` | tramas que se van a decodificar: `⌈muestras totales / 256⌉ + 1` (`+0x1131e5`…`+0x1131f1`) |
| `+0x40`, `+0x44`, `+0x48` | supertrama de inicio, tramas que se descartan dentro de ella y muestras que se recortan de la siguiente |
| `+0x4c` | bytes pendientes de la supertrama actual |
| `+0x50` | tramas decodificadas (una por llamada a `Decode`, `+0x1133ce`) |
| `+0x54` | posición en muestras |
| `+0x68`, `+0x6c` | 2 banderas: 0 = bucle activo, 1 = primera trama ya descartada |
| `+0x70` | bloque de control |
| `+0x80` | búfer de lectura (`at9 input`, `0x800` bytes, `+0x113acd`) |

### 1.3 `Decode`: entrada, salida y bytes consumidos

La rutina de decodificación `+0x1132b0` produce, en cada llamada, una trama:

```
si id < 0 o no hay fuente de lectura: devuelve falso              // +0x1132c1, +0x1132d1
si pendientes == 0:                                                // +0x1132f9
    lee tamaño_bloque bytes en el búfer de lectura                // +0x11331e (asíncrono; espera con
    pendientes += leídos; posición_archivo += leídos             //  sceKernelUsleep(100), +0x113353)
au.datos  = búfer + tamaño_bloque − pendientes                     // +0x113373…+0x113383
au.bytes  = pendientes                                             // +0x113387
pcm.datos = destino del llamador; pcm.bytes = canales × 512        // +0x11338b, +0x113392
si sceAudiodecDecode(id, ctrl) ≠ 0: devuelve falso                 // +0x113398
pendientes −= au.bytes; tramas_decodificadas += 1                 // +0x1133b6…+0x1133ce
```

- **[V]** La biblioteca **sobrescribe `au.bytes` con los bytes consumidos** y `pendientes` baja en esa
  cantidad. Cuando llega a 0, la llamada siguiente lee otra supertrama. `pendientes` es un `u32`: si
  la biblioteca devolviera más bytes de los que recibió, el puntero del AU siguiente saldría del búfer.
- **[V]** Una trama por llamada: la capacidad del PCM es justo una trama (256 muestras × canales × 2
  bytes) y el juego cuenta una trama por llamada en `+0x50`, que compara con `+0x38`.
- **[V] Medido con LibAtrac9 sobre los cinco clips** (programa de prueba local, 2026-10-19): las tramas
  tienen tamaño variable y muchas supertramas acaban en relleno.

  | Clip | Supertramas | Bytes por trama | Supertramas con relleno | Relleno |
  |---|---|---|---|---|
  | `bgm_typ` | 10 074 | 117–151 | 4 230 (42 %) | 1–9 B |
  | `bgm_rare` | 94 | 22–55 | 94 | 127–229 B (229 en 88) |
  | `vse_stereo` | 2 477 | 93–99 | 247 | 1–4 B |
  | `vse_mono` | 325 | 12–181 | 128 | 1–333 B |
  | `vse_mono_small` | 48 | 12–31 | 48 | 57–109 B |

- **[D] Bytes consumidos.** En las tramas 1 a N−1 de la supertrama, `au.bytes` = lo que usó LibAtrac9
  en esa trama. En la **última trama** (N = tramas por supertrama, 4 en los cinco clips),
  `au.bytes` = **todo lo que quedaba**, el `au.bytes` de entrada, relleno incluido. Así la suma de una
  supertrama es exactamente el tamaño de bloque y `pendientes` llega a 0. Si en la última trama solo se
  contara lo usado, el juego mandaría el relleno como si fuera otra trama (229 bytes en casi todo
  `bgm_rare`). El contador de tramas lo lleva el runtime, como `Instance.frames` y
  `superframe_remain` en `src/runtime_ajm.c`. Criterio de prueba (tarea 4):
  `consumed_over_superframe_equals_block_size`.
- **[D]** Si LibAtrac9 usa más bytes de los que trae `au.bytes`, la trama está cortada: error, sin
  escribir `au.bytes`.
- **[V]** El AU de entrada siempre empieza en una frontera de supertrama después de `CreateDecoder` o
  de `ClearContext`: la preparación y el salto del bucle ponen la lectura en
  `supertrama × tamaño_bloque + inicio_de_datos` (`+0x113278`, `+0x113553`) y `pendientes` a 0
  (`+0x113231`, `+0x113579`).

### 1.4 La info del flujo (bsi)

- **[V]** El juego lee **solo bsi `+4`** (bloque `+0x68`), en línea, en tres sitios vivos:
  - `+0x112926`, **antes del primer `Decode`** (la llamada es `+0x11294c`), y guarda el valor en la
    pila;
  - `+0x112b02` y `+0x112cac`, al volcar al mezclador.
- **[V] Es el número de canales.** El juego avanza el puntero del PCM acumulado `muestras × bsi[4] × 2`
  bytes (`+0x1129ee`, `+0x1129f2`; `+0x112b4b`, `+0x112b54`) y pasa el valor como sexto argumento al
  mezclador (`+0x112b34`, `+0x112ce3`).
- **[D] `CreateDecoder` escribe bsi `+4` = canales.** El juego lo lee antes del primer `Decode`, así
  que escribirlo en `Decode` es tarde. Si queda a 0, el juego escribe cada trama encima de la anterior
  y avisa al mezclador con 0 canales. `Decode` lo vuelve a escribir (es el mismo valor).
- **[D]** El resto de los `0x24` bytes no lo lee el juego y no se rellena: se deja como lo dejó el
  juego (`tamaño = 0x24` y ceros). Inventar campos (frecuencia, tasa de bits) no aporta nada y no se
  puede comprobar.

### 1.5 El PCM

- **[V] El juego interpreta el PCM como 16 bits entrelazado.** Calcula las muestras producidas como
  `pcm.bytes / ((canales × 2) & 0xfffe)` (`+0x11340d`…`+0x113425`). Recorta por delante las muestras que
  tenga que descartar con un `memcpy` sobre el mismo búfer (`+0x113454`) y avanza `muestras × canales
  × 2` bytes.
- **[V]** La biblioteca **sobrescribe `pcm.bytes` con los bytes producidos**: el juego lo lee después
  de `Decode` (`+0x113409`, `+0x113429`). Con 48 kHz son `256 × canales × 2` = la capacidad entera.
- **[V]** El juego descarta él mismo la **primera trama** después de `CreateDecoder` y después de cada
  `ClearContext`, con la bandera 1 del flujo (`+0x1133c7`…`+0x113407`). Después descarta `+0x44`
  tramas más y recorta `+0x48` muestras de la siguiente. **La biblioteca no descarta nada:** devuelve
  cada trama tal como la da LibAtrac9, sin retardo del codificador ni gapless (al contrario que AJM).
- **[H]** El `1` del segundo campo de los parámetros sería el tamaño de muestra (16 bits). El juego no
  lo vuelve a leer y no hay fuente que lo confirme. Solo pesa como dato de entrada: si llegara otro
  valor, error y traza.

### 1.6 Errores

- **[V]** El juego no distingue códigos: cualquier valor ≠ 0 de `Decode` hace que la rutina devuelva
  "falso" (`+0x11339d`, `+0x1133a1`). El bucle de decodificación termina (`+0x112961`, `+0x1129c7`) y entonces:
  1. vuelca al mezclador lo acumulado (`+0x112c98`…`+0x112d09`);
  2. avisa al mezclador (vtable `+0x60`, `+0x112d1b`);
  3. borra el decodificador y pone el identificador a −1 (`+0x112d28`, `+0x112d31`).
- **[V]** No hay ni reintento ni espera: **un error de `Decode` corta la pista**. Después, el hilo
  mira la bandera 1 de la petición (repetir, `+0x1127cf`…`+0x1127eb`). Si está puesta, vuelve a leer
  la cabecera y llama otra vez a `CreateDecoder` desde el principio (`+0x112740`). **[H]** Con una
  pista que repite y un `Decode` que siempre falla, el hilo daría vueltas sin esperar a nada:
  `CreateDecoder`, un `Decode` fallido y `DeleteDecoder`, una y otra vez. Una respuesta de error
  provisional en `Decode` no puede llegar a una ejecución real.
- **[V]** Con un identificador negativo (un `CreateDecoder` fallido), la rutina devuelve "falso" sin
  llamar a `Decode` y no hay `DeleteDecoder` (`+0x112d24`). Con la bandera de repetir, el resultado es
  el mismo bucle sobre `CreateDecoder`.
- **[V]** El fin de la pista no lo señala la biblioteca. El juego cuenta tramas (`+0x50` contra `+0x38`)
  y recorta la última a `muestras totales mod 256` (`+0x113491`…`+0x1134aa`). Además tiene un corte
  propio cerca del final (`+0x113589`…`+0x1135b6`). La biblioteca no necesita ningún código de "fin
  de flujo".

### 1.7 Saltos y bucles

- **[V] Bucle dentro de la pista: `ClearContext`.**
  - El analizador de cabeceras copia `fmt `, `fact` y la cabecera de `smpl` en las mismas posiciones
    que en el RIFF. Las entradas de bucle (dos como mucho) van a `+0x90`, 8 bytes más allá que en el
    archivo (`+0x112fa0`…`+0x11301c`), así que el inicio queda en `+0x98` y el fin en `+0x9c`.
  - La preparación les resta el tercer campo de `fact`, el retardo (256 en los cinco clips)
    (`+0x11324f`…`+0x113268`).
  - Los cinco clips tienen un bucle `smpl`. En `bgm_typ` va de la muestra 1 340 639 a la
    10 315 522; en los demás, de la 256 al final.
  - `+0x10fcb0` solo lo activa si el inicio y el fin son distintos y el modo de la petición
    (objeto del hilo `+0xac`) vale de 1 a 3 (`+0x10fcb6`, `+0x10fd11`…`+0x10fd2b`).
  - Fija la supertrama del inicio del bucle (flujo `+0x40`), las tramas que hay que descartar
    dentro de ella y las muestras que hay que recortar.

  Al llegar al final, la rutina de decodificación:
  1. recalcula los descartes (`+0x113507`…`+0x113537`);
  2. llama a `ClearContext(id)` (`+0x11353f`);
  3. pone la lectura al principio de esa supertrama y los pendientes a 0;
  4. borra la bandera 1, así que la primera trama siguiente se descarta (`+0x113544`…`+0x113580`).
- **[V] Repetir la pista entera o empezar en otra posición: `DeleteDecoder` + `CreateDecoder`.** La
  posición inicial entra en la preparación (`+0x11275f`, `+0x11289c`), que calcula la supertrama, las
  tramas y las muestras que se descartan (`+0x1131dd`…`+0x11322e`). No hay ningún otro sitio que
  llame a `ClearContext`.
- **[V] `ClearContext` tiene que dejar LibAtrac9 al principio de una supertrama.** LibAtrac9 lleva su
  propio índice de trama dentro de la supertrama (`Frame.IndexInSuperframe`, `unpack.c:36-46`). Si le
  llega una trama marcada como primera de supertrama con el índice ≠ 0, devuelve
  `ERR_UNPACK_SUPERFRAME_FLAG_INVALID` (`0x82000000`). Medido sobre los cinco clips: decodificar una
  trama de la supertrama 0 y después la primera de la supertrama 1 da `0x82000000` en todos. El juego
  llama a `ClearContext` en cualquier trama (donde acabe el bucle), así que sin reiniciar ese índice el
  primer `Decode` tras el salto fallaría y, por 1.6, cortaría la pista. LibAtrac9 no expone una
  función para reiniciarlo: o se vuelve a llamar a `Atrac9InitDecoder` (reescribe las tablas globales:
  con el rwlock en escritura) o se toca la estructura interna. Lo decide la tarea 4. El historial del
  IMDCT afecta solo a la primera trama, que el juego descarta (1.5).

### 1.8 Contrato que se deduce

| Función | Qué tiene que hacer el runtime |
|---|---|
| `InitLibrary(1)` | 0 (el juego no mira el valor) |
| `CreateDecoder(ctrl, 1)` | validar `ctrl` y los parámetros (`tamaño = 0xC`, `config[0] = 0xFE`), crear el decodificador, escribir bsi `+4` = canales y devolver un identificador ≥ 0 |
| `Decode(id, ctrl)` | una trama: PCM de 16 bits entrelazado en `pcm.datos`, `pcm.bytes` = bytes producidos, `au.bytes` = bytes consumidos (en la última trama de la supertrama, todo el `au.bytes` de entrada); 0 si va bien |
| `ClearContext(id)` | volver al estado de recién creado: índice de trama 0 en el runtime y en LibAtrac9 |
| `DeleteDecoder(id)` | liberar; el juego no mira el valor |
| `TermLibrary(1)` | liberar todo; solo al cerrar el juego |

Un error en cualquier llamada corta la pista sin avisar (1.6), así que cada suposición se comprueba en
cada llamada y se traza cuando no se cumple.

### 1.9 Preguntas para la ejecución de confirmación (tarea 7)

- Qué pistas piden bucle (`smpl`) o repetir, y si se ve algún `ClearContext`.
- Cuántos decodificadores hay vivos a la vez y desde qué hilos.
- Que el desfase acumulado del trazador (bytes que avanza el juego menos bytes usados por LibAtrac9 y
  relleno) sea 0 en todas las supertramas.
- Que ningún `Decode` reciba un AU que no empiece en una trama.
- Qué suena primero después del prólogo.
