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

// Penetracion (px) entre dos rectangulos girados por SAT: 0 = no se solapan (o solo se tocan).
inline double rectPenetration(const Shape& a, const Shape& b) {
    double A[4][2], B[4][2];
    auto corners = [](const Shape& s, double c[4][2]) {
        const double r = s.rot * 3.14159265358979323846 / 180.0, cs = std::cos(r), sn = std::sin(r), hx = s.w * 0.5, hy = s.h * 0.5;
        const double lx[4] = {-hx, hx, hx, -hx}, ly[4] = {-hy, -hy, hy, hy};
        for (int i = 0; i < 4; ++i) { c[i][0] = s.cx + lx[i] * cs - ly[i] * sn; c[i][1] = s.cy + lx[i] * sn + ly[i] * cs; }
    };
    corners(a, A); corners(b, B);
    double minPen = 1e18;
    for (int k = 0; k < 2; ++k) {
        double (*P)[2] = k == 0 ? A : B;
        for (int e = 0; e < 2; ++e) {
            double nx = P[e + 1][1] - P[e][1], ny = -(P[e + 1][0] - P[e][0]);
            const double L = std::hypot(nx, ny); if (L < 1e-12) continue; nx /= L; ny /= L;
            double a0 = 1e18, a1 = -1e18, b0 = 1e18, b1 = -1e18;
            for (int i = 0; i < 4; ++i) {
                double pa = A[i][0] * nx + A[i][1] * ny, pb = B[i][0] * nx + B[i][1] * ny;
                a0 = std::min(a0, pa); a1 = std::max(a1, pa); b0 = std::min(b0, pb); b1 = std::max(b1, pb);
            }
            const double pen = std::min(a1, b1) - std::max(a0, b0);
            if (pen <= 0) return 0.0;
            minPen = std::min(minPen, pen);
        }
    }
    return minPen;
}

// Compacta el z-order a la PROFUNDIDAD REAL de solapamiento GEOMETRICO: dos formas que se solapan (SAT, en continuo, no por
// centros de pixel) tienen z DISTINTO y en el orden correcto; las que no se tocan comparten z. En GD el orden entre formas con
// el mismo z NO esta garantizado, por eso un solape real nunca puede quedar empatado. Resultado visual identico al z unico.
inline void compactZ(std::vector<Shape>& shapes, int W, int H) {
    const size_t n = shapes.size();
    if (n < 2) { for (auto& s : shapes) s.z = 0; return; }
    std::vector<size_t> ord(n);
    for (size_t i = 0; i < n; ++i) ord[i] = i;
    std::stable_sort(ord.begin(), ord.end(), [&](size_t a, size_t b) { return shapes[a].z < shapes[b].z; });
    const int cell = 8, gx = (W + cell - 1) / cell + 1, gy = (H + cell - 1) / cell + 1;
    std::vector<std::vector<uint32_t>> grid(size_t(gx) * gy);
    std::vector<uint16_t> depth(n, 1);
    std::vector<uint32_t> seen(n, 0); uint32_t stamp = 0;
    constexpr double kEps = 0.02;                                   // por debajo: solo se tocan (teselado BSP)
    for (size_t oi : ord) {
        const Shape& s = shapes[oi];
        double ex, ey; shapeExtents(s, ex, ey);
        int x0 = std::clamp(int(std::floor((s.cx - ex) / cell)), 0, gx - 1), x1 = std::clamp(int(std::floor((s.cx + ex) / cell)), 0, gx - 1);
        int y0 = std::clamp(int(std::floor((s.cy - ey) / cell)), 0, gy - 1), y1 = std::clamp(int(std::floor((s.cy + ey) / cell)), 0, gy - 1);
        ++stamp; uint16_t d = 1;
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x)
            for (uint32_t other : grid[size_t(y) * gx + x]) {
                if (seen[other] == stamp) continue;
                seen[other] = stamp;
                if (rectPenetration(s, shapes[other]) > kEps) d = std::max<uint16_t>(d, uint16_t(depth[other] + 1));
            }
        depth[oi] = d;
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) grid[size_t(y) * gx + x].push_back(uint32_t(oi));
    }
    for (size_t i = 0; i < n; ++i) shapes[i].z = int16_t(std::min<int>(depth[i] - 1, 32000));
}

// Saneado de un Result completo: dentro del lienzo + z-order compacto (los estilos mono usan z<=0 como "capa base": no se tocan).
inline void sanitizeResult(Result& r) {
    if (r.rasterW <= 0 || r.rasterH <= 0 || r.shapes.empty()) return;
    confineShapes(r.shapes, r.rasterW, r.rasterH);
    int mz = 0; for (auto& s : r.shapes) mz = std::max<int>(mz, s.z);
    if (!r.mono && mz > 64) compactZ(r.shapes, r.rasterW, r.rasterH);
}

} // namespace gdx
