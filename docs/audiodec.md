# Audiodec en Tsukihime (CUSA47999)

Cómo usa el juego `libSceAudiodec` (ATRAC9) y `libSceAudiodecCpu` (HEVAG), y qué contrato tiene que
cumplir el runtime (`src/runtime_audiodec.c` y `src/runtime_audiodec_cpu.c`). La spec es
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

*Ejecución trazada (2026-10-21, 1.10):* el juego confirma el contrato de 1.8. Hubo 158 decodificadores
sin errores y con desfase 0, y los 18 `ClearContext` cayeron a mitad de supertrama, donde predice 1.7.

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
  `InitLibrary` después de `TermLibrary` (1.8). *(2026-10-21, 1.10: no se llamó en 12 minutos de
  juego.)*
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
- **[H]** Hasta 32 decodificadores a la vez, uno por hilo `at9`. *(2026-10-21, 1.10: como mucho 9 vivos
  a la vez en la ejecución, con 26 bloques de control distintos, uno por hilo.)*

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

*Respondidas el 2026-10-21 en 1.10, menos la última: la ejecución trazada arrancó con una partida con
progreso, sin el prólogo.*

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

### 1.10 Lo observado en la ejecución (tarea 7, 2026-10-21)

La ejecución `a2` duró 12 min 13 s. Arrancó con la partida con progreso, así que fue directa al menú de
título; el usuario jugó hasta el título del capítulo 1. Se lanzó con `GOW3_TRACE_AUDIODEC=2` (cada
llamada, sin tope) y `GOW3_AUDIODEC_AU_DUMP`.

- Log: `out/run-audiodec-a2.log` (498 MB).
- Volcados: `out/audiodec-au-a2/`.
- Análisis: `out/spec4-t7/analyze.py` → `a2-analysis.txt`.
- Todo sin versionar; es contenido del juego.

`analyze.py` identifica cada flujo por su contenido. Compara cada volcado con los datos de todas las
entradas de `bgm` y `vse` que empiezan igual. Luego recorre lo que consumió cada `Decode` sobre los datos
de la pista, desde el principio y, después de cada `ClearContext`, desde la supertrama del inicio del
bucle (1.7).

- **[V] Ningún error en 158 decodificadores.** Hubo 303 323 tramas: 1 `InitLibrary`, 158
  `CreateDecoder`, 303 323 `Decode`, 18 `ClearContext` y 158 `DeleteDecoder`, todos con resultado
  ≥ 0. No hay `STOP:`, `Fault:` ni ningún búfer rechazado por la comprobación de memoria del guest.
  Todas las direcciones de `ctrl`, `au.data` y `pcm.data` están por encima de `USER_MIN`; la más baja
  es `0x1006e71348`. Con las dos ejecuciones del 2026-10-20 son 277 decodificadores sin un error.
- **[V] Desfase 0 en las 303 323 tramas.** El AU siempre empieza en una trama, y lo que el juego avanza
  coincide con lo que consume LibAtrac9 más el relleno de la última trama (1.3 [D]).
- **[V] Lo consumido es la pista, byte a byte.** Los 54 flujos que son entradas de `bgm` o `vse` se
  reproducen exactos sobre los datos de su pista, también tras cada salto de bucle.
- **[V] El PCM es el que espera el juego** (1.5): capacidad y `pcm.bytes` de `0x400` en estéreo y `0x200`
  en mono, es decir, 256 muestras × canales × 2. El usuario oye la música y los efectos bien.
- **[V] bsi** (1.4): el juego lo entrega con `tamaño = 0x24` y ceros. Tras `CreateDecoder` vale `+4` =
  canales (2 en 54 flujos, 1 en 104) y el resto sigue a cero.
- **[V] Secuencia por pista** (1.1): `CreateDecoder` → `Decode` → (`ClearContext` → `Decode`)* →
  `DeleteDecoder`.
  - Tras `CreateDecoder` y tras cada `ClearContext`, el primer `Decode` es la trama 1 de su supertrama
    y recibe la supertrama entera.
  - Las tramas siguen 1, 2, 3, 4, y la última consume el resto.
  - Los 158 terminan con `DeleteDecoder`.
- **`TermLibrary`** no se llamó en 12 minutos de juego. Sigue **[H]** que solo llegue al cerrar.
- **[V] Decodificadores a la vez** (1.1): como mucho 9 vivos a la vez y 26 bloques de control distintos.
  Encaja con un bloque por hilo `at9`, de los que hay 32: el bloque está en el objeto de flujo del hilo
  y su dirección no se repite entre hilos.
- **[V] Bucles** (1.7): 5 decodificadores llamaron a `ClearContext`, 18 veces en total. **Todos** lo
  hacen en la trama que predice la fórmula de 1.7, y siempre a mitad de supertrama:

  | Pista | `ClearContext` | Trama de la supertrama | `Decode` por pasada (medido = previsto) |
  |---|---|---|---|
  | `bgm 34` (música del juego) | 2 | 1 | 33 437, luego 27 645 |
  | `vse 885` (dos flujos) | 13 + 1 | 2 | 3 114 |
  | `vse 574` (dos flujos) | 1 + 1 | 3 | 8 979 la primera pasada; el juego la borró en la segunda |

  Es la prueba en el juego de que `ClearContext` tiene que crear un handle nuevo de LibAtrac9: con el
  mismo handle, el primer `Decode` de cada vuelta habría dado `0x82000000` y cortado la pista.
- **[V] Pistas sin vuelta.** `bgm 2` (la música del menú de título, el primer flujo) y `bgm 52` se
  borraron antes del final de su bucle, cuando cambió la escena.
  - 42 efectos `vse` terminaron 1 o 2 tramas antes de `+0x38`, sin ningún error. **[H]** Sería el corte
    propio del juego cerca del final (1.6).
  - Otros 7 los cortó el juego antes, al cambiar de escena.
- **Qué suena primero** con la partida actual: `bgm 2`, en el menú de título (`ctrl` `0x1006eb2048`).
  Después del prólogo queda sin ver con traza. El log de la ejecución que pasó el prólogo se perdió
  (paso 1), aunque su primer flujo tenía el mismo `ctrl` y la misma configuración `fe740ff0`.
- **[H] Voces.** Hay 104 flujos mono `fe700bf0` (83 576 tramas, ~446 s) que no son ninguna entrada de
  `bgm` ni de `vse`: solo comparten con los `vse` mono 775–783 la primera supertrama, de silencio. Son
  103 flujos distintos, y LibAtrac9 los decodifica sin errores. Lo más probable es que sean las voces de
  `voice.mrg`, que el juego ya entrega descomprimidas (sin `MZX`). Lo confirma la tarea 8. *(Tarea 8:
  **[V]**, son voces de `voice.mrg` descomprimidas, 1.11. Y son 102 flujos distintos, no 103: dos
  entradas se decodificaron dos veces.)*
- **Fixture dorado** (paso 4): `tests/data/audiodec_ctrl_stereo.bin` (`bgm 2`, decodificador 1) y
  `audiodec_ctrl_mono.bin` (el primer flujo mono, decodificador 11). Son el bloque de control tal como
  lo trazó el juego, antes y después de `CreateDecoder` y de su primer `Decode`: `ctrl`, AU, PCM,
  parámetros y bsi, sin audio. `golden_blocks_from_the_game` (`tests/test_audiodec.c`) exige que el
  runtime deje el mismo bloque.
- **Coste de la traza.** Con `=2`, el hilo `hevag` escribe dos líneas por trama. El puerto de HEVAG
  (puerto 2, 256 tramas) se quedó sin datos en 19 de 146 ventanas de 5 s; con `=1` le pasó en 1 de 112.
  Hoy no se oye, porque HEVAG suena en silencio hasta la tarea 12. Los puertos de la música (0 y 1) solo
  se quedaron sin datos en la primera ventana, al arrancar, igual que sin traza.

### 1.11 Las voces y la reproducción offline (tarea 8, 2026-10-21)

**[V] De dónde salen las voces.** `voice.hed` ya está descifrado:
- Cada entrada ocupa 4 bytes: `{u16 sector bajo; u16 (bits 16–19 del sector << 12) | sectores}`, con
  sectores de `0x800`. Hay 16 468 entradas, encadenadas sin huecos; las 4 últimas son centinelas con
  `0xFFFF` en las dos mitades.
- `voice.nam` lleva un nombre de 8 bytes por entrada (`A00_`, `B80_`…), 16 464 en total, y 2 bytes
  más al final.

Cada entrada de `voice.mrg` empieza con una cabecera `LV` de 7 bytes. Después vienen `MZX0`, un `u32`
con el tamaño descomprimido y los datos MZX.
- MZX trabaja con palabras de 16 bits. Los 2 bits bajos de cada byte de control eligen una orden:
  repetir la última palabra, copiar desde atrás, tomar una palabra de un anillo de 64 o copiar
  literales; los 6 bits altos dan la cuenta.
- En las voces, las palabras van tal cual, sin invertir los bits (`xor 0xFF`).
- Una copia desde atrás también cambia la palabra que repite la orden siguiente.

Dentro hay un RIFF ATRAC9 como los de 2.2 de la spec: 16 460 entradas `fe700bf0` (mono, bloque 384) y
dos de cada una de `fe7004f0` y `fe740bf0`. El descompresor provisional es `out/spec4-t8/mzx_match.c`,
sin versionar, porque el runtime no necesita MZX.

- **[V] El juego descomprime él mismo.** Los 104 flujos mono de `a2` coinciden byte a byte con el
  principio del `data` de 102 entradas, de la 15490 a la 15615 (nombres `B80_`), en el orden de la
  escena. El AU que llega a `Decode` es ATRAC9 normal, nunca `LV`/`MZX0`.
  - Dos entradas se decodificaron dos veces. La 15585 lo hizo con dos decodificadores a la vez: son los
    ids 11 y 12, los primeros flujos mono. La 15490 lo hizo dos veces seguidas, con los ids 13 y 14.
  - En la ejecución `a4` (abajo) hay 31 flujos de voz, de 29 entradas entre la 15487 y la 15592.
- **Ninguna voz llega al final de su `data`.** A cada una le faltan entre 345 y 6 045 bytes.
  - 40 acaban a menos de una supertrama (384 bytes) del final.
  - Las otras 64 las cortó el jugador al avanzar el texto.
  - El corte no sale exacto de las muestras y el retardo del bloque `fact`: se desvía una trama arriba
    o abajo según la voz.
  - **[H]** Es el corte propio del juego, como en los efectos (1.6). No hay ningún error.
- **Ejecuciones de la tarea 8,** las dos con `GOW3_TRACE_AUDIODEC=1` y `GOW3_AUDIODEC_AU_DUMP`. Ninguna
  tiene `STOP:`, `Fault:`, búferes rechazados ni desfase distinto de 0.

  | Ejecución | Quién y cuánto | Log | Volcados | Decodificadores | Tramas | Errores |
  |---|---|---|---|---|---|---|
  | `a4` | el usuario, escenas con voces, unos 3 min | `out/run-audiodec-a4.log` | `out/audiodec-au-a4/` | 62 | 59 453 | 0 |
  | `a5` | menú de título, cerrada con `SIGTERM` tras la música | `out/run-audiodec-a5.log` | `out/audiodec-replay/` | 10 | 14 405 | 0 |

**Reproducción offline.** Desde la tarea 8, `GOW3_AUDIODEC_AU_DUMP=<dir>` escribe dos archivos por
decodificador:
- `dec<id>.au`, lo consumido;
- `dec<id>.txt`, con una línea `config <hex>` y una línea `clear <n>` por cada `ClearContext`, donde
  `n` son los bytes volcados antes de él.

El test `voice_stream_replay_matches_libatrac9` (`tests/test_audiodec.c`) toma el directorio de
`argv[2]`, o `out/audiodec-replay` por defecto.
- Pasa cada flujo por la API, tal como lo entregó el juego, y por LibAtrac9 directamente.
- En cada `ClearContext` empieza de nuevo con un estado limpio.
- Exige el mismo PCM en cada trama y que lo consumido sume exactamente los bytes volcados.
- Si no hay volcados, imprime `SKIP:`, porque los volcados son contenido del juego y no se versionan.

| Corpus | Flujos | Tramas | `ClearContext` |
|---|---|---|---|
| `a2`: `.txt` reconstruidos de la traza con `out/spec4-t8/make_notes.py`, en `out/spec4-t8/replay-a2` | 158 | 303 323 | 18 |
| `a4` sin `dec38` | 61 | 50 650 | 0 |
| `a5` (`out/audiodec-replay`, el que usa `build.sh --test`) | 10 | 14 405 | 0 |

Lo que muestran los corpus:
- En los tres, las tramas reproducidas son exactamente las que contó el runtime.
- Ocho mutantes caen dentro de la reproducción (`out/spec4-t8/mutate.py`):
  - del runtime, la última trama sin el relleno, `ClearContext` sobre el mismo handle o sin reiniciar
    la trama, y el PCM con una muestra de menos;
  - del test, la reproducción sin `ClearContext`;
  - del corpus, un `clear` corrido, uno perdido y una configuración cambiada.

**[V] Volcados al cerrar.** Al cerrar la ventana, el juego termina en `runtime_report` y `_Exit(0)`
(`gpu/shim/gow3gpu.cpp`) sin cerrar archivos. SDL convierte `SIGTERM` en el mismo cierre.
- En `a4`, la música seguía viva al cerrar y perdió su `.txt` y el final de su `.au`, que estaban en
  el búfer de stdio.
- Desde la tarea 8, `runtime_audiodec_report` escribe a disco los volcados de los decodificadores vivos.
- En `a5`, la música (`dec3`) estaba viva al cerrar: se escribió en el último segundo del log y se
  reproduce entera.
- El volcado de pilas por `SIGALRM` no pasa por el informe, así que ahí se siguen perdiendo los finales.

## 2. HEVAG (`libSceAudiodecCpu`) — desensamblado previo (2026-10-20, tarea 6)

Solo lo que hace falta para fijar la respuesta de error de la tarea 6. La fase A de HEVAG (tarea 10)
lo contrasta con el PR #5289 de shadPS4 y con una ejecución en 2.6, que responde las preguntas de 2.5.
Lo que implementa la tarea 12 y lo que se vio en el juego con el decodificador está en 2.7.

### 2.1 Quién llama

- **[V]** Las tres funciones están en dos rutinas de un objeto de flujo HEVAG (vtable `0x432430`, con
  el bloque de control en `+0x30`):
  - la preparación `+0x1155b0`, con `QueryMemSize` en `+0x115788` e `InitDecoder` en `+0x115811`;
  - la decodificación `+0x115880`, con `Decode` en `+0x11590c`.

  Sus ranuras de la GOT son `0x435850`, `0x435858` y `0x435860` (`tools/got_import.py`).
- **[V]** La preparación la llama `+0x115d10` al empezar una voz (en `+0x115cc0`), que ignora lo que
  devuelve. Si la cabecera no empieza por `VAGp` (`+0x1155c1`), devuelve "falso" sin llamar a la
  biblioteca.
- **[V]** La decodificación la llama el bucle del mezclador (`+0x115f50`, dentro de `+0x115e80`), que
  pide muestras hasta tener las que necesita o hasta que la voz acaba.
- **[V]** El códec es siempre `0x1008` (`mov edx,0x1008` en `+0x11565b`, `+0x115804` y `+0x115903`):
  el `Hevag` del PR #5289.
- **[V]** No hay `Delete` ni `Term`. La memoria de trabajo es del juego, que la libera él mismo
  (`+0x11552c`, `+0x11557c`).

### 2.2 Estructuras

**Bloque de control**: `0xe0` bytes alineados a `0x40`, del asignador del juego (`+0x1154ce`; el
`+0x00` es la vtable `0x432470`). Antes de `QueryMemSize` el juego pone a cero `+0x28`…`+0xdf`
(`+0x1155f1`…`+0x115613`) y escribe:

| Desplazamiento | Contenido | Lo que escribe el juego |
|---|---|---|
| `+0x08` | `ctrl`: puntero a los parámetros | → `+0x58` |
| `+0x10` | puntero a la info del flujo (bsi) | → `+0x98` |
| `+0x18` | puntero al AU | → `+0x28` |
| `+0x20` | puntero al PCM | → `+0x40` |
| `+0x28` | AU, el descriptor de `0x18` bytes de ATRAC9 (1.2) | tamaño `0x18` |
| `+0x40` | PCM, igual | tamaño `0x18` |
| `+0x58` | parámetros, `0x40` bytes | tamaño `0x40`; en `+4`…`+0x33`, la cabecera `VAGp` sin sus bytes reservados (marca, versión, tamaño de los datos, frecuencia, canales en `+0x22` y nombre en `+0x24`); `+0x34` = 1 |
| `+0x98` | info del flujo, `0x30` bytes | tamaño `0x30` |
| `+0xc8` | recurso: la memoria de trabajo, el mismo descriptor | tamaño `0x18`; los datos y el tamaño, después de `QueryMemSize` |

Las tres funciones reciben `ctrl` (el bloque `+8`), el recurso (el bloque `+0xc8`) y el códec:
`QueryMemSize(ctrl, recurso, 0x1008)`. **[V]** El descriptor, el `ctrl` y la firma coinciden con los
del PR #5289 (2.4 de la spec), así que dejan de ser hipótesis. El `1` de los parámetros `+0x34` sigue
**[H]** (¿PCM de 16 bits, como el `1` de ATRAC9?).

### 2.3 Lo que hace el juego con cada resultado

- **[V] `QueryMemSize`:** no mira el valor devuelto.
  - Lee `recurso.bytes` (`+0x11578d`). Si es mayor que la memoria que ya tiene el flujo (`+0x1c`),
    reserva ese tamaño de su asignador (`+0x1157af`) y lo apunta.
  - Después pone `recurso.datos` (`+0x115809`) y llama a `InitDecoder`.
  - **El campo nunca está sin inicializar**, porque el juego lo pone a 0 antes de la llamada
    (`+0x1155f1`). Si sigue a 0, el juego no reserva nada y pasa en `recurso.datos` lo que tuviera:
    `NULL` la primera vez.
- **[V] `InitDecoder`:** tampoco mira el valor. La preparación devuelve "verdadero" (`+0x115834`) y
  quien la llama no lo mira.
- **[V] `Decode`:** tampoco mira el valor (`+0x115911` compara el contador de llamadas del flujo, no
  `eax`). En cada llamada:
  - **AU:** la primera vez, la cabecera de `0x30` bytes y una trama (16 bytes por canal); después,
    una trama. `au.datos` = cabecera + bytes ya consumidos.
  - **PCM:** capacidad = canales × máx(muestras pedidas, 28) × 2 bytes, como mucho `0x380`
    (`+0x1158d4`…`+0x1158ec`). Son 16 bits: el mezclador extiende palabras con signo (`+0x116145`).
  - **Después:** muestras = `pcm.bytes` / (canales × 2), salvo la primera llamada, que da 0 (es la de
    la cabecera). Avanza `au.bytes` y lo resta de lo que queda del flujo (`+0x115944`…`+0x115951`).
  - **bsi `+0x28`** (un byte): 7 acaba la voz y 6 apunta la posición (**[H]** inicio de bucle)
    (`+0x11595b`…`+0x115970`). Son las banderas de trama del VAG clásico.
  - Si no queda nada del flujo (`+0x115898`), acaba la voz sin llamar a la biblioteca.
- **[V] El mezclador** (`+0x115f50`) vuelve a llamar hasta tener las muestras que necesita o hasta que
  la voz acaba. Si `Decode` diera siempre 0 muestras y 0 bytes consumidos, el bucle no acabaría nunca.

### 2.4 Respuesta de la tarea 6 [D]

*Sustituida el 2026-10-21 por el decodificador de la tarea 12 (2.7). Un error de `Decode` sigue dejando
`pcm.bytes` = 0 y el AU como llegó, y ahora también la bandera 7, para que la voz acabe en el acto.*

Hasta que exista el decodificador (tareas 10–12), las tres devuelven `API_FAIL` (`0x80F70000`) y lo
registran como las de ATRAC9 (spec 3), con su propio tope de 400 líneas. Como el juego no mira el
valor, lo que decide lo que hace después es lo que la respuesta deja en el bloque:

| Función | Qué deja |
|---|---|
| `QueryMemSize` | `recurso.bytes` = 0, explícito aunque el juego ya lo puso a 0: no reserva nada |
| `InitDecoder` | nada |
| `Decode` | `pcm.bytes` = 0 (ninguna muestra) y `au.bytes` como llegó (el juego da el AU por consumido) |

Así la voz avanza por sus datos sin producir nada y acaba cuando se terminan: silencio, sin bucle. Las
otras opciones fallan:

- con `pcm.bytes` como llegó, el juego mezclaría en cada llamada 28 muestras de lo que tuviera su
  búfer (`0x3000` bytes, `+0x115c33`);
- con `au.bytes` = 0, la voz no avanza y el bucle del mezclador no acaba.

Solo se escribe en un descriptor mapeado y de `0x18` bytes. Un códec distinto de `0x1008` da `STOP:`.
El test `cpu_errors_end_the_voice_in_silence` (`tests/test_audiodec.c`) emula las tres rutinas del
juego.

**[V]** Para que la voz acabe, lo que queda del flujo tiene que llegar justo a 0. El tamaño de la voz
(la preparación lo recibe en `edx` y lo guarda en el flujo `+0x10`; **[H]** es el tamaño de la entrada
del `.hed`) tiene que ser `0x30` + un múltiplo de 16 × canales. Lo cumplen las 19 entradas de
`vag.mrg` (2026-10-20): todas son mono salvo `dummy`, que es estéreo (1 984 bytes = 62 × 32).

### 2.5 Para la fase A de HEVAG (tarea 10)

*Respondidas el 2026-10-21 en 2.6.*

- Cuánta memoria de trabajo pide la biblioteca real y si guarda ahí el estado del decodificador (no
  hay `Delete`, así que el plan lo quiere en esa memoria).
- Qué consume la primera llamada (¿solo la cabecera, `0x30` bytes, o también la trama?) y qué escribe
  en bsi, además de `+0x28`.
- Qué son el `1` de los parámetros `+0x34` y el campo `+0x18` del flujo.
- Cómo junta el juego las parejas `_L`/`_R`.
- De dónde sale el tamaño de la voz (`+0x115d09`).

### 2.6 Fase A de HEVAG (tarea 10, 2026-10-21)

Fuentes:

- el PR #5289 tal como está el 2026-10-21;
- el desensamblado de la preparación (`+0x1155b0`), la decodificación (`+0x115880`), el arranque de la
  voz (`+0x115cc0`) y el mezclador (`+0x115e80`);
- las 19 entradas de `vag.mrg`;
- una ejecución trazada, `out/run-audiodec-b1.log` (`GOW3_TRACE_AUDIODEC=2`, partida actual, menú de
  título, cerrada con `SIGTERM` 60 s después de abrirse la ventana). El análisis es
  `out/spec4-t10/analyze.py` → `out/spec4-t10/b1-analysis.txt` (sin versionar).

#### El PR #5289

- **[V]** Sigue abierto y sin fusionar: un solo commit (`27bd9b5`) y la última actividad el 2026-10-08.
  Su autor dice que escribe un decodificador HEVAG en HLE, pero no hay PR de eso (búsqueda en GitHub
  del 2026-10-21).
- El PR no decodifica. Reenvía cada llamada a la tabla de funciones que exporta
  `libSceAudiodecCpuHevag.sprx` del firmware (NID `lYA31T9O1KU`), que no está en el dump. Por eso no
  dice nada del tamaño de la memoria de trabajo, de la bsi ni del formato del PCM.

| Dato del PR | En el juego | Estado |
|---|---|---|
| `Hevag = 0x1008` | `mov edx,0x1008` en las tres llamadas (2.1) | [V] |
| descriptor `{this_size; reserved; data /*+8*/; data_size /*+0x10*/; reserved2}` de `0x18` bytes | AU, PCM y recurso del bloque (2.2) | [V] |
| `QueryCtrl {param}`, `InitCtrl {param, bsi_info}`, `DecodeCtrl {param, bsi_info, au_info, pcm_item}` | el juego pasa siempre el mismo `ctrl` de `0x20` bytes (bloque `+8`) | [V] |
| firma `(ctrl, recurso, códec)` | las tres llamadas | [V] |
| `QueryMemSize` escribe el tamaño en `data_size`; `Decode` cambia las capacidades del AU y del PCM por los bytes consumidos y producidos | el juego lee `recurso.bytes` (`+0x11578d`), `au.bytes` y `pcm.bytes` (`+0x115917`…`+0x115951`) | [V] |
| errores `0x80F70000`…`0x80F70015` | el juego no mira el valor devuelto (2.3) | [H] |
| `ClearContext` | el juego no la importa | — |

Los errores del PR, para la tarea 12 (todos **[H]**: salen del PR, no de la biblioteca real):
`API_FAIL` `0x80F70000`, `INVALID_TYPE` `…01`, `ARG` `…02`, `INVALID_SIZE` `…03`, `INVALID_PARAM_SIZE`
`…04`, `INVALID_BSI_INFO_SIZE` `…05`, `INVALID_AU_INFO_SIZE` `…06`, `INVALID_PCM_ITEM_SIZE` `…07`,
`INVALID_RESOURCE_SIZE` `…08`, `INVALID_CTRL_POINTER` `…09`, `INVALID_PARAM_POINTER` `…0A`,
`INVALID_BSI_INFO_POINTER` `…0B`, `INVALID_AU_INFO_POINTER` `…0C`, `INVALID_PCM_ITEM_POINTER` `…0D`,
`INVALID_RESOURCE_POINTER` `…0E`, `INVALID_AU_POINTER` `…0F`, `INVALID_PCM_POINTER` `…10`,
`INVALID_WORK_MEM_POINTER` `…11`, `INVALID_WORD_LENGTH` `…12`, `INVALID_AU_SIZE` `…13`,
`INVALID_PCM_SIZE` `…14` e `INVALID_WORK_MEM_SIZE` `…15`.

El despachador del PR comprueba, en este orden:

1. los punteros nulos: `ctrl`, parámetros, bsi, el descriptor del AU y sus datos, y el descriptor del
   PCM y sus datos;
2. que el AU y el PCM no midan 0;
3. que el AU, el PCM y el recurso tengan `this_size` = `0x18`;
4. que el recurso no sea nulo y, en `InitDecoder` y `Decode`, que la memoria de trabajo tampoco.

#### Lo que manda el juego (ejecución b1)

- **[V]** En el menú de título, 38 voces en 60 s: `moon_se00001` 13 parejas, `moon_se00002` 4 y
  `moon_se00003` y `moon_se00004` una. La primera es la pareja `moon_se00001_L`/`_R`.
- **[V]** Todas son mono, a 48 000 Hz, versión `0x00020001`, parámetros `+0x34` = 1 y códec `0x1008`.
  Los parámetros son `{u32 tamaño = 0x40; "VAGp"; versión; 0; tamaño de los datos y frecuencia, en
  big-endian; …; canales en +0x22; nombre en +0x24; 1 en +0x34}`.
- **[V] Solo hay dos bloques de control:** `_L` va siempre en uno y `_R` en el otro. Cada voz nueva
  llama una vez a `QueryMemSize` y a `InitDecoder` sobre el mismo bloque y la misma memoria de trabajo
  (la del flujo, `+0x40`, que solo crece). Así que `InitDecoder` tiene que dejar el estado como nuevo.
- **[V] AU:** la primera llamada manda `0x40` bytes desde la cabecera (`au.datos` empieza por `VAGp`);
  después, 16 bytes cada vez, seguidos.
- **[V] PCM:** capacidad de 56 bytes, 28 muestras mono. Es `canales × máx(muestras de la llamada
  anterior, 28) × 2`, con tope `0x380` (2.3). Un `Decode` que da una trama por llamada recibe siempre
  `56 × canales`.
- **[V] El tamaño de la voz es el de la entrada del `.hed`, en sectores de `0x800`**, no `0x30` más el
  tamaño de los datos. En `moon_se00001_L` el juego avanzó 20 480 bytes, y el `VAGp` mide 18 800. Lo lee
  del campo `+0x10` del objeto de datos del sonido (`+0x115d09`). Detrás del `VAGp`, hasta el final del
  sector, hay tramas `0c 00 …` (desplazamiento 12 y códigos a cero): silencio.
- **[V] Las 19 entradas terminan en una trama con bandera 1 seguida de otra con bandera 7** (en
  `dummy`, una pareja de cada). No hay banderas de bucle: solo 0, 1 y 7.
- **Por eso, con un decodificador de verdad, la voz tiene que acabar por la bandera 7** de bsi `+0x28`.
  Si no, las voces mono reproducirían el relleno. En `dummy` (estéreo, 16 bytes de relleno y tramas de
  32) lo que queda del flujo no llegaría nunca a 0 y daría la vuelta.
- **[V]** De la bsi, el juego solo lee el byte `+0x28` (`+0x11595b`). El resto del código solo la pone
  a cero y escribe su tamaño (`+0x115603`…`+0x115647`).
- **[V]** Todos los búferes están en memoria del guest por encima de `USER_MIN`. El bloque y el búfer
  de PCM de la voz (`0x3000` bytes, `+0x115c33`) salen del asignador del juego, y `au.datos` apunta a
  los datos de `vag.mrg` cargados. No hay ninguna línea con `unmapped` ni `null`.
- **[V] El mezclador** (`+0x1160b4`…`+0x11624e`) divide cada muestra entre 32 767 y la suma a la salida
  estéreo en coma flotante. Una voz mono va con el panorama de la voz (la rampa `+0xc8`, que empieza en
  0,5): izquierda = mín(2(1 − p), 1) y derecha = mín(2p, 1). Una estéreo va por parejas entrelazadas.
- **[V] Las parejas `_L`/`_R` son dos voces mono que mezcla el juego.** La biblioteca decodifica cada
  una por separado y no junta nada. **[H]** Que `_L` vaya a la izquierda y `_R` a la derecha lo decide
  quien pone el panorama de la voz, y eso no se ve aquí.
- **[H]** El campo `+0x18` del flujo guarda los bytes consumidos en cada llamada y, con la bandera 6,
  la posición de esa trama (el inicio del bucle). La decodificación no lo lee y ninguna entrada tiene
  bandera 6, así que no pesa.
- **[H]** El `1` de los parámetros `+0x34` sería el tamaño de muestra (16 bits), como el `1` de ATRAC9
  (1.5). El PR tiene un `INVALID_WORD_LENGTH`.

#### El decodificador (vgmstream `db77f98`)

- **[V]** `decode_hevag` (`src/coding/psv_decoder.c`) decodifica tramas de 16 bytes con 28 muestras:
  - índice de la tabla = `(byte 1 & 0xf0) | (byte 0 >> 4)`, desplazamiento = `byte 0 & 0xf` y
    bandera = `byte 1 & 0xf`;
  - **4 muestras de historia `int32_t`** y una tabla de **128 × 4 coeficientes `float`**, no enteros;
  - con la bandera 7, las muestras son 0.
- **[V]** En estéreo, el entrelazado es de `0x10` bytes, con los canales en la cabecera `+0x1e`
  (`src/meta/vag.c`, versiones `0x00020001` y `0x00030000`). La licencia es tipo ISC (`COPYING`).
- **[V]** Las 19 entradas usan índices hasta `0x7f` (el nibble alto del byte 1 va de 0 a 7), así que
  hace falta la tabla extendida.

#### Contrato que se deduce

**[D] El estado vive en la memoria de trabajo del guest.** `AudiodecCpu` no tiene `Delete`, así que
una tabla del runtime crecería sin liberarse nunca. El juego reutiliza la memoria de cada flujo y la
libera él (2.1).

| Función | Qué tiene que hacer el runtime |
|---|---|
| `QueryMemSize(ctrl, recurso, 0x1008)` | validar `ctrl`, los parámetros (`tamaño = 0x40`) y el recurso (`0x18`); escribir en `recurso.bytes` el tamaño del estado para los canales de los parámetros `+0x22` (0 cuenta como 1, como hacen el juego y vgmstream) |
| `InitDecoder(ctrl, recurso, 0x1008)` | validar además la bsi (`0x30`), la memoria (no nula, mapeada y con `recurso.bytes` ≥ ese tamaño) y los parámetros (`VAGp` y `+0x34` = 1); escribir en la memoria el estado inicial: canales, historia a 0 y la cabecera todavía pendiente |
| `Decode(ctrl, recurso, 0x1008)` | la primera llamada tras `InitDecoder` comprueba la cabecera `VAGp` y consume solo sus `0x30` bytes, sin PCM (`pcm.bytes` = 0, bsi `+0x28` = 0); las siguientes decodifican una trama por canal, `16 × canales` bytes, en `28 × canales` muestras de 16 bits entrelazadas, con `au.bytes` = `16 × canales`, `pcm.bytes` = `56 × canales` y bsi `+0x28` = la bandera de la trama |

- **[D] La primera llamada solo consume la cabecera.** El juego tira las muestras de la primera llamada
  (2.3) y avanza lo que diga `au.bytes`. Así que la biblioteca real puede haber consumido la cabecera
  sola o con la primera trama, y el juego no lo distingue. Consumiendo solo la cabecera no se pierde
  ninguna trama. En las 19 entradas la primera trama es de ceros, así que las dos opciones suenan igual.
- **[D] Una trama por llamada,** aunque el AU o el PCM tengan sitio para más. Es lo que manda el juego,
  y así la bandera de la bsi corresponde a una sola trama.
- Con más de 16 canales, una trama no cabe en el tope de PCM del juego (`0x380` = 16 × 28 × 2): error
  de tamaño de PCM.
- Un AU con menos de una trama por canal, o un PCM con menos de `56 × canales` bytes, da error sin leer
  ni escribir fuera.
- Los códigos de error son los del PR, todos **[H]**. El juego no los distingue.
- Concurrencia: no hay estado compartido. La tabla de vgmstream es constante y cada flujo tiene su
  memoria. Solo los contadores del informe van con el mutex.

### 2.7 Implementación (tarea 12, 2026-10-21)

`src/runtime_audiodec_cpu.c` cumple el contrato de 2.6 con el decodificador de
`third_party/vgmstream_hevag/`. Commit `cba20ba`.

**La memoria de trabajo** es una cabecera de `0x10` bytes seguida de un `HevagState` (4 muestras
`int32_t`, 16 bytes) por canal: 32 bytes en mono y 48 en estéreo. La cabecera guarda una marca
(`HVAG`), los canales y si la cabecera `VAGp` del flujo sigue pendiente. Todo se lee y se escribe con
`memcpy`, porque la memoria es del juego y no tiene alineación garantizada.

| Función | Qué hace |
|---|---|
| `QueryMemSize` | comprueba `ctrl`, los parámetros (`0x40`) y el recurso (`0x18`), y escribe en `recurso.bytes` `0x10 + 16 × canales` (los de los parámetros `+0x22`; 0 cuenta como 1) |
| `InitDecoder` | comprueba además la bsi (`0x30`) y la memoria (no nula, `recurso.bytes` ≥ ese tamaño, mapeada). Desde ahí **borra la marca** y después mira los parámetros (`VAGp` en `+4` y `+0x34` = 1). Si todo cuadra, escribe la cabecera con la de `VAGp` pendiente y la historia a cero |
| `Decode` | comprueba además el AU y el PCM, y que la memoria esté mapeada en los `recurso.bytes` que dice el juego, tenga la marca y mida lo que piden sus canales. Con la cabecera pendiente, exige `VAGp` y los mismos canales, consume `0x30` bytes y deja `pcm.bytes` = 0 y la bandera 0. Después, una trama por canal: `au.bytes` = 16 × canales, `pcm.bytes` = 56 × canales y la bandera de la trama |

- **[D] El borrado de la marca en `InitDecoder`.** El juego reutiliza la memoria del flujo para la voz
  siguiente (2.6). Si `InitDecoder` falla por los parámetros, la memoria todavía tendría el estado de
  la voz anterior, y el primer `Decode` decodificaría la cabecera `VAGp` como si fuera una trama.
- **[D] En estéreo, la bandera es 7 si algún canal acaba;** si no, la del primer canal. En las 19
  entradas los dos canales acaban a la vez, así que solo cuenta con datos rotos.
- **[D] Lo que deja un error de `Decode`:** `pcm.bytes` = 0, el AU como llegó y la bandera 7, cuando
  el descriptor del PCM y la bsi son válidos. Así el juego, que no mira el resultado, no mezcla nada,
  avanza lo que mandó y acaba la voz en el acto. Con la respuesta de la tarea 6 (sin la bandera), la
  voz seguía en silencio hasta el final de sus datos. En `dummy` (estéreo) eso no llegaba nunca a 0.
- **Errores** (los del PR, **[H]**), en el orden del despachador del PR:
  - punteros: `ctrl` (8, `0x10` o `0x20` bytes según la función), parámetros, bsi, descriptores del AU
    y del PCM, y recurso;
  - tamaños: parámetros, bsi, descriptores y recurso;
  - memoria de trabajo: `INVALID_WORK_MEM_POINTER` o `…_SIZE`;
  - tamaños del AU y del PCM frente a la trama: `INVALID_AU_SIZE` e `INVALID_PCM_SIZE`;
  - datos del AU y del PCM: `INVALID_AU_POINTER` e `INVALID_PCM_POINTER`.

  Sin `VAGp` o con otros canales: `ARG`. Con `+0x34` ≠ 1: `INVALID_WORD_LENGTH`. Si la memoria no
  tiene la marca: `API_FAIL`. Un puntero mapeado solo en parte cuenta como no mapeado. No se lee ni se
  escribe fuera de lo comprobado.
- **El registro** es el de libSceAudiodec: la primera llamada y el primer error de cada función, o
  todas con `GOW3_TRACE_AUDIODEC`, con una línea `before:` y otra con el resultado y `after:` (bsi,
  descriptores, recurso, memoria de trabajo y 16 bytes del PCM). Tiene su propio tope de 400 líneas.
- **Contador:** `Runtime: AudiodecCpu decoders=…, frames=…, errors=…`. `decoders` son los
  `InitDecoder` correctos (uno por voz) y `frames` los `Decode` que dan PCM; el de la cabecera no
  cuenta.

#### Lo que se vio en el juego (ejecución b2)

`out/run-audiodec-b2.log` (sin versionar): partida actual, `GOW3_TRACE_AUDIODEC=1`, menú de título,
unos 154 s (9 261 *flips*), ventana cerrada por el usuario.

- **[V]** `QueryMemSize` da `0x20` y la memoria que pasa el juego es la del flujo (`0x1007…`).
  `InitDecoder` deja `HVAG`, 1 canal y la cabecera pendiente. El primer `Decode` consume `0x30`, y los
  siguientes 16 bytes y 56 bytes de PCM, como en 2.6.
- **[V]** Al cerrar: `AudiodecCpu decoders=240, frames=319494, errors=0` y `Audiodec decoders=35,
  frames=44342, errors=0`. Ningún `STOP:` ni `Fault`, y `unimplemented=0`.
- **[V] Los efectos de `vag.mrg` suenan bien**, según el usuario («Se escucha todo bien»).
- **[H]** El redondeo es el de vgmstream, no se sabe si es el de Sony (spec §11). De oído no se nota.

