# v0.5.11-beta
Dos ajustes nuevos: **Toggle en vez de Alpha** (menos lag al jugar) y **volcado de la interfaz** (para arreglar fallos de UI sin capturas).
- **Ajuste "Usar Toggle triggers en vez de Alpha"** (`use-toggles`, desactivado por defecto). Los triggers que muestran/ocultan cada frame pasan de Alpha (1007, clave 35 = opacidad) a **Toggle (1049, clave 56 = activar grupo)**.
  Los dos son absolutos (mostrar = 1, ocultar = 0), asi que toda la logica (pasos idempotentes, autocorreccion, reafirmar los ultimos pasos) sigue igual: mismos grupos, mismas posiciones, mismo orden. Un solo punto de emision (`detail::addAlpha` en `gifanim.hpp`), asi que vale para los 29 metodos.
  Con Toggle los frames ocultos quedan desactivados (GD no los dibuja ni los procesa) en vez de seguir dibujandose con opacidad 0. El "Patron de calibracion" (con su prueba de bucle de 12 frames) tambien usa el ajuste: es la forma rapida de comprobarlo en GD.
  - Nuevo `tests/toggle_test.cpp`: para 14 metodos comprueba que la cadena Toggle es EXACTAMENTE la Alpha cambiando solo el id y la clave, y que el simulador dibuja los mismos pixeles en cada frame (2 vueltas), tambien con lag (saltos de 50 ms al reves) y triggers perdidos.
  - El simulador (`tests/gdsim.hpp`) y las comprobaciones de estructura entienden Toggle. El informe de diagnostico cuenta Alpha/Toggle por separado, rellena la tabla `triggers.tsv` con la clave 56 y avisa si una cadena mezcla los dos.
- **Ajuste "Diagnostico de la interfaz (volcado de UI)"** (`ui-dump`) con dos opciones mas (`ui-dump-auto`, `ui-dump-scene`). Muestra un boton **UI** en la ventana; al pulsarlo guarda `ui-dumps/ui_<fecha>_manual.txt` (y lo copia al portapapeles) con:
  - cada nodo de la ventana: clase, id, tag, z, visibilidad (propia y efectiva), posicion, ancla, tamano, escala, giro, skew, color, opacidad, texto, fuente, estado de botones/checks/sliders, **caja en unidades de la ventana** (X desde la izquierda, Y desde arriba: las mismas que usa `init()`) y escala real hasta la pantalla;
  - entorno (winSize, pixeles, factor de contenido, framebuffer), estado interno de la ventana, todos los ajustes del mod;
  - tablas de textos y de botones y **comprobaciones automaticas**: textos solapados, texto sobre boton, botones solapados, elementos fuera de la ventana, tamano cero, texto minusculo, botones activos pero ocultos, valores NaN;
  - con `ui-dump-scene`, un segundo archivo `_escena.txt` con el arbol de toda la escena (editor incluido); con `ui-dump-auto`, volcado automatico al abrir y al cambiar el estado (se conservan los 12 mas recientes).
  - Motor sin Geode en `src/engine/uidump.hpp` (probado en `tests/uidump_test.cpp`) y recolector en `src/ui/UiCollect.hpp` (`tests/uicollect_test.cpp`, con un mock de cocos2d en `tests/mock/`). Las llamadas a bindings dudosos van con `if constexpr (requires ...)`: si no existen se omite ese dato y el mod compila igual.
- La lista de ajustes del informe de colocacion se comparte con el volcado de UI (`GifPopup::settingsText`) e incluye los ajustes nuevos.
- NO verificado en GD real ni compilado con el SDK de Geode (solo el simulador, g++ con un mock de cocos2d y stubs de Mod/Notification). Si algun binding no coincide, el compilador lo dira en una linea concreta.

# v0.5.10-beta
Fusion de la 0.5.9 (arreglos de los metodos 20/27 y del pintor) con las novedades de la 0.5.10.
- **Procesamiento de los metodos 18-29: Mejorado / Clasico 0.5.5** (ajuste nuevo). Clasico = mediana 3x3, sin peso de bordes, sin tamano minimo ni formas orientadas, sin colores planos y "Calidad de calculo" de siempre:
  medido en tu GIF, el 18 da exactamente lo mismo que la 0.5.5 (3746 formas/frame, 15,2 dB) y el 21 lo calcula en 3,9 s en vez de 13,3 s. Los arreglos de 20/26/27 se mantienen en los dos modos.
- **Colores planos (estilizado cel)**: antes de trazar, los frames se reducen a una paleta corta (k-means++ sobre todos los frames + filtro de moda 3x3). Ajuste "Colores planos (metodos 18-29)": Auto / Pocos (14) / Medios (24) / Muchos (40) / Desactivado.
  Medido en tu GIF a 300 obj/frame con los arreglos de la 0.5.9: PSNR igual (+-0,3 dB), SSIM +0,005 en el 18, igual en el 23 y -0,015 en el 25. Es un ajuste de estilo, no una mejora medible de fidelidad.
- **Aviso al colocar con menos de 900 objetos por frame**, con el cálculo de cuantos frames de 1500 objetos caben en el mismo total.
- Nuevo test `tests/chain_check.cpp` (comprueba que la cadena de GD colocada da el mismo PSNR que calculo el motor) y variables de entorno POSTER/CLASSIC/RASTER/EFFORT en los bancos de prueba.
- Informe de diagnostico: incluye los ajustes `layered-mode` y `layered-flat`.

# v0.5.9-beta (arreglos de 20/27, pintor)
Metodos 18-29: calidad y frames rotos (medido con tu GIF de 32 frames, 300 objetos/frame, esfuerzo Alto; SSIM/PSNR contra el GIF sin dithering)
- Revertido el peso de bordes del pintor (edgeWeight 3 -> 0, como en la v0.5.5): con 3 empeoraba (SSIM -8 %, -1 dB).
- Nuevo: las formas del pintor nacen alargadas siguiendo el contorno del dibujo (trazos de pelo, lineart) en vez de rectangulos al azar (`orient`, 0.7).
- Tamano minimo de forma segun el presupuesto (sizeFloor 0.7): menos confeti.
- Metodos 20 y 27 (Vidas): arreglado el colapso de los ultimos frames (10-13 dB, celdas gigantes). Causa: los rellenos de cobertura gastaban 50-300 objetos por
  frame sin que el reparto los contara, y el presupuesto se agotaba hacia el frame 17. Ahora (a) cuentan en la reserva, (b) el reparto entre frames es parejo y
  (c) los rellenos crecen por debajo de las formas nuevas del mismo frame y solo se colocan los que cubren area de verdad.
- Resultado en tu GIF: todos los frames de 18-29 quedan entre 14 y 18 dB (antes 10-13 en los ultimos frames del 20/27).

# v0.5.8-beta
- **Analisis con el GIF REAL (catgirl pat, 360x334, 32 frames) y 11 informes nuevos.** Resultado: en la 0.5.7 el filtro anti-dithering casi no tocaba el grano de ese GIF (el 54 % de los pixeles vecinos saltan >40 niveles: dithering muy fuerte,
  tipico de los GIF de gif-makers online; mi GIF de prueba tenia un dithering mucho mas suave y el filtro estaba afinado para el). Por eso todos los metodos seguian ~15,7 dB y con "ruido"/cubos: se dibujaba el grano.
  - **Filtro adaptativo** (`lay::chooseDenoise`): mide el ruido del GIF (diferencia media entre vecinos que no son borde: pixel art limpio 0-2, GIF de este tipo 30) y elige: sin filtro (<2,5), suave (<6), medio (<18) o fuerte (>=18: bilateral 7x7, sigma color 50, 2 pasadas).
    Con tu GIF el objetivo queda limpio (contornos y rellenos planos) y las zonas se vuelven faciles de representar. Informe: "ruido/dithering medido".
  - **Peso de bordes en el pintor** (`edgeWeight` = 3 por defecto): los pixeles junto a un contorno marcado pesan hasta 4 veces en la eleccion de formas y en el color medio, asi que los objetos se gastan en contornos y dedos en vez de en manchas. El PSNR baja ~0,4 dB (la metrica no premia contornos) pero los contornos se ven.
    Con pesos 1 el resultado es identico al anterior (hash comprobado). Opciones `sizeFloor` (tamano final del pintor segun presupuesto; probado, sin mejora clara: desactivado).
  - **Duracion de calculo: dos niveles nuevos, Extremo y Ultra** (busqueda de 256x512 / 512x1024 candidatos). Medido en tu GIF a 1000 objetos/frame (PSNR vs el objetivo limpio): Normal 18,7 | Maximo 20,3 | Extremo 20,7 | Ultra 20,9 dB (Ultra ~4x mas lento que Normal). Aumentar la duracion es la forma mas segura de ganar detalle a cualquier presupuesto.
  - **20 y 27 (vidas)**: estimacion mas prudente del area futura para que los ultimos frames no se queden sin formas. Siguen siendo metodos para GIF de cambios lentos; con movimiento casi total como este (cambia el 50-70 % de la imagen en cada frame) 18/19/21/22/24/25/29 rinden mas.
  - **26** sigue siendo para ciclos de color/destellos: con movimiento real deja halos fantasma (las formas son fijas).
- Medido en el GIF real (PSNR contra el objetivo limpio, 300 obj/frame): 18 -> 18,1 dB, 21 -> 17,4, 26 -> 17,8; a 1000 obj/frame: 18 -> 21,3, 21 -> 21,3, 26 -> 19,0.
- NO verificado en GD real ni compilado con Geode (solo mi simulador y compilacion con g++ imitando las macros de Windows).

# v0.5.7-beta
- **Analisis de los 11 informes reales (metodos 18-29 con el GIF de la catgirl 360x334, 300 obj/frame): todos daban ~15 dB y se veian "blandos", sin contornos.** Causas encontradas y arregladas:
  - **El filtro de ruido (mediana 3x3) borraba las lineas finas y convertia el dithering en manchas.** Ahora es un filtro bilateral 5x5 anti-dithering que respeta los bordes (`lay::bilateral5`, ajuste "Quitar ruido del GIF"). Medido contra los frames LIMPIOS en un GIF con dithering Bayer:
    18: 23,4 -> 25,7 dB | 19: 23,3 -> 25,2 | 21/29: 23,0 -> 24,3 | 22: 23,4 -> 25,7 | 24: 22,8 -> 23,5 | 25: 22,5 -> 23,5 | 26: 24,1 -> 27,8 (con menos objetos), con los mismos objetos.
    En GIF sin dither iguala a desactivar el filtro (antes la mediana perdia ~2,5 dB).
  - **Metodos 20 y 27 dejaban huecos** (pixeles sin ninguna forma: se ve el fondo del nivel, las "manchas negras" del frame 16): la limpieza de motas dejaba fuera de la mascara los pixeles sueltos sin cubrir. Ahora lo no cubierto siempre se pinta
    (con margen de 1 px) y una garantia final rellena lo que quede con rectangulos voraces. Huecos medidos: 0,34 % -> 0 %.
  - **20 y 27: los ultimos frames se quedaban sin objetos** (13-15 dB en el ultimo). Reserva minima por frame restante y estimacion del area futura con a priori; peor frame 13,0 -> 17,6/18,3 dB.
  - **26: el plan elegia gastar la mitad del presupuesto** (tolerancia de 1 dB). Con "maxima calidad" ahora tolera 0,5 dB (0,35 en Alto, 0,2 en Maximo).
- El informe de diagnostico incluye ahora `original.gif` (el GIF de entrada, para reproducir todo exactamente) y mide por frame los **huecos sin cubrir** (aviso si > 0,05 %).
- **Nuevo ajuste "Guardar informe de diagnostico al colocar"** (Ajustes > Diagnostico, desactivado por defecto). Cada vez que colocas un GIF guarda en `<carpeta del mod>/diagnostics/<fecha>_m<metodo>/`:
  - `informe.txt`: version, plataforma, ajustes del mod y de la ventana, datos del GIF (tamano, frames, retardos), plan de frames, tiempo de calculo, estadisticas de la cadena (objetos por id, caja, z-order, grupos, canales),
    **comprobaciones automaticas** (grupos objetivo sin objetos, triggers spawn que nadie lanza, canales de color sin Color trigger, canal 1 usado, z=0, escalas diminutas/enormes, NaN, mas de 200000 objetos),
    la estructura del metodo (capas, visibles por frame, grupos, canales, paleta; para 18/19 restOf/patchOf/medoid/parent), los triggers por grupo y el PSNR de cada frame medido contra el GIF.
  - `cadena.txt`: la cadena de GD exacta que se coloco (con ella reproduzco todo en el simulador), `objetos.tsv` (id, x, y, escala, giro, z, capa, canal, grupos, HSV) y `triggers.tsv` (tipo, posicion, objetivo,
    grupos propios, opacidad, fundido, retardo, color, canal).
  - `comparacion.png` (10 frames: original / lo que calculo el mod / diferencia x3) y `detalle.png` (2 frames grandes). Se conservan los 8 informes mas recientes. Nunca rompe la colocacion: si falla algo, solo avisa.
  - Motor: `src/engine/diag.hpp` (sin dependencias de Geode, incluye un codificador PNG propio). Prueba: `tests/report_test.cpp`. El comprobador detecta el fallo de la 0.5.5 (20-29 sin Color triggers de paleta).

# v0.5.6-beta
- **Arreglo de compilacion en Windows**: en `lsa_stt.hpp` una variable se llamaba `far`, que `windows.h` define como macro vacia (error `expected unqualified-id`); renombrada. Quitado tambien el `getenv` de depuracion (aviso de MSVC). Probado ademas compilando con `-Dfar= -Dnear= -std=c++23`.
- **Metodos 20-29 con "Canales (sin HSV)": faltaban los Color triggers.** El exportador de las capas LSA no emitia los Color triggers de la paleta (los objetos usaban canales que nadie coloreaba:
  en el simulador 7,3 dB, es decir, imagen sin color). Ahora se colocan igual que en 18/19 (uno por canal, a la izquierda del inicio, cuadricula compacta) y se cuentan en el tope de objetos.
  Prueba nueva en `tests/lsa_test.cpp` (cada canal usado tiene su Color trigger; el canal 1 nunca se usa): falla con la 0.5.5 y pasa ahora.
- **Metodo 26 rehecho: teselado espacio-temporal.** Antes: k-means por pixel + descomposicion golosa del mapa de etiquetas = miles de rectangulos finos (las "rayas"), colores mezclados y 9000 objetos.
  Ahora (`engine/lsa_stt.hpp`): (1) BSP cuyo error se mide en TODOS los frames a la vez (imagenes integrales por frame): las hojas son rectangulos exactos que tapizan el lienzo sin huecos ni solapes;
  (2) las trayectorias de color de las hojas se agrupan en K canales (k-means ponderado por area; el color de cada canal en cada frame es la media ponderada: optimo en minimos cuadrados);
  (3) hojas hermanas con el mismo canal se fusionan. El numero de rectangulos y de canales lo elige un modelo de coste (objetos + Color triggers) y se queda con el plan MAS BARATO a menos de 1 dB del mejor
  posible (0,7 dB con duracion Alto, 0,4 con Maximo): no gasta objetos que no mejoran la imagen. Medido contra la 0.5.5: ciclos de color igual de exactos (31,5 dB); GIF con movimiento +0,7 a +10 dB (rebote 31,9 -> 41,8 dB).
  Los Color triggers lanzados por spawn ahora van en una cuadricula compacta (40 columnas) en vez de una fila de decenas de miles de unidades.
- **Metodos 20 y 27 (vidas de formas): frames sueltos a 13-17 dB.** Una forma "seguia valiendo" solo si su error frente al frame era pequeno; una forma ya aproximada marcaba casi toda la imagen como mala,
  moria en el frame siguiente aunque no cambiara nada y el frame se quedaba con 4 formas gigantes encima. Ahora vale mientras el CONTENIDO bajo ella no cambie respecto al frame donde nacio, lo que hay que
  repintar es solo lo cambiado + lo no cubierto, y el presupuesto se reparte a densidad uniforme sobre el area que falta. Frame peor: 13,3 -> 21,3 dB (media 20,9 -> 22,4 dB). Ademas, si la estructura gasta
  mas del 9 % del presupuesto en triggers, las vidas se alargan.
- **Mas rapido SIN cambiar el resultado** (comprobado con un hash de la cadena de GD entera: identica a la 0.5.5 en 7, 8, 18-25, 27 y 29 con calidad Rapido/Normal/Alto, HSV y canales):
  - `Geo::eval` (rectangulos girados: 7, 8 y 18-29) suma por filas con prefijos en vez de pixel a pixel (sumas enteras exactas).
  - Optimizador por forma (23 y 27): la ganancia de cada candidato se calcula una vez por pixel y forma en lugar de re-dibujar toda la region por candidato: 23 pasa de 27,9 s a 5,9 s y 27 de 16,6 s a 3,8 s (catgirl 36 frames, 800 obj/frame).
  - Comparacion de variantes (21, 22, 25, 29) con salida anticipada exacta, `renderShapes` con camino rapido para rectangulos sin giro y mediana 3x3 por insercion.
  - Resto de metodos: 1,2x - 1,6x mas rapidos (18: 2,0 -> 1,6 s; 21: 3,6 -> 2,3 s; 22: 2,8 -> 1,9 s).
- **Nuevo ajuste "Duracion de calculo (metodos 18-29)"**: Rapido / Normal / Alto / Maximo. Normal = el resultado de siempre (mismo hash). Rapido acorta la busqueda de cada objeto (~1,15x-1,35x mas rapido,
  ~0,1-0,3 dB menos), Alto y Maximo buscan mas (~1,4x-2x mas lento, ~+0,1-0,4 dB). "Calidad de calculo" queda solo para 7 y 8. El 28 (Auto) no prueba el optimizador (23) con Rapido.
- Aviso: "Quitar ruido del GIF" (mediana 3x3) es un tope de calidad en pixel art (borra detalles de 1 px): en el GIF de la catgirl, desactivarlo sube el 26 de 25 a 31 dB y el 18 de 24 a 28 dB.
- Pruebas nuevas: `tests/bench_lsa.cpp` (tiempo, PSNR y hash por metodo/ajuste). `tests/dispatch.hpp`: variable CHANNELS=1 para probar el modo canales.
- NO verificado en GD real: nada de esto (el simulador es mi modelo de GD).

# v0.5.5-beta
- **Metodos 18 y 19 arreglados con GIFs largos.** Con mas de 40 capas el esquema de diferencias no se autocorregia: un trigger perdido dejaba capas colgadas (con 48-100 frames,
  33-63 % de las muestras con fallos en el modelo hostil). Ahora cada paso reafirma el estado ACTUAL de todas las capas que cambiaron en los ultimos 3 pasos y se lanza dos veces:
  0-0,3 % de fallos con 16, 48 y 100 frames (`tests/corpus_test.cpp`).
- **10 metodos nuevos (20-29)**, todos sobre un nucleo comun de "capas con visibilidad por frame" (`engine/lsa*.hpp`) con el mismo exportador autocorregible. Solo usan lo ya verificado
  (cuadrados escalados/girados, HSV o canales, Alpha/Spawn/Color triggers), sin Move/Rotate/Scale/Gradient (sus claves no las he podido comprobar en GD):
  20 Vidas de formas (reciclaje: una forma vive mientras siga valiendo) | 21 Biblioteca de teselas | 22 Keyframes + residuos por tesela | 23 Optimizador por forma (calidad maxima) |
  24 Reparto RD + frames repetidos | 25 Fondo + regiones de movimiento | 26 Animacion por colores (Color triggers) | 27 Vidas + optimizador | 28 Auto (prueba varios y elige) | 29 Cuadtree temporal.
  Cada uno elige solo sus parametros con un modelo de coste (calidad frente a objetos + triggers) y el trazador interno prueba BSP puro y BSP+pintor y se queda con el mejor.
- Resultados medidos (250 objetos/frame): bola que rebota -> el 25 llega a 43 dB con 2981 objetos (el plano da 35 dB con 7772); ping-pong -> +1,7 dB con menos objetos; el 23 gana
  a todo en GIFs con movimiento de todo el cuerpo (+0,6 dB). Los metodos de teselas/vidas NO ganan con movimiento global: para eso esta el 28 (Auto).
- Interfaz: 10 entradas nuevas en el selector de metodo (20-29), con descripcion; las opciones de canales/colores se aplican igual que en 18/19 (el 26 usa sus propios canales).
- Pruebas: `tests/corpus_test.cpp` + `tests/run_corpus.sh` (reanudable) + `tests/gen_corpus.py` (22 GIFs: tamanos 16-384 px, 1-100 frames, 20-200 ms, paleta 8/16/256, transparencia,
  ping-pong, desplazamiento, rotacion, destellos...), `tests/lsa_test.cpp` (invariantes de la cadena: z>=1, escalas, grupos, triggers sin pisarse, antes que los objetos, determinismo, PSNR
  simulado = calculado) y `tests/robust_test.cpp` (triggers perdidos / lag).
- NO verificado en GD real: nada de esto. El simulador es mi modelo de GD; sigue valiendo lo de "NO verificado" del README.

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
