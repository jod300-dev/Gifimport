# v0.5.4-beta
- **Triggers autocorregibles (todos los metodos 1-19)**. Analisis de tus dos videos (1-17 y 18-19, mismo GIF de 17 frames, bucle de ~1,29 s):
  - 1-17: en CADA vuelta hay exactamente 1 hueco totalmente vacio de la duracion de un frame, siempre en la misma posicion, y el salto de pose a traves del hueco es ~1,5-2 veces
    el normal: el hueco **sustituye a un frame** (no es una pausa extra del bucle). Es decir: en ese paso se oculto el frame anterior pero no se mostro el nuevo.
  - 18-19: faltan las orejas y quedan trozos de capas encimados/estaticos = una capa que no se oculto/mostro a tiempo.
  Mi modelo de GD no reproduce el fallo con triggers perfectos (lo que ya sabiamos), pero con un trigger perdido o fuera de orden el diseno anterior da EXACTAMENTE esos sintomas
  (nuevo `tests/robust_test.cpp`: 3 % de Alpha perdidos -> 2,5 % de muestras vacias en 1-17 y 7-9 % de capas mezcladas en 18/19; lag + 2 % perdidos -> 20 % mezcladas).
  Cambio: cada paso F_i ahora fija el estado COMPLETO (idempotente): muestra su frame/capas y oculta TODO lo demas (hasta 12 frames / 40 capas; con mas frames oculta los 5 anteriores)
  y **se ejecuta dos veces** (en t_i y 20 ms despues, nunca mas tarde que el 40 % del frame). Con el mismo modelo hostil: 0 % de fallos en 1-17 y 0-0,3 % en 18/19.
  Coste: mas triggers (p. ej. 16 frames: de 67 a 147 en 1-17, de ~76 a ~358 en 18). El presupuesto de objetos ya los descuenta.
- **Frame vacio = hueco**: si un frame sale con 0 objetos (excepcion al convertirlo, frame transparente...) ahora se repite el ultimo frame valido en vez de dejar la animacion vacia.
- **Colocacion**: se cuenta cuantos objetos crea GD en cada tanda; si crea menos de los pedidos el aviso final lo dice (antes fallaba en silencio).
- Nuevo modelo de fallos en `tests/gdsim.hpp` (lag con orden inverso, Alpha/Spawn perdidos) y `tests/robust_test.cpp`.
- NO verificado: la causa exacta en GD real (por que se pierde ese trigger). Si sigue pasando, ver README "NO verificado".

# v0.5.3-beta
- **Decodificador de GIF propio** (`src/engine/gifdecode.hpp`). stb_image componia MAL los GIF con "disposal 3" (restaurar al frame anterior) y el error se acumula frame a frame:
  restos de otros frames y "estatica" en los ultimos frames. El nuevo sigue la especificacion (disposal 0-3, transparencia, tablas locales/globales, entrelazado, subrectangulos),
  es **identico pixel a pixel a Pillow** en 22 GIFs de prueba (`tests/check_decoder.py`) y no falla con archivos truncados/corruptos (40 mutaciones bajo ASan/UBSan).
  Sigue decodificando en streaming con tope de memoria (400 MB, reduciendo frames si hace falta). stb queda solo como respaldo.
- **Color triggers**: la cuadricula compacta (30 u x 30 u, 20 columnas, sin huecos) ahora la usan TODOS los modos. Los metodos 18 y 19 tenian su propio codigo con 48 x 52 u (por eso
  seguian saliendo con huecos en tus Unnamed_10/11/12). Una sola funcion (`detail::colorTriggerPos`) y una prueba por modo.
- **Calibracion + prueba de bucle**: "Patron de calibracion" ahora anade debajo 12 frames SINTETICOS conocidos (fondo de color de la rueda cromatica, franja negra que gira, cuadrado blanco que
  avanza) con la animacion real (triggers, grupos, bandas de z, bucle). Al jugarlo deben verse 12 colores en orden, sin mezclas, y el ultimo frame (rosa) pasando limpio al primero (rojo).
  Sirve para separar "fallo de triggers/bucle en GD" de "fallo de los datos del GIF".
- Analisis de tus Unnamed_12 (arbol, 16 frames) y Unnamed_13 (plano, 16 frames, 1150 obj/frame): en mi simulacion de GD la logica de triggers es correcta (un solo frame visible por paso;
  el arbol muestra las cadenas de deltas correctas, con z de hijo > z de padre) y la calidad por frame NO baja al final con el motor (PSNR estable con 1 y 4 hilos). Lo que si se ve en los
  DATOS de los ultimos frames es "estatica" en el vestido: apunta a la entrada (decodificacion) y no a los triggers.
- Arreglo de un `vexing parse` en calib.hpp (aviso del compilador).

# v0.5.2-beta
- Color triggers: **cuadricula compacta** (paso 30 u en x e y, 20 columnas, sin huecos). Antes 48 u x 52 u, con espacios entre ellos. La prueba lo comprueba.
- Z-order: **cada frame tiene su PROPIA banda de z** (disjunta; frame 0 la mas alta, el ultimo la mas baja). Antes todos los frames menos el 0 compartian el rango
  1..N y sus formas se intercalaban cuando varios frames estaban apilados/visibles a la vez. Con bandas disjuntas el frame de la banda alta tapa por completo al de abajo
  aunque GD resuelva distinto los empates de z. Lo mismo para los rests y parches del metodo 18 (fondo < rests < parches, cada uno en su banda). Si no caben (> 30000 niveles) se usa el esquema antiguo.
- Analisis de tus dos .gmd (Unnamed_10 = metodo 18/19 con 37 frames; Unnamed_11 = metodo plano de 37 frames, 500 objetos/frame): en mi modelo de GD los dos se ven limpios
  (el apilado del editor muestra el frame 0 completo). La captura "amalgamada" del plano coincide visualmente con "solo se ven las primeras ~35 formas grandes de cada frame":
  la causa EXACTA en GD no esta confirmada (ver README, "NO verificado").

# v0.5.1-beta
- FIX COMPILACION (Windows, Geode 5.11.2): `Notification::waitAndHide` no existe. El progreso de la colocacion por tandas ahora es una etiqueta propia (`CCLabelBMFont`)
  y el aviso final es una `Notification::create(..., 3.5f)->show()` normal (como en versiones anteriores). Era el unico error del log.
- COMPATIBILIDAD con cualquier GIF (probado con ~30 GIFs dificiles x 19 metodos x HSV/canales, con ASan/UBSan: `tests/fuzz_gifs.cpp`, `tests/make_fuzz_gifs.py`):
  - stb_image 2.29: error con GIFs de disposal 3 ("restaurar al frame anterior"): `two_back` apuntaba fuera del buffer (cierre del juego / frames basura). Corregido.
  - GIFs con transparencia: BSP, Quadtree y los metodos 7/8 daban 0 objetos cuando el sprite estaba rodeado de transparencia (la penalizacion de alfa daba ganancia 0 al partir
    zonas mixtas): ahora es una impureza de Gini (concava). El Quadtree tambien mira un nivel mas abajo cuando la ganancia es exactamente 0.
  - Cada frame con transparencia se recorta a su zona opaca antes de convertir (todo el presupuesto va al sprite) y se recoloca en el lienzo completo.
  - 7 y 8 con transparencia: los rectangulos girados solo se colocan dentro de la zona opaca; si no caben se completa con BSP y se elige el mejor.
  - Metodo 4 (Piramide): bucle infinito con ciertos frames transparentes; ahora acotado, con BSP de respaldo.
  - Metodos 18/19: el fondo comun solo incluye pixeles opacos en TODOS los frames (un parche pinta pero no borra); el fondo puede estar vacio; el umbral de reutilizacion es
    relativo al contenido, no al lienzo (un sprite pequeno dejaba todos los frames "casi iguales" y 0 objetos).
  - Memoria: decodificacion por streaming con tope de 400 MB; si el GIF lo pasa, los frames se reducen al decodificar (se avisa). Ya no se rechaza "GIF demasiado grande".
  - Con GIFs opacos el resultado es identico al de la 0.5.0 (mismos PSNR).

# v0.5.0-beta
- FIX (metodos 7 y 8 eran practicamente el mismo): el 7 era "BSP del 6 % + rotados" y el 8 "BSP del 15 % + rotados" (y el 8 se quedaba en BSP puro si ya llegaba a 38 dB).
  Ahora el **7 es Geometrize puro** (un fondo plano de 1 objeto + solo rectangulos girados, estilo Primitive: sin cuadricula) y el **8 es el hibrido de verdad**
  (30 % de los objetos en base BSP + girados solo donde queda error; solo se queda en BSP si este ya llega a 45 dB, y entonces lo avisa). Con fotos se ven claramente distintos.
- TRIGGERS: dos filas, cada una **avanzando a la derecha uno tras otro** (paso 30 u, sin huecos): **Alpha arriba, Spawn debajo**. Los **2 Spawn de arranque**
  vuelven a ir **uno encima del otro** (x = -30, como en la v0.4.2). Solo los Color triggers pueden ir apilados.
- FIX (18/19 y cualquier metodo con muchos objetos): el editor de GD 2.2 tiene un **limite de 200000 objetos**; si la cadena lo pasa, GD corta los ULTIMOS objetos y como los
  triggers iban al final se perdian (frames incompletos + animacion sin triggers). Ahora: (1) el tope total nunca pasa de `200000 - 3000 - objetos que ya tenga el nivel`,
  (2) la cadena lleva los **triggers PRIMERO** y los objetos despues, (3) si aun asi no cabe, no se coloca nada y avisa.
- FIX (cuelgues/cierres con decenas de miles de objetos): la colocacion se hace **por tandas** (~1500 objetos, ~25 ms por fotograma, con aviso de progreso) en vez de una sola
  llamada gigante; y **no se seleccionan** los objetos si son mas de 8000 (seleccionar 100k-200k congelaba el editor).
- FIX 18/19: tope DURO (objetos + triggers + Color triggers <= tope total; el reparto era una estimacion y se pasaba), y menos memoria de trabajo (pixeles de trabajo 9M/7M en vez de 16M/10M).
- En modo canales el presupuesto por frame descuenta los Color triggers de la paleta.
- Nuevo `tests/fullsim_test.cpp` + `tests/gdsim.hpp`: simulador de GD de punta a punta (HSV/canales, Color/Alpha/Spawn triggers, z-order con empates al azar y orden aleatorio
  dentro de un mismo instante) que mide el PSNR de lo que vería GD contra el GIF para los 19 metodos. sanity_test actualizado (dos filas, arranque apilado, triggers primero, tope duro, 7 != 8).
- `tools/gmd_view.py`: resumen adaptado al reparto en dos filas y comprueba el orden triggers/objetos.

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
