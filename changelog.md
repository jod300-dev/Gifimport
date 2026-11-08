# v0.4.3-beta
- FIX: formas solapadas con el mismo z-order (orden arbitrario en GD): el z-order ahora se compacta con solape geometrico exacto (SAT); 0 empates.
- Bucle de triggers autocorregido: cada vuelta oculta de nuevo todo salvo el frame 0.
- Nuevo ajuste "Modo al colocar": Animacion / Hoja de frames (sin triggers) / Solo el frame 0, para diagnosticar problemas en el juego.
- sanity_test: comprueba empates de z y la hoja de frames; verificado que falla con el codigo anterior.

# v0.4.2-beta
- FIX: formas fuera del lienzo (astillas/halo alrededor de la imagen) en los metodos 7, 8, 9, 10, 12, 15, 16, 17, 18 y 19. `Geo` ya no las genera y `convert()` confina todo resultado (`sanitize.hpp`).
- FIX: z-order compactado en todos los metodos (de ~1000 valores a decenas).
- Editor limpio: el frame 0 queda encima del resto (antes se veian todos mezclados). En el juego no cambia nada.
- La ventana muestra el presupuesto aplicado cuando el "Tope total de objetos" lo recorta, p. ej. `3000 (tope: 1176)`.
- Nueva `tests/sanity_test.cpp`: comprueba formas dentro del lienzo, z-order, presupuesto, objetos exportados dentro de la imagen y frame 0 encima, en los 19 metodos.

# v0.4.1-beta
- Nuevo check "Canales (sin HSV)" junto a "Escala de grises": colores solo en canales en los 19 metodos (paleta comun + Color triggers; el canal 1 nunca se usa, siempre desde el 2). Apagado por defecto: HSV en todos los modos.
- Ajustes eliminados (sustituidos por el check): "Colores en canales" y "Primer canal de color".

# v0.4.0-beta
- Metodo 19 (arbol de deltas): cada frame se predice del frame mas parecido y solo pinta lo que cambia (arbol de expansion minima, lazo cerrado, dos pasadas). Mas calidad por objeto que el 18.
- core.hpp / resto del motor sin cambios.

# v0.3.0-beta
- Metodo 18: colores en canales (sin HSV) con paleta comun y Color triggers al inicio; el canal 1 nunca se usa. Ajustes: colores en canales, tamano de paleta, primer canal.

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
