# Plan de implementación: reescritura de instrucciones Haswell (ISA rewrite)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** hacer que el juego funcione en CPUs con AVX pero sin MOVBE, BMI1 y LZCNT (Sandy/Ivy Bridge, AMD FX), reescribiendo en la carga cada sitio de esas instrucciones hacia un stub que la emula en C.

**Architecture:** un localizador en Python (`iced-x86`) decodifica la imagen enlazada y escribe una tabla `G3ISA1`; el probe, cuando el CPUID no tiene los conjuntos (o en modo forzado), verifica cada sitio, genera un stub de 256 bytes por sitio en un área contigua a la imagen y cambia la instrucción por `jmp rel32` (o `int3` en raros sitios de 4 bytes sin instrucción siguiente reubicable). El stub guarda el estado, llama a `isa_emulate` (C puro, flags exactas) y vuelve.

**Tech Stack:** C11 (clang MSYS2 CLANG64 en Windows, GCC en Linux), Python 3 + `iced-x86` 1.21.0, unittest, CMake/Ninja (`gpu/CMakeLists.txt`) y `build.sh`.

**Spec:** `docs/specs/2026-10-13-isa-rewrite-design.md` (léela antes de cada tarea; las secciones citadas abajo son de ella).

## Restricciones globales

- C: `-std=c11 -Wall -Wextra -Werror`; headers con guarda `GOW3_*_H`; comentarios en inglés como el resto de `src/`.
- `src/isa_emulate.c` se compila con `-mgeneral-regs-only` y no llama a libc (ni `memcpy`).
- Convención de llamada del stub hacia C: macro `ABI` de `src/runtime.h` (`__attribute__((sysv_abi))`).
- Registro de la tabla: 64 bytes; cabecera: 64 bytes; magic `G3ISA1\0\0` (spec 4.4). Stub: como máximo 256 bytes.
- Textos de log exactos (spec 4, 5.1, 5.2, 5.8): `CPU: missing ...`, `CPU: complete`, `ISA: ... sites (...)`, `ISA: rewrite table unavailable (...)`, `ISA: CPU lacks ...: N sites rewritten (...)`, `ISA: forced rewrite of all N sites (GOW3_ISA_REWRITE=1)`, `ISA: most executed sites:`.
- Código de salida 20 para toda línea `STOP:`.
- Numeración de registros: la de x86 (0 rax ... 4 rsp ... 15 r15); `0xFF` = ausente.
- Commits: mensaje generado con la skill `caveman-commit`, formato del proyecto `Tipo(Escopo): descrição em português` (ej.: `Feat(Probe): ...`), sin mención a IA, sin `Co-Authored-By`.
- Tests de Python: `python -m unittest discover -s tests -p "test_*.py"` (4 fallos + 1 error preexistentes en `test_packaged_vulkan` y `test_mods` en Windows; no regresar el resto).
- Build en el worktree (memoria `worktree-build-msys2`): `subst W: "<ruta del worktree>"`, luego en MSYS2 CLANG64 `cmake -S gpu -B out/w -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DGOW3_PGO=off -DGOW3_LTO=OFF -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++` desde `/w`, y `ninja -C out/w <target>`. Los ejecutables clang64 necesitan `PATH=/c/msys64/clang64/bin:$PATH` en Git Bash.

## Foco de la revisión

Entradas que la spec implica y que quedarían sin test; cada una obtuvo el test indicado en la tarea responsable.

1. Dos sitios adyacentes de 5 bytes: el retorno del primer stub cae en el `E9` del segundo y debe continuar al segundo stub (Tarea 5, `test_adjacent_sites`).
2. Sitio `JMP_RELOCATE` con instrucción reubicada de 15 bytes: el stub no puede pasar de 256 bytes; `isa_rewrite_apply` falla de forma ruidosa, no corrompe al vecino (Tarea 5, `test_largest_stub_fits`).
3. `int3` que no es nuestro (breakpoint real): `isa_rewrite_trap` devuelve 0 y el camino fatal de hoy continúa (Tarea 6, `test_trap_unknown_address`).
4. Tabla de otra build (sha256 o tamaño distintos): no se escribe nada y el mensaje `STOP` nombra el motivo (Tarea 5, `test_table_mismatch`; Tarea 7, inspección manual del log).
5. `GOW3_ISA_REWRITE=0` en CPU antigua: `run.py` no ejecuta el localizador ni pasa la flag, y el probe se detiene en el `STOP` (Tarea 8, `test_rewrite_disabled`).

---

### Task 1: `cpu_check` en dos grupos y modo `--cpu-features`

**Files:**
- Modify: `src/cpu_check.h`, `src/cpu_check.c`
- Modify: `src/probe.c` (bloque `GOW3_CPU_CHECK` en `main`, justo después del parseo de las opciones; y el `if (argc == 2 && !strcmp(argv[1], "--vulkan-only"))`)
- Test: `tests/test_cpu_check.c`, `tests/test_probe.py`

**Interfaces:**
- Produces (`src/cpu_check.h`):
  ```c
  enum { CPU_SET_MOVBE = 1u << 0, CPU_SET_BMI1 = 1u << 1, CPU_SET_LZCNT = 1u << 2, CPU_SET_F16C = 1u << 3 };
  /* Fatal sets the host lacks (SSE4.2, POPCNT, AVX), comma separated in out; returns how many. */
  int cpu_missing_fatal(CpuFeatureWords words, char *out, size_t size);
  /* Rewritable sets the host lacks (MOVBE, F16C, BMI1, LZCNT): CPU_SET_* mask; names in out. */
  unsigned cpu_missing_rewritable(CpuFeatureWords words, char *out, size_t size);
  ```
  `cpu_missing_features` se elimina (los dos llamadores migran en esta tarea). Orden de los nombres: el de la tabla actual (SSE4.2, POPCNT, MOVBE, AVX, F16C, BMI1, LZCNT), así que Sandy Bridge imprime `MOVBE, F16C, BMI1, LZCNT`.

- [ ] **Paso 1: reescribir `tests/test_cpu_check.c` para la API nueva**

Aserciones (mismas constantes `L1_*`, `L7_*`, `E1_*` del test actual):
```c
/* Jaguar-class host */
assert(cpu_missing_fatal((CpuFeatureWords){LEAF1_ALL, L7_BMI1, E1_LZCNT}, out, sizeof out) == 0 && !strcmp(out, ""));
assert(cpu_missing_rewritable((CpuFeatureWords){LEAF1_ALL, L7_BMI1, E1_LZCNT}, out, sizeof out) == 0 && !strcmp(out, ""));
/* Sandy Bridge (#33) */
assert(cpu_missing_fatal((CpuFeatureWords){L1_SSE42 | L1_POPCNT | L1_AVX, 0, 0}, out, sizeof out) == 0);
assert(cpu_missing_rewritable((CpuFeatureWords){L1_SSE42 | L1_POPCNT | L1_AVX, 0, 0}, out, sizeof out)
       == (CPU_SET_MOVBE | CPU_SET_F16C | CPU_SET_BMI1 | CPU_SET_LZCNT));
assert(!strcmp(out, "MOVBE, F16C, BMI1, LZCNT"));
/* Ivy Bridge (#66) */
assert(cpu_missing_rewritable((CpuFeatureWords){L1_SSE42 | L1_POPCNT | L1_AVX | L1_F16C, 0, 0}, out, sizeof out)
       == (CPU_SET_MOVBE | CPU_SET_BMI1 | CPU_SET_LZCNT));
assert(!strcmp(out, "MOVBE, BMI1, LZCNT"));
/* Nehalem: no AVX, fatal */
assert(cpu_missing_fatal((CpuFeatureWords){L1_SSE42 | L1_POPCNT, 0, 0}, out, sizeof out) == 1 && !strcmp(out, "AVX"));
/* short buffer: cut, never overrun; count complete */
memset(out, 'x', sizeof out);
assert(cpu_missing_fatal((CpuFeatureWords){0, 0, 0}, out, 8) == 3 && strlen(out) < 8);
/* this machine (newer than 2013) */
assert(cpu_missing_fatal(cpu_feature_words(), out, sizeof out) == 0 && cpu_missing_rewritable(cpu_feature_words(), out, sizeof out) == 0);
```

- [ ] **Paso 2: compilar el test y verlo fallar**

Run: `clang -std=c11 -O2 -g -Wall -Wextra -Werror -I. -Isrc tests/test_cpu_check.c -o out/cpu-check-test`
Expected: error de compilación `call to undeclared function 'cpu_missing_fatal'`.

- [ ] **Paso 3: implementar en `src/cpu_check.c`**

Separa la tabla `sets[]` en dos (fatal y reescribible, cada entrada de la reescribible con su bit `CPU_SET_*`); una función estática común da formato a los nombres y devuelve el contador o la máscara.

- [ ] **Paso 4: compilar y ejecutar el test**

Run: mismo comando del paso 2, luego `out/cpu-check-test`
Expected: `cpu-check-test ok`

- [ ] **Paso 5: probe: `--cpu-features` y comprobación en dos grupos**

En `main`, junto a `--vulkan-only`: `if (argc == 2 && !strcmp(argv[1], "--cpu-features"))` imprime `STOP: this CPU lacks instruction sets the game's code uses: %s (Intel Sandy Bridge 2011 or newer, or AMD FX or newer, is needed)` y devuelve 20 cuando `cpu_missing_fatal` > 0 (respetando `GOW3_CPU_CHECK=0`: entonces imprime `CPU: complete`); si no, `CPU: missing %s` o `CPU: complete` y devuelve 0. En el bloque `GOW3_CPU_CHECK` existente: fatal → el mismo `STOP`, `return 20`; reescribible → guardar en `static unsigned cpu_missing_sets` e imprimir `CPU: missing %s` (la Tarea 7 usa la máscara). Actualiza la línea `Usage:`.

- [ ] **Paso 6: test de Python del modo nuevo en `tests/test_probe.py`**

En la clase existente (`skipUnless(EXE.exists())`):
```python
def test_cpu_features_mode_reports_this_machine(self):
    result = subprocess.run([str(EXE.resolve()), '--cpu-features'], capture_output=True, text=True)
    self.assertEqual(result.returncode, 0)
    self.assertRegex(result.stdout, r'^CPU: (complete|missing [A-Z0-9., ]+)\n$')
```

- [ ] **Paso 7: build del probe y tests**

Run (MSYS2, ver Restricciones globales): `ninja -C out/w gow3-probe cpu-check-test && out/w/cpu-check-test.exe`; copia `out/w/gow3-probe.exe` a `out/gow3-probe.exe` (con las DLL en el PATH) y `python -m unittest discover -s tests -p "test_probe.py" -v` (el `.exe` en `out/` hace que pase el `skipUnless`).
Expected: `cpu-check-test ok`; `test_cpu_features_mode_reports_this_machine ... ok`.

- [ ] **Paso 8: commit**

`git add src/cpu_check.c src/cpu_check.h src/probe.c tests/test_cpu_check.c tests/test_probe.py`; mensaje mediante `caveman-commit` (sugerencia: `Feat(Probe): separar conjuntos fatais dos reescrevíveis no CPUID`).

---

### Task 2: localizador, decodificación y clasificación de sitios

**Files:**
- Create: `scripts/isa_rewrite.py`
- Test: `tests/test_isa_rewrite.py`

**Interfaces:**
- Produces (`scripts/isa_rewrite.py`):
  ```python
  KIND_MOVBE_LOAD, KIND_MOVBE_STORE, KIND_LZCNT, KIND_TZCNT, KIND_ANDN, KIND_BEXTR, KIND_BLSI, KIND_BLSMSK, KIND_BLSR, KIND_VCVTPH2PS, KIND_VCVTPS2PH = range(1, 12)
  MODE_JMP, MODE_JMP_RELOCATE, MODE_INT3, MODE_UNSUPPORTED = range(4)
  NO_REG = 0xFF
  SET_OF_KIND = {KIND_MOVBE_LOAD: 'MOVBE', KIND_MOVBE_STORE: 'MOVBE', KIND_LZCNT: 'LZCNT', KIND_TZCNT: 'BMI1', KIND_ANDN: 'BMI1', ..., KIND_VCVTPS2PH: 'F16C'}

  @dataclass
  class Site:
      offset: int; length: int; kind: int; width: int; mode: int
      dst: int; src1: int; src2: int; mem_slot: int          # mem_slot: 0 none, 1 dst, 2 src
      base: int; index: int; scale: int; mem_flags: int     # mem_flags bit 0: RIP-relative, disp absolute
      disp: int; original: bytes; relocated: bytes

  def scan_segment(code: bytes, address: int) -> list[Site]
  ```
  `iced_x86` se importa en `try/except ImportError` en la parte superior del módulo (`iced_x86 = None` cuando falta); `scan_segment` lanza `RuntimeError('iced_x86 is not installed')` en ese caso.

- [ ] **Paso 1: escribir los tests (fallando)**

`tests/test_isa_rewrite.py`, `unittest`, `from paths import ROOT`, `import isa_rewrite`, clase decorada con `@unittest.skipUnless(isa_rewrite.iced_x86, 'pip install iced-x86')`. Bytes en hexadecimal (todos verificados con el decodificador):

```python
BLSR_ECX = bytes.fromhex('c4e270f3c9')          # blsr ecx, ecx            (5)
MOVBE_STORE = bytes.fromhex('0f38f108')          # movbe [rax], ecx         (4)
BEXTR_RIP = bytes.fromhex('c4e270f7050c000000')  # bextr eax, [rip+0xc], ecx (9)
LZCNT_AX = bytes.fromhex('66f30fbdc1')           # lzcnt ax, cx             (5)
TZCNT_RAX = bytes.fromhex('f3480fbcc1')          # tzcnt rax, rcx           (5)
NOP, RET, JMP_SELF = b'\x90', b'\xc3', bytes.fromhex('ebfe')
MOV_RIP = bytes.fromhex('8b0500000000')          # mov eax, [rip+0]         (6)
MOV_FS = bytes.fromhex('648b042500000000')       # mov eax, fs:[0]          (8): not a site

def test_blsr_is_a_jmp_site(self):
    s, = isa_rewrite.scan_segment(BLSR_ECX, 0x1000)
    self.assertEqual((s.offset, s.length, s.kind, s.width, s.mode), (0x1000, 5, isa_rewrite.KIND_BLSR, 32, isa_rewrite.MODE_JMP))
    self.assertEqual((s.dst, s.src1, s.src2, s.mem_slot), (1, 1, isa_rewrite.NO_REG, 0))
    self.assertEqual((s.original, s.relocated), (BLSR_ECX, b''))

def test_bextr_rip_relative_memory_source(self):
    s, = isa_rewrite.scan_segment(BEXTR_RIP, 0x1000)
    self.assertEqual((s.kind, s.width, s.dst, s.src1, s.src2, s.mem_slot), (isa_rewrite.KIND_BEXTR, 32, 0, isa_rewrite.NO_REG, 1, 2))
    self.assertEqual((s.base, s.index, s.mem_flags, s.disp), (isa_rewrite.NO_REG, isa_rewrite.NO_REG, 1, 0x1000 + 9 + 0xc))

def test_sixteen_bit_lzcnt_and_sixty_four_bit_tzcnt(self):
    a, b = isa_rewrite.scan_segment(LZCNT_AX + TZCNT_RAX, 0)
    self.assertEqual((a.kind, a.width, a.length), (isa_rewrite.KIND_LZCNT, 16, 5))
    self.assertEqual((b.kind, b.width, b.dst, b.src1), (isa_rewrite.KIND_TZCNT, 64, 0, 1))

def test_four_byte_store_relocates_a_plain_next_instruction(self):
    s, = isa_rewrite.scan_segment(MOVBE_STORE + NOP + RET, 0)
    self.assertEqual((s.kind, s.mode, s.mem_slot, s.base, s.dst, s.src1), (isa_rewrite.KIND_MOVBE_STORE, isa_rewrite.MODE_JMP_RELOCATE, 1, 0, isa_rewrite.NO_REG, 1))
    self.assertEqual(s.relocated, NOP)

def test_four_byte_site_falls_back_to_int3(self):
    for tail in (JMP_SELF, MOV_RIP, BLSR_ECX, b''):       # branch, RIP-relative, another site, end of segment
        s = isa_rewrite.scan_segment(MOVBE_STORE + tail, 0)[0]
        self.assertEqual((s.mode, s.relocated), (isa_rewrite.MODE_INT3, b''), tail.hex())

def test_next_instruction_that_is_a_branch_target_blocks_relocation(self):
    code = bytes.fromhex('eb04') + MOVBE_STORE + NOP + RET   # jmp +4 lands on the nop
    s, = isa_rewrite.scan_segment(code, 0)
    self.assertEqual(s.mode, isa_rewrite.MODE_INT3)

def test_unsupported_encodings(self):
    for code, why in ((b'\x64' + BEXTR_RIP, 'segment prefix'), (b'\x67' + MOVBE_STORE, '32-bit address'),
                      (bytes.fromhex('c4e2791310'), 'vcvtph2ps xmm2, [rax]')):
        s, = isa_rewrite.scan_segment(code + RET, 0)
        self.assertEqual(s.mode, isa_rewrite.MODE_UNSUPPORTED, why)
    self.assertEqual(isa_rewrite.scan_segment(bytes.fromhex('c4e2791310') + RET, 0)[0].kind, isa_rewrite.KIND_VCVTPH2PS)

def test_non_sites_are_ignored(self):
    self.assertEqual(isa_rewrite.scan_segment(MOV_FS + RET, 0), [])
```

- [ ] **Paso 2: ejecutar y ver el fallo**

Run: `python -m unittest discover -s tests -p "test_isa_rewrite.py" -v`
Expected: `ModuleNotFoundError: No module named 'isa_rewrite'` (o skip si `iced_x86` no está en el Python del repositorio: instala `pip install iced-x86==1.21.0` en ese Python antes de seguir).

- [ ] **Paso 3: implementar `scan_segment`**

`Decoder(64, code, ip=address)` en una sola pasada. Para cada instrucción guarda `targets.add(ins.near_branch_target)` cuando `ins.flow_control` es `UNCONDITIONAL_BRANCH`, `CONDITIONAL_BRANCH` o `CALL` y `ins.op0_kind == OpKind.NEAR_BRANCH64`. Un mnemónico en `Mnemonic.MOVBE/LZCNT/TZCNT/ANDN/BEXTR/BLSI/BLSMSK/BLSR/VCVTPH2PS/VCVTPS2PH` crea un `Site` (MOVBE: load si `ins.op0_kind == OpKind.REGISTER`). Registros: `RegisterExt.number(ins.op_register(i))`; ancho: `RegisterExt.size(reg) * 8` del operando registro (op1 en el store). Memoria: `ins.memory_base/memory_index/memory_index_scale/memory_displacement`; RIP-relativo (`ins.is_ip_rel_memory_operand`): `base = index = NO_REG`, `mem_flags = 1`, `disp = ins.ip_rel_memory_address`. `UNSUPPORTED`: `ins.segment_prefix != Register.NONE`, `ins.has_lock_prefix`, base o índice con `RegisterExt.size != 8`, o kind F16C. Los sitios de 4 bytes quedan pendientes hasta la instrucción siguiente: es "plana" si `flow_control == FlowControl.NEXT`, no `is_ip_rel_memory_operand`, no `has_rep_prefix`/`has_repne_prefix`, no es sitio, `ins.next_ip <= address + len(code)` e `ins.ip not in targets` al final de la pasada (la decisión sobre destinos se toma tras el barrido completo, por eso guarda el candidato y resuélvelo al final). Un sitio pendiente sin instrucción siguiente → `INT3`.

- [ ] **Paso 4: ejecutar los tests**

Run: `python -m unittest discover -s tests -p "test_isa_rewrite.py" -v`
Expected: 8 tests `ok`.

- [ ] **Paso 5: commit**

`git add scripts/isa_rewrite.py tests/test_isa_rewrite.py`; mensaje mediante `caveman-commit` (sugerencia: `Feat(ISA): localizar sítios MOVBE, BMI1 e LZCNT na imagem`).

---

### Task 3: tabla `G3ISA1`, lectura del `boot-linked.bin` y línea de comandos

**Files:**
- Modify: `scripts/isa_rewrite.py`
- Create: `tests/data/isa_fixture.bin`
- Test: `tests/test_isa_rewrite.py`

**Interfaces:**
- Produces (`scripts/isa_rewrite.py`):
  ```python
  HEADER = struct.Struct('<8sQ32sII8x')                 # magic, image size, sha256, sites, unsupported  (64 bytes)
  RECORD = struct.Struct('<QBBBBBBBBBBBB4xq15sB15sx')   # spec 4.4 (64 bytes)
  def read_boot_linked(data: bytes) -> tuple[list[tuple[int, int, int]], bytes]   # segments (address, size, flags), image
  def encode_table(image: bytes, sites: list[Site]) -> bytes
  def summary(sites: list[Site]) -> str
  def main(argv: list[str]) -> int                      # 0 ok, 1 = table unavailable
  def fixture_code() -> bytes                            # the code bytes behind tests/data/isa_fixture.bin
  ```
  `main`: `isa_rewrite.py <boot-linked.bin> --out <dir>`; escribe `<dir>/isa-rewrite.bin`, imprime `summary`. Sin `iced_x86`, archivo ausente o formato no válido: imprime `ISA: rewrite table unavailable (<motivo>)` y devuelve 1.

- [ ] **Paso 1: tests (fallando)**

```python
def boot_linked(segments):      # helper: fake G3PROBE5 as link_modules.py writes it, no modules/bindings/names/relocs
    image = b''.join(code.ljust(size, b'\0') for code, size, flags in segments)
    table = [(a, s, f) for a, (c, s, f) in zip(accumulate_addresses(segments), segments)]
    return (struct.pack('<8s6Q', b'G3PROBE5', len(image), 0, len(table), 0, 0, 1) + struct.pack('<Q', 0)
            + struct.pack('<4Q', 0, 0, 0, 0) + struct.pack('<Q', 0) + struct.pack('<Q', 0)
            + b''.join(struct.pack('<3Q', *t) for t in table) + image)

def test_read_boot_linked_returns_segments_and_image(self): ...   # two segments, one executable (flags 5), one data (flags 6)

def test_encode_table_header_and_record(self):
    sites = isa_rewrite.scan_segment(BLSR_ECX + MOVBE_STORE + NOP + RET, 0x2000)
    image = bytes(0x2000) + BLSR_ECX + MOVBE_STORE + NOP + RET
    table = isa_rewrite.encode_table(image, sites)
    magic, size, sha, count, unsupported = isa_rewrite.HEADER.unpack_from(table, 0)
    self.assertEqual((magic, size, sha, count, unsupported), (b'G3ISA1\0\0', len(image), hashlib.sha256(image).digest(), 2, 0))
    self.assertEqual(len(table), 64 + 2 * 64)
    offset, length, kind, width, mode, dst, src1, src2, mem_slot, base, index, scale, flags, disp, original, rl, relocated = isa_rewrite.RECORD.unpack_from(table, 64 + 64)
    self.assertEqual((offset, length, kind, mode, mem_slot, base, rl, relocated[:rl]), (0x2005, 4, isa_rewrite.KIND_MOVBE_STORE, isa_rewrite.MODE_JMP_RELOCATE, 1, 0, 1, NOP))
    self.assertEqual(original[:length], MOVBE_STORE)

def test_summary_line(self):
    sites = isa_rewrite.scan_segment(BLSR_ECX + MOVBE_STORE + NOP + bytes.fromhex('c4e2791310') + RET, 0)
    self.assertEqual(isa_rewrite.summary(sites), 'ISA: 3 sites (MOVBE 1, BMI1 1, LZCNT 0, F16C 1); 1 relocated, 0 int3, 1 unsupported')

def test_main_writes_table_for_executable_segments_only(self):   # tmp dir; data segment holds BLSR bytes too but is not scanned
def test_main_reports_unavailable_input(self):                     # missing file -> returncode 1, stdout startswith 'ISA: rewrite table unavailable ('
def test_fixture_file_is_current(self):
    code = isa_rewrite.fixture_code()
    expected = isa_rewrite.encode_table(code, isa_rewrite.scan_segment(code, 0))
    self.assertEqual((ROOT / 'tests/data/isa_fixture.bin').read_bytes(), expected)
```
`fixture_code()` devuelve, en este orden: `BLSR_ECX`, `MOVBE_STORE + NOP`, `BEXTR_RIP`, `LZCNT_AX`, `TZCNT_RAX`, `MOVBE_STORE + JMP_SELF` (pasa a `INT3`), `RET`. Ese archivo es el que carga el test C de la Tarea 5.

- [ ] **Paso 2: ejecutar y ver el fallo**

Run: `python -m unittest discover -s tests -p "test_isa_rewrite.py" -v`
Expected: `AttributeError: module 'isa_rewrite' has no attribute 'HEADER'`.

- [ ] **Paso 3: implementar lectura, codificación, resumen y `main`**

`read_boot_linked`: usa `unpack`/`span` de `prepare.py` y el orden del escritor en `link_modules.py` (cabecera `<8s6Q`, procparam `Q`, TLS `4Q`, `nmodules` + `7Q` cada uno, `nbindings` + `3Q` cada uno, `ns` segmentos `3Q`, `ni` nombres de 128 bytes, `nr` reubicaciones `QQqq`, imagen hasta el final). Exige magic `G3PROBE5`. `summary`: recuentos por conjunto mediante `SET_OF_KIND`, `relocated` = modo 1, `int3` = modo 2, `unsupported` = modo 3. Genera `tests/data/isa_fixture.bin` con `python -c "import isa_rewrite as r; c = r.fixture_code(); open('tests/data/isa_fixture.bin', 'wb').write(r.encode_table(c, r.scan_segment(c, 0)))"` desde `scripts/`.

- [ ] **Paso 4: ejecutar los tests**

Run: `python -m unittest discover -s tests -p "test_isa_rewrite.py" -v`
Expected: todos `ok` (14 tests).

- [ ] **Paso 5: commit**

`git add scripts/isa_rewrite.py tests/test_isa_rewrite.py tests/data/isa_fixture.bin`; mensaje mediante `caveman-commit` (sugerencia: `Feat(ISA): gravar a tabela G3ISA1 a partir do boot-linked.bin`).

---

### Task 4: emulador `isa_emulate` con oráculo nativo

**Files:**
- Create: `src/isa_emulate.h`, `src/isa_emulate.c`
- Test: `tests/test_isa_emulate.c`
- Modify: `gpu/CMakeLists.txt` (bloque `if (WIN32)`, junto a `cpu-check-test`), `build.sh` (bloque `--test`)

**Interfaces:**
- Produces (`src/isa_emulate.h`):
  ```c
  #include <stdint.h>
  #include "runtime.h"   /* ABI */
  enum { ISA_MOVBE_LOAD = 1, ISA_MOVBE_STORE, ISA_LZCNT, ISA_TZCNT, ISA_ANDN, ISA_BEXTR, ISA_BLSI, ISA_BLSMSK, ISA_BLSR, ISA_VCVTPH2PS, ISA_VCVTPS2PH };
  enum { ISA_MODE_JMP = 0, ISA_MODE_JMP_RELOCATE, ISA_MODE_INT3, ISA_MODE_UNSUPPORTED };
  enum { ISA_NO_REG = 0xff, ISA_MEM_NONE = 0, ISA_MEM_DST = 1, ISA_MEM_SRC = 2, ISA_MEM_RIP_RELATIVE = 1 };
  typedef struct {                       /* one G3ISA1 record (spec 4.4), as stored in the file */
      uint64_t offset;
      uint8_t length, kind, width, mode, dst, src1, src2, mem_slot, base, index, scale, mem_flags;
      uint8_t reserved[4];
      int64_t disp;
      uint8_t original[15];
      uint8_t relocated_length;
      uint8_t relocated[15];
      uint8_t reserved2;
  } IsaSite;
  _Static_assert(sizeof(IsaSite) == 64, "G3ISA1 record");
  typedef struct { uint64_t rflags; uint64_t gpr[16]; } IsaRegs;   /* gpr: x86 numbering; gpr[4] = guest rsp */
  enum { ISA_CF = 1u << 0, ISA_ZF = 1u << 6, ISA_SF = 1u << 7, ISA_OF = 1u << 11 };
  /* Memory operands with mem_flags & ISA_MEM_RIP_RELATIVE address image + disp. */
  void isa_emulate_set_image(unsigned char *image);
  /* Executes the site's instruction on regs (spec 5.6). */
  void ABI isa_emulate(const IsaSite *site, IsaRegs *regs);
  /* Flags the instruction defines (the test compares only these). */
  uint64_t isa_defined_flags(uint8_t kind);
  ```

- [ ] **Paso 1: escribir `tests/test_isa_emulate.c` (fallando)**

Compilado con `-mbmi -mlzcnt -mmovbe`. Se salta con `puts("isa-emulate-test skipped: CPU lacks BMI1, LZCNT or MOVBE")` y `return 0` si `!__builtin_cpu_supports("bmi") || !__builtin_cpu_supports("lzcnt") || !__builtin_cpu_supports("movbe")`. Oráculo: macro que ejecuta la instrucción nativa mediante inline asm y captura las flags con `pushfq; pop`. Para cada ancho en {16, 32, 64} y cada kind de registro (LZCNT, TZCNT, ANDN, BEXTR, BLSI, BLSMSK, BLSR), con `dst = 0 (rax)`, `src1 = 1 (rcx)`, `src2 = 2 (rdx)`:
```c
static const uint64_t EDGES[] = {0, 1, 2, 0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff, 0x7fffffff, 0x80000000, 0xffffffff,
                                 0x7fffffffffffffff, 0x8000000000000000, 0xffffffffffffffff};
/* ctrl for BEXTR: start and len in {0, 1, 7, 8, 15, 16, 31, 32, 63, 64, 255} crossed */
```
más 20 000 entradas de un xorshift64 por caso. Aserciones por entrada: `regs.gpr[0]` igual al nativo (para 16 bits el nativo y el emulado parten de `rax = 0x1122334455667788` y se compara el valor entero de `rax`: bits altos preservados; para 32 bits el resultado pone a cero los 32 altos) y `(regs.rflags ^ native_flags) & isa_defined_flags(kind) == 0` con `regs.rflags` inicial alternando `0` y `0xcd5` (todas las flags definidas activadas) para pillar una flag no escrita. Memoria: `MOVBE_LOAD`/`MOVBE_STORE` en los tres anchos con `base = 1 (rcx)` apuntando a un buffer, `disp = 8`, y `BEXTR` de 32 bits con `mem_slot = 2`, `mem_flags = 1`, `disp` = offset del buffer dado por `isa_emulate_set_image`. Un fallo imprime kind, ancho, entrada, esperado y obtenido.

- [ ] **Paso 2: targets de build**

`gpu/CMakeLists.txt`: `add_executable(isa-emulate-test EXCLUDE_FROM_ALL ../tests/test_isa_emulate.c ../src/isa_emulate.c)` con las mismas propiedades de `cpu-check-test` más `set_source_files_properties(../src/isa_emulate.c PROPERTIES COMPILE_OPTIONS "-mgeneral-regs-only")` y `set_source_files_properties(../tests/test_isa_emulate.c PROPERTIES COMPILE_OPTIONS "-mbmi;-mlzcnt;-mmovbe")`. `build.sh --test`: `"$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -mgeneral-regs-only -c src/isa_emulate.c -o out/isa_emulate.o` y `"$CC" ... -mbmi -mlzcnt -mmovbe -I. -Isrc tests/test_isa_emulate.c out/isa_emulate.o -o out/isa-emulate-test && out/isa-emulate-test`.

- [ ] **Paso 3: compilar y ver el fallo**

Run: `ninja -C out/w isa-emulate-test`
Expected: error `'isa_emulate.h' file not found`.

- [ ] **Paso 4: implementar `src/isa_emulate.c`**

Funciones estáticas `read_operand(site, regs, which)`, `write_dst(site, regs, value)` (reglas de ancho de la spec 5.6), `effective_address(site, regs)`, `set_flags(regs, mask, value)`. Sin libc: lecturas y escrituras de memoria con punteros `uint16_t*/uint32_t*/uint64_t*` (los accesos desalineados son válidos en x86). `bsr`/`bsf` con `__builtin_clzll`/`__builtin_ctzll` protegidos frente a cero. BEXTR: `start > width - 1 → 0`; `len` saturado en `width - start`. Kinds F16C: nada (nunca se aplican). `isa_defined_flags`: ANDN/BEXTR/BLSI/BLSMSK/BLSR → `ISA_SF|ISA_ZF|ISA_CF|ISA_OF` (BEXTR: `ISA_ZF|ISA_CF|ISA_OF`), LZCNT/TZCNT → `ISA_CF|ISA_ZF`, MOVBE → 0.

- [ ] **Paso 5: compilar y ejecutar**

Run: `ninja -C out/w isa-emulate-test && PATH=/c/msys64/clang64/bin:$PATH out/w/isa-emulate-test.exe`
Expected: `isa-emulate-test ok`.

- [ ] **Paso 6: commit**

`git add src/isa_emulate.c src/isa_emulate.h tests/test_isa_emulate.c gpu/CMakeLists.txt build.sh`; mensaje mediante `caveman-commit` (sugerencia: `Feat(ISA): emular MOVBE, BMI1 e LZCNT em C com flags exatas`).

---

### Task 5: tabla, stubs y aplicación (`isa_rewrite.c`)

**Files:**
- Create: `src/isa_rewrite.h`, `src/isa_rewrite.c`
- Test: `tests/test_isa_stub.c`
- Modify: `gpu/CMakeLists.txt`, `build.sh` (target `isa-stub-test`: `tests/test_isa_stub.c ../src/isa_rewrite.c ../src/isa_emulate.c`, mismos flags de la Tarea 4; Windows enlaza `bcrypt -pthread` como los demás)

**Interfaces:**
- Consumes: `IsaSite`, `IsaRegs`, `isa_emulate`, `isa_emulate_set_image` (Tarea 4); `CPU_SET_*` (Tarea 1).
- Produces (`src/isa_rewrite.h`):
  ```c
  #include "isa_emulate.h"
  #include "cpu_check.h"
  typedef struct {
      IsaSite *sites; uint64_t *hits;      /* hits[i] belongs to sites[i] */
      uint32_t count, unsupported;
      uint64_t image_size; uint8_t sha256[32];
      unsigned char *image, *stubs;        /* set by isa_rewrite_apply */
  } IsaTable;
  typedef struct { uint32_t rewritten, jmp, relocated, int3, skipped, unsupported_needed; } IsaReport;
  enum { ISA_STUB_BYTES = 256 };
  /* Parses a G3ISA1 file (spec 4.4). NULL and one stderr line ("ISA: <reason>") when invalid. */
  IsaTable *isa_table_load(const char *path);
  /* 0 when size and sha256 match the image. */
  int isa_table_matches(const IsaTable *t, const unsigned char *image, uint64_t size);
  size_t isa_stub_area_size(const IsaTable *t);           /* count * ISA_STUB_BYTES */
  unsigned isa_site_set(const IsaSite *s);                 /* CPU_SET_* of the kind */
  /* Rewrites the sites whose set is in `sets` (spec 5.4). Writes nothing and returns
   * unsupported_needed > 0 when a needed site is UNSUPPORTED. `stubs` has isa_stub_area_size bytes, RW. */
  IsaReport isa_rewrite_apply(IsaTable *t, unsigned char *image, unsigned char *stubs, unsigned sets);
  ```

- [ ] **Paso 1: escribir `tests/test_isa_stub.c` (fallando)**

El test reserva una región RWX (`VirtualAlloc(..., PAGE_EXECUTE_READWRITE)` / `mmap(PROT_READ|PROT_WRITE|PROT_EXEC)`) de 3 páginas: página 0 = "imagen" con fragmentos, páginas 1-2 = área de stubs. Los fragmentos son funciones SysV (`typedef uint64_t (ABI *Snippet)(uint64_t in, uint64_t *out)`), escritas byte a byte:
```c
/* S1 at 0x000: mov qword [rsp-8], 0xdeadbeef (sign-extended) ; mov ecx, edi ; blsr ecx, ecx ; mov rdx, [rsp-8] ;
 *              mov [rsi+8], rdx ; pushfq ; pop qword [rsi] ; mov eax, ecx ; ret     -> site blsr (5 bytes, JMP)
 *              (the red-zone slot is read before pushfq overwrites it; mov never touches flags) */
/* S2 at 0x040: mov ecx, esi ; movbe [rdi], ecx ; mov eax, ecx ; ret                 -> site movbe store (4 bytes) + relocated `mov eax, ecx` (89 c8) */
/* S3 at 0x080: mov ecx, edi ; blsr ecx, ecx ; blsr ecx, ecx ; mov eax, ecx ; ret     -> two adjacent JMP sites */
/* S4 at 0x0c0: lzcnt eax, edi ; ret                                                 -> 4-byte site, mode INT3 (Task 6 runs it) */
```
Tabla montada a mano (`IsaSite` por fragmento, `original` copiado de los bytes). Tests (`assert`):
- `test_jmp_site`: aplica con `sets = 0xffffffff`; llama a S1 con `in = 0xf0`: devuelve `_blsr_u32(0xf0) == 0xe0`, flags en `out[0]` con `(out[0] ^ native) & isa_defined_flags(ISA_BLSR) == 0` para `in` en `{0, 1, 0xf0, 0x80000000}`, `out[1] == 0xffffffffdeadbeef` (red zone intacta); los primeros 5 bytes del sitio son `E9 rel32` y `stubs + 0*256` empieza con `48 8D A4 24 00 FF FF FF`.
- `test_relocated_site`: S2 con `in = 0x11223344` escribe `0x44332211` en el buffer y devuelve `0x11223344` (la instrucción reubicada se ejecutó); bytes del sitio: `E9 rel32` + `90`.
- `test_adjacent_sites`: S3 con `in = 0xf0` devuelve `0xc0`.
- `test_xmm_preserved`: rellena xmm0-15 con patrones mediante `vmovdqu`/`movdqu`, llama a S1, lee de vuelta: iguales.
- `test_sets_filter`: nueva aplicación sobre una copia de la imagen con `sets = CPU_SET_LZCNT`: solo cambia S4 (`report.rewritten == 1`, S1 intacto).
- `test_skipped_when_bytes_differ`: altera un byte de S1 antes de aplicar: `report.skipped == 1`, bytes del sitio sin cambios.
- `test_table_mismatch`: `isa_table_matches` con otro tamaño u otro sha256 → `!= 0`.
- `test_unsupported_blocks_apply`: tabla con un sitio `ISA_MODE_UNSUPPORTED` de kind ANDN: `report.unsupported_needed == 1`, imagen byte a byte igual a la de antes.
- `test_largest_stub_fits`: sitio `JMP_RELOCATE` con `relocated_length = 15` (15 `90`): se aplica sin fallo y el `stub` termina dentro de 256 bytes (el byte 255 del stub es 0).
- `test_table_load_fixture`: `isa_table_load("tests/data/isa_fixture.bin")` (ruta relativa a la raíz, pasada por `argv[1]`): `count == 6`, `unsupported == 0`, `sites[0].kind == ISA_BLSR`, `sites[1].mode == ISA_MODE_JMP_RELOCATE`, `sites[5].mode == ISA_MODE_INT3`, `isa_stub_area_size == 6 * 256`.

- [ ] **Paso 2: compilar y ver el fallo**

Run: `ninja -C out/w isa-stub-test`
Expected: `'isa_rewrite.h' file not found`.

- [ ] **Paso 3: implementar `src/isa_rewrite.c`**

`isa_table_load`: `fread` de la cabecera, valida magic y `count <= 1u << 20`, reserva `sites` y `hits` (`calloc`). `isa_rewrite_apply`: la pasada 1 cuenta `unsupported_needed` (modo 3 e `isa_site_set` en `sets`) y devuelve antes si > 0; la pasada 2 por cada sitio seleccionado: `memcmp(image + offset, original, length)` → `skipped`; genera el stub en `stubs + i * ISA_STUB_BYTES` con una función `emit_stub(unsigned char *stub, const IsaSite *s, uint64_t *hits, unsigned char *image)` que escribe el pseudo-asm de la spec 5.5 y devuelve el tamaño (`fail` si > 256; usa un `assert` interno más retorno de error); luego escribe los bytes del sitio según el modo (spec 4.3). Encodings fijos, obtenidos con `llvm-mc --show-encoding -x86-asm-syntax=intel` (en `C:/msys64/clang64/bin`), por ejemplo: `lea rsp,[rsp-0x100]` = `48 8D A4 24 00 FF FF FF`; `pushfq` = `9C`; `popfq` = `9D`; `mov [rsp+disp8], rax` = `48 89 44 24 disp8` (REX.R `4C` para r8-r15, campo reg en el modrm); `mov rbp, rsp` = `48 89 E5`; `and rsp, -16` = `48 83 E4 F0`; `mov rax, imm64` = `48 B8 imm64` (`48 BF` rdi, `48 BE` rsi); `inc qword [rax]` = `48 FF 00`; `call rax` = `FF D0`; `jmp rel32` = `E9 rel32`. Orden de guardado: `lea rsp,[rsp-0x100]`, 15 `mov [rsp+8*i], reg` (i ≠ 4), `lea rax,[rsp+0x100]`, `mov [rsp+0x20], rax`, `pushfq` (entonces `IsaRegs* == rsp`), contador, `mov rbp, rsp`, `and rsp, -16`, `mov rdi, site`, `mov rsi, rbp`, `mov rax, isa_emulate`, `call rax`, `mov rsp, rbp`, `popfq`, 15 `mov reg, [rsp+8*i]`, `lea rsp,[rsp+0x100]`, instrucción reubicada, `jmp` a `image + offset + length` (modo 0) o `image + offset + 4 + relocated_length` (modo 1). `rel32 = destino - (dirección del byte siguiente al jmp)`, verificado en `int32_t`.

- [ ] **Paso 4: compilar y ejecutar**

Run: `ninja -C out/w isa-stub-test && PATH=/c/msys64/clang64/bin:$PATH out/w/isa-stub-test.exe tests/data/isa_fixture.bin`
Expected: `isa-stub-test ok` (el test de S4/INT3 solo en la Tarea 6).

- [ ] **Paso 5: commit**

`git add src/isa_rewrite.c src/isa_rewrite.h tests/test_isa_stub.c gpu/CMakeLists.txt build.sh`; mensaje mediante `caveman-commit` (sugerencia: `Feat(ISA): gerar stubs e reescrever os sítios da tabela`).

---

### Task 6: trap `int3`, informe de ejecuciones y mapeo de stubs

**Files:**
- Modify: `src/isa_rewrite.h`, `src/isa_rewrite.c`
- Test: `tests/test_isa_stub.c`

**Interfaces:**
- Produces (`src/isa_rewrite.h`):
  ```c
  /* int3 at `address` (the CC byte): 1 when it is a rewritten INT3 site; regs updated, *next_rip = site + 4. */
  int isa_rewrite_trap(IsaTable *t, uintptr_t address, IsaRegs *regs, uintptr_t *next_rip);
  /* Site index whose stub holds rip, or -1. */
  int isa_rewrite_site_of_stub(const IsaTable *t, uintptr_t rip);
  /* "ISA: most executed sites:" then up to 10 lines "  0x<offset> <mnemonic> <hits>" (hits > 0 only). */
  void isa_rewrite_report(const IsaTable *t, FILE *out);
  const char *isa_kind_name(uint8_t kind);   /* "movbe", "lzcnt", "tzcnt", "andn", "bextr", "blsi", "blsmsk", "blsr", "vcvtph2ps", "vcvtps2ph" */
  ```

- [ ] **Paso 1: tests (fallando)**

- `test_int3_site_via_handler`: el test instala su propio handler (Windows: `AddVectoredExceptionHandler` tratando `EXCEPTION_BREAKPOINT`: monta `IsaRegs` desde el `CONTEXT` (`Rax..R15`, `EFlags`), llama a `isa_rewrite_trap(t, (uintptr_t)record->ExceptionAddress, &regs, &rip)`, copia de vuelta y `Rip = rip`, `EXCEPTION_CONTINUE_EXECUTION`; Linux: `sigaction(SIGTRAP)` con `REG_RIP - 1`). Llama a S4 con `in = 0x1000`: devuelve `_lzcnt_u32(0x1000) == 19`; el `hits` del índice de S4 en la tabla (el quinto sitio: S3 tiene dos) vale 1 tras una llamada.
- `test_trap_unknown_address`: `isa_rewrite_trap(t, (uintptr_t)image + 0x3ff, ...)` devuelve 0 y no altera `regs`.
- `test_site_of_stub`: `isa_rewrite_site_of_stub(t, (uintptr_t)stubs + 256 + 10) == 1`; fuera del área → -1.
- `test_report_top_sites`: tabla propia con cuatro sitios en los offsets `0x10` (blsr), `0x20` (movbe), `0x30` (lzcnt), `0x40` (bextr) y `hits = {5, 0, 7, 1}`; escribe en `tmpfile()`, lee de vuelta: `ISA: most executed sites:\n  0x30 lzcnt 7\n  0x10 blsr 5\n  0x40 bextr 1\n` y nada más (un sitio con 0 ejecuciones queda fuera).

- [ ] **Paso 2: compilar y ver el fallo**

Run: `ninja -C out/w isa-stub-test`
Expected: `call to undeclared function 'isa_rewrite_trap'`.

- [ ] **Paso 3: implementar**

`isa_rewrite_trap`: búsqueda binaria por `offset == address - image` entre los sitios con modo `ISA_MODE_INT3` y byte `CC` presente (aplicados), `isa_emulate`, `hits[i]++`, `*next_rip = address + 4`. Informe: ordenación parcial de los 10 mayores sin reservar memoria.

- [ ] **Paso 4: compilar y ejecutar**

Run: `ninja -C out/w isa-stub-test && PATH=/c/msys64/clang64/bin:$PATH out/w/isa-stub-test.exe tests/data/isa_fixture.bin`
Expected: `isa-stub-test ok`.

- [ ] **Paso 5: commit**

`git add src/isa_rewrite.c src/isa_rewrite.h tests/test_isa_stub.c`; mensaje mediante `caveman-commit` (sugerencia: `Feat(ISA): atender int3 dos sítios restantes e relatar os mais executados`).

---

### Task 7: integración en el probe

**Files:**
- Modify: `src/probe.c` (parseo de las opciones en `main`; `allocate(round_page(size))` de la imagen; bloque tras `patch_tcb_loads` y antes del primer `protect`; `vectored_fault`, `fatal_exception`, `describe_address` en Windows; `fault()` y la instalación de `sigaction` en Linux), `src/runtime.h`, `src/runtime.c` (`runtime_report`)
- Modify: `build.sh` (línea del `gow3-probe`: `out/isa_emulate.o` compilado con `-mgeneral-regs-only` y `src/isa_rewrite.c`), `gpu/CMakeLists.txt` (`add_executable(gow3-probe ...)` incorpora `../src/isa_rewrite.c ../src/isa_emulate.c` con la `COMPILE_OPTIONS` de `isa_emulate.c`)

**Interfaces:**
- Consumes: `IsaTable`, `isa_table_load`, `isa_table_matches`, `isa_stub_area_size`, `isa_rewrite_apply`, `isa_rewrite_trap`, `isa_rewrite_site_of_stub`, `isa_rewrite_report`, `isa_emulate_set_image`; `cpu_missing_sets` de la Tarea 1.
- Produces: `src/runtime.h`: `void runtime_set_report_hook(void (*hook)(void));` llamado dentro de `runtime_report()` cuando está definido (el probe registra una función que llama a `isa_rewrite_report(table, stdout)`).

Sin test unitario nuevo: la verificación es el probe construido, el `isa-stub-test` y la ronda de extremo a extremo de la Tarea 10.

- [ ] **Paso 1: opción y política**

`--isa-rewrite <file>` en el bucle de opciones (`isa_file`), en la línea `Usage:`. Tras el bloque de CPU: `const char *policy = getenv("GOW3_ISA_REWRITE")`; `unsigned isa_sets = policy && !strcmp(policy, "1") ? 0xffffffffu : policy && !strcmp(policy, "0") ? 0 : cpu_missing_sets;`. Si `cpu_missing_sets && !isa_sets` (política `0`) o `isa_sets && cpu_missing_sets && !isa_file` → `printf("STOP: this CPU lacks %s and no rewrite table matches the image\n", missing)`, `return 20`.

- [ ] **Paso 2: cargar la tabla y reservar el área de stubs**

Declara en la parte superior de `probe.c`, junto a `image`: `static IsaTable *isa_table;` (los handlers y el hook de informe leen de aquí) y `static void isa_report_hook(void) { isa_rewrite_report(isa_table, stdout); }`. Antes de `image = allocate(round_page(size))`: `isa_table = isa_sets && isa_file ? isa_table_load(isa_file) : NULL;` (`NULL` con `cpu_missing_sets` → el mismo `STOP` del paso 1). `size_t stub_area = isa_table ? round_page(isa_stub_area_size(isa_table)) : 0; image = allocate(round_page(size) + stub_area); unsigned char *stubs = image + round_page(size);`. Tras el `fread` de la imagen: `if (isa_table && isa_table_matches(isa_table, image, size))` → `STOP` idéntico, `return 20`. En el paso 3, lee `isa` como `isa_table`.

- [ ] **Paso 3: aplicar**

Justo después de la línea `Guest TCB loads redirected` (dentro del `#ifdef _WIN32`, pero la llamada queda fuera de él) y antes de los `protect`:
```c
if (isa) {
    isa_emulate_set_image(image);
    IsaReport r = isa_rewrite_apply(isa, image, stubs, isa_sets);
    if (r.unsupported_needed) { printf("STOP: this CPU lacks %s and %u sites use encodings the rewrite cannot emulate\n", missing, r.unsupported_needed); return 20; }
    if (isa_sets == 0xffffffffu) printf("ISA: forced rewrite of all %u sites (GOW3_ISA_REWRITE=1)\n", r.rewritten);
    else printf("ISA: CPU lacks %s: %u sites rewritten (%u jmp, %u relocated, %u int3), %u skipped\n", missing, r.rewritten, r.jmp, r.relocated, r.int3, r.skipped);
    runtime_set_report_hook(isa_report_hook);
    protect(stubs, stub_area, 5);
}
```
`missing` es el buffer de nombres de `cpu_missing_rewritable` (en modo forzado con CPU completa imprime la línea `forced`). Los `protect` de la imagen siguen como están.

- [ ] **Paso 4: handlers**

Windows, en `vectored_fault` antes del bloque de fallos fatales: `if (code == EXCEPTION_BREAKPOINT && isa_table)` → monta `IsaRegs` del `CONTEXT` (`Rax, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi, Rdi, R8..R15`, `EFlags`), `isa_rewrite_trap(isa_table, (uintptr_t)record->ExceptionAddress, &regs, &rip)`; si 1, copia de vuelta, `Rip = rip`, `EXCEPTION_CONTINUE_EXECUTION`. Linux: `sigaction(SIGTRAP, &sa, NULL)` junto con los otros; en `fault()`, `sig == SIGTRAP && isa_table` → `IsaRegs` de `uc_mcontext.gregs[REG_RAX...]` y `REG_EFL`, `address = REG_RIP - 1`; si 1, copia de vuelta, `REG_RIP = rip`, `return`. En `describe_address` (Windows) y en el `snprintf` de "Guest fault" (Linux): si `isa_rewrite_site_of_stub(isa_table, rip) >= 0`, añade `While emulating site 0x<offset> (<isa_kind_name>)`. `fatal_exception` y `fault()` llaman a `isa_rewrite_report(isa_table, stderr)` antes de terminar cuando hay `isa_table`.

- [ ] **Paso 5: build y tests**

Run: `ninja -C out/w gow3-probe isa-stub-test isa-emulate-test cpu-check-test` y los tres tests; `python -m unittest discover -s tests -p "test_*.py"`.
Expected: build sin avisos; `isa-stub-test ok`, `isa-emulate-test ok`, `cpu-check-test ok`; Python sin regresión (solo los 5 fallos preexistentes).

- [ ] **Paso 6: commit**

`git add src/probe.c src/runtime.c src/runtime.h build.sh gpu/CMakeLists.txt`; mensaje mediante `caveman-commit` (sugerencia: `Feat(Probe): aplicar a reescrita ISA quando a CPU não tem os conjuntos`).

---

### Task 8: `run.py` llama a `--cpu-features` y al localizador

**Files:**
- Modify: `run.py` (`main`, antes de `run_script('prepare.py', ...)` y después de `run_script('link_modules.py', ...)`; montaje de `command`)
- Test: `tests/test_run_isa.py`

**Interfaces:**
- Produces (`run.py`, funciones puras a nivel de módulo):
  ```python
  def parse_cpu_features(output: str) -> list[str]     # 'CPU: missing MOVBE, BMI1, LZCNT\n' -> ['MOVBE', 'BMI1', 'LZCNT']; 'CPU: complete' -> []
  def isa_rewrite_wanted(missing: list[str], env: Mapping[str, str]) -> bool   # env GOW3_ISA_REWRITE: '1' -> True, '0' -> False, else bool(missing)
  ```

- [ ] **Paso 1: tests (fallando)**

`tests/test_run_isa.py` (`sys.path.insert(0, str(ROOT))`, `import run`):
```python
def test_parse_cpu_features(self):
    self.assertEqual(run.parse_cpu_features('CPU: missing MOVBE, BMI1, LZCNT\n'), ['MOVBE', 'BMI1', 'LZCNT'])
    self.assertEqual(run.parse_cpu_features('CPU: complete\n'), [])
def test_rewrite_wanted_follows_cpu_by_default(self):
    self.assertTrue(run.isa_rewrite_wanted(['MOVBE'], {}))
    self.assertFalse(run.isa_rewrite_wanted([], {}))
def test_rewrite_forced(self):
    self.assertTrue(run.isa_rewrite_wanted([], {'GOW3_ISA_REWRITE': '1'}))
def test_rewrite_disabled(self):
    self.assertFalse(run.isa_rewrite_wanted(['MOVBE', 'BMI1'], {'GOW3_ISA_REWRITE': '0'}))
```

- [ ] **Paso 2: ejecutar y ver el fallo**

Run: `python -m unittest discover -s tests -p "test_run_isa.py" -v`
Expected: `AttributeError: module 'run' has no attribute 'parse_cpu_features'`.

- [ ] **Paso 3: implementar**

En `main`, después de resolver `probe`: `features = subprocess.run([str(probe), '--cpu-features'], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, text=True, creationflags=no_console())`; `returncode == 20` → `print(features.stdout, end='', flush=True)` y `sys.exit(20)`; `missing = parse_cpu_features(features.stdout)`. Después de `link_modules.py`: `isa_args = []`; si `isa_rewrite_wanted(missing, env)`: `result = subprocess.run([... run_script-style command for 'isa_rewrite.py', out / 'boot-linked.bin', '--out', out], stdout=PIPE, text=True, ...)`; imprime `result.stdout`; `returncode == 0` → `isa_args = ['--isa-rewrite', str(out / 'isa-rewrite.bin')]`. (Reutiliza `run_script` con un parámetro `check=False` que devuelve `(returncode, stdout)` en lugar de `sys.exit`.) Añade `*isa_args` al `command` del probe.

- [ ] **Paso 4: ejecutar los tests**

Run: `python -m unittest discover -s tests -p "test_run_isa.py" -v`
Expected: todos `ok`.

- [ ] **Paso 5: commit**

`git add run.py tests/test_run_isa.py`; mensaje mediante `caveman-commit` (sugerencia: `Feat(Run): gerar a tabela ISA quando o probe reporta conjuntos ausentes`).

---

### Task 9: empaquetado, READMEs y documentación

**Files:**
- Modify: `packaging/windows/package.sh` (tras el `pip install ... pyinstaller`; lista `hidden`), `packaging/linux/build.sh` (lista `hidden`; comentario sobre el wheel), `packaging/windows/README.md` (párrafo de PyInstaller), `README.md` y `README.pt-BR.md` (sección Requisitos, ítem del procesador), `packaging/windows/README-Windows.txt` (ítem del procesador)

- [ ] **Paso 1: empaquetado**

`package.sh`: `out/pyenv/Scripts/python.exe -m pip install -q --disable-pip-version-check pyinstaller iced-x86==1.21.0`; `iced_x86` en la lista `for module in ...` de los dos scripts. `packaging/linux/build.sh`: comentario encima de la lista: `# iced-x86 (pip install iced-x86==1.21.0): scripts/isa_rewrite.py, CPUs without MOVBE/BMI1/LZCNT.` `packaging/windows/README.md`: una frase en el párrafo de PyInstaller indicando que el venv recibe `iced-x86`.

- [ ] **Paso 2: READMEs**

Ítem del procesador (en): `- A 64-bit processor with AVX: Intel Sandy Bridge (2011) or newer, AMD FX or newer. On CPUs without MOVBE, BMI1 or LZCNT (Sandy/Ivy Bridge, AMD FX) the loader rewrites the game's instructions that use them to emulated stubs and logs an `ISA:` line; performance on those CPUs has not been measured yet. Older CPUs stop at start with a `STOP: this CPU lacks` line in the log.` Traducción equivalente en `README.pt-BR.md`; versión corta en `README-Windows.txt`.

- [ ] **Paso 3: verificación**

Run: `grep -n "iced" packaging/windows/package.sh packaging/linux/build.sh packaging/windows/README.md && grep -n "Sandy Bridge" README.md README.pt-BR.md packaging/windows/README-Windows.txt`
Expected: cada archivo aparece al menos una vez; `bash -n packaging/windows/package.sh packaging/linux/build.sh` sin error.

- [ ] **Paso 4: commit**

`git add packaging/windows/package.sh packaging/linux/build.sh packaging/windows/README.md README.md README.pt-BR.md packaging/windows/README-Windows.txt`; mensaje mediante `caveman-commit` (sugerencia: `Docs(README): requisito de CPU passa a AVX com reescrita ISA`).

---

### Task 10: verificación de extremo a extremo en modo forzado

**Files:** ninguno (verificación).

- [ ] **Paso 1: build completo y tests**

Run (MSYS2 en `/w`): `ninja -C out/w gow3-probe gow3-gpu-capabilities gow3-play cpu-check-test isa-emulate-test isa-stub-test`, los tres tests, y `python -m unittest discover -s tests -p "test_*.py"`.
Expected: tres `ok`; Python solo con los 5 fallos preexistentes.

- [ ] **Paso 2: juego en modo forzado**

Con `out/gow3-probe.exe` actualizado y el Python del repositorio con `iced-x86`: `GOW3_ISA_REWRITE=1 GOW3_GAME_DIR="C:/Users/Lucas Emiliano/Documents/My Games/GOW III/God.of.War.III.Remastered.PS4-CUSA01623/Image0" python run.py`.
Expected en el log: `CPU: complete`, `ISA: 14xx sites (MOVBE 104, BMI1 12xx, LZCNT 77, F16C 0); 8x relocated, 0 int3, 0 unsupported`, `ISA: forced rewrite of all 14xx sites (GOW3_ISA_REWRITE=1)`; el juego abre, llega al menú y a una lucha sin fallos nuevos; al cerrar, `ISA: most executed sites:` con hasta 10 líneas. Registra las líneas `ISA:` en el resumen de la tarea (dicen si el enfoque C de la spec compensa algún día).

- [ ] **Paso 3: sin la variable**

Mismo comando sin `GOW3_ISA_REWRITE`: ninguna línea `ISA:`, juego igual que antes.

- [ ] **Paso 4: Linux (cuando haya máquina)**

`bash build.sh --test` y la misma ronda forzada con `run.sh`. Registra el resultado; un fallo aquí no bloquea el merge de Windows, pasa a ser una issue.
