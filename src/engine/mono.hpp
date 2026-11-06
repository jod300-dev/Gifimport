// ============================================================================
//  Estilos monocromos "ligeros": solo tinta (negro, canal 1010) sobre papel
//  (blanco, canal 1011). No necesitan HSV por objeto y suelen usar muchos menos
//  objetos que los métodos normales. Todos respetan el presupuesto.
//
//   9) Halftone   10) Engrave   11) Hatch   12) Stipple   13) Dither
//  14) Ink        15) Strokes   16) Spiral  17) Truchet
// ============================================================================
#pragma once
#include "regions.hpp"
#include "lightcolor.hpp"

namespace gdx {

constexpr double kPi = 3.14159265358979323846;

// ------------------------------------------------------- imagen de tinta ----

struct MonoCtx {
    int W = 0, H = 0;
    std::vector<float> dark;             // 0..1 = cuánta tinta hace falta (transparente = 0)
    bool hasAlpha = false;
    std::vector<double> sat;             // suma acumulada de `dark`
    uint8_t ink = 0, paper = 255;
    // media de `dark` en un cuadrado de radio r alrededor de (cx, cy), en unidades del raster
    double boxMean(double cx, double cy, double r) const {
        int x0 = int(std::floor(cx - r)), x1 = int(std::ceil(cx + r));
        int y0 = int(std::floor(cy - r)), y1 = int(std::ceil(cy + r));
        x0 = std::clamp(x0, 0, W); x1 = std::clamp(x1, 0, W);
        y0 = std::clamp(y0, 0, H); y1 = std::clamp(y1, 0, H);
        if (x1 <= x0 || y1 <= y0) return 0.0;
        size_t st = size_t(W) + 1;
        double s = sat[size_t(y1) * st + x1] - sat[size_t(y0) * st + x1] - sat[size_t(y1) * st + x0] + sat[size_t(y0) * st + x0];
        return s / (double(x1 - x0) * (y1 - y0));
    }
    float px(int x, int y) const { return dark[size_t(std::clamp(y, 0, H - 1)) * W + std::clamp(x, 0, W - 1)]; }
};

// Pasa a gris, estira niveles (percentiles 2–98 %) y calcula la "tinta" necesaria.
inline MonoCtx monoPrepare(const Image& src, const Options& o, int maxSide) {
    MonoCtx m;
    int w, h; fitSize(src.w, src.h, maxSide, w, h);
    Image ras = resizeArea(src, w, h);
    m.W = w; m.H = h;
    m.ink = o.invertInk ? 255 : 0; m.paper = o.invertInk ? 0 : 255;
    std::vector<float> Y(size_t(w) * h, 0.f);
    std::vector<uint8_t> opq(size_t(w) * h, 0);
    int hist[256] = {0}; size_t n = 0;
    for (size_t i = 0; i < Y.size(); ++i) {
        const uint8_t* p = &ras.rgba[i * 4];
        if (p[3] < o.alphaThreshold) { m.hasAlpha = true; continue; }
        opq[i] = 1;
        Y[i] = (0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]) / 255.f;
        ++hist[std::clamp(int(Y[i] * 255.f + 0.5f), 0, 255)]; ++n;
    }
    float lo = 0.f, hi = 1.f;
    if (n) {
        size_t a = 0, tlo = n / 50, thi = n - n / 50; bool gotLo = false;
        for (int v = 0; v < 256; ++v) {
            a += hist[v];
            if (!gotLo && a > tlo) { lo = v / 255.f; gotLo = true; }
            if (a >= thi) { hi = v / 255.f; break; }
        }
        if (hi - lo < 0.1f) { lo = std::max(0.f, lo - 0.05f); hi = std::min(1.f, hi + 0.05f); if (hi - lo < 0.05f) { lo = 0.f; hi = 1.f; } }
    }
    m.dark.assign(Y.size(), 0.f);
    for (size_t i = 0; i < Y.size(); ++i) {
        if (!opq[i]) continue;
        float y = std::clamp((Y[i] - lo) / (hi - lo), 0.f, 1.f);
        m.dark[i] = (o.invertInk ? y : 1.f - y) * (o.colorChannels ? 0.62f : 1.0f);
    }
    size_t st = size_t(w) + 1;
    m.sat.assign(st * (h + 1), 0.0);
    for (int y = 0; y < h; ++y) {
        double row = 0;
        for (int x = 0; x < w; ++x) {
            row += m.dark[size_t(y) * w + x];
            m.sat[size_t(y + 1) * st + (x + 1)] = m.sat[size_t(y) * st + (x + 1)] + row;
        }
    }
    return m;
}

inline Shape inkShape(const MonoCtx& m, float cx, float cy, float len, float th, float rotDeg, int z = 1) {
    Shape s; s.cx = cx; s.cy = cy; s.w = len; s.h = th; s.rot = rotDeg;
    s.r = s.g = s.b = m.ink; s.z = int16_t(z);
    return s;
}

// Papel: un rectángulo del color de fondo (solo si la imagen no tiene transparencia).
inline void addPaper(const MonoCtx& m, std::vector<Shape>& out) {
    if (m.hasAlpha) return;
    Shape s; s.cx = m.W * 0.5f; s.cy = m.H * 0.5f; s.w = float(m.W); s.h = float(m.H);
    s.r = s.g = s.b = m.paper; s.z = 0;
    out.insert(out.begin(), s);
}

// Busca el parámetro más pequeño (= más detalle) cuyo recuento cabe en el presupuesto.
// countFn(p) debe ser (aprox.) no creciente en p.
template <class F>
double fitDecreasing(double lo, double hi, size_t budget, F&& countFn, Progress* prog, float p0, float p1) {
    if (countFn(lo) <= budget) return lo;
    if (countFn(hi) > budget) return hi;
    for (int it = 0; it < 24 && hi / lo > 1.004; ++it) {
        double mid = std::sqrt(lo * hi);
        if (countFn(mid) <= budget) hi = mid; else lo = mid;
        report(prog, p0 + (p1 - p0) * float(it + 1) / 24.f);
        if (isCancelled(prog)) break;
    }
    return hi;
}

// ------------------------------------------------- líneas paralelas (base) ---
// Recorre líneas paralelas de ángulo theta y separación p. A lo largo de cada
// línea mide la tinta necesaria y emite segmentos de grosor constante por tramos.

template <class WFn>
size_t traceLines(const MonoCtx& m, double thetaDeg, double p, double phase, double q, int levels, double step,
                  WFn widthFn, size_t limit, std::vector<Shape>* out, int z, double overlap, double minLen) {
    const double rad = thetaDeg * kPi / 180.0, cs = std::cos(rad), sn = std::sin(rad);
    const double W = m.W, H = m.H;
    double cn[4] = {0, -sn * W, cs * H, -sn * W + cs * H};
    double nmin = *std::min_element(cn, cn + 4), nmax = *std::max_element(cn, cn + 4);
    long k0 = long(std::ceil((nmin - phase) / p)), k1 = long(std::floor((nmax - phase) / p));
    size_t count = 0;
    for (long k = k0; k <= k1; ++k) {
        double o = phase + k * p, Px = -o * sn, Py = o * cs, t0 = -1e18, t1 = 1e18;
        auto clip = [&](double P0, double d, double lim) {
            if (std::abs(d) < 1e-9) return (P0 >= 0 && P0 <= lim);
            double ta = (0 - P0) / d, tb = (lim - P0) / d; if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb); return true;
        };
        if (!clip(Px, cs, W) || !clip(Py, sn, H) || t1 <= t0) continue;
        int runLevel = 0; double runA = 0, runB = 0;
        auto flush = [&]() -> bool {
            if (runLevel > 0 && (runB - runA) >= minLen) {
                if (++count > limit) return false;
                if (out) {
                    double tm = (runA + runB) * 0.5;
                    out->push_back(inkShape(m, float(Px + tm * cs), float(Py + tm * sn), float(runB - runA + overlap),
                                            float(runLevel * q), float(thetaDeg), z));
                }
            }
            runLevel = 0; return true;
        };
        for (double t = t0; t < t1; t += step) {
            double te = std::min(t + step, t1), tm = (t + te) * 0.5;
            double d = m.boxMean(Px + tm * cs, Py + tm * sn, p * 0.5);
            double w = widthFn(d);
            int lv = w <= 0 ? 0 : std::clamp(int(std::ceil(w / q - 1e-6)), 1, levels);
            if (lv == runLevel && lv > 0) runB = te;
            else { if (!flush()) return limit + 1; if (lv > 0) { runLevel = lv; runA = t; runB = te; } }
        }
        if (!flush()) return limit + 1;
    }
    return count;
}

// ------------------------------------------------------------ 9) Halftone ---

inline size_t halftoneRun(const MonoCtx& m, double p, size_t limit, std::vector<Shape>* out) {
    const double c = std::cos(kPi / 4), s = std::sin(kPi / 4);
    size_t count = 0;
    double R = std::hypot(double(m.W), double(m.H));
    long n = long(R / p) + 2;
    for (long j = -n; j <= n; ++j)
        for (long i = -n; i <= n; ++i) {
            double u = i * p, v = j * p;
            double x = m.W * 0.5 + u * c - v * s, y = m.H * 0.5 + u * s + v * c;
            if (x < 0 || y < 0 || x > m.W || y > m.H) continue;
            double d = m.boxMean(x, y, p * 0.5);
            if (d < 0.05) continue;
            double side = p * std::sqrt(std::min(1.0, d) * 1.02);
            if (++count > limit) return limit + 1;
            if (out) out->push_back(inkShape(m, float(x), float(y), float(side), float(side), 45.f));
        }
    return count;
}

inline std::vector<Shape> halftoneShapes(const MonoCtx& m, int budget, Progress* prog) {
    size_t B = size_t(std::max(1, budget - (m.hasAlpha ? 0 : 1)));
    double p = fitDecreasing(3.0, std::max(m.W, m.H) * 0.5, B, [&](double v) { return halftoneRun(m, v, B, nullptr); }, prog, 0.f, 0.9f);
    std::vector<Shape> out; halftoneRun(m, p, B, &out); addPaper(m, out); return out;
}

// ------------------------------------------------------------ 10) Engrave ---

inline std::vector<Shape> engraveShapes(const MonoCtx& m, int budget, Progress* prog) {
    size_t B = size_t(std::max(1, budget - (m.hasAlpha ? 0 : 1)));
    auto width = [](double p) { return [p](double d) { return d < 0.07 ? 0.0 : p * std::min(1.0, d * 1.05); }; };
    auto run = [&](double p, size_t lim, std::vector<Shape>* out) {
        return traceLines(m, 45.0, p, 0.0, p / 6.0, 6, std::max(1.0, p * 0.5), width(p), lim, out, 1, 0.35, 0.0);
    };
    double p = fitDecreasing(2.8, std::max(m.W, m.H) * 0.5, B, [&](double v) { return run(v, B, nullptr); }, prog, 0.f, 0.9f);
    std::vector<Shape> out; run(p, B, &out); addPaper(m, out); return out;
}

// ------------------------------------------------------------- 11) Hatch ----

inline std::vector<Shape> hatchShapes(const MonoCtx& m, int budget, Progress* prog) {
    size_t B = size_t(std::max(1, budget - (m.hasAlpha ? 0 : 1)));
    struct Layer { double theta, thr; };
    const Layer layers[4] = {{45, 0.24}, {135, 0.47}, {0, 0.68}, {90, 0.86}};
    auto run = [&](double p, size_t lim, std::vector<Shape>* out) {
        size_t total = 0;
        for (int li = 0; li < 4; ++li) {
            double thr = layers[li].thr;
            auto width = [p, thr](double d) { return d < thr ? 0.0 : p * 0.27; };   // grosor fijo = 34 % de la separación
            size_t left = total >= lim ? 0 : lim - total;
            size_t c = traceLines(m, layers[li].theta, p, p * 0.25 * li, p * 0.34, 1, std::max(1.0, p * 0.6), width, left, out, 1 + li, 0.3, p * 0.5);
            total += c;
            if (total > lim) return lim + 1;
        }
        return total;
    };
    double p = fitDecreasing(3.4, std::max(m.W, m.H) * 0.5, B, [&](double v) { return run(v, B, nullptr); }, prog, 0.f, 0.9f);
    std::vector<Shape> out; run(p, B, &out); addPaper(m, out); return out;
}

// ----------------------------------------------------------- 12) Stipple ----

inline size_t stippleRun(const MonoCtx& m, const std::vector<float>& dx, const std::vector<float>& dy,
                         const std::vector<float>& du, double r0, size_t limit, std::vector<Shape>* out, Rng* rot) {
    struct Pt { float x, y, r; };
    std::vector<Pt> pts;
    const double cell = r0, W = m.W, H = m.H;
    int gx = int(W / cell) + 2, gy = int(H / cell) + 2;
    std::vector<std::vector<int>> grid(size_t(gx) * gy);
    size_t count = 0;
    for (size_t i = 0; i < dx.size(); ++i) {
        double x = dx[i], y = dy[i];
        double d = m.boxMean(x, y, 1.0);
        if (d < 0.05 || du[i] > std::pow(d, 0.8)) continue;
        double R = r0 / std::sqrt(std::max(d, 0.0625));
        int cx = int(x / cell), cy = int(y / cell), span = int(std::ceil(R * 1.2 / cell)) + 1;
        bool ok = true;
        for (int yy = std::max(0, cy - span); yy <= std::min(gy - 1, cy + span) && ok; ++yy)
            for (int xx = std::max(0, cx - span); xx <= std::min(gx - 1, cx + span) && ok; ++xx)
                for (int id : grid[size_t(yy) * gx + xx]) {
                    const Pt& q = pts[id];
                    double lim = 0.5 * (R + q.r);
                    if ((q.x - x) * (q.x - x) + (q.y - y) * (q.y - y) < lim * lim) { ok = false; break; }
                }
        if (!ok) continue;
        if (++count > limit) return limit + 1;
        grid[size_t(cy) * gx + cx].push_back(int(pts.size()));
        pts.push_back({float(x), float(y), float(R)});
        if (out) {
            double sz = r0 * (0.55 + 0.5 * d);
            out->push_back(inkShape(m, float(x), float(y), float(sz), float(sz), rot ? rot->range(0.f, 90.f) : 0.f));
        }
    }
    return count;
}

inline std::vector<Shape> stippleShapes(const MonoCtx& m, int budget, uint32_t seed, Progress* prog) {
    size_t B = size_t(std::max(1, budget - (m.hasAlpha ? 0 : 1)));
    Rng rng(seed);
    size_t N = std::max<size_t>(80000, size_t(budget) * 24);
    std::vector<float> dx(N), dy(N), du(N);
    for (size_t i = 0; i < N; ++i) { dx[i] = rng.f() * m.W; dy[i] = rng.f() * m.H; du[i] = rng.f(); }
    double r0 = fitDecreasing(1.6, std::max(m.W, m.H) * 0.5, B, [&](double v) { return stippleRun(m, dx, dy, du, v, B, nullptr, nullptr); }, prog, 0.f, 0.9f);
    Rng rr(seed + 7);
    std::vector<Shape> out; stippleRun(m, dx, dy, du, r0, B, &out, &rr); addPaper(m, out); return out;
}

// ------------------------------------------------- 13) Dither y 14) Ink -----
// Se binariza el raster y se cubren los píxeles de tinta con rectángulos voraces.

inline std::vector<Shape> binaryRects(const std::vector<uint8_t>& black, int w, int h, uint8_t ink, size_t limit, size_t* countOut) {
    std::vector<int16_t> lab(size_t(w) * h);
    for (size_t i = 0; i < lab.size(); ++i) lab[i] = black[i] ? 0 : -1;
    std::vector<std::array<uint8_t, 3>> pal = {{ink, ink, ink}};
    std::vector<Shape> out;
    size_t c = greedyRects(lab, w, h, pal, limit, countOut ? nullptr : &out);
    if (countOut) *countOut = c;
    return out;
}

inline std::vector<uint8_t> bayerMask(const MonoCtx& m) {
    static const int b8[64] = {
         0, 32,  8, 40,  2, 34, 10, 42, 48, 16, 56, 24, 50, 18, 58, 26,
        12, 44,  4, 36, 14, 46,  6, 38, 60, 28, 52, 20, 62, 30, 54, 22,
         3, 35, 11, 43,  1, 33,  9, 41, 51, 19, 59, 27, 49, 17, 57, 25,
        15, 47,  7, 39, 13, 45,  5, 37, 63, 31, 55, 23, 61, 29, 53, 21};
    std::vector<uint8_t> b(size_t(m.W) * m.H, 0);
    for (int y = 0; y < m.H; ++y)
        for (int x = 0; x < m.W; ++x)
            b[size_t(y) * m.W + x] = m.dark[size_t(y) * m.W + x] > (b8[(y & 7) * 8 + (x & 7)] + 0.5f) / 64.f ? 1 : 0;
    return b;
}

inline std::vector<uint8_t> inkMask(const MonoCtx& m) {
    std::vector<uint8_t> b(size_t(m.W) * m.H, 0), c;
    double rw = std::max(3.0, std::max(m.W, m.H) / 14.0);
    for (int y = 0; y < m.H; ++y)
        for (int x = 0; x < m.W; ++x) {
            double d = m.dark[size_t(y) * m.W + x], lm = m.boxMean(x + 0.5, y + 0.5, rw);
            b[size_t(y) * m.W + x] = (d > 0.18 && (d > lm + 0.07 || d > 0.82)) ? 1 : 0;
        }
    c = b;                                                  // quita píxeles de tinta aislados
    for (int y = 0; y < m.H; ++y)
        for (int x = 0; x < m.W; ++x) {
            if (!b[size_t(y) * m.W + x]) continue;
            int nb = 0;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
                if ((dx || dy) && x + dx >= 0 && x + dx < m.W && y + dy >= 0 && y + dy < m.H) nb += b[size_t(y + dy) * m.W + x + dx];
            if (nb == 0) c[size_t(y) * m.W + x] = 0;
        }
    return c;
}

// style: 0 = Dither, 1 = Ink. Busca la mayor resolución que cabe en el presupuesto.
inline std::vector<Shape> binaryStyleShapes(const Image& src, const Options& o, int style, Result& res, Progress* prog) {
    size_t B = size_t(std::max(1, o.budget - 1));
    auto build = [&](int side, MonoCtx& m, std::vector<uint8_t>& mask) {
        m = monoPrepare(src, o, side);
        mask = style == 0 ? bayerMask(m) : inkMask(m);
    };
    auto countFor = [&](const MonoCtx& m, const std::vector<uint8_t>& mask, std::vector<Shape>* out) -> size_t {
        size_t c = 0;
        if (!out) { binaryRects(mask, m.W, m.H, m.ink, B, &c); return c; }
        *out = binaryRects(mask, m.W, m.H, m.ink, B, nullptr);
        return out->size();
    };
    int lo = 12, hi = std::min(o.maxRaster, 1024);
    int best = lo;
    {   // mayor lado con recuento <= B
        auto fits = [&](int side) { MonoCtx m; std::vector<uint8_t> mask; build(side, m, mask); return countFor(m, mask, nullptr) <= B; };
        if (fits(hi)) best = hi;
        else {
            for (int it = 0; it < 14 && hi - lo > 1; ++it) {
                int mid = (lo + hi) / 2;
                if (fits(mid)) { lo = mid; best = mid; } else hi = mid;
                report(prog, 0.9f * float(it + 1) / 14.f);
                if (isCancelled(prog)) break;
            }
        }
    }
    MonoCtx m; std::vector<uint8_t> mask; build(best, m, mask);
    std::vector<Shape> out; countFor(m, mask, &out);
    for (auto& s : out) s.z = 1;
    addPaper(m, out);
    res.rasterW = m.W; res.rasterH = m.H; res.inkValue = m.ink;
    return out;
}

// ----------------------------------------------------------- 15) Strokes ----

inline size_t strokesRun(const MonoCtx& m, const std::vector<std::array<float, 4>>& cand /*mag,x,y,angle*/,
                         double sep, size_t limit, std::vector<Shape>* out) {
    int gx = int(m.W / sep) + 2, gy = int(m.H / sep) + 2;
    std::vector<std::vector<std::pair<float, float>>> grid(size_t(gx) * gy);
    size_t count = 0;
    for (auto const& c : cand) {
        double x = c[1], y = c[2];
        int cx = int(x / sep), cy = int(y / sep);
        bool ok = true;
        for (int yy = std::max(0, cy - 1); yy <= std::min(gy - 1, cy + 1) && ok; ++yy)
            for (int xx = std::max(0, cx - 1); xx <= std::min(gx - 1, cx + 1) && ok; ++xx)
                for (auto const& q : grid[size_t(yy) * gx + xx])
                    if ((q.first - x) * (q.first - x) + (q.second - y) * (q.second - y) < sep * sep) { ok = false; break; }
        if (!ok) continue;
        if (++count > limit) return limit + 1;
        grid[size_t(cy) * gx + cx].push_back({float(x), float(y)});
        if (out) out->push_back(inkShape(m, float(x), float(y), float(sep * 1.9), float(std::max(1.1, sep * 0.32)), c[3]));
    }
    return count;
}

inline std::vector<Shape> strokesShapes(const MonoCtx& m, int budget, Progress* prog) {
    size_t B = size_t(std::max(1, budget - (m.hasAlpha ? 0 : 1)));
    std::vector<float> G(size_t(m.W) * m.H), mag(G.size(), 0.f), ang(G.size(), 0.f), GX(G.size(), 0.f), GY(G.size(), 0.f);
    for (int y = 0; y < m.H; ++y) for (int x = 0; x < m.W; ++x) G[size_t(y) * m.W + x] = float(m.boxMean(x + 0.5, y + 0.5, 1.0));
    auto g = [&](int x, int y) { return G[size_t(std::clamp(y, 0, m.H - 1)) * m.W + std::clamp(x, 0, m.W - 1)]; };
    for (int y = 0; y < m.H; ++y)
        for (int x = 0; x < m.W; ++x) {
            float gx = (g(x + 1, y - 1) + 2 * g(x + 1, y) + g(x + 1, y + 1)) - (g(x - 1, y - 1) + 2 * g(x - 1, y) + g(x - 1, y + 1));
            float gy = (g(x - 1, y + 1) + 2 * g(x, y + 1) + g(x + 1, y + 1)) - (g(x - 1, y - 1) + 2 * g(x, y - 1) + g(x + 1, y - 1));
            size_t i = size_t(y) * m.W + x;
            GX[i] = gx; GY[i] = gy;
            mag[i] = std::sqrt(gx * gx + gy * gy);
            ang[i] = float(std::atan2(gx, -gy) * 180.0 / kPi);           // dirección tangente al borde (grados)
        }
    std::vector<std::array<float, 4>> cand;                              // {fuerza, x, y, ángulo}
    for (int y = 1; y < m.H - 1; ++y)
        for (int x = 1; x < m.W - 1; ++x) {
            size_t i = size_t(y) * m.W + x;
            float mg = mag[i];
            if (mg < 0.05f) continue;
            int ax = int(std::lround(GX[i] / mg)), ay = int(std::lround(GY[i] / mg));   // supresión de no-máximos
            if (mg >= mag[size_t(y + ay) * m.W + x + ax] && mg >= mag[size_t(y - ay) * m.W + x - ax])
                cand.push_back({mg, float(x) + 0.5f, float(y) + 0.5f, ang[i]});
        }
    std::sort(cand.begin(), cand.end(), [](auto const& a, auto const& b) { return a[0] > b[0]; });
    if (cand.size() > 20) cand.resize(size_t(cand.size() * 0.55));           // solo los bordes más fuertes
    double sep = fitDecreasing(2.0, std::max(m.W, m.H) * 0.3, B, [&](double v) { return strokesRun(m, cand, v, B, nullptr); }, prog, 0.f, 0.9f);
    std::vector<Shape> out; strokesRun(m, cand, sep, B, &out); addPaper(m, out); return out;
}

// ------------------------------------------------------------ 16) Spiral ----

inline size_t spiralRun(const MonoCtx& m, double p, size_t limit, std::vector<Shape>* out) {
    const double a = p / (2.0 * kPi), cx = m.W * 0.5, cy = m.H * 0.5, Rmax = std::hypot(cx, cy) + p;
    const double ds = std::max(2.0, p * 0.9), q = p / 6.0;
    size_t count = 0;
    double phi = 2.0 * kPi * 0.25;
    double x0 = cx + a * phi * std::cos(phi), y0 = cy + a * phi * std::sin(phi);
    while (a * phi < Rmax) {
        double r = a * phi;
        phi += ds / std::sqrt(r * r + a * a);
        double x1 = cx + a * phi * std::cos(phi), y1 = cy + a * phi * std::sin(phi);
        double mx = (x0 + x1) * 0.5, my = (y0 + y1) * 0.5;
        if (mx >= 0 && my >= 0 && mx <= m.W && my <= m.H) {
            double d = m.boxMean(mx, my, p * 0.5);
            if (d >= 0.07) {
                int lv = std::clamp(int(std::ceil(p * std::min(1.0, d * 1.05) / q - 1e-6)), 1, 6);
                if (++count > limit) return limit + 1;
                if (out) out->push_back(inkShape(m, float(mx), float(my), float(std::hypot(x1 - x0, y1 - y0) + 0.5),
                                                 float(lv * q), float(std::atan2(y1 - y0, x1 - x0) * 180.0 / kPi)));
            }
        }
        x0 = x1; y0 = y1;
    }
    return count;
}

inline std::vector<Shape> spiralShapes(const MonoCtx& m, int budget, Progress* prog) {
    size_t B = size_t(std::max(1, budget - (m.hasAlpha ? 0 : 1)));
    double p = fitDecreasing(3.0, std::max(m.W, m.H) * 0.5, B, [&](double v) { return spiralRun(m, v, B, nullptr); }, prog, 0.f, 0.9f);
    std::vector<Shape> out; spiralRun(m, p, B, &out); addPaper(m, out); return out;
}

// ----------------------------------------------------------- 17) Truchet ----

inline size_t truchetRun(const MonoCtx& m, double c, size_t limit, std::vector<Shape>* out) {
    size_t count = 0;
    int nx = int(std::ceil(m.W / c)), ny = int(std::ceil(m.H / c));
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            double x = (i + 0.5) * c, y = (j + 0.5) * c;
            if (x > m.W || y > m.H) continue;
            double d = m.boxMean(x, y, c * 0.5);
            if (d < 0.07) continue;
            // orientación local de la estructura (gradiente en la celda)
            double gx = m.boxMean(x + c * 0.5, y, c * 0.5) - m.boxMean(x - c * 0.5, y, c * 0.5);
            double gy = m.boxMean(x, y + c * 0.5, c * 0.5) - m.boxMean(x, y - c * 0.5, c * 0.5);
            double rot;
            if (std::hypot(gx, gy) > 0.04) {
                double tang = std::atan2(gx, -gy) * 180.0 / kPi;                // tangente al borde
                tang = std::fmod(std::fmod(tang, 180.0) + 180.0, 180.0);
                rot = (std::abs(tang - 45.0) < std::abs(tang - 135.0)) ? 45.0 : 135.0;
            } else {
                uint32_t h = uint32_t(i) * 73856093u ^ uint32_t(j) * 19349663u; h ^= h >> 13; h *= 0x5bd1e995u;
                rot = (h & 0x10000u) ? 45.0 : 135.0;
            }
            double th = std::min(c * 0.9, std::max(c * 0.12, c * std::min(1.0, d) * 0.82));
            int n = d > 0.82 ? 2 : 1;                                           // muy oscuro: X
            for (int k = 0; k < n; ++k) {
                if (++count > limit) return limit + 1;
                if (out) out->push_back(inkShape(m, float(x), float(y), float(c * 1.43), float(th), float(k == 0 ? rot : (rot == 45.0 ? 135.0 : 45.0))));
            }
        }
    return count;
}

inline std::vector<Shape> truchetShapes(const MonoCtx& m, int budget, Progress* prog) {
    size_t B = size_t(std::max(1, budget - (m.hasAlpha ? 0 : 1)));
    double c = fitDecreasing(3.0, std::max(m.W, m.H) * 0.5, B, [&](double v) { return truchetRun(m, v, B, nullptr); }, prog, 0.f, 0.9f);
    std::vector<Shape> out; truchetRun(m, c, B, &out); addPaper(m, out); return out;
}

// -------------------------------------------------------------- 18) ASCII ---
// Filas de texto (objeto 914). Las filas largas se parten en trozos de `asciiChunk`
// caracteres (GD recorta/entrecorta los textos muy largos) colocados uno junto a otro.
// Caracteres por densidad de tinta + caracteres direccionales en los bordes.
// En modo color: base de color detrás y cada tramo de texto con la tinta de su tono.

inline void pushTextChunks(Result& res, const std::string& line, int a, int b, int r, int chunk, const Rgb3& col) {
    for (int s0 = a; s0 <= b; s0 += chunk) {
        int s = s0, e = std::min(b, s0 + chunk - 1);
        while (s <= e && line[size_t(s)] == ' ') ++s;                 // recorta espacios de los extremos
        while (e >= s && line[size_t(e)] == ' ') --e;
        if (s > e) continue;
        TextRow t; t.text = line.substr(size_t(s), size_t(e - s + 1));
        t.cx = (s + e + 1) * 0.5f; t.cy = r + 0.5f; t.colored = true;
        t.r = col[0]; t.g = col[1]; t.b = col[2];
        res.textRows.push_back(t);
    }
}

inline void asciiShapes(const Image& src, const Options& o, Result& res) {
    const double aspect = double(src.w) / src.h;
    const double ca = std::clamp<double>(o.asciiAspect, 0.3, 3.0);
    const bool color = o.colorChannels;
    // en color los tramos son más cortos (el color cambia de un tramo a otro) y hay menos columnas
    const int chunk = color ? std::clamp(std::min(o.asciiChunk, 14), 8, 400) : std::clamp(o.asciiChunk, 8, 400);
    int cols = color ? std::clamp(60 + (o.budget - 500) * 200 / 9500, 60, 260) : std::clamp(80 + (o.budget - 500) * 320 / 9500, 60, 400);
    const std::string ramp = " .:-=+*#%@";                          // de claro a oscuro (tinta)

    for (int attempt = 0; attempt < 10; ++attempt) {
        int rows = std::max(1, int(std::lround(cols / aspect / ca)));
        if (rows > 400) { rows = 400; cols = std::max(10, int(std::lround(rows * aspect * ca))); }
        MonoCtx m = monoPrepare(src, o, std::max(cols, rows) * 6);
        const double cw = double(m.W) / cols, chh = double(m.H) / rows;
        res = Result{};                                              // reintento limpio
        res.mono = true; res.inkValue = m.ink; res.hadAlpha = m.hasAlpha; res.aspect = float(aspect);
        res.rasterW = cols; res.rasterH = rows;
        const bool inv = m.ink != 0;

        std::vector<std::string> lines(size_t(rows), std::string(size_t(cols), ' '));
        std::vector<float> dens(size_t(rows) * cols, 0.f);
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                double x = (c + 0.5) * cw, y = (r + 0.5) * chh;
                double d = m.boxMean(x, y, std::max(cw, chh) * 0.5);
                double gx = m.boxMean(x + cw, y, cw * 0.5) - m.boxMean(x - cw, y, cw * 0.5);
                double gy = m.boxMean(x, y + chh, chh * 0.5) - m.boxMean(x, y - chh, chh * 0.5);
                char ch;
                if (d < 0.04) ch = ' ';
                else if (std::hypot(gx, gy) > 0.42 && d > 0.15 && d < 0.80) {   // borde: carácter direccional
                    double tang = std::atan2(gx, -gy) * 180.0 / kPi;
                    tang = std::fmod(std::fmod(tang, 180.0) + 180.0, 180.0);
                    if (tang < 22.5 || tang >= 157.5) ch = '-';
                    else if (tang < 67.5) ch = '\\';
                    else if (tang < 112.5) ch = '|';
                    else ch = '/';
                } else {
                    int idx = std::clamp(int(std::lround(std::pow(std::min(1.0, d), 0.9) * (ramp.size() - 1))), 0, int(ramp.size()) - 1);
                    ch = ramp[size_t(idx)];
                }
                lines[size_t(r)][size_t(c)] = ch;
                dens[size_t(r) * cols + c] = float(std::min(1.0, d));
            }

        res.previewImg.w = cols * 4; res.previewImg.h = rows * 4;
        res.previewImg.rgba.assign(size_t(res.previewImg.w) * res.previewImg.h * 4, 0);
        auto paintCell = [&](int c, int r, const Rgb3& bg, const Rgb3& fg, double d) {
            for (int yy = 0; yy < 4; ++yy) for (int xx = 0; xx < 4; ++xx) {
                uint8_t* p = res.previewImg.at(c * 4 + xx, r * 4 + yy);
                for (int k = 0; k < 3; ++k) p[k] = uint8_t(std::lround(bg[k] + (double(fg[k]) - bg[k]) * d));
                p[3] = 255;
            }
        };

        if (!color) {
            // blanco y negro: papel + filas de texto con la tinta
            const Rgb3 ink = {m.ink, m.ink, m.ink}, paper = {m.paper, m.paper, m.paper};
            for (int r = 0; r < rows; ++r) pushTextChunks(res, lines[size_t(r)], 0, cols - 1, r, chunk, ink);
            for (int r = 0; r < rows; ++r) for (int c = 0; c < cols; ++c) paintCell(c, r, paper, ink, dens[size_t(r) * cols + c]);
            if (!m.hasAlpha) {                                           // papel: exactamente columnas x filas (no el raster interno)
                Shape pp; pp.cx = cols * 0.5f; pp.cy = rows * 0.5f; pp.w = float(cols); pp.h = float(rows);
                pp.r = pp.g = pp.b = m.paper; pp.z = 0;
                res.shapes.push_back(pp);
            }
            res.colored = !o.lightSpecialChannels;
            if (o.lightSpecialChannels) for (auto& t : res.textRows) t.colored = false;
            if (int(res.shapes.size() + res.textRows.size()) <= o.budget) break;
        } else {
            // color: clusters de color de las celdas con tinta; base de color detrás
            Image ras = resizeArea(src, cols, rows);
            Stats st; st.build(ras, o.alphaThreshold);
            std::vector<WCol> samples; std::vector<int> cellIdx;
            std::vector<Rgb3> cellCol(size_t(rows) * cols, Rgb3{128, 128, 128});
            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < cols; ++c) {
                    auto q = st.query(std::max(0, c - 1), std::max(0, r - 1), std::min(cols, c + 2), std::min(rows, r + 2));
                    if (q.n <= 0) continue;
                    cellCol[size_t(r) * cols + c] = {uint8_t(q.sr / q.n + 0.5), uint8_t(q.sg / q.n + 0.5), uint8_t(q.sb / q.n + 0.5)};
                    if (lines[size_t(r)][size_t(c)] != ' ') samples.push_back({float(q.sr / q.n), float(q.sg / q.n), float(q.sb / q.n), 1.f});
                }
            int Kc = std::clamp(o.paletteSize / 2, 2, std::max(2, o.maxChannels / 2));
            std::vector<Rgb3> C = medianCutW(samples, Kc);
            if (C.empty()) C.push_back({128, 128, 128});
            std::vector<int> lab(size_t(rows) * cols, 0);
            for (size_t i = 0; i < lab.size(); ++i) lab[i] = nearestIdx(C, cellCol[i][0], cellCol[i][1], cellCol[i][2]);

            for (int r = 0; r < rows; ++r) {
                struct Run { int a, b, label, ink; };
                std::vector<Run> runs; int cur = -1;
                for (int c = 0; c < cols; ++c) {
                    bool inked = lines[size_t(r)][size_t(c)] != ' ';
                    if (inked) cur = lab[size_t(r) * cols + c]; else if (cur < 0) continue;     // los espacios heredan el color anterior
                    if (!runs.empty() && runs.back().label == cur) { runs.back().b = c; runs.back().ink += inked ? 1 : 0; }
                    else runs.push_back({c, c, cur, inked ? 1 : 0});
                }
                const int kMinInk = 4;                                   // tramos con muy poca tinta adoptan el color vecino
                for (size_t i = 0; i < runs.size(); ++i)
                    if (runs[i].ink < kMinInk) { if (i > 0) runs[i].label = runs[i - 1].label; else if (runs.size() > 1) runs[i].label = runs[i + 1].label; }
                std::vector<Run> merged;
                for (auto const& rn : runs) {
                    if (!merged.empty() && merged.back().label == rn.label) { merged.back().b = rn.b; merged.back().ink += rn.ink; }
                    else merged.push_back(rn);
                }
                for (auto const& rn : merged) {
                    if (rn.ink == 0) continue;
                    pushTextChunks(res, lines[size_t(r)], rn.a, rn.b, r, chunk, inkOf(C[size_t(rn.label)], inv));
                    for (int c = rn.a; c <= rn.b; ++c) lab[size_t(r) * cols + c] = rn.label;     // el color final de la celda
                }
            }
            // base de color detrás (mosaico BSP) con el tono aclarado/oscurecido del cluster
            int textObjs = int(res.textRows.size());
            int baseBudget = std::min(std::max(100, o.budget / 4), o.budget - textObjs);
            if (baseBudget < 60) { cols = int(cols * 0.85); continue; }     // demasiados objetos de texto: menos columnas
            std::vector<Shape> base = treeShapes(ras, baseBudget, o.alphaThreshold, true, nullptr);
            // densidad media de texto por hoja -> cobertura aproximada de tinta (los glifos cubren ~45 % de su celda)
            std::vector<double> dsat(size_t(cols + 1) * (rows + 1), 0.0);
            for (int y = 0; y < rows; ++y) {
                double row = 0;
                for (int x = 0; x < cols; ++x) {
                    row += lines[size_t(y)][size_t(x)] != ' ' ? dens[size_t(y) * cols + x] : 0.0;
                    dsat[size_t(y + 1) * (cols + 1) + (x + 1)] = dsat[size_t(y) * (cols + 1) + (x + 1)] + row;
                }
            }
            for (auto& s : base) {
                int x0 = std::clamp(int(std::floor(s.cx - s.w * 0.5)), 0, cols), x1 = std::clamp(int(std::ceil(s.cx + s.w * 0.5)), 0, cols);
                int y0 = std::clamp(int(std::floor(s.cy - s.h * 0.5)), 0, rows), y1 = std::clamp(int(std::ceil(s.cy + s.h * 0.5)), 0, rows);
                double cov = 0;
                if (x1 > x0 && y1 > y0) {
                    size_t st2 = size_t(cols) + 1;
                    cov = (dsat[size_t(y1) * st2 + x1] - dsat[size_t(y0) * st2 + x1] - dsat[size_t(y1) * st2 + x0] + dsat[size_t(y0) * st2 + x0]) / (double(x1 - x0) * (y1 - y0)) * 0.45;
                }
                int i = nearestIdx(C, s.r, s.g, s.b);
                double r, g, b; compensatedBase(s.r, s.g, s.b, inkOf(C[size_t(i)], inv), cov, r, g, b);
                s.r = uint8_t(r + 0.5); s.g = uint8_t(g + 0.5); s.b = uint8_t(b + 0.5); s.z = 0;
            }
            quantizeLeaves(base, std::max(2, o.paletteSize - int(C.size())));
            res.shapes = std::move(base);
            res.colored = true;
            {   // vista previa: la base de color + la tinta de cada celda según su densidad
                Image bimg = renderShapesScaled(res.shapes, cols, rows, 4);
                for (int r = 0; r < rows; ++r) for (int c = 0; c < cols; ++c) {
                    const Rgb3& cc = C[size_t(lab[size_t(r) * cols + c])];
                    Rgb3 ik = inkOf(cc, inv); double dd = dens[size_t(r) * cols + c] * 0.45;
                    for (int yy = 0; yy < 4; ++yy) for (int xx = 0; xx < 4; ++xx) {
                        const uint8_t* bp = bimg.at(c * 4 + xx, r * 4 + yy); uint8_t* p = res.previewImg.at(c * 4 + xx, r * 4 + yy);
                        if (bp[3] == 0) { p[3] = 0; continue; }                      // transparente
                        for (int k = 0; k < 3; ++k) p[k] = uint8_t(std::lround(bp[k] + (double(ik[k]) - bp[k]) * dd));
                        p[3] = 255;
                    }
                }
            }
            if (int(res.shapes.size() + res.textRows.size()) <= o.budget) break;
        }
        cols = std::max(10, int(cols * 0.85));                          // no cabe: menos columnas y reintenta
    }
    res.note = color
        ? "ASCII a color: usa fuente monoespaciada y calibra ancho/alto de caracter en los ajustes."
        : "ASCII: elige una fuente monoespaciada en el nivel (p. ej. fuente 5) y ajusta ancho/alto de caracter en los ajustes.";
}

// ------------------------------------------------------------- despacho -----

inline std::vector<Shape> monoShapes(const Image& src, Method method, const Options& o, Result& res, Progress* prog) {
    if (method == Method::Ascii) { asciiShapes(src, o, res); return res.shapes; }
    if (method == Method::Dither || method == Method::Ink)
        return binaryStyleShapes(src, o, method == Method::Dither ? 0 : 1, res, prog);
    int side = std::min(o.maxRaster, 400);
    MonoCtx m = monoPrepare(src, o, side);
    res.rasterW = m.W; res.rasterH = m.H; res.inkValue = m.ink;
    std::vector<Shape> out;
    switch (method) {
        case Method::Halftone: out = halftoneShapes(m, o.budget, prog); break;
        case Method::Engrave:  out = engraveShapes(m, o.budget, prog); break;
        case Method::Hatch:    out = hatchShapes(m, o.budget, prog); break;
        case Method::Stipple:  out = stippleShapes(m, o.budget, o.seed, prog); break;
        case Method::Strokes:  out = strokesShapes(m, o.budget, prog); break;
        case Method::Spiral:   out = spiralShapes(m, o.budget, prog); break;
        case Method::Truchet:  out = truchetShapes(m, o.budget, prog); break;
        default: break;
    }
    return out;
}

} // namespace gdx
