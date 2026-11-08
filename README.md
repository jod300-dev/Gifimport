# GIF Importer (mod de Geode) — BETA

Basado en el motor del *Image Exporter*. Convierte un GIF animado en objetos de GD + triggers que lo reproducen en bucle.

## Como funciona (N frames)
| Grupo | Contenido |
|---|---|
| `G_i` | objetos del frame *i* |
| `F_i` | 2 Alpha: mostrar `G_i` (1) y ocultar `G_{i-1}` (0) |
| `L`   | Spawn(`F_i`, retardo `t_i`) para cada frame + Spawn(`L`, retardo = duracion total) → **bucle infinito** |
| `I`   | Alpha(`G_i`, 0) para i ≥ 1 (ocultar al empezar) |

Mas 2 Spawn de posicion en x = -30 (lanzan `L` e `I`). Total: **4N + 3 triggers** y **2N + 2 grupos**. Los grupos se asignan por encima del mas alto que use el nivel.

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
* **Colores en canales (sin HSV)** = check **"Canales (sin HSV)"** de la ventana (junto a *Escala de grises*), **apagado por defecto: el color va por HSV en los 19 metodos**. Encendido, vale para **todos los metodos (1-19)**: los colores del GIF se cuantizan a una paleta comun (corte mediano ponderado por area + Lloyd; *Colores de la paleta* en ajustes, 128 por defecto) y cada objeto lleva solo `21,<canal>`, sin claves HSV. El color de cada canal lo fija un **Color trigger** (id 899) colocado al inicio del nivel (x <= -30). **El canal 1 nunca se usa** y los canales empiezan **siempre por el 2** (`2 .. 1+colores`). Los estilos de tinta (metodos de tinta/boceto) tambien pasan a canales normales (negro y blanco = 2 canales). Codigo: `src/engine/chanframes.hpp`; prueba: `tests/chan_test.cpp`.
* Ajustes propios (seccion "Capas"): *Umbral de cambio*, *Quitar ruido del GIF*, *Rectangulos rotados*, *Reutilizar parches (%)*, *Colores de la paleta (modo canales)*.
* Se calcula entero en la vista previa y **Colocar lo reutiliza** (instantaneo). El estado muestra objetos totales, rests y PSNR medio.
* Limitacion: un parche no puede borrar lo que hay debajo; si un pixel pasa de opaco a transparente en un frame, ese cambio no se refleja.
* Codigo nuevo: `src/engine/layered.hpp` (el resto del motor no cambia). Prueba: `tests/layered_test.cpp`.

## Metodo 19: Arbol de deltas (prediccion)
Generaliza el metodo 18: en vez de K rests fijos, **cada frame se predice del frame mas parecido** (arbol de expansion minima sobre el area que cambia entre pares de frames; la raiz es el fondo).

* Un frame solo pinta una **delta**: lo que difiere del composite *real* (en lazo cerrado, incluido el error de aproximacion) de su padre. Lo que se ve en el frame i es el camino `fondo -> ... -> padre -> i`; las deltas se apilan (z creciente) y solo se ocultan al cambiar de rama (triggers Alpha por la diferencia entre caminos consecutivos).
* Medicion previa: el arbol solo necesita pintar ~15-25 % del area que habria que pintar comparando cada frame con el fondo.
* **Dos pasadas**: una rapida con BSP mide las areas reales de cada delta (un hijo tambien corrige el error de su padre, asi que no basta con diferencias entre frames verdaderos) y luego el trazado final reparte el presupuesto (densidad ~ (frames que ven la capa)^0,3) segun esas areas.
* Reutiliza todo lo del 18: capas = BSP + rectangulos rotados, colores en canales (sin HSV, canal 1 libre), z-order compacto y reutilizacion de frames casi identicos (delta vacia = cero objetos).
* Con transparencia una delta no puede borrar lo del padre: en ese caso cada frame va directamente sobre el fondo.
* Resultado en el GIF de prueba (34 frames, ~10k objetos): metodo actual 20.4 dB, metodo 18 22.9 dB, **metodo 19 23.2 dB** (peor frame 22.2 vs 21.7). Codigo: `src/engine/deltatree.hpp`.

## Ventana
Metodo (flechas, 17 de imagen + el 18 de capas + el 19 de arbol) · **Objetos por frame** (10–5000) · Lado largo · **Frames maximos** (2–300) · Escala de grises · GIF / Colocar / Ajustes.

## Compilar
Sube la carpeta a GitHub (el workflow `build.yml` compila Windows/macOS/iOS/Android y deja el `.geode` en "Build Output"), o local con `geode build`.

## Probar la logica sin Geode
```
g++ -O2 -std=c++20 -pthread -Iinclude -Isrc/engine tests/gif_test.cpp -o gif_test && ./gif_test tests/test.gif
```
Decodifica el GIF, convierte frames con varios metodos y **simula los triggers** comprobando que cada frame se muestra en su momento y que el bucle se repite.

## Probar el metodo 18 sin Geode
```
g++ -O2 -std=c++20 -pthread -Iinclude -Isrc/engine tests/layered_test.cpp -o layered_test && ./layered_test tests/test.gif [maxFrames] [objetosPorFrame] ... [arbol 0|1] (ver cabecera del test)
```
Calcula las capas, genera la cadena de GD, **simula los triggers**, rasteriza los objetos visibles de cada frame (2 vueltas del bucle), mide el PSNR contra el GIF y lo compara con el metodo normal (BSP) con el mismo numero de objetos.

## Errores conocidos y como se evitan
Analisis de lo visto en el juego (preview limpio, pero astillas / frames "amalgamados" al colocar). Cada punto tiene prueba automatica en `tests/sanity_test.cpp` (comprobada contra el codigo anterior: detecta el fallo).

| Error | Causa | Arreglo |
|---|---|---|
| Astillas / halo alrededor de la imagen (metodos 7, 8, 9, 10, 12, 15, 16, 17, 18, 19) | Rectangulos girados que se salian del lienzo (hasta ~68 px). El preview los recortaba; GD **no**. | `Geo` ya no genera formas que sobresalgan (las alineadas a ejes se recortan) y `sanitizeResult` (`src/engine/sanitize.hpp`) confina **todo** resultado en `convert()`: unico punto de entrada de todos los metodos. |
| z-order con ~1000 valores distintos (metodos 7/8) | Un z por forma | Compactado a la profundidad real de solapamiento (16-20 valores). |
| Frames "amalgamados" en el editor | El editor no ejecuta triggers: todos los frames se ven apilados | El **frame 0 se dibuja encima** (z mayor): el editor muestra el frame 0 limpio. En el juego solo hay un frame visible a la vez, asi que no cambia nada. (En 18/19 el editor muestra las capas apiladas: es lo esperado.) |
| "Pido 3000 y sale como 1150" | El **Tope total de objetos** (40000 por defecto) reserva espacio para formas, triggers y paleta | La ventana muestra el presupuesto aplicado; los modos 18/19 verifican el recuento real y pueden recortar formas pequeñas, recalculando el PSNR. |

Notas: la calidad sube de forma continua con el presupuesto (medido de 500 a 4000 objetos/frame en todos los metodos): no hay valores "buenos" y "malos". Con *Lado largo* pequeno (p. ej. 210 u) los objetos miden <1 unidad: sube el lado largo si se ve borroso.

| Orden arbitrario entre formas con el mismo z | En GD las formas con el mismo z-order NO tienen orden garantizado (depende de como entran a pantalla). La compactacion anterior detectaba solapes por centros de pixel y dejaba 39-90 pares por frame (trazos finos) empatados | `compactZ` usa solape **geometrico exacto (SAT)**: ningun par solapado comparte z (0 empates; la prueba lo exige y falla con el codigo anterior). |
| Frames superpuestos si un trigger falla una vez | Un `hide` perdido deja dos frames visibles hasta el final | En cada vuelta el bucle vuelve a ejecutar el grupo de inicio (oculta todo salvo el frame 0): se autocorrige (+1 trigger). |
| No saber si el fallo es de la conversion o de los triggers | — | Ajuste **Modo al colocar**: *Hoja de frames* (todos los frames lado a lado, objetos estaticos, sin triggers) o *Solo el frame 0*. |

Los triggers de arranque estan por defecto en x = -30: solo se ejecutan si juegas **desde el inicio del nivel**. Si pruebas con *Playtest desde la posicion del editor* o un Start Pos mas a la derecha, desactiva *Triggers de arranque en el inicio* para que queden junto a la imagen.

| Triggers pisados / frames "amalgamados" | Triggers de frames en cuadricula (v0.4.4) | **Una sola fila, uno tras otro hacia la derecha** (paso 30 u, sin huecos); ninguno encima/debajo de otro. GD ejecuta los triggers de un mismo instante de izquierda a derecha, asi el orden de x es el orden de los frames. Solo los Color triggers pueden ir apilados. La prueba lo exige. |

| Metodos 7/8 (y 4/18/19) se ven como el BSP grueso en el juego (bloques planos, sin detalle) aunque el preview es bueno | **z-order 0 = "por defecto" en GD**: se dibuja encima de los z 1..N, asi la base (z=0) tapaba todos los rectangulos girados | Nunca se emite z = 0: todo z se desplaza +1 (`zShift`). La prueba falla si aparece un z <= 0. Confirmable con el patron de calibracion (fila 3, derecha). |

Regla para metodos futuros: pasar siempre por `convert()` (o llamar a `sanitizeResult`), y ejecutar `tests/sanity_test.cpp`.

## Patron de calibracion (diagnostico)
Ajuste **Modo al colocar -> Patron de calibracion**: al pulsar *Colocar* se colocan ~115 objetos de prueba (no usa el GIF) generados por el **mismo exportador** que el resto. Compara lo que ves con `calibracion_esperado.png`:

| Fila | Prueba | Si GD se ve distinto, significa que... |
|---|---|---|
| 1 | Orden por z-order: rojo y azul solapados en 6 casos (z 1/5, creado al reves, 1/30, 1/300, empate creado azul-rojo y rojo-azul) | el orden de dibujo no sigue al z-order (o hay un limite de z) |
| 2 | Franja 40x5 girada 0,30,60,90,120,150 sobre un cuadrado gris | el sentido/eje de giro o la escala no uniforme con rotacion difieren del modelo |
| 3 | Base gris + franja verde girada (creada antes/despues; franjas de 1 u, 0.5 u; 8 apiladas) | las formas de los metodos 7/8/18/19 se tapan entre si |
| 4 | Rayas negras de alto 6, 3, 1.5, 0.75, 0.4, 0.2 u | hay un tamano minimo visible: los trazos finos desaparecen |
| 5 | 60 tiras de 0.5 u: degradado continuo | GD redondea posiciones/escalas sub-unidad |
| 6 | Canales 2,3,4,5 (rojo, verde, azul, amarillo) por Color trigger, y los mismos colores en HSV | los colores de canal no se aplican en el editor |

## Verificacion pendiente en Geode
* `src/ui` y `src/main.cpp` no se pudieron compilar en este entorno porque no esta configurado `GEODE_SDK`; deben compilarse con Geode SDK 5.11.2 y probarse dentro de Geometry Dash. El motor sin dependencias de Geode si fue compilado y probado con los tests incluidos.
* Los triggers se simulan en las pruebas sin Geode (flags Spawn Triggered/Multi-Trigger, orden horizontal, retardo del bucle y dos vueltas completas). Aun falta verificar su ejecucion real y el posicionamiento de inicio dentro de Geometry Dash.
* Cada frame se convierte por separado (metodos 1-17): puede haber parpadeo entre frames. Los metodos 18 y 19 lo reducen mucho (fondo trazado una vez).
* Retardos < 0.03 s se suben a 0.03 s (ajustable).

## Herramienta: ver un .gmd sin abrir GD
`python3 tools/gmd_view.py nivel.gmd` resume el nivel (objetos por id, z-orders, ¿hay z=0?, orden de los triggers) y con `-o vista.png [--group N] [--zero-default 12]` dibuja los objetos 3092 del mod. Exporta el nivel desde GD (.gmd) y subelo para diagnosticar.
