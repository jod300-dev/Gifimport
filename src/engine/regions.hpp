// ============================================================================
//  5) Regiones por paleta + rectángulos voraces
//  6) Franjas horizontales (run-length) con tolerancia de color
//  Ambos buscan solos la mejor combinación resolución/colores/tolerancia que
//  cabe en el presupuesto de objetos y elige la de mayor PSNR.
// ============================================================================
#pragma once
#include <array>
#include <queue>
#include <unordered_map>
#include "partition.hpp"

namespace gdx {

// --------------------------------------------------- cuantización de color --

class Quantizer {
public:
    Quantizer(const Image& im, int alphaThr) : m_im(im), m_thr(alphaThr) {
        m_cnt.assign(32768, 0); m_sr.assign(32768, 0); m_sg.assign(32768, 0); m_sb.assign(32768, 0);
        std::unordered_map<uint32_t, int> uniq;
        bool tooMany = false;
        for (size_t i = 0; i < im.rgba.size(); i += 4) {
            if (im.rgba[i + 3] < alphaThr) continue;
            uint8_t r = im.rgba[i], g = im.rgba[i + 1], b = im.rgba[i + 2];
            int bin = binOf(r, g, b);
            ++m_cnt[bin]; m_sr[bin] += r; m_sg[bin] += g; m_sb[bin] += b;
            if (!tooMany) {
                uint32_t key = (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
                if (!uniq.count(key)) { int id = int(uniq.size()); uniq[key] = id; m_exactPal.push_back({r, g, b}); }
                if (uniq.size() > 256) { tooMany = true; m_exactPal.clear(); }
            }
        }
        for (int b = 0; b < 32768; ++b) if (m_cnt[b]) m_used.push_back(b);
        m_exact = !tooMany && !m_exactPal.empty();
        m_uniq = std::move(uniq);
    }
    bool isExact() const { return m_exact; }
    int exactColors() const { return m_exact ? int(m_exactPal.size()) : 0; }

    // Etiqueta cada píxel (−1 = transparente) y devuelve la paleta.
    void labels(int K, std::vector<int16_t>& lab, std::vector<std::array<uint8_t, 3>>& pal) const {
        lab.assign(size_t(m_im.w) * m_im.h, -1);
        pal.clear();
        if (m_used.empty()) return;
        if (m_exact && K >= int(m_exactPal.size())) {      // pixel art: paleta exacta, sin pérdida
            pal = m_exactPal;
            for (size_t p = 0, i = 0; p < lab.size(); ++p, i += 4) {
                if (m_im.rgba[i + 3] < m_thr) continue;
                uint32_t key = (uint32_t(m_im.rgba[i]) << 16) | (uint32_t(m_im.rgba[i + 1]) << 8) | m_im.rgba[i + 2];
                lab[p] = int16_t(m_uniq.at(key));
            }
            return;
        }
        // --- median cut sobre bins de 5 bits ---
        struct Box { std::vector<int> bins; double sse; };
        auto boxSse = [&](const std::vector<int>& bins) {
            double n = 0, r = 0, g = 0, b = 0, q = 0;
            for (int bi : bins) {
                double c = m_cnt[bi];
                n += c; r += m_sr[bi]; g += m_sg[bi]; b += m_sb[bi];
                double mr = m_sr[bi] / c, mg = m_sg[bi] / c, mb = m_sb[bi] / c;
                q += c * (mr * mr + mg * mg + mb * mb);
            }
            return n > 0 ? std::max(0.0, q - (r * r + g * g + b * b) / n) : 0.0;
        };
        std::vector<Box> boxes;
        boxes.push_back({m_used, boxSse(m_used)});
        K = std::max(1, K);
        while (int(boxes.size()) < K) {
            int bi = -1; double bs = 1e-9;
            for (int i = 0; i < int(boxes.size()); ++i)
                if (boxes[i].bins.size() > 1 && boxes[i].sse > bs) { bs = boxes[i].sse; bi = i; }
            if (bi < 0) break;
            std::vector<int> bins = std::move(boxes[bi].bins);
            double mn[3] = {1e9, 1e9, 1e9}, mx[3] = {-1e9, -1e9, -1e9};
            for (int b : bins) {
                double c = m_cnt[b], v[3] = {m_sr[b] / c, m_sg[b] / c, m_sb[b] / c};
                for (int k = 0; k < 3; ++k) { mn[k] = std::min(mn[k], v[k]); mx[k] = std::max(mx[k], v[k]); }
            }
            int ch = 0;
            for (int k = 1; k < 3; ++k) if (mx[k] - mn[k] > mx[ch] - mn[ch]) ch = k;
            auto val = [&](int b) { double c = m_cnt[b]; return (ch == 0 ? m_sr[b] : ch == 1 ? m_sg[b] : m_sb[b]) / c; };
            std::sort(bins.begin(), bins.end(), [&](int a, int b) { return val(a) < val(b); });
            double total = 0; for (int b : bins) total += m_cnt[b];
            double acc = 0; size_t cut = 1;
            for (size_t i = 0; i < bins.size(); ++i) { acc += m_cnt[bins[i]]; if (acc >= total * 0.5) { cut = i + 1; break; } }
            cut = std::clamp<size_t>(cut, 1, bins.size() - 1);
            std::vector<int> a(bins.begin(), bins.begin() + cut), b(bins.begin() + cut, bins.end());
            boxes[bi] = {a, boxSse(a)};
            boxes.push_back({b, boxSse(b)});
        }
        std::vector<int16_t> lut(32768, -1);
        for (size_t i = 0; i < boxes.size(); ++i) {
            double n = 0, r = 0, g = 0, b = 0;
            for (int bin : boxes[i].bins) { n += m_cnt[bin]; r += m_sr[bin]; g += m_sg[bin]; b += m_sb[bin]; lut[bin] = int16_t(i); }
            pal.push_back({uint8_t(std::clamp(r / n + 0.5, 0.0, 255.0)), uint8_t(std::clamp(g / n + 0.5, 0.0, 255.0)),
                           uint8_t(std::clamp(b / n + 0.5, 0.0, 255.0))});
        }
        for (size_t p = 0, i = 0; p < lab.size(); ++p, i += 4)
            if (m_im.rgba[i + 3] >= m_thr) lab[p] = lut[binOf(m_im.rgba[i], m_im.rgba[i + 1], m_im.rgba[i + 2])];
    }

private:
    static int binOf(int r, int g, int b) { return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3); }
    const Image& m_im; int m_thr;
    std::vector<uint32_t> m_cnt; std::vector<double> m_sr, m_sg, m_sb;
    std::vector<int> m_used;
    std::vector<std::array<uint8_t, 3>> m_exactPal;
    std::unordered_map<uint32_t, int> m_uniq;
    bool m_exact = false;
};

// ------------------------------------------------ rectángulos voraces -------
// Cubre las zonas de igual etiqueta con rectángulos grandes. Para cada semilla
// prueba "ancho primero" y "alto primero" y se queda con el de mayor área.

inline size_t greedyRects(const std::vector<int16_t>& lab, int W, int H,
                          const std::vector<std::array<uint8_t, 3>>& pal, size_t budget,
                          std::vector<Shape>* out) {
    std::vector<uint8_t> done(size_t(W) * H, 0);
    size_t count = 0;
    auto at = [&](int x, int y) { return size_t(y) * W + x; };
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            size_t p = at(x, y);
            if (done[p] || lab[p] < 0) continue;
            int16_t L = lab[p];
            auto same = [&](int xx, int yy) { size_t q = at(xx, yy); return !done[q] && lab[q] == L; };
            int w1 = 1; while (x + w1 < W && same(x + w1, y)) ++w1;
            int h1 = 1;
            for (; y + h1 < H; ++h1) { bool ok = true; for (int i = 0; i < w1 && ok; ++i) ok = same(x + i, y + h1); if (!ok) break; }
            int h2 = 1; while (y + h2 < H && same(x, y + h2)) ++h2;
            int w2 = 1;
            for (; x + w2 < W; ++w2) { bool ok = true; for (int j = 0; j < h2 && ok; ++j) ok = same(x + w2, y + j); if (!ok) break; }
            int rw = w1, rh = h1;
            if (w2 * h2 > w1 * h1) { rw = w2; rh = h2; }
            for (int j = 0; j < rh; ++j) for (int i = 0; i < rw; ++i) done[at(x + i, y + j)] = 1;
            if (++count > budget) return budget + 1;
            if (out) out->push_back(makeRectShape({x, y, x + rw, y + rh}, pal[L][0], pal[L][1], pal[L][2]));
        }
    return count;
}

// PSNR de un conjunto de formas (sin rotación) escaladas al raster de referencia.
inline double psnrScaled(const Image& reference, const std::vector<Shape>& shapes, int w, int h, int alphaThr) {
    float sx = float(reference.w) / w, sy = float(reference.h) / h;
    std::vector<Shape> sc = shapes;
    for (auto& s : sc) { s.cx *= sx; s.cy *= sy; s.w *= sx; s.h *= sy; }
    return computePsnr(reference, renderShapes(sc, reference.w, reference.h), alphaThr);
}

struct Candidate { std::vector<Shape> shapes; int w = 0, h = 0; double psnr = -1; };

// ----------------------------------------------------------- 5) Regiones ----

inline Candidate regionsBest(const Image& src, const Options& o, Progress* prog) {
    int Wb, Hb; fitSize(src.w, src.h, std::min(o.maxRaster, 512), Wb, Hb);
    Image reference = resizeArea(src, Wb, Hb);
    const double scales[] = {1.0, 0.78, 0.6, 0.46, 0.35, 0.27, 0.2, 0.14, 0.1};
    Candidate best;
    int ns = int(sizeof(scales) / sizeof(scales[0]));
    for (int si = 0; si < ns; ++si) {
        if (isCancelled(prog)) break;
        int w = std::max(1, int(std::lround(Wb * scales[si]))), h = std::max(1, int(std::lround(Hb * scales[si])));
        if (si > 0 && w == best.w && h == best.h) continue;
        Image ras = (w == Wb && h == Hb) ? reference : resizeArea(src, w, h);
        Quantizer qz(ras, o.alphaThreshold);
        std::vector<int16_t> lab; std::vector<std::array<uint8_t, 3>> pal;
        auto count = [&](int K) { qz.labels(K, lab, pal); return greedyRects(lab, w, h, pal, size_t(o.budget), nullptr); };
        int Kmax = 256;
        if (qz.isExact()) Kmax = std::max(2, qz.exactColors());
        int K;
        if (count(Kmax) <= size_t(o.budget)) K = Kmax;
        else if (count(2) > size_t(o.budget)) { report(prog, float(si + 1) / ns); continue; }
        else {
            int lo = 2, hi = Kmax;                                  // lo cabe, hi no
            while (hi - lo > 1) { int mid = (lo + hi) / 2; if (count(mid) <= size_t(o.budget)) lo = mid; else hi = mid; }
            K = lo;
        }
        qz.labels(K, lab, pal);
        std::vector<Shape> shapes;
        greedyRects(lab, w, h, pal, size_t(o.budget), &shapes);
        double ps = psnrScaled(reference, shapes, w, h, o.alphaThreshold);
        if (ps > best.psnr) { best.shapes = std::move(shapes); best.w = w; best.h = h; best.psnr = ps; }
        report(prog, float(si + 1) / ns);
        if (qz.isExact() && K == Kmax && si == 0) break;           // sin pérdida: no hay nada mejor
    }
    if (best.shapes.empty() && best.w == 0) {                       // último recurso: cuadrícula
        int gw, gh; best.shapes = gridShapes(src, o.budget, o.alphaThreshold, gw, gh); best.w = gw; best.h = gh;
    }
    return best;
}

// ------------------------------------------------------------ 6) Franjas ----

inline size_t runRows(const Image& im, int tol, int alphaThr, size_t budget, std::vector<Shape>* out) {
    size_t count = 0;
    for (int y = 0; y < im.h; ++y) {
        int x = 0;
        while (x < im.w) {
            const uint8_t* p = im.at(x, y);
            if (p[3] < alphaThr) { ++x; continue; }
            double sr = p[0], sg = p[1], sb = p[2]; int n = 1, x2 = x + 1;
            while (x2 < im.w) {
                const uint8_t* q = im.at(x2, y);
                if (q[3] < alphaThr) break;
                double mr = sr / n, mg = sg / n, mb = sb / n;
                if (std::abs(q[0] - mr) > tol || std::abs(q[1] - mg) > tol || std::abs(q[2] - mb) > tol) break;
                sr += q[0]; sg += q[1]; sb += q[2]; ++n; ++x2;
            }
            if (++count > budget) return budget + 1;
            if (out) out->push_back(makeRectShape({x, y, x2, y + 1},
                uint8_t(std::clamp(sr / n + 0.5, 0.0, 255.0)), uint8_t(std::clamp(sg / n + 0.5, 0.0, 255.0)),
                uint8_t(std::clamp(sb / n + 0.5, 0.0, 255.0))));
            x = x2;
        }
    }
    return count;
}

inline Candidate runsBestOne(const Image& src, const Options& o, Progress* prog) {
    int Wb, Hb; fitSize(src.w, src.h, std::min(o.maxRaster, 640), Wb, Hb);
    Image reference = resizeArea(src, Wb, Hb);
    const double scales[] = {1.0, 0.8, 0.65, 0.5, 0.4, 0.3, 0.22, 0.15, 0.1};
    Candidate best;
    int ns = int(sizeof(scales) / sizeof(scales[0]));
    for (int si = 0; si < ns; ++si) {
        if (isCancelled(prog)) break;
        int R = std::max(1, int(std::lround(Hb * scales[si])));
        if (si > 0 && R == best.h) continue;
        Image ras = (R == Hb) ? reference : resizeArea(src, Wb, R);
        size_t B = size_t(o.budget);
        int tol;
        if (runRows(ras, 0, o.alphaThreshold, B, nullptr) <= B) tol = 0;
        else if (runRows(ras, 160, o.alphaThreshold, B, nullptr) > B) { report(prog, float(si + 1) / ns); continue; }
        else {
            int lo = 0, hi = 160;                                   // lo no cabe, hi cabe
            while (hi - lo > 1) { int mid = (lo + hi) / 2; if (runRows(ras, mid, o.alphaThreshold, B, nullptr) <= B) hi = mid; else lo = mid; }
            tol = hi;
        }
        std::vector<Shape> shapes;
        runRows(ras, tol, o.alphaThreshold, B, &shapes);
        double ps = psnrScaled(reference, shapes, Wb, R, o.alphaThreshold);
        if (ps > best.psnr) { best.shapes = std::move(shapes); best.w = Wb; best.h = R; best.psnr = ps; }
        report(prog, float(si + 1) / ns);
    }
    if (best.w == 0) { int gw, gh; best.shapes = gridShapes(src, o.budget, o.alphaThreshold, gw, gh); best.w = gw; best.h = gh; }
    return best;
}

inline Image transposed(const Image& s) {
    Image t; t.w = s.h; t.h = s.w; t.rgba.resize(s.rgba.size());
    for (int y = 0; y < s.h; ++y) for (int x = 0; x < s.w; ++x)
        std::copy(s.at(x, y), s.at(x, y) + 4, t.at(y, x));
    return t;
}

// Franjas: prueba en filas y en columnas (imagen traspuesta) y se queda con la dirección que comprime mejor.
inline Candidate runsBest(const Image& src, const Options& o, Progress* prog) {
    Candidate a = runsBestOne(src, o, prog);
    if (isCancelled(prog)) return a;
    Candidate b = runsBestOne(transposed(src), o, prog);
    if (b.psnr > a.psnr + 0.05 && !b.shapes.empty()) {
        for (auto& s : b.shapes) { std::swap(s.cx, s.cy); std::swap(s.w, s.h); }
        std::swap(b.w, b.h);
        return b;
    }
    return a;
}

} // namespace gdx
