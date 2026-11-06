// ============================================================================
//  Refinado por visibilidad para métodos con capas solapadas (Pirámide, Geometrize, Híbrido):
//   * cada forma toma como color la media del objetivo en SUS píxeles visibles (lo óptimo para esa
//     geometría y ese orden de dibujo; el color original se calculó antes de que otras formas la taparan)
//   * las formas totalmente tapadas no se ven: se eliminan y su presupuesto se puede reinvertir
// ============================================================================
#pragma once
#include "core.hpp"

namespace gdx {

// Rasteriza los ids de las formas (orden estable por z) y devuelve, por forma, píxeles visibles y color medio.
// Devuelve el nº de formas eliminadas (si prune) y deja `shapes` recoloreadas.
inline int refitVisible(std::vector<Shape>& shapes, const Image& target, int alphaThr, bool prune) {
    const int W = target.w, H = target.h;
    if (shapes.empty() || W <= 0 || H <= 0) return 0;
    std::vector<size_t> order(shapes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return shapes[a].z < shapes[b].z; });
    std::vector<int32_t> top(size_t(W) * H, -1);
    for (size_t oi : order) {
        const Shape& s = shapes[oi];
        const double rad = s.rot * 3.14159265358979323846 / 180.0, cs = std::cos(rad), sn = std::sin(rad);
        const double hx = s.w * 0.5, hy = s.h * 0.5;
        const double ex = std::abs(hx * cs) + std::abs(hy * sn), ey = std::abs(hx * sn) + std::abs(hy * cs);
        int x0 = std::max(0, int(std::floor(s.cx - ex - 0.5))), x1 = std::min(W - 1, int(std::ceil(s.cx + ex + 0.5)));
        int y0 = std::max(0, int(std::floor(s.cy - ey - 0.5))), y1 = std::min(H - 1, int(std::ceil(s.cy + ey + 0.5)));
        const bool axis = std::abs(std::fmod(s.rot, 90.0f)) < 1e-4f;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                double dx = x + 0.5 - s.cx, dy = y + 0.5 - s.cy;
                bool in;
                if (axis && std::abs(std::fmod(s.rot, 180.0f)) < 1e-4f) in = std::abs(dx) <= hx + 1e-4 && std::abs(dy) <= hy + 1e-4;
                else { double lx = dx * cs + dy * sn, ly = -dx * sn + dy * cs; in = lx >= -hx - 1e-4 && lx < hx + 1e-4 && ly >= -hy - 1e-4 && ly < hy + 1e-4; }
                if (in) top[size_t(y) * W + x] = int32_t(oi);
            }
    }
    std::vector<uint32_t> vis(shapes.size(), 0), cnt(shapes.size(), 0);
    std::vector<double> sr(shapes.size(), 0), sg(shapes.size(), 0), sb(shapes.size(), 0);
    for (size_t p = 0; p < top.size(); ++p) {
        int32_t id = top[p]; if (id < 0) continue;
        ++vis[size_t(id)];
        const uint8_t* t = &target.rgba[p * 4];
        if (t[3] >= alphaThr) { ++cnt[size_t(id)]; sr[size_t(id)] += t[0]; sg[size_t(id)] += t[1]; sb[size_t(id)] += t[2]; }
    }
    int removed = 0;
    std::vector<Shape> out; out.reserve(shapes.size());
    for (size_t i = 0; i < shapes.size(); ++i) {
        Shape s = shapes[i];
        if (vis[i] == 0 && prune && std::min(s.w, s.h) >= 1.0f) { ++removed; continue; }   // totalmente tapada
        if (cnt[i] > 0) {
            s.r = uint8_t(std::clamp(sr[i] / cnt[i] + 0.5, 0.0, 255.0));
            s.g = uint8_t(std::clamp(sg[i] / cnt[i] + 0.5, 0.0, 255.0));
            s.b = uint8_t(std::clamp(sb[i] / cnt[i] + 0.5, 0.0, 255.0));
        }
        out.push_back(s);
    }
    shapes = std::move(out);
    return removed;
}

} // namespace gdx
