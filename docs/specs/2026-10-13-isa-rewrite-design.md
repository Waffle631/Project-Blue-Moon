# Reescritura de instrucciones Haswell para CPUs antiguas (ISA rewrite)

Fecha: 2026-10-13. Estado: aprobado en conversación; a la espera de revisión del texto.

## 1. Objetivo

Hacer que el juego funcione en CPUs que tienen AVX pero no tienen los conjuntos de instrucciones que el
código del juego usa y que solo llegaron con Intel Haswell (2013): MOVBE, BMI1 y LZCNT.
Hoy el probe se detiene con `STOP: this CPU lacks instruction sets the game's code uses`
(issues #33 y #66: Xeon E5-2689 Sandy Bridge, Xeon E3-1230 v2 y E5-2667 v2 Ivy Bridge).

CPUs objetivo: Intel Sandy Bridge (2011) e Ivy Bridge (2012), AMD FX Bulldozer y Piledriver.
Fuera del alcance: CPUs sin AVX (Nehalem, Core 2, Phenom II). Exigirían emular todo el
código VEX del juego y siguen en el `STOP`.

Éxito: el juego abre y es jugable en esas CPUs, confirmado por quienes reportaron las issues con
una build de prueba. En CPUs con todos los conjuntos no cambia nada: cero sitios tocados.

## 2. Contexto

El código del guest (`eboot.bin`, `libc.prx`, `libSceFios2.prx`) se compila para el Jaguar
de la PS4 y se ejecuta directamente en la CPU del host. Censo hecho en la build CUSA01623 con `llvm-objdump` y
confirmado con `iced-x86` (barrido lineal de los segmentos ejecutables):

| Módulo | BEXTR | ANDN | BLSI/BLSR | LZCNT | TZCNT | MOVBE | F16C |
|---|---|---|---|---|---|---|---|
| eboot.bin (1,07 M instrucciones) | 842-846 | 139 | 31 | 42 | 8 | 94 | 0 |
| libc.prx | 62 | 33 | 11 | 35 | 10 | 0 | 0 |
| libSceFios2.prx | 54 | 41 | 0 | 0 | 0 | 10 | 0 |

Unos 1400 sitios (eboot 1156, libc 151, libSceFios2 105). El sitio del fallo de la issue
#33 (libc offset 0x25225) es
`blsr rax, rsi`. Tres hechos moldean el diseño:

1. **LZCNT y TZCNT no fallan en una CPU antigua.** Sin el conjunto, la CPU ignora el prefijo
   `F3` y ejecuta BSR/BSF: resultado erróneo en silencio. Emular solo en el trap de instrucción
   ilegal nunca los vería. Por eso la reescritura es estática, antes de que el juego se ejecute.
2. **Las flags se consumen.** `blsr eax,eax; jne` y `andn ...; jz` son patrón de clang.
   La emulación reproduce con exactitud las flags definidas por el SDM.
3. **El trap es caro.** Una excepción vectorizada en Windows cuesta unos 10 µs. Con 958
   BEXTR en código del juego, "trap en todo" sería injugable. El camino principal es un
   salto a un stub (50-100 ns); el trap queda solo como reserva rara.

Tamaños: toda instrucción VEX (BMI1) tiene 5 bytes o más; LZCNT/TZCNT y MOVBE sin REX tienen
4 bytes (85 sitios: 62 LZCNT/TZCNT, 23 MOVBE). En los 67 sitios de 4 bytes del eboot, ninguno
tiene la instrucción siguiente como destino de un salto directo.

## 3. Visión general

```
run.py
  gow3-probe --cpu-features          -> "CPU: missing MOVBE, BMI1, LZCNT" | "CPU: complete"
  prepare.py / link_libc.py / link_modules.py   (como hoy)
  isa_rewrite.py out/boot-linked.bin --out out  (solo si falta algo o GOW3_ISA_REWRITE=1)
  gow3-probe out/boot-linked.bin ... --isa-rewrite out/isa-rewrite.bin
      cpu_check: conjuntos fatales -> STOP; reescribibles -> lista
      carga imagen, reubicaciones, patches, cheats, hooks, TCB (como hoy)
      isa_rewrite_apply: verifica bytes, genera stubs, aplica jmp/int3
      protect(); los módulos arrancan; el juego se ejecuta
```

Componentes nuevos:

- `scripts/isa_rewrite.py`: localizador. Decodifica los segmentos ejecutables de la imagen
  enlazada con `iced-x86` y escribe la tabla `out/isa-rewrite.bin`.
- `src/isa_emulate.c/.h`: emulación pura de cada instrucción sobre un struct de
  registros. Sin dependencia del SO, sin libc, sin registros vectoriales.
- `src/isa_rewrite.c/.h`: lee la tabla, verifica los sitios, genera los stubs, aplica la
  reescritura, atiende el trap `int3`, cuenta ejecuciones y registra en el log.

Componentes modificados: `src/probe.c`, `src/cpu_check.c/.h`, `run.py`, `build.sh`,
`gpu/CMakeLists.txt`, `packaging/windows/package.sh`, `packaging/linux/build.sh`,
`README.md`, `README.pt-BR.md`, `packaging/windows/README-Windows.txt`.

## 4. Localizador: `scripts/isa_rewrite.py`

Uso: `isa_rewrite.py <boot-linked.bin> --out <dir>`. Escribe `<dir>/isa-rewrite.bin` e
imprime una línea de resumen:
`ISA: 1412 sites (MOVBE 104, BMI1 1231, LZCNT 77, F16C 0); 85 relocated, 0 int3, 0 unsupported`.

### 4.1 Entrada

Lee el formato G3PROBE5 que escribe `link_modules.py`: cabecera, tabla de módulos,
bindings, segmentos (`address, size, flags`), nombres, reubicaciones y la imagen. Solo los
segmentos con `flags & 1` (ejecutables) se decodifican. Los bytes son los de la imagen
antes de las reubicaciones (el probe aplica reubicaciones en datos, no en código; el probe
verifica los bytes de cada sitio antes de parchear).

### 4.2 Decodificación

`iced_x86.Decoder(64, bytes, ip=address)` en barrido lineal por segmento (1,07 M
instrucciones en 0,28 s). Durante el barrido recoge:

- los sitios: instrucciones cuyo mnemónico está en MOVBE, LZCNT, TZCNT, ANDN, BEXTR, BLSI,
  BLSMSK, BLSR, VCVTPH2PS, VCVTPS2PH;
- el conjunto de destinos de salto directo: `near_branch_target` de jmp, jcc y call con
  operando `NEAR_BRANCH64`.

### 4.3 Decisión por sitio

| Caso | Modo | Bytes en el sitio |
|---|---|---|
| tamaño ≥ 5 | `JMP` | `E9 rel32` seguido de `90` hasta el tamaño original |
| tamaño 4 e instrucción siguiente plana | `JMP_RELOCATE` | `E9 rel32` cubre los 4 bytes y el primero de la siguiente; el resto de la siguiente pasa a `90` |
| tamaño 4 sin siguiente plana | `INT3` | `CC` en el primer byte; los otros 3 se quedan |
| encoding fuera del emulador | `UNSUPPORTED` | nada |

Instrucción siguiente "plana": `flow_control == NEXT`, no RIP-relativa, sin prefijo REP,
no es otro sitio, termina dentro del mismo segmento y su inicio no está en el conjunto de
destinos de salto directo. Un sitio de 4 bytes al final del segmento pasa a `INT3`.

Encodings fuera del emulador (`UNSUPPORTED`): prefijo de segmento en el operando de memoria,
address-size de 16 bits, prefijo LOCK, y las dos instrucciones F16C (cero sitios hoy; el
emulador no las implementa). El probe se detiene con `STOP` si la CPU necesita un sitio
`UNSUPPORTED`.

### 4.4 Formato de la tabla `G3ISA1`

Little-endian. Cabecera de 64 bytes:

| Offset | Campo |
|---|---|
| 0 | `"G3ISA1\0\0"` |
| 8 | u64 tamaño de la imagen |
| 16 | sha256 de la imagen (32 bytes) |
| 48 | u32 número de sitios |
| 52 | u32 número de sitios `UNSUPPORTED` |
| 56 | reservado (ceros) |

Registros de 64 bytes, ordenados por offset:

| Offset | Campo |
|---|---|
| 0 | u64 offset del sitio en la imagen |
| 8 | u8 tamaño de la instrucción original (4-15) |
| 9 | u8 tipo: 1 MOVBE load, 2 MOVBE store, 3 LZCNT, 4 TZCNT, 5 ANDN, 6 BEXTR, 7 BLSI, 8 BLSMSK, 9 BLSR, 10 VCVTPH2PS, 11 VCVTPS2PH |
| 10 | u8 ancho en bits: 16, 32 o 64 |
| 11 | u8 modo: 0 JMP, 1 JMP_RELOCATE, 2 INT3, 3 UNSUPPORTED |
| 12 | u8 registro destino (0-15; 0xFF si el destino es memoria) |
| 13 | u8 registro fuente 1 (0-15; 0xFF ausente o memoria) |
| 14 | u8 registro fuente 2 (BEXTR: control; ANDN: segunda fuente; 0xFF ausente) |
| 15 | u8 operando de memoria: 0 ninguno, 1 destino, 2 fuente |
| 16 | u8 base (0-15; 0xFF ninguna), u8 índice (0-15; 0xFF ninguno), u8 escala (1, 2, 4, 8; 0 sin operando de memoria), u8 flags: bit 0 = RIP-relativo ya resuelto |
| 20 | reservado (4 bytes) |
| 24 | i64 desplazamiento; si es RIP-relativo, offset absoluto en la imagen |
| 32 | bytes originales (15 bytes) |
| 47 | u8 tamaño de la instrucción reubicada (0 si no hay) |
| 48 | bytes de la instrucción reubicada (15 bytes) |
| 63 | reservado |

Numeración de registros: la de x86 (0 rax, 1 rcx, 2 rdx, 3 rbx, 4 rsp, 5 rbp, 6 rsi,
7 rdi, 8-15 r8-r15).

### 4.5 Fallos

Sin `iced_x86` importable, o archivo de entrada no válido: imprime
`ISA: rewrite table unavailable (<motivo>)` y sale con código 1, sin escribir la tabla.
`run.py` continúa sin `--isa-rewrite`.

## 5. Probe

### 5.1 Activación

Opción nueva `--isa-rewrite <archivo>`. Variable `GOW3_ISA_REWRITE`:

- ausente: automático. Reescribe solo los sitios de los conjuntos que el CPUID no tiene.
- `1`: reescribe todos los sitios, incluso con CPU completa. Modo de prueba.
- `0`: nunca reescribe. Una CPU antigua se detiene en el `STOP`, como hoy.

Orden en `main`: la comprobación de CPU sigue antes que todo. Conjunto fatal ausente (SSE4.2,
POPCNT, AVX) → `STOP` con texto nuevo: `(Intel Sandy Bridge 2011 or newer, or AMD FX or
newer, is needed)`, código 20. Conjunto reescribible ausente (MOVBE, BMI1, LZCNT, F16C)
sin `--isa-rewrite`, con tabla no válida, o con sha256 distinto al de la imagen → `STOP:
this CPU lacks MOVBE, BMI1, LZCNT and no rewrite table matches the image`, código 20.
Con `GOW3_ISA_REWRITE=0` el texto es `STOP: this CPU lacks MOVBE, BMI1, LZCNT and the
rewrite is disabled (GOW3_ISA_REWRITE=0)`, código 20. Con tabla válida → continúa.

### 5.2 `cpu_check`

`cpu_missing_features` pasa a separar los dos grupos (dos buffers o una máscara por
conjunto). Modo nuevo `gow3-probe --cpu-features`: imprime `CPU: missing MOVBE, BMI1,
LZCNT` o `CPU: complete` y sale con 0; si falta un conjunto fatal imprime la línea `STOP:` y sale con 20.
`GOW3_CPU_CHECK=0` sigue saltándose toda la comprobación.

### 5.3 Memoria

El área de stubs se reserva en el mismo `allocate` de la imagen, justo después de `round_page(size)`:
256 bytes por sitio, redondeado a página (unos 360 KiB para 1400 sitios). Así
todo `rel32` alcanza (imagen ≤ 512 MiB, límite de `prepare.py`). El área entra en el
`protect()` final como ejecutable y solo lectura. Los registros de los sitios quedan en el heap,
con el campo `hits` (u64) por sitio.

Un fallo de CPU con RIP dentro del área de stubs lo reportan los handlers de hoy con la
línea extra `While emulating site 0x<offset> (<mnemónico>)`.

### 5.4 Aplicación (`isa_rewrite_apply`)

Último paso antes de `protect()`, después de `apply_patches`, cheats, hooks y
`patch_tcb_loads`. Para cada sitio seleccionado:

1. Compara los bytes de la imagen con los bytes originales del registro. Si difieren (un patch
   tocó ahí): lo salta, cuenta en `skipped` e imprime `ISA: site 0x<offset> <mnemónico>
   skipped: bytes changed`. En `JMP_RELOCATE` compara también la instrucción siguiente con la
   copia reubicada; si difiere (un patch o hook cambió la siguiente), el sitio pasa a `INT3`,
   se cuenta en `int3` y se imprime `ISA: site 0x<offset> <mnemónico>: next instruction
   changed, int3 fallback`.
2. Genera el stub en la posición `stub_base + 256 * i`.
3. Escribe los bytes del modo (tabla en 4.3). En `JMP_RELOCATE` el retorno es a
   `sitio + 4 + tamaño_reubicado`; en `JMP`, a `sitio + tamaño`.

Los sitios `UNSUPPORTED` de un conjunto que la CPU no tiene detienen el probe con `STOP` antes de
cualquier escritura.

### 5.5 Stub

Un stub completo por sitio, sin stub compartido. Pseudo-asm:

```
lea  rsp, [rsp-0x100]        ; no altera flags; sale de la red zone SysV y de los frames de señal
pushfq                       ; rflags en [rsp]: inicio del IsaRegs
mov  [rsp+8+8*0], rax        ; gpr[0..15] excepto rsp
mov  [rsp+8+8*1], rcx
...                          ; rdx rbx rbp rsi rdi r8..r15
lea  rax, [rsp+0x108]        ; rsp original
mov  [rsp+8+8*4], rax        ; gpr[4]
mov  rax, imm64 &record.hits
inc  qword [rax]             ; contador aproximado (sin lock)
mov  rbp, rsp                ; IsaRegs*
and  rsp, -16                ; alineación SysV
mov  rdi, imm64 record
mov  rsi, rbp
mov  rax, imm64 isa_emulate
call rax
mov  rsp, rbp
popfq                        ; flags calculadas por el emulador
mov  rax, [rsp+8*0]          ; restaura gpr excepto rsp
...
lea  rsp, [rsp+0x100]
<instrucción reubicada>      ; solo en JMP_RELOCATE
jmp  rel32 retorno
```

`isa_emulate` tiene la firma `void ABI isa_emulate(const IsaSite *site, IsaRegs *regs)`
con la macro `ABI` (SysV) que el probe ya usa para el código del guest, igual en ambos SO.
`IsaRegs` es `{ uint64_t rflags; uint64_t gpr[16]; }`.

### 5.6 Emulador (`isa_emulate.c`)

Compilado con `-mgeneral-regs-only` y sin llamadas a libc: ningún registro XMM/YMM se
toca, por eso el stub no los guarda. Semántica del Intel SDM. Las flags definidas se
escriben; las indefinidas quedan como estaban.

| Instrucción | Resultado | Flags |
|---|---|---|
| ANDN dst, s1, s2 | `~s1 & s2` | SF, ZF según el resultado; OF=CF=0 |
| BEXTR dst, src, ctrl | `start = ctrl[7:0]`, `len = ctrl[15:8]`; `(src >> start) & ((1 << len) - 1)`; `start ≥ ancho` → 0; `start + len > ancho` → solo los bits hasta `ancho-1` | ZF según el resultado; OF=CF=0 |
| BLSI dst, src | `-src & src` | SF, ZF según el resultado; CF = src ≠ 0; OF=0 |
| BLSMSK dst, src | `(src - 1) ^ src` | SF según el resultado; CF = src == 0; ZF=0; OF=0 |
| BLSR dst, src | `(src - 1) & src` | SF, ZF según el resultado; CF = src == 0; OF=0 |
| LZCNT dst, src | src == 0 → ancho; si no `ancho - 1 - bsr(src)` | CF = src == 0; ZF = dst == 0 |
| TZCNT dst, src | src == 0 → ancho; si no `bsf(src)` | CF = src == 0; ZF = dst == 0 |
| MOVBE r, m / m, r | intercambio de bytes al ancho dado | ninguna |

Escritura en el destino: 64 bits escriben todo; 32 bits ponen a cero los 32 altos; 16 bits (LZCNT,
TZCNT, MOVBE) conservan los bits altos. Operando de memoria: los registros del guest ya
guardan direcciones del host, así que dirección = `base + índice × escala + desplazamiento`;
el RIP-relativo viene de la tabla como offset absoluto y se convierte en `image + desplazamiento`. Un store en
una página rastreada por la GPU cae en el `gow3gpu_handle_fault` existente y continúa, como un
store del guest.

### 5.7 Trap `int3`

Windows: `EXCEPTION_BREAKPOINT` en `vectored_fault`, antes del camino fatal. Linux:
`SIGTRAP` en `fault()`, registrado junto con `SIGSEGV`, `SIGILL` y `SIGBUS`. Sitio: en
Windows `ExceptionAddress` ya apunta al `int3`; en Linux es `REG_RIP − 1`. Búsqueda
binaria en la tabla.
Sin sitio: camino de hoy. Con sitio: monta `IsaRegs` a partir del `CONTEXT`/`ucontext`,
llama a `isa_emulate`, copia de vuelta registros y flags, RIP = sitio + tamaño (4), incrementa
`hits`.

### 5.8 Log

En la aplicación:
`ISA: CPU lacks MOVBE, BMI1, LZCNT: 1412 sites rewritten (1327 jmp, 85 relocated, 0 int3), 0 skipped`.
En modo forzado: `ISA: forced rewrite of all 1412 sites (GOW3_ISA_REWRITE=1), 0 skipped`.
En la salida normal y en el informe de fallo: `ISA: most executed sites:` seguido de los 10
sitios con más `hits` (`0x<offset> <mnemónico> <hits>`). Sirve para decidir si algún día
vale la pena un camino rápido en asm para algún sitio.

## 6. `run.py`

1. Antes de los scripts de preparación, ejecuta `gow3-probe --cpu-features`. Salida 20: imprime la
   línea `STOP:` y termina. Lee la lista de conjuntos ausentes.
2. Después de `link_modules.py`, si la lista no está vacía o `GOW3_ISA_REWRITE=1`:
   `run_script('isa_rewrite.py', out / 'boot-linked.bin', '--out', out)`. Éxito →
   añade `--isa-rewrite out/isa-rewrite.bin` al comando del probe. Fallo → imprime la
   línea del script y continúa sin la flag (el probe da el `STOP` con el motivo).
3. `GOW3_ISA_REWRITE=0`: no ejecuta el script ni pasa la flag.

## 7. Empaquetado y documentación

- `packaging/windows/package.sh`: `pip install iced-x86` en `out/pyenv` y
  `--hidden-import iced_x86` en PyInstaller.
- `packaging/linux/build.sh`: `--hidden-import iced_x86`; `packaging/windows/README.md` y un
  comentario en `packaging/linux/build.sh` dejan constancia de que el Python de la build necesita el wheel
  `iced-x86` (el fork del AppImage lo instala).
- `build.sh` y `gpu/CMakeLists.txt`: `src/isa_rewrite.c` y `src/isa_emulate.c` en el probe;
  `isa_emulate.c` con `-mgeneral-regs-only`; nuevos targets de test.
- READMEs (en, pt-BR, README-Windows.txt): el requisito de CPU pasa a "64 bits con AVX:
  Intel Sandy Bridge 2011, AMD FX o más nuevo". Párrafo corto: en CPUs sin MOVBE,
  BMI1 o LZCNT el probe emula esas instrucciones y registra la línea `ISA:` en el log; el
  rendimiento en esas CPUs aún no se ha medido.
- Launcher: sin cambios. La pestaña Log muestra las líneas.

## 8. Tests

- `tests/test_isa_rewrite.py` (unittest, `skipUnless(iced_x86)`): monta un G3PROBE5
  falso con dos segmentos ejecutables a partir de bytes escritos a mano. Cubre cada
  mnemónico y ancho, operando de memoria y RIP-relativo, las cuatro reglas de 4 bytes
  (siguiente plana → `JMP_RELOCATE`; destino de salto, RIP-relativa, salto u otro sitio →
  `INT3`; fin de segmento → `INT3`), `UNSUPPORTED`, round-trip de la tabla y sha256. Sin
  binarios del juego.
- `tests/test_isa_emulate.c`: oráculo nativo. Compilado con `-mbmi -mlzcnt -mmovbe`,
  ejecuta miles de entradas aleatorias más bordes (0, 1, bit alto, todo 1, `start` y
  `len` saturados, `start + len > ancho`) por instrucción y ancho, y compara resultado
  y flags definidas con la instrucción real mediante inline asm. Se salta si el CPUID de la máquina no
  tiene los conjuntos.
- `tests/test_isa_stub.c`: imagen RWX pequeña con fragmentos reales, incluidos casos de 4
  bytes con instrucción siguiente y un `INT3`; tabla fixture en `tests/data/`; aplica
  `isa_rewrite_apply`, ejecuta el fragmento reescrito y el nativo, compara registros, flags y
  memoria; comprueba que XMM0-15 y los 128 bytes bajo `rsp` queden intactos; ejercita el trap mediante el
  handler de la plataforma.
- De extremo a extremo: `GOW3_ISA_REWRITE=1` en Windows y en el Linux de desarrollo. El juego
  abre y se juega con `ISA: forced rewrite of all N sites`, sin fallos nuevos. Build de prueba
  para quienes reportaron las issues #33 y #66.
- `build.sh --test` y `gpu/CMakeLists.txt` incorporan `isa-emulate-test` e `isa-stub-test`
  siguiendo el patrón de `cpu-check-test`. Python: `python -m unittest discover -s tests`.

## 9. Riesgos y límites aceptados

- Destino de salto indirecto (jump table) en la instrucción reubicada de un sitio de 4 bytes: el
  localizador solo conoce saltos directos. Mitigación: solo 85 sitios, y la ronda de extremo a
  extremo en modo forzado ejercita el juego real. Un sitio problemático pasa a `INT3` mediante
  un ajuste en el localizador.
- Los hooks (cheats, multiplicadores) que copian instrucciones del guest a trampolines
  propios no están cubiertos. En una copia, MOVBE y BMI1 fallan en una CPU antigua con el
  informe fatal de hoy, que nombra el offset; LZCNT y TZCNT no fallan: se ejecutan como
  BSR y BSF y dan un resultado erróneo en silencio.
- Rendimiento en las CPUs objetivo no medido; los contadores de `hits` indican dónde optimizar.
- El stub usa unos 0x120 bytes de la pila del guest más el frame C, comparable a una
  entrega de señal.
- `-mgeneral-regs-only` debe existir en el clang de MSYS2 y en el GCC de Linux; el plan
  lo verifica en la primera compilación.

## 10. Fuera del alcance

- Stubs en asm especializado por sitio (enfoque C). Queda como optimización futura si los
  contadores muestran un sitio caliente.
- Emulación de F16C (cero sitios en las builds conocidas).
- CPUs sin AVX.
- Interfaz en el launcher.
