# v0.3.0-beta
- Proyecto independiente: se elimino todo lo del Image Exporter (17 metodos, estilos de tinta, regiones, geometrize, ASCII, canales de color...). Solo queda el **metodo de parches** (trazador BSP).
- Ventana minima: objetos por frame, lado largo, escala de grises, GIF / Colocar / Ajustes. "Frames maximos" paso a Ajustes.
- Triggers mas robustos: un unico trigger de arranque (antes 2), sin grupo de inicio aparte; el primer frame de cada vuelta vuelve a ocultar todos los demas, asi cada vuelta se resincroniza sola.
- Los triggers ya no se colocan en x negativa. Nuevo ajuste "Trigger de arranque: x" (por defecto 0).
- El algoritmo del metodo vive solo en `src/engine/gifpatch.hpp` (se puede reemplazar sin tocar la ventana).

# v0.2.1-beta
- Fix de compilacion en Windows: la variable `near` chocaba con la macro de windows.h.

# v0.2.0-beta
- Metodo de parches (estatico + parches).

# v0.1.0-beta
- Primera version.
