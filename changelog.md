# v0.4.5-beta
- FIX (metodos 7 y 8, tambien 4/18/19): GD trata el z-order **0** como "z por defecto del objeto" y lo dibuja ENCIMA de los z 1..N. La base BSP
  (z = 0) tapaba todos los rectangulos girados y en el juego solo se veia el BSP grueso (con 500 objetos: bloques planos, sin ojos ni detalle).
  Ahora **nunca se emite z = 0** (todo z se desplaza +1; `GdExportOptions::zShift`). Reproducido con tus capturas M7/M8: el resultado simulado de la
  cadena vieja es identico a lo que se veia en GD y el de la nueva cadena es igual a la vista previa.
- REVERTIDO el reparto de triggers de la v0.4.4: ahora van en **una sola fila, uno tras otro hacia la derecha** (paso 30 u, sin huecos), ninguno encima
  ni debajo de otro. Los de arranque (I a la izquierda de L) tambien en la misma fila, fuera del rango x de los demas. Solo los Color triggers pueden
  ir apilados.
- Patron de calibracion: nueva prueba z=0 frente a z=1 (fila 3, derecha; **z crudo**, sin el desplazamiento). Esperado: el azul (z=1) encima del rojo.
- sanity_test: falla si se emite z <= 0 (comprobado contra el codigo anterior) y si los triggers no van estrictamente a la derecha en una fila.
- Nueva herramienta `tools/gmd_view.py`: lee .gmd, resume el nivel y dibuja los objetos 3092 (escala/giro/color/z) para diagnosticar sin abrir GD.

# v0.4.4-beta
- Nuevo modo al colocar "Patron de calibracion" (+ `calibracion_esperado.png`): prueba por separado z-order, rotacion, tamanos minimos, precision sub-unidad y canales de color en GD.
- Triggers ordenados: 48 x 52 u, 20 columnas, un bloque por funcion; 0 solapes (antes 446-781 pares). Color triggers tambien separados.
- Fix: en el patron de calibracion el ultimo objeto quedaba pegado al primer Color trigger (faltaba ';').
- sanity_test: comprueba que ningun trigger spawn-triggered se pisa con otro y valida el patron de calibracion.

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


## v0.4.6-beta - seguridad de presupuesto y colocacion

- El limite total reserva tambien los Color Triggers (metodos 1-17 y modos 18/19).
- Los modos por capas aplican un control final con el numero real de triggers y, si hace falta, recortan formas pequeñas antes de exportar; el PSNR se recalcula despues del recorte.
- La interfaz ya no comunica exito cuando Geode no crea objetos; detecta importaciones parciales.
- Documentacion corregida: el bucle normal usa `4N + 3` triggers, no `4N + 2`.
