# GIF Importer (mod de Geode) — BETA

Basado en el motor del *Image Exporter*. Convierte un GIF animado en objetos de GD + triggers que lo reproducen en bucle.

## Como funciona (N frames)
| Grupo | Contenido |
|---|---|
| `G_i` | objetos del frame *i* |
| `F_i` | 2 Alpha: mostrar `G_i` (1) y ocultar `G_{i-1}` (0) |
| `L`   | Spawn(`F_i`, retardo `t_i`) para cada frame + Spawn(`L`, retardo = duracion total) → **bucle infinito** |
| `I`   | Alpha(`G_i`, 0) para i ≥ 1 (ocultar al empezar) |

Mas 2 Spawn de posicion en x = -30 (lanzan `L` e `I`). Total: **4N + 2 triggers** y **2N + 2 grupos**. Los grupos se asignan por encima del mas alto que use el nivel.

## Metodo 18: Capas (fondo + rests + parches)
Reproduce el mecanismo del mod *GIF to GD*: en vez de dibujar cada frame entero, la animacion se descompone en capas que casi no cambian.

| Capa | Contenido |
|---|---|
| `still` | partes quietas del GIF (mediana temporal), trazadas **una sola vez**, siempre visibles (sin grupo ni trigger) |
| `rest_k` | poses de reposo (medoides de K grupos de frames): solo lo que difiere del still. K se elige solo (y un rest solo se crea si compensa) |
| `patch_i` | por frame: solo lo que difiere de `still + rest` ya renderizados (**lazo cerrado**: el error de una capa lo corrige la siguiente) |

* Los triggers son los mismos (Alpha + Spawn en bucle): `F_i` muestra `patch_i` y oculta el anterior; el rest solo cambia cuando hace falta; `I` oculta lo que no se ve en el frame 0.
* **Objetos por frame x frames = total** que se reparte entre las capas (igual que los otros metodos, asi se comparan a igualdad de objetos). El reparto sigue densidad ~ (frames en que se ve la capa)^0,3: el fondo recibe mucho mas que un parche.
* **Cada capa = base BSP + rectangulos rotados** colocados por busqueda guiada por el error *directamente sobre lo que ya se ve* (clase `Geo` del metodo 7/8, sin modificarla); luego se recolorea cada forma con lo visible, se quitan las tapadas y ese presupuesto se reinvierte. Contornos y diagonales cuestan 1 objeto por trazo en vez de decenas de bloques. Solo con GIF sin transparencia (con transparencia se usa solo BSP).
* **Reutilizacion de parches**: un frame casi igual a otro (mismo rest) muestra el mismo parche (0 objetos extra; pausas, duplicados, idas y vueltas). Ese presupuesto se reparte entre el resto.
* **z-order compacto**: el z de cada capa se reduce a la profundidad real de solapamiento (decenas de valores, no miles); mismo resultado visual.
* Ajustes propios (seccion "Capas"): *Umbral de cambio*, *Quitar ruido del GIF*, *Rectangulos rotados*, *Reutilizar parches (%)*.
* Se calcula entero en la vista previa y **Colocar lo reutiliza** (instantaneo). El estado muestra objetos totales, rests y PSNR medio.
* Limitacion: un parche no puede borrar lo que hay debajo; si un pixel pasa de opaco a transparente en un frame, ese cambio no se refleja.
* Codigo nuevo: `src/engine/layered.hpp` (el resto del motor no cambia). Prueba: `tests/layered_test.cpp`.

## Ventana
Metodo (flechas, 17 de imagen + el 18 de capas) · **Objetos por frame** (10–5000) · Lado largo · **Frames maximos** (2–300) · Escala de grises · GIF / Colocar / Ajustes.

## Compilar
Sube la carpeta a GitHub (el workflow `build.yml` compila Windows/macOS/iOS/Android y deja el `.geode` en "Build Output"), o local con `geode build`.

## Probar la logica sin Geode
```
g++ -O2 -std=c++20 -pthread -Iinclude -Isrc/engine tests/gif_test.cpp -o gif_test && ./gif_test tests/test.gif
```
Decodifica el GIF, convierte frames con varios metodos y **simula los triggers** comprobando que cada frame se muestra en su momento y que el bucle se repite.

## Probar el metodo 18 sin Geode
```
g++ -O2 -std=c++20 -pthread -Iinclude -Isrc/engine tests/layered_test.cpp -o layered_test && ./layered_test tests/test.gif [maxFrames] [objetosPorFrame]
```
Calcula las capas, genera la cadena de GD, **simula los triggers**, rasteriza los objetos visibles de cada frame (2 vueltas del bucle), mide el PSNR contra el GIF y lo compara con el metodo normal (BSP) con el mismo numero de objetos.

## NO verificado
* `src/ui` y `src/main.cpp` **no se han compilado ni probado en el juego** (incluidas las ramas nuevas del metodo 18; el motor `layered.hpp` si esta probado) (se escribieron contra el patron del Image Exporter). Puede pedir algun ajuste menor.
* Las claves de triggers (Alpha 1007: 51/35/10; Spawn 1268: 51/63/62/87; grupos 57) siguen el formato estandar de 2.2 pero no estan probadas en GD.
* Cada frame se convierte por separado: puede haber parpadeo entre frames. Sube los objetos por frame para reducirlo.
* Retardos < 0.03 s se suben a 0.03 s (ajustable).
