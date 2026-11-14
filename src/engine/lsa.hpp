// ============================================================================
//  LSA = "layer-set animation": metodos 20-29.
//  Una animacion es un conjunto de CAPAS (cada una = un grupo de formas) y, por frame, el subconjunto de capas visibles.
//  Todos los metodos nuevos solo difieren en COMO eligen las capas y los subconjuntos; un unico exportador (lsa_methods.hpp) genera
//  los triggers autocorregibles (Alpha absoluto, pasos idempotentes lanzados dos veces, reafirmacion de los ultimos pasos).
//  Este archivo solo contiene los tipos (lo incluye layered.hpp para que LayeredAnim pueda llevar una LsaAnim).
// ============================================================================
#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>
#include "core.hpp"

namespace gdx {

struct LsaLayer {
    std::vector<Shape> shapes;      // raster W x H de la animacion; Shape::z = z LOCAL de la capa (>= 0)
    int depth = 0;                  // banda de z: mayor = se dibuja encima. Capas con la MISMA depth comparten banda (z local propio)
    bool always = false;            // siempre visible: sin grupo ni triggers
};

struct LsaAnim {
    int W = 0, H = 0, frames = 0;
    int kind = 0;                                       // 1..10 = metodo 20..29
    std::vector<LsaLayer> layers;
    std::vector<std::vector<int>> vis;                  // por frame: ids de capas NO 'always' visibles (ordenados)
    std::vector<double> psnr;                           // fidelidad de cada frame montado (dB)
    double psnrAvg = 0, psnrMin = 0;
    std::string note;
    // Animacion por colores (metodo 26): el canal c vale colorAt[f][c] en el frame f; las formas guardan el indice de canal en (r, g).
    int colorChannels = 0;
    std::vector<std::vector<std::array<uint8_t, 3>>> colorAt;
    // Paleta global de canales (opcional, igual que las capas 18/19): sustituye al HSV.
    std::vector<std::array<uint8_t, 3>> palette;
    int firstChannel = 2;

    size_t objectCount() const { size_t n = 0; for (auto& l : layers) n += l.shapes.size(); return n; }
    bool ok() const { return W > 0 && frames >= 1 && !layers.empty() && int(vis.size()) == frames && objectCount() > 0; }
};

} // namespace gdx
