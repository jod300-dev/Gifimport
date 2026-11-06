# v0.2.2-beta
- Fix de compilacion en Windows: `far` y `small` son macros de windows.h; renombrados en layered.hpp.

# v0.2.1-beta
- Metodo 18: cada capa lleva ahora rectangulos rotados guiados por el error sobre el composite real (mucho mas detalle en contornos y diagonales con los mismos objetos).
- Metodo 18: los frames casi identicos reutilizan parche; z-order compactado; nuevos ajustes "Rectangulos rotados" y "Reutilizar parches".

# v0.2.0-beta
- Metodo 18 (capas): fondo trazado una vez + rests (poses de reposo) + parches por frame, en lazo cerrado. Mas nitidez a igualdad de objetos y sin parpadeo del fondo.
- Nuevos ajustes: umbral de cambio y quitar ruido del GIF.
- Los metodos 1-17 no cambian.

# v0.1.0-beta
- Primera version: importa GIFs animados con Alpha triggers + Spawn triggers en bucle infinito.
- 17 metodos de imagen (los del Image Exporter, sin ASCII), objetos por frame, frames maximos, escala de grises.
- Retardos reales del GIF, velocidad ajustable y tope total de objetos.
