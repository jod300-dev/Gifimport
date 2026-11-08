# GIF Importer v0.4.6-beta — informe de QA

## Fixture utilizada

- Archivo: `tests/fixtures/di-gi-charat-puchiko.gif`
- Resolucion: 165 x 192 px
- Frames: 34
- Duracion por frame: 100 ms
- Duracion total / loop: 3.4 s

## Cambios principales

- El z-order se compacta siempre en los resultados a color, no solo cuando el valor maximo supera 64. Las formas que se solapan no quedan empatadas en z.
- El saneado de resultados sigue confinando las formas giradas/alineadas al lienzo antes de exportarlas.
- El presupuesto de metodos 1-17 ahora reserva tambien los Color Triggers de la paleta, ademas de los triggers de animacion.
- Los metodos 18 y 19 reservan los triggers de color; despues de construir el horario real verifican el total de objetos. Si hace falta, eliminan primero detalles pequeños, compactan la paleta y recalculan el PSNR.
- La interfaz ya no anuncia una colocacion exitosa cuando Geode no devuelve objetos; tambien detecta una importacion parcial.
- Corregido el recuento del bucle normal: `4N + 3` triggers (no `4N + 2`).

## Pruebas ejecutadas

| Prueba | Resultado |
|---|---|
| `gif_test` — decodificacion, exportacion y simulacion de dos vueltas | OK; 12 frames de prueba, duracion conservada en 3.4 s |
| Esquema Alpha/Spawn | OK; conteo exacto, flags Spawn Triggered/Multi-Trigger, self-spawn con retardo positivo y orden horizontal |
| `chan_test` — metodos 1-17 | 17/17 OK; cero objetos HSV, cero usos del canal 1, canales y Color Triggers coherentes |
| `sanity_test` — limites, z-order, exportacion y geometria | 364 comprobaciones OK |
| Metodo 18, HSV, tope 2.000 | 1.843 formas + 137 triggers = 1.980/2.000; PSNR medio 20.32 dB; cero desajustes en simulacion |
| Metodo 19, HSV, tope 2.000 | 1.770 formas + 216 triggers = 1.986/2.000; PSNR medio 19.16 dB; cero desajustes en simulacion |
| Metodo 18, canales, paleta 900, tope 2.000 | 942 formas + 137 triggers base + 900 Color Triggers = 1.979/2.000; cero desajustes |
| Metodo 19, canales, paleta 900, tope 2.000 | 905 formas + 216 triggers base + 868 Color Triggers usados = 1.989/2.000; cero desajustes |

Los simuladores comparan el frame reconstruido con el raster calculado y ejecutan dos ciclos del loop. Los recuentos de Color Triggers se incluyen en `totalGD`.

## Limitacion que queda abierta

No se pudo compilar `src/ui/GifPopup.cpp` ni `src/main.cpp` porque esta sesion no tiene `GEODE_SDK` configurado. Tampoco se puede lanzar Geometry Dash aquí. Por tanto, los resultados demuestran que el motor y las cadenas de objetos pasan las pruebas locales, pero no certifican el comportamiento final de la UI ni la activacion real dentro del juego. La prueba final pendiente es compilar con Geode SDK 5.11.2 e importar el GIF en un nivel de prueba, comenzando desde el inicio del nivel para validar los triggers de arranque.
