// ============================================================================
//  gdx :: Saneado de resultados (red de seguridad comun a TODOS los metodos)
//  Errores reales encontrados en GD que esto evita (ver README "Errores conocidos"):
//    1. Formas FUERA del lienzo: en el juego no se recortan -> astillas/halo alrededor de la imagen (el preview si las recortaba).
//    2. z-order con miles de valores distintos (un z por forma): innecesario y fragil.
//  Sin dependencias de Geode.
// ============================================================================
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "core.hpp"

namespace gdx {

// Extensiones (semi-anchos) del rectangulo girado en ejes de pantalla.
inline void shapeExtents(const Shape& s, double& ex, double& ey) {
    const double rad = double(s.rot) * 3.14159265358979323846 / 180.0, cs = std::cos(rad), sn = std::sin(rad);
    ex = std::abs(s.w * 0.5 * cs) + std::abs(s.h * 0.5 * sn);
    ey = std::abs(s.w * 0.5 * sn) + std::abs(s.h * 0.5 * cs);
}

// Cuanto sobresale la forma del lienzo [0,W]x[0,H] (0 = dentro).
inline double shapeOverhang(const Shape& s, int W, int H) {
    double ex, ey; shapeExtents(s, ex, ey);
    return std::max({-(double(s.cx) - ex), (double(s.cx) + ex) - W, -(double(s.cy) - ey), (double(s.cy) + ey) - H, 0.0});
}

inline bool isAxisAligned(const Shape& s) {
    double a = std::fmod(std::abs(double(s.rot)), 90.0);
    return a < 0.05 || a > 89.95;
}

// Deja todas las formas DENTRO del lienzo: las alineadas a ejes se recortan exactamente; las giradas se reducen
// (mismo centro) hasta caber; si quedan casi vacias se eliminan (el borde ya lo cubre la base).
// Devuelve cuantas formas se tocaron.
inline int confineShapes(std::vector<Shape>& shapes, int W, int H, double tol = 0.15) {
    int touched = 0;
    size_t w = 0;
    for (size_t i = 0; i < shapes.size(); ++i) {
        Shape s = shapes[i];
        if (shapeOverhang(s, W, H) <= tol) { shapes[w++] = s; continue; }
        ++touched;
        double ex, ey; shapeExtents(s, ex, ey);
        if (isAxisAligned(s)) {
            double x0 = std::max(0.0, s.cx - ex), x1 = std::min<double>(W, s.cx + ex), y0 = std::max(0.0, s.cy - ey), y1 = std::min<double>(H, s.cy + ey);
            if (x1 - x0 < 0.2 || y1 - y0 < 0.2) continue;
            const bool swap = std::fmod(std::abs(double(s.rot)), 180.0) > 45.0 && std::fmod(std::abs(double(s.rot)), 180.0) < 135.0;
            s.cx = float((x0 + x1) * 0.5); s.cy = float((y0 + y1) * 0.5);
            s.w = float(swap ? (y1 - y0) : (x1 - x0)); s.h = float(swap ? (x1 - x0) : (y1 - y0));
        } else {
            double ax = std::min<double>(s.cx, W - s.cx), ay = std::min<double>(s.cy, H - s.cy);
            if (ax <= 0.0 || ay <= 0.0) continue;
            double f = std::min({1.0, ax / std::max(ex, 1e-6), ay / std::max(ey, 1e-6)});
            if (f < 0.3) continue;
            s.w = float(s.w * f); s.h = float(s.h * f);
        }
        shapes[w++] = s;
    }
    shapes.resize(w);
    return touched;
}

// Compacta el z-order a la PROFUNDIDAD REAL de solapamiento: dos formas que no se tocan comparten z. El resultado visual es
// identico (el orden relativo de las que si se solapan se conserva) pero en vez de miles de z distintos quedan unas decenas.
inline void compactZ(std::vector<Shape>& shapes, int W, int H) {
    if (shapes.size() < 2) { for (auto& s : shapes) s.z = 0; return; }
    std::vector<size_t> ord(shapes.size());
    for (size_t i = 0; i < ord.size(); ++i) ord[i] = i;
    std::stable_sort(ord.begin(), ord.end(), [&](size_t a, size_t b) { return shapes[a].z < shapes[b].z; });
    std::vector<uint16_t> pd(size_t(W) * H, 0);
    std::vector<uint16_t> depth(shapes.size(), 1);
    for (size_t oi : ord) {
        const Shape& s = shapes[oi];
        const double rad = s.rot * 3.14159265358979323846 / 180.0, cs = std::cos(rad), sn = std::sin(rad);
        const double hx = s.w * 0.5, hy = s.h * 0.5;
        const double ex = std::abs(hx * cs) + std::abs(hy * sn), ey = std::abs(hx * sn) + std::abs(hy * cs);
        int x0 = std::max(0, int(std::floor(s.cx - ex - 0.5))), x1 = std::min(W - 1, int(std::ceil(s.cx + ex + 0.5)));
        int y0 = std::max(0, int(std::floor(s.cy - ey - 0.5))), y1 = std::min(H - 1, int(std::ceil(s.cy + ey + 0.5)));
        auto inside = [&](int x, int y) {
            double dx = x + 0.5 - s.cx, dy = y + 0.5 - s.cy, lx = dx * cs + dy * sn, ly = -dx * sn + dy * cs;
            return lx >= -hx - 1e-4 && lx < hx + 1e-4 && ly >= -hy - 1e-4 && ly < hy + 1e-4;
        };
        uint16_t d = 1;
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) if (inside(x, y)) d = std::max<uint16_t>(d, uint16_t(pd[size_t(y) * W + x] + 1));
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) if (inside(x, y)) pd[size_t(y) * W + x] = d;
        depth[oi] = d;
    }
    for (size_t i = 0; i < shapes.size(); ++i) shapes[i].z = int16_t(std::min<int>(depth[i] - 1, 32000));
}

// Saneado de un Result completo: dentro del lienzo + z-order compacto (los estilos mono usan z<=0 como "capa base": no se tocan).
inline void sanitizeResult(Result& r) {
    if (r.rasterW <= 0 || r.rasterH <= 0 || r.shapes.empty()) return;
    confineShapes(r.shapes, r.rasterW, r.rasterH);
    int mz = 0; for (auto& s : r.shapes) mz = std::max<int>(mz, s.z);
    if (!r.mono && mz > 64) compactZ(r.shapes, r.rasterW, r.rasterH);
}

} // namespace gdx
