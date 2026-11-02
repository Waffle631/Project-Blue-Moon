# Spec 9 — Texto y guiones: herramienta de extracción y mod de texto (borrador de la Beta)

Fecha: 2026-11-02. Estado: **borrador, sin aprobar** (ninguna tarea empezada). Roadmap: `docs/tsuki/ROADMAP.md`,
sección "Spec 9". Depende de: Spec 8 (la Alpha publicada); no toca el renderizador ni el audio. Es la primera spec de la Beta.

Marcas: **[V]** comprobado el 2026-11-02 con el dump; **[D]** decisión del usuario; **[?]** hipótesis que la tarea 1
debe confirmar o descartar antes de escribir código de producción.

## 1. Objetivo

Dejar al proyecto con dos cosas:

1. **Una herramienta que extrae los guiones y el texto del juego** (`allscr.mrg` y `script_text.mrg`) a un formato legible
   y estable, para poder leer las rutas y preparar una traducción.
2. **Un mod de texto**: un paquete pequeño con las líneas nuevas que, al arrancar, hace que el juego muestre ese
   texto en lugar del original, sin tocar los archivos del juego y sin distribuir ninguno de sus contenidos.

El usuario quiere esto como base de una traducción futura **[D]** (2026-11-02). La traducción en sí queda fuera de esta spec.

## 2. Decisiones del usuario

- **[D]** El alcance es `allscr.mrg` y `script_text.mrg`. Imágenes con texto, menús y vídeos quedan fuera (§9).
- **[D]** Esta spec es un borrador para la Beta; no se implementa hasta aprobarla.
- **[D]** La herramienta de extracción es un entregable propio y versionado, no los scripts sueltos de `out/`.

## 3. Hechos comprobados

- **[V]** `image/script_text.mrg` (12 601 040 bytes) es un archivo `mrgd00` con 10 entradas, en pares: tabla de offsets y
  texto. La entrada 0/1 es japonés, 2/3 inglés, 4/5 chino simplificado, 6/7 chino tradicional y 8/9 una quinta tabla
  (probablemente coreano; en 8/9 las líneas son casi todo `　`, el espacio ideográfico). La cabecera son 8 bytes por
  entrada, en unidades de 0x800 (`out/tsuki-scripts/mrg.py` la decodifica).
- **[V]** Cada tabla de offsets son enteros de 32 bits **big-endian**, un offset por línea más el final; el texto es UTF-8, una
  línea por entrada, terminada en `\r\n`. El inglés tiene **43 971 líneas**.
- **[V]** El texto inglés no lleva códigos de control: los únicos caracteres menores que 0x20 son los `\r\n` de fin de
  línea. Caracteres fuera de ASCII en el inglés: `―` (1091), `■` (478), `é` (15), `â` (3), `ê` (2), `°`, `è`, `ä`.
- **[V]** `image/allscr.mrg` (9,6 MB) contiene 550 entradas `MZX0`, todas descomprimen sin error con `out/tsuki-scripts/mzx.py`
  (literales con XOR `0xFF`). Cada una es un guion de comandos separados por `;`, como `_WKST(F023,eq,9)`,
  `_PGST(10013)` o `_TOMP(5)`. Los más frecuentes son `_WKST` (variables y banderas), `_STCC`, `_WNTY`, `_STCH`, `_STCP`.
- **[V]** Los guiones citan el texto por número de línea, con el formato `$NNNNNN` de seis cifras: `_ZM01d5a($000100)`
  muestra la línea 100. Las opciones del jugador usan `_SELR` y `_MSAD`. Hay 43 930 referencias a 43 902 líneas distintas;
  el máximo es la 43 958. Quedan **69 líneas** de las 43 971 sin referencia directa: 13 de ellas están por encima del máximo citado (43 958) y las
  otras 56 no se han mirado **[?]**.
- **[V]** `allscr.mrg` y `script_text.mrg` son archivos sueltos de `image/`: la superposición de `scripts/mods.py` ya sabría
  reemplazarlos si aceptara esa ruta. Hoy solo acepta `dvdroot_ps4/` (estructura del juego anterior).
- **[V]** El juego abre `image/` por `/app0/image/...` (`tests/test_runtime.c:354` lo usa de ejemplo).

## 4. Diseño

### 4.1. Herramienta de extracción (`tools/tsuki_script_dump.py`, nombre provisional)

Entrada: la carpeta del dump (`image/allscr.mrg`, `image/script_text.mrg`). Salida, en una carpeta que elige el usuario:

- `text/en.tsv` (y el mismo para `ja`, `zc`, `zt` si el usuario los pide): `número<TAB>texto`, con los `\r\n` finales
  fuera y los saltos internos escapados. Un archivo por idioma.
- `scripts/NNNN.txt`: cada guion de `allscr.mrg` descomprimido, un comando por línea, **con el texto inglés al lado** de
  cada `$NNNNNN` para que el guion se pueda leer como una novela.
- `index.tsv`: qué guion contiene cada línea, y a qué ruta o capítulo pertenece (**[?]** ver §8, riesgo 2).

Requisitos:

- Nada de lo extraído se versiona ni se publica. La carpeta de salida va a `out/` y `.gitignore` la cubre.
- Las rutinas de MZX y `mrgd00` pasan de los scripts sueltos de `out/tsuki-scripts/` a un módulo propio con tests, para que
  el mod y la herramienta compartan el mismo código.
- Lectura repetible: dos ejecuciones sobre el mismo dump dan archivos idénticos byte a byte.

### 4.2. Formato del mod de texto

Un mod es una carpeta con un archivo por idioma que reemplaza (por ahora solo inglés **[D]**):

```text
mods/
  Nombre del mod/
    text/en.tsv          número<TAB>texto   (solo las líneas que cambian)
```

- Las líneas que el mod no incluye se quedan como en el juego.
- El mod **nunca contiene `script_text.mrg`** ni el texto original. Lleva solo sus propias líneas.
- El número de líneas total no cambia: el mod no puede añadir ni quitar líneas, porque los guiones las citan por número.
  Un número fuera de rango, repetido o con formato incorrecto se rechaza con un mensaje claro que nombra el archivo y la línea.
- Los saltos de línea dentro de una línea de texto se escriben con la misma convención que `en.tsv` de la herramienta.

### 4.3. Construcción en el arranque

En cada inicio con un mod de texto activo:

1. Se lee `image/script_text.mrg` **del dump del usuario**.
2. Se sustituyen las líneas del mod en la tabla inglesa (entradas 2 y 3), recalculando los offsets de 32 bits y
   la cabecera `mrgd00` (alineaciones de 0x800).
3. El resultado se escribe en el árbol temporal de enlaces (`out/mod-game-*`, ya existente) como `image/script_text.mrg`
   y el resto de `image/` se enlaza como hoy. `allscr.mrg` no se toca.
4. El log dice `Mods: <n> text lines replaced` y avisa de cuántas líneas del mod no se usaron.

Para eso `scripts/mods.py` aprende la estructura real del juego (`image/`, `data/`). El resto del soporte del juego anterior
(`dvdroot_ps4/`) se borra, como manda la regla de no dejar código del juego anterior (decisión que esta spec
propone; hay que aprobarla). El resto de lo suelto de `image/` (vídeos, fuentes) queda cubierto, pero no es objetivo de la spec.

### 4.4. Fuente

El inglés usa los glifos que ya hay en `ft_en.mrg`. Si la traducción necesita otros (por ejemplo `á í ó ú ñ ¿ ¡`), la
tarea 3 mira si la fuente los tiene. Si no los tiene, ampliarla es una decisión aparte (§8, riesgo 1): o se estudia el
formato de `ft_en.mrg` en una spec nueva, o el mod se limita al juego de glifos existente.

## 5. Plan de tareas

1. **Experimento de validación (sin código nuevo en el árbol).** Un script en `out/` reconstruye `script_text.mrg`
   con una línea cambiada. Se comprueba en el juego (partida nueva, `out/savedata-sin-progreso/`):
   (a) la línea cambiada se ve; (b) una línea mucho más larga se ajusta o desborda; (c) una con `á`/`ñ` tiene glifo;
   (d) el juego no verifica tamaños ni offsets del archivo. Resultado en esta spec, §10, y cada punto confirma o
   corrige una hipótesis [?].
2. **Módulo de formatos** con tests: leer y escribir `mrgd00`; descomprimir MZX; ida y vuelta de `script_text.mrg` sin
   cambios debe dar el archivo original byte a byte (test con el dump, saltado si no está).
3. **Herramienta de extracción** (§4.1): texto, guiones, índice. Averiguar de dónde sale el nombre de cada ruta y capítulo.
4. **Mod de texto** (§4.2 y §4.3), con tests C/Python que no necesiten el dump (archivos sintéticos) y uno con el dump.
5. **Launcher.** Los mods dejan de estar desactivados por defecto solo si esta spec lo aprueba; hasta entonces siguen igual.
6. **Documentación.** `docs/MODS.md` se reescribe para este juego; se añade `docs/tsuki-texto.md` con los formatos; los
   README no anuncian los mods hasta que el usuario lo decida.
7. **Regresión.** El recorrido grabado (`tools/route.sh replay`) con un mod de una sola línea cambiada sale igual que
   `out/baseline/6a/` salvo en esa línea; sin mod, igual que siempre.

Cada tarea se documenta en esta spec al cerrarla, y la spec se commitea.

## 6. Archivos afectados

`scripts/mods.py` (estructura `image/`, construcción del `.mrg`), un módulo nuevo para `mrgd00` y MZX, `tools/` (la
herramienta de extracción), `tests/`, `docs/MODS.md`, `launcher/` (solo si se activa la opción de mods), `.gitignore`,
`docs/tsuki/ROADMAP.md`.

## 7. Criterios de aceptación

- [ ] La ida y vuelta de `script_text.mrg` sin cambios da el archivo original, y los 550 guiones de `allscr.mrg` descomprimen.
- [ ] La herramienta extrae texto, guiones e índice de un dump completo, dos veces con el mismo resultado.
- [ ] Un mod de una línea cambia esa línea en el juego, y solo esa. Verificado con captura y con el recorrido grabado.
- [ ] Un mod con una línea larga y otra con un carácter ausente de la fuente tiene el resultado documentado (se ve, se
      ajusta o desborda, se cae), sin cuelgues.
- [ ] Entradas inválidas (número fuera de rango, repetido, línea mal formada) se rechazan con un mensaje claro, sin arrancar el juego a medias.
- [ ] El mod no contiene ni reproduce el texto original; `git ls-files` no contiene salidas de la herramienta.
- [ ] `bash build.sh --test` y `python3 -m unittest discover -s tests` pasan.

## 8. Riesgos

| # | Riesgo | Impacto | Mitigación |
|---|---|---|---|
| 1 | La fuente `ft_en.mrg` no tiene los glifos que necesita el idioma de destino | Alto para una traducción con acentos | La tarea 1(c) lo comprueba antes de invertir; si falta, spec aparte para ampliar la fuente |
| 2 | El nombre de cada ruta o capítulo no está en `allscr.mrg` y el índice sale sin etiquetas | Medio | Se puede reconstruir desde `_PGST`, las etiquetas `LBL_` y `_TOMP`; si no, el índice usa solo el número de guion |
| 3 | El juego verifica tamaño, offsets o contenido de `script_text.mrg` | Alto | La tarea 1(d) lo prueba; si lo verifica, el reemplazo por lectura (en `runtime_file.c`) sustituye al reemplazo del archivo |
| 4 | Una línea más larga desborda la caja de texto, o el motor no ajusta | Medio | La tarea 1(b) lo mide. La herramienta de extracción puede avisar de las líneas que superan un largo configurable |
| 5 | Texto del juego fuera de `script_text.mrg` (menús, rótulos, títulos) | Medio | Fuera de alcance (§9). La tarea 1 anota lo que se vea sin traducir |
| 6 | Derechos de autor: distribuir texto del juego | Alto | El mod lleva solo líneas propias; la herramienta escribe solo bajo `out/` y nada de eso se versiona |
| 7 | El número de líneas distinto en otras versiones del juego | Bajo | La herramienta y el mod comprueban el recuento (43 971) y se niegan en otro caso |

## 9. Fuera de alcance

- Traducir el juego, o decidir qué idioma se traduce primero.
- Imágenes con texto (`allpac*`, `if_en.png`), menús del `eboot` y subtítulos de los vídeos.
- Ampliar la fuente (se decide tras la tarea 1).
- Reemplazar entradas de otros `.mrg` (fondos, sprites, voces, música).
- Recomprimir `allscr.mrg`: no hace falta si el mod no toca los guiones. Si hiciera falta, es una spec nueva.
- Parches de código (XML).

## 10. Registro de resultados

Vacío: la spec aún no está aprobada.
