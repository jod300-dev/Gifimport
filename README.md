# GIF Importer (mod de Geode) — BETA

Convierte un GIF animado en objetos de GD + triggers que lo reproducen en bucle. Metodo unico: **estatico + parches**.

## Metodo de parches
1. **Fondo**: cada pixel toma su color *mediano* en el tiempo (solo donde es opaco en todos los frames). Se traza una vez con `objetos por frame x multiplicador` y queda siempre visible (sin grupo ni triggers).
2. **Parche por frame**: solo los pixeles que se alejan del fondo mas que la *tolerancia*. Va en el grupo `G_i`.
3. Trazador: BSP adaptativo. Los parches pequenos y planos se recortan a su caja y, si hace falta, se les anade un anillo de 2 px para que el arbol vea el borde.

## Triggers (N frames)
| Grupo | Contenido |
|---|---|
| `G_i` | parche del frame *i* |
| `F_i` | Alpha: mostrar `G_i` (1) y ocultar el anterior (0). `F_0` oculta ademas todos los demas |
| `L`   | Spawn(`F_i`, retardo `t_i`) para cada frame + Spawn(`L`, retardo = duracion total) → **bucle infinito** |

Mas **1 Spawn de posicion** (x = *Trigger de arranque*, 0 por defecto) que lanza `L`. Total: **4N triggers** y **2N + 1 grupos**, asignados por encima del grupo mas alto que use el nivel.

## Si la animacion no arranca
- Juega con **Playtest desde el inicio** (sin Start Pos): el trigger de arranque esta en x=0 y solo se activa si el jugador pasa por ahi.
- Prueba en Ajustes otro valor de *Trigger de arranque: x* (15, -30) o desactiva *Arrancar en la x de abajo* (arranca junto a la imagen).
- La notificacion al colocar dice los grupos usados y la x del arranque.

## Actualizar el metodo
Todo el algoritmo esta en **`src/engine/gifpatch.hpp`**. La ventana solo usa `convertPatches`, `buildGifPatchString`, `combineForPreview`, `patchBaseBudget`, `patchPerFrameBudget` y las estructuras `PatchParams` / `PatchSet`: mientras esa API se mantenga, basta reemplazar ese archivo.

## Estructura
```
src/main.cpp            boton del editor
src/ui/GifPopup.*       ventana
src/engine/gifpatch.hpp metodo de parches   <- el que se actualiza
src/engine/gifanim.hpp  decodificar GIF, plan de frames, triggers
src/engine/engine.hpp   trazador BSP
src/engine/partition.hpp, refit.hpp, core.hpp, gdexport.hpp
```

## Compilar
Sube la carpeta a GitHub (el workflow `build.yml` compila Windows/macOS/iOS/Android y deja el `.geode` en "Build Output"), o local con `geode build`.

## Probar la logica sin Geode
```
g++ -O2 -std=c++20 -pthread -Iinclude -Isrc/engine tests/gif_test.cpp -o gif_test && ./gif_test mi.gif
```
Decodifica el GIF, calcula fondo + parches, genera la cadena y **simula los triggers** (3 vueltas) comprobando que cada frame se muestra en su momento. Tambien mide el PSNR.

## NO verificado
* `src/ui` y `src/main.cpp` no se han probado en el juego.
* Las claves de triggers (Alpha 1007: 51/35/10; Spawn 1268: 51/63/62/87; grupos 57) siguen el formato estandar de 2.2 pero no estan probadas en GD.
