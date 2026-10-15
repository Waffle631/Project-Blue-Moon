# Spec 4 — Audio: Audiodec y AudiodecCpu

Fecha: 2026-10-14. Estado: **borrador para aprobación**. Roadmap: `.waffle/ROADMAP.md`, sección "Spec 4".

Marcas: **[V]** comprobado; **[H]** hipótesis a confirmar en la Fase A; **[D]** decisión del usuario.

## 1. Objetivo

Que el audio que Tsukihime decodifica con `libSceAudiodec` y `libSceAudiodecCpu` (BGM, voces y efectos)
se reproduzca sin chirridos ni cortes. El proyecto es un experimento y un hobby: **no hay tope de
tiempo**; si la investigación se alarga, se alarga.

## 2. Hechos comprobados

- **[V]** El eboot importa nueve funciones de Audiodec (6 de `libSceAudiodec`, 3 de
  `libSceAudiodecCpu`), todas sin implementar:

| NID | Nombre |
|---|---|
| `VjhsmxpcezI` | `sceAudiodecInitLibrary` |
| `h5jSB2QIDV0` | `sceAudiodecTermLibrary` |
| `O3f1sLMWRvs` | `sceAudiodecCreateDecoder` |
| `Tp+ZEy69mLk` | `sceAudiodecDeleteDecoder` |
| `KHXHMDLkILw` | `sceAudiodecDecode` |
| `6Vf9WTLDoss` | `sceAudiodecClearContext` |
| `ktD2w3D4G2U` | `sceAudiodecCpuQueryMemSize` |
| `hdFsxo3MFu8` | `sceAudiodecCpuInitDecoder` |
| `lSVTiWV5wLc` | `sceAudiodecCpuDecode` |

- **[V]** El juego no importa AJM. `out/analysis.json` lista `libSceAudiodec.prx` y
  `libSceAudiodecCpu.prx` entre los módulos necesarios; no están en `sce_module/` (solo `libc`,
  `libSceFios2` y `libSceNpToolkit2`), así que son módulos del sistema, que el juego carga con
  `sceSysmoduleLoadModule` (`src/runtime_content.c:81`).
- **[V]** **shadPS4 upstream no tiene una implementación HLE de Audiodec**: su README pide colocar
  `libSceAudiodec.sprx` y otros módulos de firmware en `sys_modules` (carga el módulo real), y las
  notas de las versiones 0.17 y 0.18 no mencionan Audiodec. No hay código que copiar. (Fuente:
  búsqueda del 2026-10-14 en el README y las notas de versión de shadPS4.)
- **[V]** Decodificadores disponibles: LibAtrac9 (`third_party/LibAtrac9`, `out/libatrac9.a`) para
  ATRAC9 y FFmpeg (`src/runtime_mp3.c`, 145 líneas) para MP3/AAC. `src/runtime_ajm.c` (576 líneas)
  ya conecta ambos con la API AJM de GoW3: es la mejor plantilla de cómo guardar el estado del
  decodificador, medir tramas y devolver PCM.
- **[V]** Datos del juego: `image/bgm.{hed,mrg}`, `voice*.{hed,mrg,nam}`, `vag.{hed,mrg}`,
  `se_nam.mrg`, `vse.{hed,mrg}`. El nombre `vag` sugiere formato de PlayStation (ADPCM) que el juego
  podría decodificar por su cuenta sin Audiodec. **[H]** Solo algunos activos usan Audiodec; cuáles se
  verá en la Fase A.
- **[V]** `GOW3_AUDIO_TRACE` solo traza aperturas bajo `sound/` (`src/runtime_file.c:248`), ruta de
  GoW3; no sirve para Tsukihime. La Spec 6 lo generaliza.

## 3. Fase A — Investigación (primero)

Entregable: `docs/audiodec.md`, con el contenido de abajo. Sin él no se pasa a la Fase B.

1. **Instrumentación.** Un trazador `GOW3_TRACE_AUDIODEC=1` que, por cada llamada, imprima el nombre,
   los argumentos y un volcado hexadecimal acotado de las estructuras que el guest pasa por puntero
   (primeros 256 bytes), antes y después de la llamada. Todo se resuelve con una implementación
   **provisional que devuelve un error explícito tras trazar** (no éxito): el juego se detiene en el
   primer uso real y se aprende qué manda.
2. **Qué hay que averiguar (todo [H] hasta entonces).**
   - Qué módulos del sistema carga el juego (identificadores de `sceSysmoduleLoadModule`).
   - Qué `codecType` pide cada `CreateDecoder`/`InitDecoder`, y para qué recurso.
   - Layout de la estructura de control (`SceAudiodecCtrl` en la API pública) y de los parámetros
     por códec: dónde están el puntero de entrada, su tamaño, el búfer de salida, y dónde se escribe
     el tamaño consumido y el producido.
   - Secuencia de llamadas por pista: `InitLibrary` → `CreateDecoder` → `Decode`* → `ClearContext`
     (en un salto o un bucle) → `DeleteDecoder` → `TermLibrary`.
   - Códigos de error que el juego espera distinguir (fin de flujo, trama corrupta).
   - Diferencia real entre `Audiodec` y `AudiodecCpu` para este juego (probablemente la misma
     funcionalidad con memoria de trabajo entregada por el guest: `QueryMemSize`/`InitDecoder`).
3. **Fuentes.** El código de llamada y de proceso del propio juego (desensamblado de `eboot.bin`) y
   cualquier cabecera o documentación pública de la comunidad que exista. No se cita una
   estructura que no esté verificada con el comportamiento del juego.
4. **Datos de entrada.** Qué formato tienen los bloques en `bgm.mrg` y `voice*.mrg` (cabecera, tamaño
   de trama). Sirve para el test de la Fase B.

**Sin tope de tiempo (decidido 2026-10-14).** Lo único que se mantiene es el orden: la Fase A termina
con `docs/audiodec.md` antes de empezar la Fase B, para no implementar a ciegas. Si la Fase A revela
que hace falta otra estrategia, se vuelve a hablar con el usuario.

## 4. Fase B — Implementación

Un archivo nuevo, `src/runtime_audiodec.c`, con una tabla de decodificadores por identificador. Cada
uno guarda el códec, los parámetros, un contexto de LibAtrac9 o de FFmpeg y un búfer de salida.

- `Decode`: lee la trama de entrada del guest (validando que la dirección sea de memoria mapeada,
  `runtime_memory_is_mapped`), la decodifica, escribe el PCM en el formato y tamaño que el juego pidió
  y rellena el tamaño consumido y el producido.
- `ClearContext`: reinicia el estado del decodificador (para saltar en una pista).
- Un códec que no soportamos produce `STOP:` con el número, nunca silencio.
- Un flujo no válido devuelve el código de error de la PS4, sin cerrar el juego.
- Estructura análoga a `runtime_ajm.c`: contadores en `runtime_report` (`Runtime: Audiodec decoders=…,
  frames=…, errors=…`).

## 5. Requisitos

- Cuando el juego envíe un bloque válido, el sistema debe devolver el PCM en el formato y tamaño que
  el juego pidió, con los bytes consumidos correctos.
- Si el stream no es válido, el sistema debe devolver el código de error de la PS4 sin cerrar el juego.
- Códec no soportado → `STOP:` con el número.

## 6. Pruebas

- `tests/test_audiodec.c`, con el mismo esquema que `tests/test_mp3.c`: decodifica un clip de
  referencia y compara con el resultado directo de FFmpeg o LibAtrac9, muestra a muestra.
- **Clips de referencia.** MP3: ya existe `tests/data/sine.mp3`. ATRAC9: no hay un clip pequeño en el
  repositorio y LibAtrac9 solo decodifica; se extrae un fragmento de una pista del propio juego en
  local (sin versionarlo: es contenido con derechos), y el test **se omite** si falta, como los tests
  que necesitan el dump. Se documenta el comando para generarlo.
- Casos: tamaño de salida correcto, bytes consumidos, flujo corrupto (error sin cerrar el juego),
  `ClearContext` seguido de otra decodificación, códec desconocido (`STOP:`).
- Ejecución real: arrancar con `GOW3_TRACE_AUDIODEC` y escuchar BGM, voz y efectos del prólogo.

## 7. Archivos

Nuevos: `src/runtime_audiodec.c`, `docs/audiodec.md`, `tests/test_audiodec.c`, `tests/test_audiodec.py`.
Modificados: `src/runtime.h` y `src/runtime.c` (resolver y `runtime_report`), `src/import_names.inc`
(9 entradas), `src/runtime_content.c` (módulos de sistema de Audiodec si hace falta aceptarlos), `build.sh`.

## 8. Criterios de aceptación

- [ ] `docs/audiodec.md` describe el códec observado, las estructuras, la secuencia y los errores.
- [ ] `out/audiodec-test` decodifica los clips de referencia y coincide con FFmpeg/LibAtrac9.
- [ ] `--check-imports` no lista ninguna API de Audiodec.
- [ ] En el juego, BGM, voces y efectos del prólogo suenan sin chirridos ni cortes (lo juzga el usuario).
- [ ] Un códec no soportado detiene con `STOP:` y el número.

## 9. Riesgos

- Es la spec con más incertidumbre técnica por la falta de referencia (riesgo "Alto" del roadmap). La
  Fase A existe para convertir la incertidumbre en datos antes de escribir la implementación.
- Si el códec es uno que ni LibAtrac9 ni FFmpeg cubren, se detiene con `STOP:`; la decisión pasa al usuario.
- La salida de audio depende de la cola de `AudioOut` (`src/runtime_audio.c`, 302 líneas); si los
  chirridos vienen de allí, no de Audiodec, se separa el diagnóstico para no culpar a esta spec.
