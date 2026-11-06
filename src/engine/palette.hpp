// ============================================================================
//  Paletas de color: corte por mediana ponderado y asignación al color más cercano.
//  Las usan los métodos principales (canales de color / grises) y los estilos ligeros.
// ============================================================================
#pragma once
#include <array>
#include "core.hpp"

namespace gdx {

using Rgb3 = std::array<uint8_t, 3>;

struct WCol { float r, g, b, w; };

inline double lumaOf(double r, double g, double b) { return 0.299 * r + 0.587 * g + 0.114 * b; }

// Corte por mediana con pesos: devuelve hasta k colores representativos.
inline std::vector<Rgb3> medianCutW(std::vector<WCol> cols, int k) {
    struct Box { std::vector<WCol> c; double sse; };
    auto sseOf = [](const std::vector<WCol>& v) {
        double w = 0, r = 0, g = 0, b = 0, q = 0;
        for (auto& c : v) { w += c.w; r += c.w * c.r; g += c.w * c.g; b += c.w * c.b; q += c.w * (double(c.r) * c.r + double(c.g) * c.g + double(c.b) * c.b); }
        return w > 0 ? std::max(0.0, q - (r * r + g * g + b * b) / w) : 0.0;
    };
    std::vector<Rgb3> pal;
    if (cols.empty() || k < 1) return pal;
    std::vector<Box> boxes;
    boxes.push_back({std::move(cols), 0});
    boxes[0].sse = sseOf(boxes[0].c);
    while (int(boxes.size()) < k) {
        int bi = -1; double bs = 1e-9;
        for (int i = 0; i < int(boxes.size()); ++i) if (boxes[i].c.size() > 1 && boxes[i].sse > bs) { bs = boxes[i].sse; bi = i; }
        if (bi < 0) break;
        std::vector<WCol> v = std::move(boxes[bi].c);
        float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
        for (auto& c : v) { float x[3] = {c.r, c.g, c.b}; for (int j = 0; j < 3; ++j) { mn[j] = std::min(mn[j], x[j]); mx[j] = std::max(mx[j], x[j]); } }
        int ch = 0; for (int j = 1; j < 3; ++j) if (mx[j] - mn[j] > mx[ch] - mn[ch]) ch = j;
        std::sort(v.begin(), v.end(), [&](const WCol& a, const WCol& b) { return (ch == 0 ? a.r : ch == 1 ? a.g : a.b) < (ch == 0 ? b.r : ch == 1 ? b.g : b.b); });
        double tot = 0; for (auto& c : v) tot += c.w;
        double acc = 0; size_t cut = 1;
        for (size_t i = 0; i < v.size(); ++i) { acc += v[i].w; if (acc >= tot * 0.5) { cut = i + 1; break; } }
        cut = std::clamp<size_t>(cut, 1, v.size() - 1);
        std::vector<WCol> a(v.begin(), v.begin() + cut), b(v.begin() + cut, v.end());
        boxes[bi] = {a, sseOf(a)};
        boxes.push_back({b, sseOf(b)});
    }
    for (auto& bx : boxes) {
        double w = 0, r = 0, g = 0, b = 0;
        for (auto& c : bx.c) { w += c.w; r += c.w * c.r; g += c.w * c.g; b += c.w * c.b; }
        if (w <= 0) continue;
        pal.push_back({uint8_t(std::clamp(r / w + 0.5, 0.0, 255.0)), uint8_t(std::clamp(g / w + 0.5, 0.0, 255.0)), uint8_t(std::clamp(b / w + 0.5, 0.0, 255.0))});
    }
    return pal;
}

inline int nearestIdx(const std::vector<Rgb3>& pal, double r, double g, double b) {
    int best = 0; double bd = 1e18;
    for (int i = 0; i < int(pal.size()); ++i) {
        double dr = pal[i][0] - r, dg = pal[i][1] - g, db = pal[i][2] - b, d = 2.0 * dr * dr + 4.0 * dg * dg + 3.0 * db * db;   // pesos perceptuales
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

// Reduce los colores de las formas a una paleta de K colores (cada uno será un canal de GD).
// Devuelve cuántos colores distintos quedan.
inline int quantizeShapesToPalette(std::vector<Shape>& shapes, int K) {
    std::vector<WCol> samples; samples.reserve(shapes.size());
    for (const Shape& s : shapes) samples.push_back({float(s.r), float(s.g), float(s.b), std::max(1.f, s.w * s.h)});
    std::vector<Rgb3> pal = medianCutW(samples, std::max(1, K));
    if (pal.empty()) return 0;
    for (Shape& s : shapes) {
        int i = nearestIdx(pal, s.r, s.g, s.b);
        s.r = pal[size_t(i)][0]; s.g = pal[size_t(i)][1]; s.b = pal[size_t(i)][2];
    }
    return int(pal.size());
}

// Pasa la imagen a escala de grises (conserva el alfa).
inline Image toGrayscale(const Image& src) {
    Image o = src;
    for (size_t i = 0; i < o.rgba.size(); i += 4) {
        uint8_t y = uint8_t(std::clamp(lumaOf(o.rgba[i], o.rgba[i + 1], o.rgba[i + 2]) + 0.5, 0.0, 255.0));
        o.rgba[i] = o.rgba[i + 1] = o.rgba[i + 2] = y;
    }
    return o;
}

} // namespace gdx
