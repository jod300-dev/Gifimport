// ============================================================================
//  7) Geometrize: rectángulos rotados colocados por búsqueda aleatoria +
//     escalada de colina, guiada por el mapa de error (estilo "primitive").
//  8) Híbrido: base BSP + refinado con rectángulos rotados en los residuos.
// ============================================================================
#pragma once
#include "partition.hpp"

namespace gdx {

struct Cand { float cx, cy, w, h, rot; };

class Geo {
public:
    // target: imagen opaca (RGB) del raster de trabajo
    Geo(const Image& target, uint32_t seed) : W(target.w), H(target.h), rng(seed) {
        px.resize(size_t(W) * H);
        canvas.assign(size_t(W) * H * 3, 0);
        for (int i = 0; i < W * H; ++i) {
            px[i].r = target.rgba[size_t(i) * 4]; px[i].g = target.rgba[size_t(i) * 4 + 1]; px[i].b = target.rgba[size_t(i) * 4 + 2];
            px[i].tt = px[i].r * px[i].r + px[i].g * px[i].g + px[i].b * px[i].b;
        }
        tx = (W + kTile - 1) / kTile; ty = (H + kTile - 1) / kTile;
        tileErr.assign(size_t(tx) * ty, 0.0);
        L = float(std::max(W, H));
    }

    // GIF con transparencia: los rectangulos solo pueden cubrir pixeles OPACOS (valid[i] = 1). Los transparentes no cuentan como error.
    void setValid(const std::vector<uint8_t>& valid) {
        hasMask = true;
        invPrefix.assign(size_t(W + 1) * H, 0);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                invPrefix[size_t(y) * (W + 1) + x + 1] = invPrefix[size_t(y) * (W + 1) + x] + (valid[size_t(y) * W + x] ? 0 : 1);
        mask = valid;
    }

    // Lienzo inicial (color plano o imagen ya dibujada).
    void setCanvasFlat(uint8_t r, uint8_t g, uint8_t b) {
        for (int i = 0; i < W * H; ++i) { canvas[i * 3] = r; canvas[i * 3 + 1] = g; canvas[i * 3 + 2] = b; }
        recomputeAll();
    }
    void setCanvasImage(const Image& c, uint8_t bgR, uint8_t bgG, uint8_t bgB) {
        for (int i = 0; i < W * H; ++i) {
            bool has = c.rgba[size_t(i) * 4 + 3] > 0;
            canvas[i * 3]     = has ? c.rgba[size_t(i) * 4]     : bgR;
            canvas[i * 3 + 1] = has ? c.rgba[size_t(i) * 4 + 1] : bgG;
            canvas[i * 3 + 2] = has ? c.rgba[size_t(i) * 4 + 2] : bgB;
        }
        recomputeAll();
    }

    // Añade `count` formas; z de la primera = z0. Devuelve las formas creadas.
    std::vector<Shape> run(int count, int z0, int quality, Progress* prog, float progBase, float progSpan) {
        std::vector<Shape> out;
        out.reserve(size_t(count));
        static const int kM[4] = {16, 32, 64, 128}, kK[4] = {32, 64, 128, 256};
        const int M = kM[std::clamp(quality, 0, 3)];                    // candidatos aleatorios por forma
        const int K = kK[std::clamp(quality, 0, 3)];                    // pasos de escalada por forma
        std::vector<double> prefix(tileErr.size() + 1, 0.0);
        for (int i = 0; i < count; ++i) {
            if ((i & 63) == 0) {
                report(prog, progBase + progSpan * float(i) / std::max(1, count));
                if (isCancelled(prog)) break;
            }
            // distribución de probabilidad por tiles (más error => más probable)
            for (size_t t = 0; t < tileErr.size(); ++t) prefix[t + 1] = prefix[t] + tileErr[t] + 1e-3;
            float frac = float(i) / std::max(1, count);
            float maxS = std::max(2.0f, L * 0.6f * std::pow(0.012f, frac));
            float minS = std::max(0.75f, maxS * 0.1f);

            Cand best{}; Ev bestEv; bestEv.gain = -1e300;
            for (int m = 0; m < M; ++m) {
                Cand c = randomCand(prefix, minS, maxS);
                Ev e = eval(c);
                if (e.gain > bestEv.gain) { bestEv = e; best = c; }
            }
            if (bestEv.gain <= -1e299) continue;
            for (int k = 0; k < K; ++k) {
                float amp = std::max(0.5f, maxS * 0.35f * (1.0f - float(k) / K));
                Cand c = mutate(best, amp);
                Ev e = eval(c);
                if (e.gain > bestEv.gain) { bestEv = e; best = c; }
            }
            if (bestEv.gain <= 0) continue;                                 // no mejora: no gastar objeto
            uint8_t R = uint8_t(std::clamp(bestEv.mr + 0.5, 0.0, 255.0)), G = uint8_t(std::clamp(bestEv.mg + 0.5, 0.0, 255.0)),
                    B = uint8_t(std::clamp(bestEv.mb + 0.5, 0.0, 255.0));
            commit(best, R, G, B);
            Shape s; s.cx = best.cx; s.cy = best.cy; s.w = best.w; s.h = best.h; s.rot = best.rot;
            s.r = R; s.g = G; s.b = B; s.z = int16_t(std::min(32000, z0 + int(out.size())));
            out.push_back(s);
        }
        return out;
    }

private:
    static constexpr int kTile = 8;
    struct Px { int32_t r, g, b, tt, e; };
    struct Ev { double gain = -1e300, mr = 0, mg = 0, mb = 0; };
    int W, H; float L;
    Rng rng;
    std::vector<Px> px;
    std::vector<uint8_t> canvas;
    int tx = 0, ty = 0;
    std::vector<double> tileErr;
    bool hasMask = false;
    std::vector<uint8_t> mask;                 // 1 = pixel opaco
    std::vector<int32_t> invPrefix;            // por fila: cuantos pixeles transparentes hay antes de x

    void recomputeAll() {
        std::fill(tileErr.begin(), tileErr.end(), 0.0);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                Px& p = px[size_t(y) * W + x];
                int dr = p.r - canvas[(size_t(y) * W + x) * 3], dg = p.g - canvas[(size_t(y) * W + x) * 3 + 1], db = p.b - canvas[(size_t(y) * W + x) * 3 + 2];
                p.e = (hasMask && !mask[size_t(y) * W + x]) ? 0 : dr * dr + dg * dg + db * db;
                tileErr[size_t(y / kTile) * tx + x / kTile] += p.e;
            }
    }

    template <class F>
    void forSpans(const Cand& c, F&& f) const {
        const double rad = c.rot * 3.14159265358979323846 / 180.0;
        const double cs = std::cos(rad), sn = std::sin(rad), hx = c.w * 0.5, hy = c.h * 0.5;
        const double ey = std::abs(hx * sn) + std::abs(hy * cs);
        int y0 = std::max(0, int(std::ceil(c.cy - ey - 0.5))), y1 = std::min(H - 1, int(std::floor(c.cy + ey - 0.5)));
        for (int y = y0; y <= y1; ++y) {
            double dy = y + 0.5 - c.cy, xmin = -1e18, xmax = 1e18;
            auto cons = [&](double a, double b, double hh) {        // |a*dx + b| <= hh
                if (std::abs(a) < 1e-9) { if (std::abs(b) > hh) { xmin = 1; xmax = 0; } return; }
                double l = (-hh - b) / a, r = (hh - b) / a;
                if (l > r) std::swap(l, r);
                xmin = std::max(xmin, l); xmax = std::min(xmax, r);
            };
            cons(cs, dy * sn, hx);
            cons(-sn, dy * cs, hy);
            if (xmin > xmax) continue;
            int xs = std::max(0, int(std::ceil(c.cx + xmin - 0.5))), xe = std::min(W - 1, int(std::floor(c.cx + xmax - 0.5)));
            if (xs <= xe) f(y, xs, xe);
        }
    }

    // Una forma girada NUNCA debe sobresalir del lienzo (en GD no se recorta: se veria como una astilla fuera de la imagen).
    bool fits(const Cand& c) const {
        const double rad = c.rot * 3.14159265358979323846 / 180.0, cs = std::cos(rad), sn = std::sin(rad), hx = c.w * 0.5, hy = c.h * 0.5;
        const double ex = std::abs(hx * cs) + std::abs(hy * sn), ey = std::abs(hx * sn) + std::abs(hy * cs);
        return c.cx - ex >= -0.25 && c.cx + ex <= W + 0.25 && c.cy - ey >= -0.25 && c.cy + ey <= H + 0.25;
    }
    // Las alineadas a ejes (0/90 grados) SI pueden llegar al borde: se recortan al lienzo.
    void clipAxis(Cand& c) const {
        double a = std::fmod(std::abs(double(c.rot)), 90.0);
        if (!(a < 0.05 || a > 89.95)) return;
        const bool swap = std::fmod(std::abs(double(c.rot)), 180.0) > 45.0 && std::fmod(std::abs(double(c.rot)), 180.0) < 135.0;
        float ex = (swap ? c.h : c.w) * 0.5f, ey = (swap ? c.w : c.h) * 0.5f;
        float x0 = std::max(0.f, c.cx - ex), x1 = std::min(float(W), c.cx + ex), y0 = std::max(0.f, c.cy - ey), y1 = std::min(float(H), c.cy + ey);
        if (x1 - x0 < 0.5f || y1 - y0 < 0.5f) return;
        c.cx = (x0 + x1) * 0.5f; c.cy = (y0 + y1) * 0.5f;
        (swap ? c.h : c.w) = x1 - x0; (swap ? c.w : c.h) = y1 - y0;
    }
    Ev eval(const Cand& c) const {
        if (!fits(c)) return Ev{};
        double n = 0, sr = 0, sg = 0, sb = 0, stt = 0, se = 0;
        bool touchesHole = false;
        forSpans(c, [&](int y, int xs, int xe) {
            if (hasMask && invPrefix[size_t(y) * (W + 1) + xe + 1] - invPrefix[size_t(y) * (W + 1) + xs] > 0) { touchesHole = true; return; }
            const Px* p = &px[size_t(y) * W + xs];
            for (int x = xs; x <= xe; ++x, ++p) { sr += p->r; sg += p->g; sb += p->b; stt += p->tt; se += p->e; }
            n += xe - xs + 1;
        });
        Ev e;
        if (touchesHole || n < 1) return e;       // nunca sobre transparente
        e.mr = sr / n; e.mg = sg / n; e.mb = sb / n;
        e.gain = se - (stt - (sr * sr + sg * sg + sb * sb) / n);
        return e;
    }

    void commit(const Cand& c, uint8_t R, uint8_t G, uint8_t B) {
        int bx0 = W, bx1 = -1, by0 = H, by1 = -1;
        forSpans(c, [&](int y, int xs, int xe) {
            for (int x = xs; x <= xe; ++x) {
                size_t i = size_t(y) * W + x;
                canvas[i * 3] = R; canvas[i * 3 + 1] = G; canvas[i * 3 + 2] = B;
                int dr = px[i].r - R, dg = px[i].g - G, db = px[i].b - B;
                px[i].e = (hasMask && !mask[i]) ? 0 : dr * dr + dg * dg + db * db;
            }
            bx0 = std::min(bx0, xs); bx1 = std::max(bx1, xe); by0 = std::min(by0, y); by1 = std::max(by1, y);
        });
        if (bx1 < 0) return;
        for (int ty0 = by0 / kTile; ty0 <= by1 / kTile; ++ty0)
            for (int tx0 = bx0 / kTile; tx0 <= bx1 / kTile; ++tx0) {
                double s = 0;
                for (int y = ty0 * kTile; y < std::min(H, (ty0 + 1) * kTile); ++y)
                    for (int x = tx0 * kTile; x < std::min(W, (tx0 + 1) * kTile); ++x) s += px[size_t(y) * W + x].e;
                tileErr[size_t(ty0) * tx + tx0] = s;
            }
    }

    Cand randomCand(const std::vector<double>& prefix, float minS, float maxS) {
        double r = rng.f() * prefix.back();
        size_t lo = 0, hi = tileErr.size();
        while (lo + 1 < hi) { size_t mid = (lo + hi) / 2; if (prefix[mid] <= r) lo = mid; else hi = mid; }
        int tcx = int(lo % tx), tcy = int(lo / tx);
        Cand c;
        c.cx = (tcx + rng.f()) * kTile; c.cy = (tcy + rng.f()) * kTile;
        c.cx = std::min(c.cx, float(W)); c.cy = std::min(c.cy, float(H));
        float lmin = std::log(minS), lmax = std::log(maxS);
        c.w = std::exp(rng.range(lmin, lmax)); c.h = std::exp(rng.range(lmin, lmax));
        c.rot = rng.range(0.f, 180.f);
        if (rng.f() < 0.2f) c.rot = 0.f;                                  // algunas alineadas a ejes: pueden cubrir hasta el borde
        clipAxis(c);
        return c;
    }

    Cand mutate(const Cand& in, float amp) {
        Cand c = in;
        switch (rng.below(4)) {
            case 0: c.cx += rng.gauss() * amp; c.cy += rng.gauss() * amp; break;
            case 1: c.w = std::max(0.75f, c.w * std::exp(rng.gauss() * 0.25f)); break;
            case 2: c.h = std::max(0.75f, c.h * std::exp(rng.gauss() * 0.25f)); break;
            default: c.rot += rng.gauss() * 18.f; break;
        }
        c.cx = std::clamp(c.cx, 0.f, float(W)); c.cy = std::clamp(c.cy, 0.f, float(H));
        c.w = std::min(c.w, 2.f * L); c.h = std::min(c.h, 2.f * L);
        clipAxis(c);
        return c;
    }
};

// Compone la imagen sobre su color medio opaco (la transparencia exacta no es
// representable con formas libres que se solapan).
inline Image compositeOnMean(const Image& src, int alphaThr, uint8_t& mr, uint8_t& mg, uint8_t& mb) {
    double r = 0, g = 0, b = 0; size_t n = 0;
    for (size_t i = 0; i < src.rgba.size(); i += 4)
        if (src.rgba[i + 3] >= alphaThr) { r += src.rgba[i]; g += src.rgba[i + 1]; b += src.rgba[i + 2]; ++n; }
    if (n) { r /= n; g /= n; b /= n; }
    mr = uint8_t(r + 0.5); mg = uint8_t(g + 0.5); mb = uint8_t(b + 0.5);
    Image o = src;
    for (size_t i = 0; i < o.rgba.size(); i += 4)
        if (o.rgba[i + 3] < alphaThr) { o.rgba[i] = mr; o.rgba[i + 1] = mg; o.rgba[i + 2] = mb; }
    for (size_t i = 3; i < o.rgba.size(); i += 4) o.rgba[i] = 255;
    return o;
}

// Tamaño de raster para Geometrize/Híbrido. El híbrido trabaja a mayor
// resolución porque su base BSP es barata; el puro necesita menos píxeles por objeto.
inline void geoRasterSize(const Image& src, const Options& o, bool hybrid, int& W, int& H) {
    int cap = std::min(o.maxRaster, hybrid ? 512 : 384);
    if (!hybrid) {
        static const double kPix[4] = {8.0, 12.0, 18.0, 22.0};
        double k = kPix[std::clamp(o.quality, 0, 3)];                      // píxeles por objeto
        double a = double(src.w) / src.h;
        double pixels = std::max(1024.0, double(o.budget) * k);
        int w = int(std::lround(std::sqrt(pixels * a))), h = int(std::lround(w / a));
        int side = std::max(w, h);
        if (side > cap) { double s = double(cap) / side; w = int(std::lround(w * s)); h = int(std::lround(h * s)); }
        W = std::clamp(w, 1, src.w); H = std::clamp(h, 1, src.h);
    } else {
        fitSize(src.w, src.h, cap, W, H);
    }
}

// Calidad efectiva: con pocos objetos sobra tiempo, así que se sube un nivel de búsqueda de forma automática.
inline int effectiveQuality(const Options& o) {
    int q = std::clamp(o.quality, 0, 3);
    if (o.budget <= 3000 && q < 3) ++q;
    return q;
}

// Fracción del presupuesto que va a la base BSP del híbrido. Medido en fotos y pinturas a 1.000, 3.000 y 10.000
// objetos: entre el 10 % y el 35 % casi no cambia el resultado (diferencias de ~0,1 dB); con más base empeora.
inline double hybridFractionFor(const Options& o) {
    return o.hybridFraction > 0 ? double(o.hybridFraction) : 0.30;
}

// hybridBase = 0 -> Geometrize (con una base BSP gruesa del 6 %); > 0 -> fracción del presupuesto para la base BSP
// Tras generar las formas se recolorean con lo que realmente queda visible, se eliminan las que otras
// tapan por completo y ese presupuesto se reinvierte en más formas (hasta 3 rondas).
inline std::vector<Shape> geometrizeShapes(const Image& src, const Options& o, double hybridBase,
                                           Progress* prog, int& outW, int& outH, Image* targetOut = nullptr) {
    const bool hybrid = hybridBase > 0.0;
    int W, H; geoRasterSize(src, o, true, W, H);          // misma resolución (hasta 512) que el híbrido: con menos se pierde detalle
    Image ras = resizeArea(src, W, H);
    uint8_t mr, mg, mb;
    Image target = compositeOnMean(ras, o.alphaThreshold, mr, mg, mb);
    outW = W; outH = H;
    if (targetOut) *targetOut = hasTransparency(ras, o.alphaThreshold) ? ras : target;   // con alfa, el PSNR se mide contra el raster con huecos
    const int budget = std::max(1, o.budget);
    const int quality = effectiveQuality(o);

    // Hibrido: si el BSP con todo el presupuesto ya es casi perfecto (logos, colores planos), no hace falta mas.
    // Umbral alto (45 dB): por debajo se hace el hibrido de verdad, para que 7 y 8 no acaben siendo el mismo resultado.
    if (hybrid) {
        std::vector<Shape> full = treeShapes(ras, budget, o.alphaThreshold, true, nullptr);
        Image r = renderShapes(full, W, H);
        if (!hasTransparency(ras, o.alphaThreshold) && computePsnr(target, r, 1) >= 45.0 && !isCancelled(prog)) { for (auto& s : full) s.z = 0; report(prog, 1.f); return full; }
    }

    std::vector<Shape> shapes;
    Geo geo(target, o.seed);
    const bool alphaImg = hasTransparency(ras, o.alphaThreshold);
    if (alphaImg) {                                              // los rectangulos solo dentro de la zona opaca
        std::vector<uint8_t> valid(size_t(W) * H, 0);
        for (size_t i = 0; i < valid.size(); ++i) valid[i] = ras.rgba[i * 4 + 3] >= o.alphaThreshold ? 1 : 0;
        geo.setValid(valid);
    }
    int z0 = 1;
    {
        if (hybrid) {
            // 8. HIBRIDO: base BSP (una parte GRANDE del presupuesto, se ve la estructura de bloques) + rotados sobre el residuo.
            int b1 = std::max(1, int(budget * hybridBase));
            shapes = treeShapes(ras, b1, o.alphaThreshold, true, nullptr);
            for (auto& s : shapes) s.z = 0;
            Image base = renderShapes(shapes, W, H);
            geo.setCanvasImage(base, mr, mg, mb);
        } else if (alphaImg) {
            // 7 con transparencia: no cabe un fondo plano (taparia lo transparente). Base BSP (45 %): cubre el contorno opaco, donde un rectangulo girado no cabe sin tocar lo transparente.
            shapes = treeShapes(ras, std::max(1, int(budget * 0.45)), o.alphaThreshold, true, nullptr);
            for (auto& s : shapes) s.z = 0;
            Image base = renderShapes(shapes, W, H);
            geo.setCanvasImage(base, mr, mg, mb);
        } else {
            // 7. GEOMETRIZE PURO (estilo Primitive): NINGUNA base BSP. Un unico rectangulo de fondo con el color medio y despues
            //    solo rectangulos girados colocados por busqueda aleatoria. Aspecto pictorico, sin cuadricula.
            Shape bg; bg.cx = W * 0.5f; bg.cy = H * 0.5f; bg.w = float(W); bg.h = float(H); bg.rot = 0;
            bg.r = mr; bg.g = mg; bg.b = mb; bg.z = 0;
            shapes.push_back(bg);
            geo.setCanvasFlat(mr, mg, mb);
        }
        report(prog, 0.05f);
        auto extra = geo.run(budget - int(shapes.size()), z0, quality, prog, 0.05f, 0.55f);
        shapes.insert(shapes.end(), extra.begin(), extra.end());
    }
    for (int round = 0; round < 3 && !isCancelled(prog); ++round) {
        refitVisible(shapes, target, 255, true);                 // recolorea y quita lo tapado
        int freed = budget - int(shapes.size());
        if (freed < std::max(6, budget / 150)) break;
        int zNext = 1; for (auto const& s : shapes) zNext = std::max<int>(zNext, s.z + 1);
        if (zNext > 31000) break;
        Image canvas = renderShapes(shapes, W, H);
        geo.setCanvasImage(canvas, mr, mg, mb);
        auto extra = geo.run(freed, zNext, quality, prog, 0.65f + 0.1f * round, 0.1f);
        shapes.insert(shapes.end(), extra.begin(), extra.end());
    }
    refitVisible(shapes, target, 255, true);
    if (int(shapes.size()) > budget) shapes.resize(size_t(budget));
    // Con transparencia los girados casi nunca caben (no pueden tocar lo transparente) y el presupuesto se queda sin gastar:
    // se compara con un BSP de presupuesto completo y se devuelve el mejor (medido sobre el raster con huecos).
    if (alphaImg && !isCancelled(prog) && int(shapes.size()) < budget) {
        std::vector<Shape> full = treeShapes(ras, budget, o.alphaThreshold, true, nullptr);
        for (auto& s2 : full) s2.z = 0;
        const double pa = computePsnr(ras, renderShapes(shapes, W, H), o.alphaThreshold);
        const double pb = computePsnr(ras, renderShapes(full, W, H), o.alphaThreshold);
        if (pb > pa) shapes = std::move(full);
    }
    return shapes;
}

} // namespace gdx
