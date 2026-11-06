// ============================================================================
//  Color para los estilos ligeros (9-18) sin HSV: base de color + tinta del mismo tono.
//
//  En vez de tinta de color sobre papel plano (que deja huecos y puntos blancos/negros),
//  se pinta primero un mosaico de color (BSP) con el tono de la imagen aclarado y encima
//  el estilo con una versión más oscura del mismo tono. Los colores se agrupan en K/2
//  tonos; cada tono usa 2 canales (base y tinta).
// ============================================================================
#pragma once
#include "partition.hpp"
#include "palette.hpp"

namespace gdx {

constexpr double kPiC = 3.14159265358979323846;

// Refuerza un poco la saturación (los tonos de base/tinta salen más vivos y menos apagados).
inline void boostSat(double& r, double& g, double& b, double k) {
    double y = lumaOf(r, g, b);
    r = y + (r - y) * k; g = y + (g - y) * k; b = y + (b - y) * k;
}

// Tinta: versión oscura del tono (o clara si la tinta es blanca sobre fondo oscuro).
inline Rgb3 inkOf(const Rgb3& c, bool inv) {
    double r = c[0], g = c[1], b = c[2];
    boostSat(r, g, b, 1.18);
    if (!inv) { r *= 0.50; g *= 0.50; b *= 0.50; }
    else { r += (255 - r) * 0.58; g += (255 - g) * 0.58; b += (255 - b) * 0.58; }
    r = std::clamp(r, 0.0, 255.0); g = std::clamp(g, 0.0, 255.0); b = std::clamp(b, 0.0, 255.0);
    return {uint8_t(std::clamp(r + 0.5, 0.0, 255.0)), uint8_t(std::clamp(g + 0.5, 0.0, 255.0)), uint8_t(std::clamp(b + 0.5, 0.0, 255.0))};
}

// Base: versión clara del tono (u oscura si la tinta es clara).
inline Rgb3 baseOf(const Rgb3& c, bool inv) {
    double r = c[0], g = c[1], b = c[2];
    boostSat(r, g, b, 1.15);
    r = std::clamp(r, 0.0, 255.0); g = std::clamp(g, 0.0, 255.0); b = std::clamp(b, 0.0, 255.0);
    if (!inv) { r += (255 - r) * 0.30; g += (255 - g) * 0.30; b += (255 - b) * 0.30; }
    else { r *= 0.62; g *= 0.62; b *= 0.62; }
    return {uint8_t(std::clamp(r + 0.5, 0.0, 255.0)), uint8_t(std::clamp(g + 0.5, 0.0, 255.0)), uint8_t(std::clamp(b + 0.5, 0.0, 255.0))};
}

// Color de base que, con una cobertura de tinta c, da el color objetivo T:  base*(1-c) + tinta*c = T.
// Sin tinta (c = 0) la base es exactamente T.
inline void compensatedBase(double Tr, double Tg, double Tb, const Rgb3& ink, double c, double& r, double& g, double& b) {
    c = std::clamp(c, 0.0, 0.85);
    r = std::clamp((Tr - ink[0] * c) / (1.0 - c), 0.0, 255.0);
    g = std::clamp((Tg - ink[1] * c) / (1.0 - c), 0.0, 255.0);
    b = std::clamp((Tb - ink[2] * c) / (1.0 - c), 0.0, 255.0);
}

// Agrupa los colores de las hojas de la base en K colores (cada uno será un canal).
inline void quantizeLeaves(std::vector<Shape>& leaves, int K) {
    std::vector<WCol> smp; smp.reserve(leaves.size());
    for (const Shape& s : leaves) smp.push_back({float(s.r), float(s.g), float(s.b), std::max(1.f, s.w * s.h)});
    std::vector<Rgb3> P = medianCutW(smp, std::max(1, K));
    if (P.empty()) return;
    for (Shape& s : leaves) { int i = nearestIdx(P, s.r, s.g, s.b); s.r = P[size_t(i)][0]; s.g = P[size_t(i)][1]; s.b = P[size_t(i)][2]; }
}

// Sustituye el papel plano de un estilo de formas por una base de color y colorea la tinta.
// `shapes` trae tinta negra/blanca (z>=1) y opcionalmente el papel (z==0, a pantalla completa).
// - La tinta toma el tono local (versión oscura). Los trazos largos se parten si el tono cambia a lo largo.
// - La base es un mosaico BSP cuyo color compensa la cobertura REAL de tinta de cada zona.
inline void colorizeLightShapes(Result& res, const Image& src, const Options& o, int baseBudget) {
    if (res.rasterW <= 0 || res.rasterH <= 0) return;
    const bool inv = res.inkValue != 0;
    const int W = res.rasterW, H = res.rasterH;
    Image ras = resizeArea(src, W, H);
    Stats st; st.build(ras, o.alphaThreshold);
    std::vector<Shape> ink;
    for (const Shape& s : res.shapes) {
        bool paper = s.z == 0 && s.w >= W - 0.5f && s.h >= H - 0.5f;
        if (!paper) ink.push_back(s);
    }
    auto meanAt = [&](double cx, double cy, double rad, double& r, double& g, double& b) {
        int x0 = std::clamp(int(std::floor(cx - rad)), 0, ras.w - 1), x1 = std::clamp(int(std::ceil(cx + rad)), x0 + 1, ras.w);
        int y0 = std::clamp(int(std::floor(cy - rad)), 0, ras.h - 1), y1 = std::clamp(int(std::ceil(cy + rad)), y0 + 1, ras.h);
        auto q = st.query(x0, y0, x1, y1);
        if (q.n <= 0) return false;
        r = q.sr / q.n; g = q.sg / q.n; b = q.sb / q.n; return true;
    };

    // 1) clusters de tono a partir de la tinta y de la base
    std::vector<Shape> base = treeShapes(ras, std::max(1, baseBudget), o.alphaThreshold, true, nullptr);
    std::vector<WCol> samples;
    for (const Shape& s : ink) {
        double r, g, b; double rad = std::clamp(0.5 * std::min(s.w, s.h) + 1.5, 1.5, 6.0);
        if (meanAt(s.cx, s.cy, rad, r, g, b)) samples.push_back({float(r), float(g), float(b), std::max(1.f, s.w * s.h) * 2.f});
    }
    for (const Shape& s : base) samples.push_back({float(s.r), float(s.g), float(s.b), std::max(1.f, s.w * s.h)});
    const int Ktot = std::clamp(o.paletteSize, 4, std::max(4, o.maxChannels));
    const int Kc = std::max(2, (Ktot + 1) / 2), Kb = std::max(2, Ktot - Kc);
    std::vector<Rgb3> C = medianCutW(samples, Kc);
    if (C.empty()) C.push_back({128, 128, 128});
    auto labelAt = [&](double cx, double cy, double rad) {
        double r, g, b; if (!meanAt(cx, cy, rad, r, g, b)) return -1; return nearestIdx(C, r, g, b);
    };

    // 2) tinta: un tono por trazo; los trazos largos se parten donde cambia el tono
    long extra = long(o.budget) - long(ink.size()) - long(base.size());
    std::vector<Shape> outInk; std::vector<int> outLab;
    outInk.reserve(ink.size() + 64);
    for (const Shape& s : ink) {
        const bool alongX = s.w >= s.h;
        const double L = alongX ? s.w : s.h, th = alongX ? s.h : s.w;
        int lab0 = labelAt(s.cx, s.cy, std::clamp(0.5 * th + 1.5, 1.5, 6.0));
        if (lab0 < 0) lab0 = 0;
        if (extra > 0 && L >= 6.0 && L >= 2.5 * th) {
            const double rad = s.rot * kPiC / 180.0, dx = alongX ? std::cos(rad) : -std::sin(rad), dy = alongX ? std::sin(rad) : std::cos(rad);
            int n = std::clamp(int(L / 2.5), 2, 48);
            std::vector<int> lb(size_t(n), lab0);
            for (int j = 0; j < n; ++j) {
                double t = ((j + 0.5) / n - 0.5) * L;
                int l = labelAt(s.cx + dx * t, s.cy + dy * t, std::clamp(0.5 * th + 1.5, 1.5, 4.0));
                lb[size_t(j)] = l < 0 ? lab0 : l;
            }
            for (int j = 1; j + 1 < n; ++j) if (lb[size_t(j)] != lb[size_t(j - 1)] && lb[size_t(j - 1)] == lb[size_t(j + 1)]) lb[size_t(j)] = lb[size_t(j - 1)];   // quita parpadeos de 1 muestra
            std::vector<std::pair<int, int>> runs;                       // (inicio, fin) inclusivos de tonos iguales
            for (int j = 0; j < n; ++j) {
                if (!runs.empty() && lb[size_t(runs.back().first)] == lb[size_t(j)]) runs.back().second = j;
                else runs.push_back({j, j});
            }
            if (runs.size() > 1 && long(runs.size()) - 1 <= extra) {
                extra -= long(runs.size()) - 1;
                for (auto& rn : runs) {
                    Shape p = s;
                    double a0 = rn.first * L / n - 0.5 * L, a1 = (rn.second + 1) * L / n - 0.5 * L, len = a1 - a0, mid = 0.5 * (a0 + a1);
                    p.cx = float(s.cx + dx * mid); p.cy = float(s.cy + dy * mid);
                    if (alongX) p.w = float(len + 0.35); else p.h = float(len + 0.35);
                    outInk.push_back(p); outLab.push_back(lb[size_t(rn.first)]);
                }
                continue;
            }
            lab0 = lb[size_t(n / 2)];
        }
        outInk.push_back(s); outLab.push_back(lab0);
    }
    std::vector<Rgb3> inkCol(C.size());
    for (size_t i = 0; i < C.size(); ++i) inkCol[i] = inkOf(C[i], inv);
    for (size_t k = 0; k < outInk.size(); ++k) {
        Shape& s = outInk[k]; const Rgb3& c = inkCol[size_t(outLab[k])];
        s.r = c[0]; s.g = c[1]; s.b = c[2]; if (s.z < 1) s.z = 1;
    }

    // 3) cobertura real de tinta (a doble resolución para no perder las líneas finas)
    const int K2 = (W * H <= 250000) ? 2 : 1;
    Image cov = renderShapesScaled(outInk, W, H, K2);
    const int CW = cov.w, CH = cov.h;
    std::vector<double> sat(size_t(CW + 1) * (CH + 1), 0.0);
    for (int y = 0; y < CH; ++y) {
        double row = 0;
        for (int x = 0; x < CW; ++x) {
            row += cov.rgba[(size_t(y) * CW + x) * 4 + 3] > 0 ? 1.0 : 0.0;
            sat[size_t(y + 1) * (CW + 1) + (x + 1)] = sat[size_t(y) * (CW + 1) + (x + 1)] + row;
        }
    }
    auto coverage = [&](const Shape& l) {
        int x0 = std::clamp(int(std::floor((l.cx - l.w * 0.5) * K2)), 0, CW), x1 = std::clamp(int(std::ceil((l.cx + l.w * 0.5) * K2)), 0, CW);
        int y0 = std::clamp(int(std::floor((l.cy - l.h * 0.5) * K2)), 0, CH), y1 = std::clamp(int(std::ceil((l.cy + l.h * 0.5) * K2)), 0, CH);
        if (x1 <= x0 || y1 <= y0) return 0.0;
        size_t st2 = size_t(CW) + 1;
        double sum = sat[size_t(y1) * st2 + x1] - sat[size_t(y0) * st2 + x1] - sat[size_t(y1) * st2 + x0] + sat[size_t(y0) * st2 + x0];
        return sum / (double(x1 - x0) * (y1 - y0));
    };

    // 4) base: color objetivo compensado por la cobertura de tinta de cada hoja, reducido a Kb colores
    for (Shape& l : base) {
        int i = nearestIdx(C, l.r, l.g, l.b);
        double r, g, b; compensatedBase(l.r, l.g, l.b, inkCol[size_t(i)], coverage(l), r, g, b);
        l.r = uint8_t(r + 0.5); l.g = uint8_t(g + 0.5); l.b = uint8_t(b + 0.5); l.z = 0;
    }
    quantizeLeaves(base, Kb);

    res.shapes.clear();
    res.shapes.reserve(base.size() + outInk.size());
    for (auto& s : base) res.shapes.push_back(s);
    for (auto& s : outInk) res.shapes.push_back(s);
    res.colored = true;
}

} // namespace gdx
