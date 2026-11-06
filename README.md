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

## Metodo 18: Estatico + parches (como el "GIF to GD")
1. **Fondo**: cada pixel toma su color *mediano* en el tiempo (solo donde es opaco en todos los frames). Se traza una vez, con `objetos por frame x 6` (ajustable), sin grupo ni triggers: siempre visible.
2. **Parche por frame**: solo los pixeles que se alejan del fondo mas que la *tolerancia* (28 por defecto) o que no son opacos siempre. Va en el grupo `G_i` y se anima con los mismos Alpha + Spawn.
3. Se traza con el metodo de imagen existente que elijas en Ajustes (BSP por defecto, Quadtree, Regiones, Cuadricula). La Piramide no se ofrece: se cuelga con parches.

Medido en `puchiko.gif` (34 frames, 100 obj/frame): +1,7 dB PSNR frente a trazar cada frame entero, con ~18 % mas objetos, y sin parpadeo en el fondo. Con la cuadricula usa 1215 objetos en vez de 3366 y sale mejor.

## Ventana
Metodo (flechas) · **Objetos por frame** (10–5000) · Lado largo · **Frames maximos** (2–300) · Escala de grises · GIF / Colocar / Ajustes.

## Compilar
Sube la carpeta a GitHub (el workflow `build.yml` compila Windows/macOS/iOS/Android y deja el `.geode` en "Build Output"), o local con `geode build`.

## Probar la logica sin Geode
```
g++ -O2 -std=c++20 -pthread -Iinclude -Isrc/engine tests/gif_test.cpp -o gif_test && ./gif_test tests/test.gif
```
`tests/patch_test.cpp` compara trazar frames enteros vs fondo + parches (PSNR): `./patch_test mi.gif <trazador 0-4> <tolerancia> <mult. fondo>`.
Decodifica el GIF, convierte frames con varios metodos y **simula los triggers** comprobando que cada frame se muestra en su momento y que el bucle se repite.

## NO verificado
* `src/ui` y `src/main.cpp` **no se han compilado ni probado en el juego** (se escribieron contra el patron del Image Exporter). Puede pedir algun ajuste menor.
* Las claves de triggers (Alpha 1007: 51/35/10; Spawn 1268: 51/63/62/87; grupos 57) siguen el formato estandar de 2.2 pero no estan probadas en GD.
* Cada frame se convierte por separado: puede haber parpadeo entre frames. Sube los objetos por frame para reducirlo.
* Retardos < 0.03 s se suben a 0.03 s (ajustable).
