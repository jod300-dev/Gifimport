// ============================================================================
//  gdx :: Colores en CANALES (sin HSV) para los metodos 1-17 (un Result por frame)
//  Sin dependencias de Geode. No modifica gifanim.hpp: es un exportador gemelo de buildGifString.
//
//   * una paleta comun a TODOS los frames (corte mediano + Lloyd, ponderada por area)
//   * cada objeto lleva solo  21,<canal>  (sin claves HSV 41/43)
//   * los canales son  2 .. 1 + K : el canal 1 NUNCA se usa y siempre se empieza por el 2
//   * el color de cada canal lo fija un Color trigger (id 899) al inicio del nivel
// ============================================================================
#pragma once
#include "layered.hpp"

namespace gdx {

constexpr int kFirstColorChannel = 2;                       // el canal 1 nunca se usa

// Cuantiza los colores de todos los frames a <= K colores y los marca como `colored`. Devuelve la paleta usada.
// (Conserva r.mono: los estilos de tinta siguen separando base y tinta en capas.)
inline std::vector<std::array<uint8_t, 3>> quantizeFrames(std::vector<Result>& results, int K) {
    K = std::clamp(K, 2, 999 - kFirstColorChannel + 1);
    std::map<uint32_t, double> hist;
    for (auto& r : results) for (auto& sh : r.shapes) hist[lay::colKey(sh.r, sh.g, sh.b)] += double(sh.w) * sh.h + 1.0;
    std::vector<std::array<uint8_t, 3>> out;
    if (hist.empty()) return out;
    auto pal = lay::buildPalette(hist, K);
    std::map<uint32_t, int> cache; std::vector<char> used(pal.size(), 0);
    auto nearest = [&](uint8_t r, uint8_t g, uint8_t b) {
        uint32_t k = lay::colKey(r, g, b);
        auto it = cache.find(k); if (it != cache.end()) return it->second;
        double c[3] = {double(r), double(g), double(b)}; int bi = 0; double bd = 1e300;
        for (size_t j = 0; j < pal.size(); ++j) { double pc[3] = {double(pal[j][0]), double(pal[j][1]), double(pal[j][2])}; double d = lay::colDist(c, pc); if (d < bd) { bd = d; bi = int(j); } }
        cache[k] = bi; return bi;
    };
    for (auto& r : results) {
        for (auto& sh : r.shapes) { int bi = nearest(sh.r, sh.g, sh.b); sh.r = pal[size_t(bi)][0]; sh.g = pal[size_t(bi)][1]; sh.b = pal[size_t(bi)][2]; used[size_t(bi)] = 1; }
        r.colored = true;
    }
    for (size_t j = 0; j < pal.size(); ++j) if (used[j]) out.push_back(pal[j]);
    return out;
}

// Un Color trigger (id 899) por canal de la paleta, a la izquierda del inicio (se ejecutan al empezar el nivel).
inline void appendColorTriggers(std::string& out, const std::vector<std::array<uint8_t, 3>>& palette, int firstChannel, double trigX, double sy) {
    for (size_t i = 0; i < palette.size(); ++i) {
        const double cx = trigX - 30.0 * double(1 + int(i) % 25), cy = sy + 60.0 + 30.0 * double(int(i) / 25);
        detail::addTriggerHead(out, 899, cx, cy);
        out += ",7,"; out += std::to_string(palette[i][0]);
        out += ",8,"; out += std::to_string(palette[i][1]);
        out += ",9,"; out += std::to_string(palette[i][2]);
        out += ",10,0,35,1,17,0,23,"; out += std::to_string(firstChannel + int(i));
        out += ';';
    }
}

// Igual que buildGifString, pero con colores en canales (results ya pasados por quantizeFrames).
inline std::string buildGifStringCh(const std::vector<Result>& results, const GifPlan& plan, const GifExportParams& p,
                                    const std::vector<std::array<uint8_t, 3>>& palette, std::string* errmsg = nullptr) {
    auto fail = [&](const char* m) { if (errmsg) *errmsg = m; return std::string(); };
    const int N = plan.count();
    if (N < 2 || int(results.size()) != N) return fail("Hacen falta al menos 2 frames");
    if (p.baseGroup < 1 || p.baseGroup + gifGroupCount(N) - 1 > 9999) return fail("No quedan grupos libres suficientes (max. 9999)");
    if (palette.empty() || kFirstColorChannel + int(palette.size()) - 1 > 999) return fail("No caben los canales de color (2..999)");
    const int G0 = p.baseGroup, F0 = G0 + N, L = G0 + 2 * N, I = G0 + 2 * N + 1;
    std::map<uint32_t, int> chOf;
    for (size_t i = 0; i < palette.size(); ++i) chOf[lay::colKey(palette[i][0], palette[i][1], palette[i][2])] = kFirstColorChannel + int(i);

    std::string out; size_t total = 0;
    for (auto& r : results) total += r.shapes.size();
    if (total == 0) return fail("Ningun frame genero objetos");
    out.reserve(total * 100 + size_t(gifTriggerCount(N) + int(palette.size())) * 90);
    for (int i = 0; i < N; ++i) {
        const Result& r = results[size_t(i)];
        if (r.shapes.empty()) continue;
        GdExportOptions eo = p.eo;
        eo.groupStr = std::to_string(G0 + i);
        eo.monoFastPath = false;
        ChannelMap cm = buildChannelMap(r);                          // orden de aparicion de los colores de ESTE frame -> canal global
        eo.channels.assign(cm.colors.size(), 0);
        for (size_t k = 0; k < cm.colors.size(); ++k) eo.channels[k] = chOf.at(lay::colKey(cm.colors[k][0], cm.colors[k][1], cm.colors[k][2]));
        std::string s = toGdString(r, eo);
        if (s.empty()) continue;
        out += s; out += ';';
    }
    const Result& r0 = results[0];
    GdSize sz = gdImageSize(r0, p.eo);
    const double left = p.eo.originX - sz.w * 0.5, top = p.eo.originY + sz.h * 0.5 + 45.0;
    const int cols = 30; int k = 0;
    auto pos = [&](double& x, double& y) { x = left + (k % cols) * 30.0; y = top + (k / cols) * 30.0; ++k; };
    double x, y, t = 0;
    for (int i = 0; i < N; ++i) { pos(x, y); detail::addSpawn(out, p, x, y, F0 + i, t, L); t += plan.delay[size_t(i)]; }
    pos(x, y); detail::addSpawn(out, p, x, y, L, plan.total, L);
    for (int i = 0; i < N; ++i) {
        pos(x, y); detail::addAlpha(out, p, x, y, G0 + i, 1, F0 + i);
        pos(x, y); detail::addAlpha(out, p, x, y, G0 + (i + N - 1) % N, 0, F0 + i);
    }
    for (int i = 1; i < N; ++i) { pos(x, y); detail::addAlpha(out, p, x, y, G0 + i, 0, I); }
    double sx = p.trigX, sy = top + ((k / cols) + 2) * 30.0;
    detail::addSpawn(out, p, sx, sy, L, 0, 0);
    detail::addSpawn(out, p, sx, sy + 30.0, I, 0, 0);
    appendColorTriggers(out, palette, kFirstColorChannel, p.trigX, sy);
    if (!out.empty() && out.back() == ';') out.pop_back();
    return out;
}

} // namespace gdx
