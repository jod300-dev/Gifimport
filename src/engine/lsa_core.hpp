// ============================================================================
//  LSA: contexto de calculo, optimizador de formas, motor de variantes por region y cierre (metodos 20-29)
// ============================================================================
#pragma once
#include "lsa_export.hpp"
#include <cstring>
#include <queue>

namespace gdx {
namespace lsa {

struct Ctx {
    int N = 0, W = 0, H = 0, thr = 128, tol = 28, threads = 1, quality = 1;
    Options base; LayeredOptions lo; Progress* prog = nullptr;
    std::vector<Image> orig, fr, sm;     // orig = frame al raster de trabajo; fr = sin dither (mediana 3x3); sm = suavizado (para comparar)
    bool opaqueAll = true; double contentArea = 1; long total = 0;
    uint32_t seed = 1337;
};

inline Ctx makeCtx(const GifData& g, const GifPlan& plan, const Options& base, const LayeredOptions& lo, Progress* prog, int threads) {
    Ctx c; c.N = plan.count(); c.base = base; c.lo = lo; c.prog = prog; c.threads = std::max(1, threads);
    c.thr = std::clamp(base.alphaThreshold, 1, 255); c.tol = std::clamp(lo.tol, 4, 120);
    c.quality = std::clamp(std::max(base.quality, 1), 0, 3); c.seed = base.seed;
    int mr = std::clamp(base.maxRaster, 64, 1024), W, H;
    fitSize(g.w, g.h, mr, W, H);
    while (int64_t(W) * H * c.N > 9000000LL && mr > 64) { mr = int(mr * 0.88); fitSize(g.w, g.h, mr, W, H); }
    c.W = W; c.H = H;
    c.orig.resize(size_t(c.N)); c.fr.resize(size_t(c.N)); c.sm.resize(size_t(c.N));
    lay::parallelFor(c.N, c.threads, [&](int i) {
        Image im = resizeArea(g.frames[size_t(plan.idx[size_t(i)])], W, H);
        if (base.grayscale) im = toGrayscale(im);
        c.fr[size_t(i)] = lo.denoise ? lay::median3(im, c.thr) : im;
        c.orig[size_t(i)] = std::move(im);
    });
    lay::parallelFor(c.N, c.threads, [&](int i) { c.sm[size_t(i)] = lay::blur3(c.fr[size_t(i)], c.thr); });
    for (int i = 0; i < c.N; ++i) {
        size_t oc = lay::opaqueCount(c.orig[size_t(i)], c.thr);
        if (oc != size_t(W) * H) c.opaqueAll = false;
        c.contentArea = std::max(c.contentArea, double(oc));
    }
    c.total = long(std::max(1, lo.perFrame)) * c.N;
    return c;
}

inline Image cropImage(const Image& s, int x0, int y0, int x1, int y1) {
    Image o; o.w = std::max(0, x1 - x0); o.h = std::max(0, y1 - y0); o.rgba.assign(size_t(o.w) * o.h * 4, 0);
    for (int y = 0; y < o.h; ++y) std::memcpy(&o.rgba[size_t(y) * o.w * 4], s.at(x0, y0 + y), size_t(o.w) * 4);
    return o;
}

// Error cuadratico medio de unas formas dibujadas sobre `canvas` (o sobre nada) en los pixeles opacos de layerImg, frente al objetivo.
inline double layerMse(const Image& target, const Image& layerImg, const Image* canvas, const std::vector<Shape>& shapes, int thr) {
    Image r = renderShapes(shapes, layerImg.w, layerImg.h);
    double acc = 0; size_t n = 0;
    for (size_t p = 0; p < size_t(layerImg.w) * layerImg.h; ++p) {
        if (layerImg.rgba[p * 4 + 3] < thr) continue;
        const uint8_t* t = &target.rgba[p * 4]; const uint8_t* q = nullptr;
        if (r.rgba[p * 4 + 3] > 0) q = &r.rgba[p * 4]; else if (canvas && canvas->rgba[p * 4 + 3] > 0) q = &canvas->rgba[p * 4];
        ++n;
        if (!q) { acc += 3.0 * 255.0 * 255.0 * 0.25; continue; }
        const double d0 = double(t[0]) - q[0], d1 = double(t[1]) - q[1], d2 = double(t[2]) - q[2]; acc += d0 * d0 + d1 * d1 + d2 * d2;
    }
    return n ? acc / (3.0 * double(n)) : 0.0;
}

// Traza una capa con el mejor trazador: BSP puro y BSP+pintor (si el GIF es opaco) y se queda con el de menor error (contenido plano: gana BSP).
inline Result fitImage(const Ctx& c, const Image& layerImg, const Image& target, const Image* baseCanvas, int budget, uint32_t seed) {
    Result r; r.rasterW = layerImg.w; r.rasterH = layerImg.h; r.aspect = layerImg.h > 0 ? float(layerImg.w) / layerImg.h : 1.f;
    if (budget < 1 || layerImg.w <= 0 || layerImg.h <= 0) return r;
    Result bsp = lay::traceLayer(layerImg, budget, c.base, 0);
    if (c.opaqueAll && c.lo.painter != 0 && budget >= 6 && target.w == layerImg.w && target.h == layerImg.h) {
        Result hy = lay::traceHybrid(layerImg, target, baseCanvas, budget, c.base, std::clamp<double>(c.lo.bspFrac, 0.05, 1.0), c.quality, seed);
        if (bsp.shapes.empty()) return hy;
        const double eb = layerMse(target, layerImg, baseCanvas, bsp.shapes, c.thr), eh = layerMse(target, layerImg, baseCanvas, hy.shapes, c.thr);
        return eh <= eb ? hy : bsp;
    }
    return bsp;
}

// Complejidad (0..2) de una region: gradiente medio. Reparte presupuesto: mas objetos donde hay mas detalle.
inline double complexityOf(const Image& im, const std::vector<uint8_t>* mask /*bbox-local o null*/) {
    double acc = 0; size_t n = 0;
    for (int y = 0; y + 1 < im.h; ++y) for (int x = 0; x + 1 < im.w; ++x) {
        if (mask && !(*mask)[size_t(y) * im.w + x]) continue;
        const uint8_t* a = im.at(x, y); const uint8_t* b = im.at(x + 1, y); const uint8_t* d = im.at(x, y + 1);
        int gx = std::max({std::abs(a[0] - b[0]), std::abs(a[1] - b[1]), std::abs(a[2] - b[2])});
        int gy = std::max({std::abs(a[0] - d[0]), std::abs(a[1] - d[1]), std::abs(a[2] - d[2])});
        acc += gx + gy; ++n;
    }
    return n ? std::clamp(acc / double(n) / 24.0, 0.0, 2.0) : 0.0;
}

// ---------------------------------------------------------------- ids (orden de pintado) ----
inline std::vector<int32_t> idBuffer(const std::vector<Shape>& sh, const std::vector<size_t>& order, int W, int H) {
    std::vector<int32_t> top(size_t(W) * H, -1);
    for (size_t oi : order) {
        const Shape& s = sh[oi];
        const double rad = s.rot * 3.14159265358979323846 / 180.0, cs = std::cos(rad), sn = std::sin(rad);
        const double hx = s.w * 0.5, hy = s.h * 0.5;
        const double ex = std::abs(hx * cs) + std::abs(hy * sn), ey = std::abs(hx * sn) + std::abs(hy * cs);
        int x0 = std::max(0, int(std::floor(s.cx - ex - 0.5))), x1 = std::min(W - 1, int(std::ceil(s.cx + ex + 0.5)));
        int y0 = std::max(0, int(std::floor(s.cy - ey - 0.5))), y1 = std::min(H - 1, int(std::ceil(s.cy + ey + 0.5)));
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
            double dx = x + 0.5 - s.cx, dy = y + 0.5 - s.cy, lx = dx * cs + dy * sn, ly = -dx * sn + dy * cs;
            if (lx >= -hx - 1e-4 && lx < hx + 1e-4 && ly >= -hy - 1e-4 && ly < hy + 1e-4) top[size_t(y) * W + x] = int32_t(oi);
        }
    }
    return top;
}

// ---------------------------------------------------------------- optimizador por coordenadas ----
// Mejora posicion, tamano, giro y color de cada forma probando pequenos cambios y quedandose con los que bajan el error cuadratico
// contra el objetivo (solo en la region que toca la forma). El orden de pintado (indice del vector) no cambia.
struct Refiner {
    int W, H, thr; const Image& tg; const Image* under;
    std::vector<Shape>& sh;
    Refiner(std::vector<Shape>& shapes, const Image& target, const Image* underCanvas, int alphaThr)
        : W(target.w), H(target.h), thr(alphaThr), tg(target), under(underCanvas), sh(shapes) {}
    static void bbox(const Shape& s, int W, int H, int& x0, int& y0, int& x1, int& y1) {
        const double rad = s.rot * 3.14159265358979323846 / 180.0, cs = std::cos(rad), sn = std::sin(rad);
        const double ex = std::abs(s.w * 0.5 * cs) + std::abs(s.h * 0.5 * sn), ey = std::abs(s.w * 0.5 * sn) + std::abs(s.h * 0.5 * cs);
        x0 = std::max(0, int(std::floor(s.cx - ex - 0.5))); x1 = std::min(W - 1, int(std::ceil(s.cx + ex + 0.5)));
        y0 = std::max(0, int(std::floor(s.cy - ey - 0.5))); y1 = std::min(H - 1, int(std::ceil(s.cy + ey + 0.5)));
    }
    // error en la region [x0..x1]x[y0..y1] con la forma k sustituida por `cand`
    double evalRegion(size_t k, const Shape& cand, int x0, int y0, int x1, int y1, const std::vector<std::array<int, 4>>& bb) const {
        std::vector<size_t> lst;
        for (size_t i = 0; i < sh.size(); ++i) {
            if (i == k) { lst.push_back(i); continue; }
            const auto& b = bb[i];
            if (b[2] < x0 || b[0] > x1 || b[3] < y0 || b[1] > y1) continue;
            lst.push_back(i);
        }
        std::vector<double> cs(lst.size()), sn(lst.size());
        for (size_t t = 0; t < lst.size(); ++t) { const Shape& s = lst[t] == k ? cand : sh[lst[t]]; const double rad = s.rot * 3.14159265358979323846 / 180.0; cs[t] = std::cos(rad); sn[t] = std::sin(rad); }
        const double miss = 3.0 * 255.0 * 255.0 * 0.25;
        double err = 0;
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
            const uint8_t* t = tg.at(x, y); const bool to = t[3] >= thr;
            const uint8_t* col = nullptr;
            for (size_t q = lst.size(); q-- > 0;) {
                const Shape& s = lst[q] == k ? cand : sh[lst[q]];
                const double dx = x + 0.5 - s.cx, dy = y + 0.5 - s.cy, lx = dx * cs[q] + dy * sn[q], ly = -dx * sn[q] + dy * cs[q];
                if (lx >= -s.w * 0.5 - 1e-4 && lx < s.w * 0.5 + 1e-4 && ly >= -s.h * 0.5 - 1e-4 && ly < s.h * 0.5 + 1e-4) { col = &s.r; break; }
            }
            // `col` apunta a r,g,b contiguos (uint8_t r,g,b) de Shape
            if (!col && under) { const uint8_t* u = under->at(x, y); if (u[3] > 0) col = u; }
            if (!to) { if (col) err += miss; continue; }
            if (!col) { err += miss; continue; }
            const double d0 = double(t[0]) - col[0], d1 = double(t[1]) - col[1], d2 = double(t[2]) - col[2];
            err += d0 * d0 + d1 * d1 + d2 * d2;
        }
        return err;
    }
    // rondas = pasadas completas; `ops` limita el trabajo total (evaluaciones de region) para acotar el tiempo.
    void run(int rounds, uint32_t seed, long ops) {
        if (sh.empty()) return;
        std::vector<std::array<int, 4>> bb(sh.size());
        for (size_t i = 0; i < sh.size(); ++i) bbox(sh[i], W, H, bb[i][0], bb[i][1], bb[i][2], bb[i][3]);
        Rng rng(seed);
        std::vector<size_t> idx(sh.size()); for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
        static const double steps[3] = {0.30, 0.12, 0.05};
        for (int rd = 0; rd < rounds && ops > 0; ++rd) {
            for (size_t i = idx.size(); i > 1; --i) std::swap(idx[i - 1], idx[rng.next() % i]);   // orden aleatorio
            const double stf = steps[std::min(rd, 2)];
            for (size_t k : idx) {
                if (ops <= 0) break;
                Shape cur = sh[k];
                if (std::min(cur.w, cur.h) < 0.9f && cur.w * cur.h < 1.5f) continue;
                const double st = std::max(0.5, stf * std::min<double>(cur.w, cur.h) + 0.5);
                const bool rotated = std::abs(std::fmod(cur.rot, 90.0f)) > 0.01f;
                Shape cands[10]; int nc = 0;
                auto mk = [&](float dx, float dy, float dw, float dh, float dr) { Shape s = cur; s.cx += dx; s.cy += dy; s.w = std::max(0.5f, s.w + dw); s.h = std::max(0.5f, s.h + dh); s.rot += dr; cands[nc++] = s; };
                mk(float(st), 0, 0, 0, 0); mk(float(-st), 0, 0, 0, 0); mk(0, float(st), 0, 0, 0); mk(0, float(-st), 0, 0, 0);
                mk(float(st) * .5f, 0, float(st), 0, 0); mk(float(-st) * .5f, 0, float(st), 0, 0); mk(0, float(st) * .5f, 0, float(st), 0); mk(0, float(-st) * .5f, 0, float(st), 0);
                if (rotated) { mk(0, 0, 0, 0, float(4.0 * stf / 0.3)); mk(0, 0, 0, 0, float(-4.0 * stf / 0.3)); }
                // las que encogen: la mitad de los casos (mismo desplazamiento con tamano menor)
                int x0, y0, x1, y1; bbox(cur, W, H, x0, y0, x1, y1);
                double bestErr = -1, bestGain = 0; int bestI = -1;
                const double e0 = evalRegion(k, cur, x0, y0, x1, y1, bb); --ops;
                for (int ci = 0; ci < nc; ++ci) {
                    int cx0, cy0, cx1, cy1; bbox(cands[ci], W, H, cx0, cy0, cx1, cy1);
                    const int ux0 = std::min(x0, cx0), uy0 = std::min(y0, cy0), ux1 = std::max(x1, cx1), uy1 = std::max(y1, cy1);
                    const double a = evalRegion(k, cur, ux0, uy0, ux1, uy1, bb), b = evalRegion(k, cands[ci], ux0, uy0, ux1, uy1, bb); ops -= 2;
                    const double gain = a - b;
                    if (gain > bestGain) { bestGain = gain; bestI = ci; bestErr = b; }
                }
                (void)e0; (void)bestErr;
                if (bestI >= 0 && bestGain > 1.0) { sh[k] = cands[bestI]; bbox(sh[k], W, H, bb[k][0], bb[k][1], bb[k][2], bb[k][3]); }
            }
        }
    }
};

// Optimiza (posicion/tamano/giro) y recolorea con lo realmente visible; quita formas tapadas. `under` = lienzo opaco bajo las formas o null.
inline void refineShapes(std::vector<Shape>& shapes, const Image& target, const Image* under, int thr, int rounds, uint32_t seed, long ops) {
    if (shapes.empty() || rounds <= 0) return;
    // el optimizador asume que el orden del vector ES el orden de pintado: se ordena por z (igual que renderShapes: estable)
    std::stable_sort(shapes.begin(), shapes.end(), [](const Shape& a, const Shape& b) { return a.z < b.z; });
    refitVisible(shapes, target, thr, true);
    Refiner R(shapes, target, under, thr);
    R.run(rounds, seed, ops);
    refitVisible(shapes, target, thr, true);
}

// ---------------------------------------------------------------- cierre ----
inline void applyPalette(const Ctx& c, LsaAnim& A) {
    if (c.lo.channels <= 0 || A.colorChannels > 0) return;
    A.firstChannel = std::clamp(c.lo.firstChannel, 2, 998);
    const int K2 = std::clamp(c.lo.channels, 2, 999 - A.firstChannel + 1);
    std::map<uint32_t, double> hist;
    for (auto& l : A.layers) for (auto& sh : l.shapes) hist[lay::colKey(sh.r, sh.g, sh.b)] += double(sh.w) * sh.h + 1.0;
    if (hist.empty()) return;
    auto pal = lay::buildPalette(hist, K2);
    std::map<uint32_t, int> cache; std::vector<char> used(pal.size(), 0);
    auto nearest = [&](uint8_t r, uint8_t g, uint8_t b) {
        uint32_t k = lay::colKey(r, g, b); auto it = cache.find(k); if (it != cache.end()) return it->second;
        double cc[3] = {double(r), double(g), double(b)}; int bi = 0; double bd = 1e300;
        for (size_t j = 0; j < pal.size(); ++j) { double pc[3] = {double(pal[j][0]), double(pal[j][1]), double(pal[j][2])}; double d = lay::colDist(cc, pc); if (d < bd) { bd = d; bi = int(j); } }
        cache[k] = bi; return bi;
    };
    for (auto& l : A.layers) for (auto& sh : l.shapes) { int bi = nearest(sh.r, sh.g, sh.b); sh.r = pal[size_t(bi)][0]; sh.g = pal[size_t(bi)][1]; sh.b = pal[size_t(bi)][2]; used[size_t(bi)] = 1; }
    for (size_t j = 0; j < pal.size(); ++j) if (used[j]) A.palette.push_back(pal[j]);
}

inline void finish(const Ctx& c, LsaAnim& A) {
    for (auto& v : A.vis) { std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); }
    applyPalette(c, A);
    capLsa(A, c.lo.maxTotal);
    const Sched S = schedule(A);
    A.psnr.assign(size_t(A.frames), 0.0);
    lay::parallelFor(A.frames, c.threads, [&](int f) {
        Result r = frameResult(A, S, f);
        Image comp = renderShapes(r.shapes, A.W, A.H);
        A.psnr[size_t(f)] = computePsnr(c.orig[size_t(f)], comp, c.thr);
    });
    double sum = 0, mn = 1e9; for (double p : A.psnr) { sum += p; mn = std::min(mn, p); }
    A.psnrAvg = sum / std::max(1, A.frames); A.psnrMin = mn;
}

// Cuantos triggers tendra una estructura (capas + visibilidad) antes de trazar: se usa para reservar presupuesto.
inline int triggerEstimate(const LsaAnim& A) {
    LsaAnim t; t.W = A.W; t.H = A.H; t.frames = A.frames; t.vis = A.vis; t.colorChannels = A.colorChannels; t.colorAt = A.colorAt;
    t.layers.resize(A.layers.size());
    for (size_t i = 0; i < A.layers.size(); ++i) { t.layers[i].always = A.layers[i].always; t.layers[i].depth = A.layers[i].depth; t.layers[i].shapes.assign(1, Shape{}); }
    return schedule(t).triggers;
}

// ---------------------------------------------------------------- motor de variantes por region ----
struct Region { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; std::vector<uint8_t> mask; /* bbox-local; vacio = rectangulo completo */
                int w() const { return x1 - x0; } int h() const { return y1 - y0; }
                bool in(int lx, int ly) const { return mask.empty() || mask[size_t(ly) * size_t(w()) + size_t(lx)]; } };

// Imagenes que hacen de "fondo" bajo las regiones (residuo = lo que el frame cambia respecto al fondo). base[f] = indice en bImg o -1.
struct BaseSet { std::vector<Image> sm, fr; std::vector<int> of; };

struct RegionData {                      // por region: apariencia de cada frame y recortes (se calcula una vez y vale para varios theta)
    std::vector<std::vector<uint8_t>> app;           // [frame] mascara bbox-local de lo que el frame pinta en la region
    std::vector<Image> cs, cb;                       // recortes del frame (suavizado) y de su fondo
};

struct VariantPlan {
    struct Var { int rep = 0; std::vector<int> frames; std::vector<uint8_t> app; int area = 0; };
    std::vector<std::vector<Var>> vars;              // por region
    std::vector<std::vector<int>> varOf;             // [region][frame] -> variante o -1
    double areaSum = 0; int nVars = 0;
};

inline std::vector<RegionData> prepRegions(const Ctx& c, const std::vector<Region>& regs, const BaseSet* base) {
    const int N = c.N; std::vector<RegionData> out; out.resize(regs.size());
    lay::parallelFor(int(regs.size()), c.threads, [&](int r) {
        const Region& R = regs[size_t(r)]; RegionData& D = out[size_t(r)];
        const int w = R.w(), h = R.h(); if (w <= 0 || h <= 0) return;
        D.app.resize(size_t(N)); D.cs.resize(size_t(N)); D.cb.resize(size_t(N));
        for (int i = 0; i < N; ++i) {
            D.cs[size_t(i)] = cropImage(c.sm[size_t(i)], R.x0, R.y0, R.x1, R.y1);
            std::vector<uint8_t> m(size_t(w) * h, 0);
            const int bi = base ? base->of[size_t(i)] : -1;
            if (bi < 0) { for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) m[size_t(y) * w + x] = R.in(x, y) ? 1 : 0; }
            else {
                D.cb[size_t(i)] = cropImage(base->sm[size_t(bi)], R.x0, R.y0, R.x1, R.y1);
                std::vector<uint8_t> cm = lay::changeMask(D.cs[size_t(i)], D.cb[size_t(i)], c.tol, c.lo.grow, c.thr);
                for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) m[size_t(y) * w + x] = (cm[size_t(y) * w + x] && R.in(x, y)) ? 1 : 0;
            }
            D.app[size_t(i)] = std::move(m);
        }
    });
    return out;
}

// Agrupa los frames de cada region en variantes: frames cuya apariencia es casi igual (fraccion de pixeles distintos <= theta) comparten capa.
inline VariantPlan planVariants(const Ctx& c, const std::vector<Region>& regs, const std::vector<RegionData>& RD, const BaseSet* base, double theta) {
    VariantPlan P; const int N = c.N;
    P.vars.assign(regs.size(), {}); P.varOf.assign(regs.size(), std::vector<int>(size_t(N), -1));
    std::vector<VariantPlan> parts; parts.resize(regs.size());
    lay::parallelFor(int(regs.size()), c.threads, [&](int r) {
        const Region& R = regs[size_t(r)]; const RegionData& D = RD[size_t(r)]; const int w = R.w(), h = R.h();
        if (w <= 0 || h <= 0 || D.app.empty()) return;
        const size_t np = size_t(w) * h, step = np > 16000 ? 2 : 1;
        auto cnt = [&](const std::vector<uint8_t>& m) { size_t n = 0; for (uint8_t v : m) n += v; return n; };
        auto dist = [&](int i, int j) {                      // "frame i pintado con la variante de j" frente a "frame i"
            const std::vector<uint8_t>& ai = D.app[size_t(i)]; const std::vector<uint8_t>& aj = D.app[size_t(j)];
            const bool hb = base && base->of[size_t(i)] >= 0;
            size_t un = 0, bad = 0;
            for (size_t p = 0; p < np; p += step) {
                if (!ai[p] && !aj[p]) continue;
                ++un;
                const uint8_t* ci = &D.cs[size_t(i)].rgba[p * 4];
                const uint8_t* want = ai[p] ? ci : (hb ? &D.cb[size_t(i)].rgba[p * 4] : ci);
                const uint8_t* got = aj[p] ? &D.cs[size_t(j)].rgba[p * 4] : (hb ? &D.cb[size_t(i)].rgba[p * 4] : ci);
                if (lay::pixDiff(want, got, c.thr) > c.tol) ++bad;
            }
            return un ? double(bad) / double(un) : 0.0;
        };
        std::vector<VariantPlan::Var>& vs = parts[size_t(r)].vars.emplace_back();
        vs.clear();
        std::vector<int>& vo = parts[size_t(r)].varOf.emplace_back(size_t(N), -1);
        for (int i = 0; i < N; ++i) {
            if (cnt(D.app[size_t(i)]) == 0) continue;
            int best = -1; double bd = theta;
            for (size_t v = 0; v < vs.size(); ++v) { double d = std::max(dist(i, vs[v].rep), dist(vs[v].rep, i)); if (d <= bd) { bd = d; best = int(v); } }
            if (best < 0) { VariantPlan::Var nv; nv.rep = i; nv.frames.push_back(i); vs.push_back(std::move(nv)); vo[size_t(i)] = int(vs.size()) - 1; }
            else { vs[size_t(best)].frames.push_back(i); vo[size_t(i)] = best; }
        }
        for (auto& v : vs) {
            if (v.frames.size() > 2 && v.frames.size() <= 10) {                      // representante = miembro mas central
                double bs = 1e300; int bi = v.rep;
                for (int a : v.frames) { double s = 0; for (int b : v.frames) if (a != b) s += dist(b, a); if (s < bs) { bs = s; bi = a; } }
                v.rep = bi;
            }
            v.app = D.app[size_t(v.rep)]; v.area = int(cnt(v.app));
        }
    });
    for (size_t r = 0; r < regs.size(); ++r) {
        if (parts[r].vars.empty()) continue;
        P.vars[r] = std::move(parts[r].vars[0]); P.varOf[r] = std::move(parts[r].varOf[0]);
        for (auto& v : P.vars[r]) { P.areaSum += v.area; ++P.nVars; }
    }
    return P;
}

// Estructura (capas + visibilidad) de un plan, sin trazar: sirve para contar triggers y reservar presupuesto.
inline LsaAnim planStructure(const Ctx& c, const VariantPlan& P, int depth) {
    LsaAnim A; A.W = c.W; A.H = c.H; A.frames = c.N; A.vis.assign(size_t(c.N), {});
    for (size_t r = 0; r < P.vars.size(); ++r) for (const auto& v : P.vars[r]) {
        LsaLayer L; L.depth = depth; L.always = int(v.frames.size()) == c.N;
        const int id = int(A.layers.size()); A.layers.push_back(L);
        for (int f : v.frames) A.vis[size_t(f)].push_back(id);
    }
    return A;
}

// Traza todas las variantes con el presupuesto `budget` (area x complejidad x tiempo visible, con topes por area) y las anade a A.
inline void fitVariants(const Ctx& c, LsaAnim& A, const std::vector<Region>& regs, const VariantPlan& P, const BaseSet* base, int depth, long budget) {
    struct Job { size_t r; int v; double wgt = 0; int cap = 0; int b = 0; Result res; };
    std::vector<Job> jobs;
    for (size_t r = 0; r < regs.size(); ++r) for (size_t v = 0; v < P.vars[r].size(); ++v) {
        Job j; j.r = r; j.v = int(v); const auto& var = P.vars[r][v];
        Image tc = cropImage(c.fr[size_t(var.rep)], regs[r].x0, regs[r].y0, regs[r].x1, regs[r].y1);
        double cx = complexityOf(tc, &var.app);
        j.wgt = double(var.area) * (0.35 + cx) * std::pow(1.0 + double(var.frames.size()), 0.35);
        j.cap = std::max(1, var.area / 3); jobs.push_back(std::move(j));
    }
    std::vector<char> done(jobs.size(), 0); long rem = std::max<long>(budget, long(jobs.size()));
    for (int it = 0; it < 6; ++it) {                                              // reparto con topes (water-filling)
        double sw = 0; for (size_t i = 0; i < jobs.size(); ++i) if (!done[i]) sw += jobs[i].wgt;
        if (sw <= 0) break;
        bool changed = false;
        for (size_t i = 0; i < jobs.size(); ++i) if (!done[i]) { int b = int(double(rem) * jobs[i].wgt / sw); if (b >= jobs[i].cap) { jobs[i].b = jobs[i].cap; rem -= jobs[i].cap; done[i] = 1; changed = true; } else jobs[i].b = std::max(1, b); }
        if (!changed) break;
    }
    lay::parallelFor(int(jobs.size()), c.threads, [&](int ji) {
        Job& j = jobs[size_t(ji)]; const Region& R = regs[j.r]; const auto& var = P.vars[j.r][size_t(j.v)];
        const int w = R.w(), h = R.h();
        Image tgt = cropImage(c.fr[size_t(var.rep)], R.x0, R.y0, R.x1, R.y1);
        Image layerImg = tgt;
        for (size_t p = 0; p < size_t(w) * h; ++p) if (!var.app[p]) layerImg.rgba[p * 4 + 3] = 0;
        Image canvas; const Image* cp = nullptr;
        const int bi = base ? base->of[size_t(var.rep)] : -1;
        if (bi >= 0) { canvas = cropImage(base->fr[size_t(bi)], R.x0, R.y0, R.x1, R.y1); cp = &canvas; }
        else if (!R.mask.empty() || true) {                                         // fuera de la apariencia el lienzo ya es igual al objetivo (error cero)
            canvas = tgt; double mr = 0, mg = 0, mb = 0; size_t n = 0;
            for (size_t p = 0; p < size_t(w) * h; ++p) if (var.app[p]) { mr += tgt.rgba[p * 4]; mg += tgt.rgba[p * 4 + 1]; mb += tgt.rgba[p * 4 + 2]; ++n; }
            if (n && n < size_t(w) * h) { for (size_t p = 0; p < size_t(w) * h; ++p) if (var.app[p]) { canvas.rgba[p * 4] = uint8_t(mr / n); canvas.rgba[p * 4 + 1] = uint8_t(mg / n); canvas.rgba[p * 4 + 2] = uint8_t(mb / n); canvas.rgba[p * 4 + 3] = 255; } cp = &canvas; }
        }
        j.res = fitImage(c, layerImg, tgt, cp, j.b, c.seed + uint32_t(ji) * 977u);
        confineShapes(j.res.shapes, w, h);
        for (Shape& s : j.res.shapes) { s.cx += float(R.x0); s.cy += float(R.y0); }
    });
    for (auto& j : jobs) {
        const auto& var = P.vars[j.r][size_t(j.v)];
        if (j.res.shapes.empty()) continue;
        LsaLayer L; L.shapes = std::move(j.res.shapes); L.depth = depth; L.always = int(var.frames.size()) == A.frames;
        const int id = int(A.layers.size()); A.layers.push_back(std::move(L));
        for (int f : var.frames) A.vis[size_t(f)].push_back(id);
    }
}

} // namespace lsa
} // namespace gdx
