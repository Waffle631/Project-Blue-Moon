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

*Revisión adversarial (2026-10-20):* cambian `ClearContext` (1.7: volver a llamar a
`Atrac9InitDecoder` no reinicia LibAtrac9), `TermLibrary` (1.1: tiene un segundo sitio), el código
muerto (1.1), el contador de vueltas del bucle (1.7) y la marca de `pcm.bytes` (1.5).

### 1.1 Quién llama y en qué orden

- **[V]** `sceAudiodecInitLibrary(1)` al crear el sistema de audio (`+0x10e20c`, dentro de `+0x10e1c0`,
  la ranura `+0x10` de la vtable `0x432318`, que el dueño llama en `+0x12a4b5`); el valor devuelto se
  ignora.
- **[V]** `sceAudiodecTermLibrary(1)` está en dos sitios, los dos del mismo objeto:
  - en el destructor del sistema de audio (`+0x10df70`, dentro de `+0x10def0`, la ranura `+0x00` de
    la vtable `0x432318`);
  - como salto final (`jmp`, no `call`, `+0x10e864`) de `+0x10e7f0`, la ranura `+0x18`. El destructor
    expande en línea ese mismo cuerpo (`+0x10df08`…`+0x10df70`).

  El valor devuelto se ignora. El §2.1 de la spec solo buscó `call` y no vio el segundo sitio.
- **[H]** `TermLibrary` solo llega al cerrar el juego. No se ha encontrado ninguna llamada a la ranura
  `+0x18`: el dueño solo llama a `+0x10` (`+0x12a4b5`) y a `+0x20` (`+0x12a9b5`). Pero una llamada
  virtual no se resuelve del todo sin ejecutar, así que el runtime tiene que admitir otro
  `InitLibrary` después de `TermLibrary` (1.8).
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
  directo, ninguna referencia relativa a `rip`, ninguna reubicación y ningún puntero de 4 u 8 bytes en
  la imagen que apunte a ella. Lo mismo pasa con:
  - `+0x1132a0`, `+0x113b80` y `+0x113b90`, que devuelven bsi `+4`, `+8` y `+0xc`;
  - `+0x112870`, que analiza la cabecera, prepara y programa el bucle, pero no decodifica;
  - `+0x113660`, `+0x1135d0` y `+0x113b30`.

  Son copias fuera de línea de funciones que el compilador expandió en línea. Los accesores muertos de
  bsi `+8` y `+0xc` indican que la biblioteca real rellena más campos, pero el juego no los lee (1.4).
- **[V]** Valores devueltos: el de `CreateDecoder` se guarda como identificador (flujo `+0x00`,
  `+0x113177`) sin comprobarlo; el de `Decode` se compara con 0 (`+0x11339d`); los de `ClearContext`
  (`+0x113544`) y `DeleteDecoder` (`+0x112d2d`) se ignoran.
- **[V]** Como mucho un decodificador vivo por hilo. `CreateDecoder` solo está en la preparación, que
  solo se llama desde `+0x11276b` (la otra llamada, `+0x1128ac`, está en `+0x112870`, muerta). El
  bucle de decodificación lo borra (`+0x112d28`) antes de repetir. Después de `CreateDecoder` la
  preparación ya no tiene ninguna salida con error (`+0x113177`…`+0x11326f`), así que no se pierde
  ningún identificador.
- **[H]** Hasta 32 decodificadores a la vez, uno por hilo `at9`.

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
| `+0x38` | tramas que se van a decodificar: `⌈muestras totales / 256⌉ + 1` (`+0x1131e5`…`+0x1131f1`); con bucle, `⌊fin / 256⌋ + 2` (1.7) |
| `+0x3c` | muestras que se dejan de la última trama (0 = entera): `muestras totales mod 256`; con bucle, `(fin + 1) mod 256` (1.7) |
| `+0x40`, `+0x44`, `+0x48` | supertrama de inicio, tramas que se descartan dentro de ella y muestras que se recortan de la siguiente |
| `+0x4c` | bytes pendientes de la supertrama actual |
| `+0x50` | índice absoluto de trama: empieza en supertrama × tramas por supertrama (`+0x113238`, `+0x113576`) y sube uno por llamada a `Decode` (`+0x1133ce`) |
| `+0x54` | posición en muestras |
| `+0x58`, `+0x5c` | vueltas del bucle: total y las que quedan (1.7) |
| `+0x60`, `+0x64` | inicio y fin del bucle en muestras, ya sin el retardo (`+0x11325a`, `+0x113268`) |
| `+0x68`, `+0x6c` | conjunto de banderas: `+0x68` = número de banderas (2, `+0x113a7c`), `+0x6c` = bits; la bandera *i* es el bit `31 − i`. 0 = bucle activo, 1 = primera trama ya descartada |
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
  la biblioteca devolviera k bytes más de los que recibió, `pendientes` daría la vuelta, el AU
  siguiente empezaría k bytes después del final de la supertrama (la resta es de 32 bits, `+0x11337a`)
  y `au.bytes` valdría casi 4 GiB.
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
- **[V] Medido** con una emulación de la rutina `+0x1132b0` y del salto de bucle sobre los cinco clips
  (`out/spec4-t2/da_verify.c`, 2026-10-20):
  - con la regla de arriba, dos vueltas de bucle sin ningún error, y los pendientes a 0 en cada
    frontera de supertrama;
  - si la última trama devuelve solo lo usado, LibAtrac9 rechaza el relleno: la 5.ª, 9.ª o 13.ª
    llamada da `0x82000002` o `0x82100006`, según el clip. Por 1.6, la pista se cortaría.
- **[V]** LibAtrac9 no recibe el tamaño del AU. Su lector de bits solo guarda el puntero y la posición
  (`bit_reader.c:6-10`), lee hasta 3 bytes por delante (`PeekInt`, `bit_reader.c:34-66`) y los bytes
  usados se calculan al final (`decoder.c:24`). En una supertrama sin relleno (5 844 de las 10 074 de
  `bgm_typ`), la última trama lee más allá del final del AU. En el juego no pasa nada: el AU vive en el
  búfer `at9 input` de `0x800` bytes y el bloque es de 512 bytes como mucho en las cinco
  configuraciones.
- **[D]** El runtime copia los `au.bytes` a un búfer propio de tamaño fijo, con ceros detrás, y
  decodifica desde ahí; si `au.bytes` no cabe, error. Si LibAtrac9 usa más bytes de los que trae
  `au.bytes`, la trama está cortada: error, sin escribir `au.bytes`.
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
- **[V]** El juego lee `pcm.bytes` después de `Decode` (`+0x113409`, `+0x113429`). **[H]** Que la
  biblioteca lo sobrescriba con los bytes producidos: a 48 kHz son `256 × canales × 2`, la misma
  capacidad que el juego ya había escrito, así que el juego no puede distinguirlo. **[D]** El runtime
  escribe los bytes producidos.
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
  mira dos banderas de la petición (objeto del hilo `+0x68`/`+0x6c`, el mismo esquema que en el flujo):
  - la 4 (parar, `+0x11279e`…`+0x1127c2`): si está puesta, vuelve a esperar otra petición;
  - si no, la 1 (repetir, `+0x1127cf`…`+0x1127eb`), que pone `+0x10fcb0` (1.7). Si está puesta,
    vuelve a leer la cabecera y llama otra vez a `CreateDecoder` desde el principio (`+0x112740`).

  **[H]** Con una pista que repite y un `Decode` que siempre falla, el hilo daría vueltas sin esperar
  ningún evento, solo la E/S de cada vuelta (la cabecera y una supertrama), hasta que otro hilo ponga
  la bandera 4 o borre la 1: `CreateDecoder`, un `Decode` fallido y `DeleteDecoder`, una y otra vez.
  Una respuesta de error provisional en `Decode` no puede llegar a una ejecución real.
- **[V]** Con un identificador negativo (un `CreateDecoder` fallido), la rutina devuelve "falso" sin
  llamar a `Decode` y no hay `DeleteDecoder` (`+0x112d24`). Con la bandera de repetir, el resultado es
  el mismo bucle sobre `CreateDecoder`.
- **[V]** El fin de la pista no lo señala la biblioteca. El juego cuenta tramas (`+0x50` contra `+0x38`)
  y deja de la última solo `+0x3c` muestras (`+0x113491`…`+0x1134aa`). Además tiene un corte
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
    (objeto del hilo `+0xac`) vale de 1 a 3 (`+0x10fcb6`, `+0x10fd11`…`+0x10fd2b`). Con otro modo lo
    desactiva y deja `+0x38`/`+0x3c` en el final de la pista (`+0x10fde3`…`+0x10fe52`). Sin puntos de
    bucle, el modo 1 pone la bandera de repetir del hilo y los demás la borran
    (`+0x10fcbe`…`+0x10fcf8`).
  - Fija la supertrama del inicio del bucle (flujo `+0x40`), las tramas que hay que descartar
    dentro de ella y las muestras que hay que recortar. Con el fin del bucle fija
    `+0x38 = ⌊fin / 256⌋ + 2` y `+0x3c = (fin + 1) mod 256` (`+0x10fd81`…`+0x10fdb2`).
  - También recibe el número de vueltas (objeto del hilo `+0xc0`, `+0x11278b`). Con 2 o más, el
    bucle se repite `vueltas − 1` veces (`+0x58`, `+0x5c`; `+0x10fd73`, `+0x10fd79`) y la última pasada
    llega al final de la pista (`+0x1134d3`…`+0x113504`). Con menos de 2, el bucle no acaba y además
    se pone la bandera de repetir (`+0x10ff26`…`+0x10ff43`).

  Al llegar al final, la rutina de decodificación:
  1. recalcula los descartes (`+0x113507`…`+0x113537`);
  2. llama a `ClearContext(id)` (`+0x11353f`);
  3. pone la lectura al principio de esa supertrama y los pendientes a 0;
  4. borra la bandera 1, así que la primera trama siguiente se descarta (`+0x113544`…`+0x113580`).
- **[V] Repetir la pista entera o empezar en otra posición: `DeleteDecoder` + `CreateDecoder`.** La
  posición inicial (objeto del hilo `+0x90`) entra en la preparación (`+0x11275f`), que calcula la
  supertrama, las tramas y las muestras que se descartan (`+0x1131dd`…`+0x11322e`). Después se pone a 0
  (`+0x112778`), así que repetir empieza desde el principio. No hay ningún otro sitio que llame a
  `ClearContext`.
- **[V] `ClearContext` tiene que dejar LibAtrac9 al principio de una supertrama.** LibAtrac9 lleva su
  propio índice de trama dentro de la supertrama (`Frame.IndexInSuperframe`, `unpack.c:36-46`). Si le
  llega una trama marcada como primera de supertrama con el índice ≠ 0, devuelve
  `ERR_UNPACK_SUPERFRAME_FLAG_INVALID` (`0x82000000`). Medido sobre los cinco clips: decodificar una
  trama de la supertrama 0 y después la primera de la supertrama 1 da `0x82000000` en todos.
- **[V] El salto cae casi siempre a mitad de supertrama.** Con el bucle activo, el juego llama a
  `ClearContext` cuando el índice de trama `+0x50` llega a `+0x38`. Así que en la supertrama donde
  acaba el bucle se han decodificado `+0x38 mod 4` tramas (4 tramas por supertrama en las cinco
  configuraciones):
  - en los clips, 3 en `vse_mono`, 1 en `vse_mono_small`, 2 en `vse_stereo` y 0 (frontera) en
    `bgm_typ` y `bgm_rare`;
  - en el dump, 93 de las 132 entradas de `bgm` y 1 040 de las 1 327 de `vse` tienen un bucle `smpl`
    que acaba a mitad de supertrama (si el juego activa su bucle, 1.9).

  Sin reiniciar el índice, el primer `Decode` tras el salto fallaría y, por 1.6, cortaría la pista.
- **[V] Volver a llamar a `Atrac9InitDecoder` sobre el mismo handle no lo reinicia.** `InitDecoder`
  (`decinit.c:27-37`) no toca `Frame.IndexInSuperframe` ni `ImdctPrevious`: solo los pone a 0 el
  `calloc` de `Atrac9GetHandle` (`libatrac9.c:8-11`). Medido con la emulación de 1.3 en los tres clips
  que saltan a mitad de supertrama:
  - sin hacer nada, o con `Atrac9InitDecoder` sobre el mismo handle, el primer `Decode` tras el salto
    da `0x82000000`;
  - liberando el handle y creando otro, o poniendo el índice a 0 a mano, no hay ningún error;
  - entre esas dos, las tramas 2 a 8 tras el salto salen idénticas. La 1 difiere en `bgm_typ` y
    `vse_stereo` por el historial del IMDCT, y es justo la que el juego descarta (1.5).
- **[D] `ClearContext` libera el handle y crea otro** (`Atrac9ReleaseHandle` + `Atrac9GetHandle` +
  `Atrac9InitDecoder`), como `at9_reset` en `src/runtime_ajm.c`. Usa solo la API pública y deja el
  decodificador como recién creado. `Atrac9InitDecoder` reescribe las tablas globales (con los mismos
  valores), así que va con el rwlock en escritura, como `CreateDecoder`; pasa una vez por vuelta del
  bucle. Criterio de prueba (tarea 4): `ClearContext` después de 1, 2 y 3 tramas de una supertrama, y
  el `Decode` siguiente da 0. Los dos clips de música vuelven en frontera de supertrama y no lo
  ejercitan.

### 1.8 Contrato que se deduce

| Función | Qué tiene que hacer el runtime |
|---|---|
| `InitLibrary(1)` | 0 (el juego no mira el valor) |
| `CreateDecoder(ctrl, 1)` | validar `ctrl` y los parámetros (`tamaño = 0xC`, `config[0] = 0xFE`), crear el decodificador, escribir bsi `+4` = canales y devolver un identificador ≥ 0 |
| `Decode(id, ctrl)` | una trama, desde una copia del AU con ceros detrás: PCM de 16 bits entrelazado en `pcm.datos`, `pcm.bytes` = bytes producidos, `au.bytes` = bytes consumidos (en la última trama de la supertrama, todo el `au.bytes` de entrada); 0 si va bien |
| `ClearContext(id)` | volver al estado de recién creado: contador de tramas del runtime a 0 y un handle de LibAtrac9 nuevo; volver a llamar a `Atrac9InitDecoder` no basta (1.7) |
| `DeleteDecoder(id)` | liberar; el juego no mira el valor |
| `TermLibrary(1)` | liberar todo; admite otro `InitLibrary` después, y los identificadores de antes dan error (1.1) |

Un error en cualquier llamada corta la pista sin avisar (1.6), así que cada suposición se comprueba en
cada llamada y se traza cuando no se cumple.

### 1.9 Preguntas para la ejecución de confirmación (tarea 7)

- Qué pistas piden bucle (`smpl`) o repetir, con qué modo y cuántas vueltas, y en qué trama de la
  supertrama cae cada `ClearContext`.
- Si se llama `TermLibrary` durante la partida (la ranura `+0x18` de la vtable `0x432318`, 1.1).
- Cuántos decodificadores hay vivos a la vez y desde qué hilos.
- Que el desfase acumulado del trazador (bytes que avanza el juego menos bytes usados por LibAtrac9 y
  relleno) sea 0 en todas las supertramas.
- Que ningún `Decode` reciba un AU que no empiece en una trama.
- Que el runtime no rechace ningún búfer del juego por no estar en memoria del guest. La comprobación
  (`runtime_memory_is_mapped`) solo reconoce los mapas de memoria directa y flexible, no la imagen
  del eboot ni las pilas de los hilos. **[H]** Todos están en esa memoria: el bloque de control y el
  `at9 input` salen del asignador del juego (`+0x12c430`; `0x1006eb2048` en el log del `STOP:`, por
  encima de `USER_MIN` = `0x1000000000`), y el PCM del objeto del hilo `+0x98` (`+0x11291a`). En el
  log no debe aparecer ningún `is not mapped guest memory`, y las direcciones de `ctrl`, `au.data` y
  `pcm.data` deben estar por encima de `USER_MIN`.
- Qué suena primero después del prólogo.
