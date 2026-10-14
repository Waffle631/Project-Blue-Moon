# Mods de archivos

El launcher tiene la página **Mods & patches**: elección de la carpeta, interruptor general, activación de cada mod
y flechas de orden de carga. Los cambios valen desde el próximo inicio. El mod más abajo en la lista tiene
prioridad; cuando dos mods reemplazan el mismo archivo, el log muestra cuál ganó.

La carpeta predeterminada es `mods/` en el directorio de datos. Cada mod va en su propia carpeta:

```text
mods/
  Nombre del mod/
    dvdroot_ps4/...
```

También se aceptan:

- `Nombre del mod/app0/dvdroot_ps4/...`;
- una carpeta adicional por fuera, como la de un archivo extraído en una carpeta con su nombre:
  `Nombre del mod/Nombre del mod v1.2/dvdroot_ps4/...` (el readme y las imágenes al lado se ignoran).

Las mayúsculas y minúsculas en los nombres del mod no importan: un archivo con otra capitalización reemplaza el
archivo del juego del mismo nombre. El log muestra cuántos archivos reemplazó el mod y cuántos añadió:
`Mods: 12 game files replaced, 0 added`. Si el mod debería reemplazar archivos y aparece
`0 game files replaced`, revisa las rutas dentro de él. Los archivos ZIP/7z deben extraerse
antes. Dos mods que reemplazan el mismo archivo no se mezclan: el último gana por completo.

Una carpeta `<carpeta del juego>-mods/dvdroot_ps4/...` junto a la carpeta del juego (convención de shadPS4)
entra antes que los mods de la lista. El interruptor general también la desactiva.

Los archivos del juego nunca se modifican. En cada inicio se monta un árbol de enlaces en
`out/mod-game-*` dentro del directorio de datos, que se borra cuando el juego termina. `/app0` queda solo
de lectura; los saves siguen en la carpeta de siempre.

No se admiten mods que reemplacen `eboot.bin`, `sce_module` o `sce_sys`, inyectores de DLL ni plugins de script.
Los parches de código van en `patches/` / `GOW3_PATCHES`. Los enlaces simbólicos dentro
del mod se rechazan.

Desde la terminal:

```sh
GOW3_GAME_DIR=/ruta/CUSA01623 GOW3_MODS_DIR=/ruta/mods python run.py
GOW3_GAME_DIR=/ruta/CUSA01623 GOW3_MODS_ENABLED=0 python run.py
```

El orden y las desactivaciones se guardan en `mods.json` en el directorio de datos:

```json
{"order": ["Primero", "Segundo"], "disabled": ["Primero"]}
```

Las carpetas nuevas entran activadas, al final de la lista, en orden alfabético. `GOW3_MODS_CONFIG` cambia la
ruta de ese archivo.

# Parches de terceros (XML de shadPS4)

Carpeta `patches/` en el directorio de datos (en el launcher, grupo **Third-party patches**, donde también
se puede elegir otra carpeta). Se sirven archivos XML en el formato shadPS4/GoldHEN: entran los
`Metadata` de la versión del juego (01.02) para `eboot.bin`, y los archivos con `TitleID` de otros juegos
se ignoran. La activación predeterminada viene del `isEnabled` del archivo; la elección del launcher se guarda en
`patches.json`:

```json
{"enabled": ["archivo.xml/Nombre del parche"], "disabled": ["archivo.xml/Otro parche"]}
```

Tipos de línea aceptados: `bytes`, `bytes16/32/64`, `float32/64`, `utf8`, `utf16`. Un parche con
líneas `mask` (búsqueda por patrón) o con una dirección fuera del eboot se ignora por completo, con aviso en el
log. Los parches de terceros se aplican después de los integrados
(`patches/God_of_War_III_Remastered.xml`): donde ambos escriben, gana el de terceros. Desde la
terminal: `GOW3_PATCHES_DIR=/ruta GOW3_PATCHES_CONFIG=/ruta/patches.json python run.py`.
