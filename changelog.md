# v0.2.1-beta
- Fix de compilacion en Windows: la variable `near` chocaba con la macro `near` de windows.h (gifpatch.hpp).

# v0.2.0-beta
- Metodo nuevo **18. Estatico + parches** (inspirado en el "GIF to GD"): lo que no cambia se traza UNA vez (fondo con presupuesto x6) y cada frame solo lleva un parche con lo que cambia. Menos objetos por frame y sin parpadeo en la parte quieta.
- 3 ajustes nuevos: trazador del metodo 18, tolerancia de cambio y presupuesto del fondo.
- Los metodos 1-17 no se tocan.

# v0.1.0-beta
- Primera version: importa GIFs animados con Alpha triggers + Spawn triggers en bucle infinito.
- 17 metodos de imagen (los del Image Exporter, sin ASCII), objetos por frame, frames maximos, escala de grises.
- Retardos reales del GIF, velocidad ajustable y tope total de objetos.
