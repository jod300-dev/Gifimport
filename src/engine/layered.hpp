// ============================================================================
//  gdx :: Metodo "Capas" (fondo + rests + parches)  --  animacion por RESIDUOS
//  Sin dependencias de Geode (se puede probar en cualquier PC). No modifica ningun otro metodo.
//
//  Idea (la misma del mod "GIF to GD"): en vez de dibujar cada frame entero con N objetos, la animacion se
//  descompone en capas que casi nunca cambian:
//
//    still  (1 capa)     partes quietas del GIF (mediana temporal), trazadas UNA sola vez, siempre visibles
//    rests  (K capas)    poses de reposo (medoides de K grupos de frames): solo lo que difiere del still
//    patch  (1 por frame) solo lo que difiere de  still + rest  de ese frame
//
//  Cada capa se calcula en LAZO CERRADO: la diferencia se mide contra lo que de verdad se vera (las formas
//  ya renderizadas de las capas de abajo), asi el error de trazado de una capa lo corrige la siguiente y no
//  se acumula. El frame i se ve como  still + rest(i) + patch(i).
//
//  Ventajas frente a "un frame = un grupo de objetos": lo quieto no parpadea entre frames, y el presupuesto
//  de objetos se gasta donde hay movimiento (un parche pequeno con 250 objetos es muy preciso).
//
//  Triggers (misma tecnica que gifanim.hpp):
//    L   bucle: Spawn(F_i, t_i) por cada frame que cambia algo + Spawn(L, duracion total)
//    F_i 2-4 Alpha: mostrar parche i / ocultar el anterior, y cambiar de rest solo si hace falta
//    I   inicio: Alpha(0) a todas las capas que no se ven en el frame 0
// ============================================================================
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <cmath>
#include <cstdlib>
#include <thread>
#include <vector>
#include "gifanim.hpp"
#include "geometrize.hpp"      // clase Geo (se reutiliza tal cual, sin modificarla)
#include "palette.hpp"

namespace gdx {

// ------------------------------------------------------------- parametros ---

struct LayeredOptions {
    int perFrame = 300;       // presupuesto MEDIO por frame: el total es perFrame x frames (igual que el metodo normal)
    int restCount = 0;        // numero de rests; 0 = automatico (se elige el que menos objetos-pixel necesita)
    int tol = 28;             // umbral de cambio (0-255, canal maximo, sobre imagen suavizada: ignora el ruido del GIF)
    int grow = 1;             // margen (px) alrededor de cada zona que cambia
    int inner = 0;            // trazador de cada capa: 0 = BSP, 1 = Regiones por paleta, 2 = el mejor de los dos
    int maxTotal = 40000;     // tope de objetos totales (triggers incluidos)
    float timeExp = 0.3f;     // la densidad de objetos de una capa crece con (frames en que se ve)^timeExp
    int painter = 1;          // 1 = tras el BSP, rectangulos rotados guiados por el error (solo GIF sin transparencia)
    float reuse = 0.005f;     // un frame reutiliza el parche de otro (mismo rest) si difieren en menos de esta fraccion del lienzo (0 = no reutilizar)
    float stillBsp = 0.55f;   // idem para el fondo (el fondo es grande y regular: mas BSP)
    float bspFrac = 0.15f;    // fraccion del presupuesto de cada capa que usa la base BSP (el resto: rectangulos rotados)
    bool tree = false;        // metodo 19: arbol de deltas (cada frame se predice del frame mas parecido, no de un rest fijo)
    int channels = 0;         // >0: colores en CANALES (sin HSV): paleta de hasta este numero de colores; 0 = HSV
    int firstChannel = 2;     // primer canal de la paleta (el canal 1 NUNCA se usa)
    bool denoise = true;      // mediana 3x3 antes de trazar (quita el dither del GIF; el PSNR se mide contra el original)
};

struct LayeredAnim {
    int W = 0, H = 0;                   // raster de trabajo comun a todas las capas
    int frames = 0;
    Result still;
    std::vector<Result> rests;          // K capas
    std::vector<Result> patches;        // indexado por frame; solo tienen objetos los frames "representantes" (shapes vacio = nada que pintar)
    std::vector<int> patchOf;           // parche que muestra cada frame: el suyo, el de otro frame casi identico (reutilizado) o -1
    std::vector<int> restOf;            // rest que usa cada frame (-1 = ninguno: el parche va directo sobre el still)
    std::vector<int> medoid;            // frame (del plan) que representa cada rest
    std::vector<double> psnr;           // fidelidad de cada frame ya montado (dB)
    double psnrAvg = 0, psnrMin = 0;
    std::vector<std::array<uint8_t, 3>> palette;   // colores de canal usados (vacio = HSV); el canal de palette[i] es firstChannel + i
    int firstChannel = 2;
    bool tree = false;                  // metodo 19: `patches[i]` = delta del frame i sobre el composite de `parent[i]`
    std::vector<int> parent;            // (solo arbol) frame padre; -1 = el fondo (still)
    std::vector<int> zNode;             // (solo arbol) z base de la delta de cada frame
    int depthMax = 0;
    int zRest = 1, zPatch = 2;          // z-order base de los rests y los parches (encima de las formas de la capa de abajo)
    int budgetTotal = 0;                // objetos de capas que se podian repartir
    int restCandidates = 0;             // K probados al elegir el numero de rests (info)
    bool cancelled = false;
    std::string note;

    size_t objectCount() const {
        size_t n = still.shapes.size();
        for (auto& r : rests) n += r.shapes.size();
        for (auto& r : patches) n += r.shapes.size();
        return n;
    }
    bool ok() const { return !cancelled && W > 0 && frames >= 2 && objectCount() > 0; }   // el fondo puede estar vacio (GIF con transparencia donde el sprite se mueve)
};

inline const char* layeredName() { return "18. Capas: fondo + rests + parches"; }
inline const char* layeredDesc() {
    return "Como GIF to GD: lo quieto se traza 1 vez, luego poses de reposo y solo lo que cambia en cada frame. "
           "Mas nitidez con los mismos objetos. 'Objetos por frame' x frames = total que se reparte entre las capas.";
}

// ============================================================================
namespace lay {

inline int pixDiff(const uint8_t* a, const uint8_t* b, int thr) {
    const bool oa = a[3] >= thr, ob = b[3] >= thr;
    if (oa != ob) return 255;
    if (!oa) return 0;
    return std::max({std::abs(int(a[0]) - b[0]), std::abs(int(a[1]) - b[1]), std::abs(int(a[2]) - b[2])});
}

// Suavizado 3x3 solo entre pixeles opacos (para comparar sin que el ruido de paleta/dither cuente como cambio).
inline Image blur3(const Image& s, int thr) {
    Image o = s;
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) {
            if (s.at(x, y)[3] < thr) continue;
            int acc[3] = {0, 0, 0}, n = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                int yy = y + dy; if (yy < 0 || yy >= s.h) continue;
                for (int dx = -1; dx <= 1; ++dx) {
                    int xx = x + dx; if (xx < 0 || xx >= s.w) continue;
                    const uint8_t* p = s.at(xx, yy);
                    if (p[3] < thr) continue;
                    acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; ++n;
                }
            }
            uint8_t* q = o.at(x, y);
            q[0] = uint8_t(acc[0] / n); q[1] = uint8_t(acc[1] / n); q[2] = uint8_t(acc[2] / n);
        }
    return o;
}

// Mediana temporal por pixel (fondo "tipico"). Un pixel solo es opaco si lo es en TODOS los frames: un parche puede pintar encima pero
// nunca borrar, asi que un fondo opaco donde algun frame es transparente dejaria un error permanente en ese frame (GIF con transparencia).
inline Image temporalMedian(const std::vector<Image>& fr, int thr) {
    const int N = int(fr.size()), W = fr[0].w, H = fr[0].h;
    Image m; m.w = W; m.h = H; m.rgba.assign(size_t(W) * H * 4, 0);
    std::vector<uint8_t> a[3];
    for (auto& v : a) v.resize(size_t(N));
    for (size_t p = 0; p < size_t(W) * H; ++p) {
        int n = 0;
        for (int i = 0; i < N; ++i) {
            const uint8_t* q = &fr[size_t(i)].rgba[p * 4];
            if (q[3] < thr) continue;
            a[0][size_t(n)] = q[0]; a[1][size_t(n)] = q[1]; a[2][size_t(n)] = q[2]; ++n;
        }
        if (n < N || n == 0) continue;                           // transparente en algun frame: no va al fondo
        uint8_t* o = &m.rgba[p * 4];
        for (int c = 0; c < 3; ++c) {
            auto mid = a[c].begin() + n / 2;
            std::nth_element(a[c].begin(), mid, a[c].begin() + n);
            o[c] = *mid;
        }
        o[3] = 255;
    }
    return m;
}

// Rellena los pixeles no validos con el color de su entorno (push-pull): suave y barato de trazar.
inline void pushPull(Image& im, const std::vector<uint8_t>& valid) {
    struct Lvl { int w, h; std::vector<float> r, g, b, wt, ar, ag, ab; };
    std::vector<Lvl> L;
    {
        Lvl l; l.w = im.w; l.h = im.h;
        const size_t n = size_t(l.w) * l.h;
        l.r.assign(n, 0); l.g.assign(n, 0); l.b.assign(n, 0); l.wt.assign(n, 0);
        for (size_t p = 0; p < n; ++p) if (valid[p]) { l.r[p] = im.rgba[p * 4]; l.g[p] = im.rgba[p * 4 + 1]; l.b[p] = im.rgba[p * 4 + 2]; l.wt[p] = 1; }
        L.push_back(std::move(l));
    }
    while (L.back().w > 1 || L.back().h > 1) {
        const Lvl& s = L.back();
        Lvl l; l.w = (s.w + 1) / 2; l.h = (s.h + 1) / 2;
        const size_t n = size_t(l.w) * l.h;
        l.r.assign(n, 0); l.g.assign(n, 0); l.b.assign(n, 0); l.wt.assign(n, 0);
        for (int y = 0; y < s.h; ++y)
            for (int x = 0; x < s.w; ++x) {
                size_t i = size_t(y) * s.w + x, j = size_t(y / 2) * l.w + x / 2;
                l.r[j] += s.r[i]; l.g[j] += s.g[i]; l.b[j] += s.b[i]; l.wt[j] += s.wt[i];
            }
        L.push_back(std::move(l));
    }
    for (int k = int(L.size()) - 1; k >= 0; --k) {
        Lvl& l = L[size_t(k)];
        const size_t n = size_t(l.w) * l.h;
        l.ar.assign(n, 0); l.ag.assign(n, 0); l.ab.assign(n, 0);
        for (int y = 0; y < l.h; ++y)
            for (int x = 0; x < l.w; ++x) {
                size_t i = size_t(y) * l.w + x;
                if (l.wt[i] > 0) { l.ar[i] = l.r[i] / l.wt[i]; l.ag[i] = l.g[i] / l.wt[i]; l.ab[i] = l.b[i] / l.wt[i]; }
                else if (k + 1 < int(L.size())) {
                    const Lvl& u = L[size_t(k + 1)];
                    size_t j = size_t(y / 2) * u.w + x / 2;
                    l.ar[i] = u.ar[j]; l.ag[i] = u.ag[j]; l.ab[i] = u.ab[j];
                }
            }
    }
    const Lvl& b = L[0];
    for (size_t p = 0; p < size_t(im.w) * im.h; ++p) {
        if (valid[p]) continue;
        im.rgba[p * 4] = uint8_t(std::clamp(b.ar[p] + 0.5f, 0.f, 255.f));
        im.rgba[p * 4 + 1] = uint8_t(std::clamp(b.ag[p] + 0.5f, 0.f, 255.f));
        im.rgba[p * 4 + 2] = uint8_t(std::clamp(b.ab[p] + 0.5f, 0.f, 255.f));
        im.rgba[p * 4 + 3] = 255;
    }
}

template <class F>
inline void parallelFor(int n, int threads, F fn) {
    std::atomic<int> next{0};
    auto worker = [&]() { for (;;) { int i = next.fetch_add(1); if (i >= n) return; fn(i); } };
    threads = std::clamp(threads, 1, std::max(1, n));
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; ++t) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();
}

// ------------------------------------------------------------ rests --------

// Distancia entre frames (L1 sobre una rejilla de <= 32x32 celdas de color medio).
inline std::vector<float> frameDistances(const std::vector<Image>& fr) {
    const int N = int(fr.size());
    const int W = fr[0].w, H = fr[0].h, gw = std::min(W, 32), gh = std::min(H, 32);
    const size_t fd = size_t(gw) * gh * 3;
    std::vector<float> feat(size_t(N) * fd, 0.f);
    for (int i = 0; i < N; ++i)
        for (int cy = 0; cy < gh; ++cy)
            for (int cx = 0; cx < gw; ++cx) {
                int x0 = cx * W / gw, x1 = std::max(x0 + 1, (cx + 1) * W / gw), y0 = cy * H / gh, y1 = std::max(y0 + 1, (cy + 1) * H / gh);
                double acc[3] = {0, 0, 0}; int n = 0;
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x) {
                        const uint8_t* p = fr[size_t(i)].at(x, y);
                        double a = p[3] / 255.0;
                        acc[0] += p[0] * a; acc[1] += p[1] * a; acc[2] += p[2] * a; ++n;
                    }
                float* f = &feat[size_t(i) * fd + (size_t(cy) * gw + cx) * 3];
                for (int c = 0; c < 3; ++c) f[c] = float(acc[c] / std::max(1, n));
            }
    std::vector<float> D(size_t(N) * N, 0.f);
    for (int i = 0; i < N; ++i)
        for (int j = i + 1; j < N; ++j) {
            const float* a = &feat[size_t(i) * fd]; const float* b = &feat[size_t(j) * fd];
            double d = 0; for (size_t k = 0; k < fd; ++k) d += std::abs(a[k] - b[k]);
            D[size_t(i) * N + j] = D[size_t(j) * N + i] = float(d);
        }
    return D;
}

// k-medoides determinista (semilla: medoide global + el mas lejano). Devuelve el frame medoide de cada grupo y
// el grupo de cada frame.
inline void kMedoids(const std::vector<float>& D, int N, int K, std::vector<int>& med, std::vector<int>& as) {
    K = std::clamp(K, 1, N);
    med.clear();
    {
        int best = 0; double bs = 1e300;
        for (int i = 0; i < N; ++i) { double s = 0; for (int j = 0; j < N; ++j) s += D[size_t(i) * N + j]; if (s < bs) { bs = s; best = i; } }
        med.push_back(best);
        while (int(med.size()) < K) {
            int farIdx = -1; double fd2 = -1;
            for (int i = 0; i < N; ++i) {
                double m = 1e300; for (int c : med) m = std::min(m, double(D[size_t(i) * N + c]));
                if (m > fd2) { fd2 = m; farIdx = i; }
            }
            med.push_back(farIdx);
        }
    }
    as.assign(size_t(N), 0);
    for (int it = 0; it < 14; ++it) {
        for (int i = 0; i < N; ++i) {
            int b = 0; double bd = 1e300;
            for (int k = 0; k < K; ++k) { double d = D[size_t(i) * N + size_t(med[size_t(k)])]; if (d < bd) { bd = d; b = k; } }
            as[size_t(i)] = b;
        }
        bool changed = false;
        for (int k = 0; k < K; ++k) {
            int best = med[size_t(k)]; double bs = 1e300;
            for (int i = 0; i < N; ++i) {
                if (as[size_t(i)] != k) continue;
                double s = 0; for (int j = 0; j < N; ++j) if (as[size_t(j)] == k) s += D[size_t(i) * N + j];
                if (s < bs) { bs = s; best = i; }
            }
            if (best != med[size_t(k)]) { med[size_t(k)] = best; changed = true; }
        }
        if (!changed) break;
    }
    for (int i = 0; i < N; ++i) {                       // asignacion final con los medoides finales
        int b = 0; double bd = 1e300;
        for (int k = 0; k < K; ++k) { double d = D[size_t(i) * N + size_t(med[size_t(k)])]; if (d < bd) { bd = d; b = k; } }
        as[size_t(i)] = b;
    }
}

// Mediana 3x3 por canal entre pixeles opacos (quita el ruido de dither conservando los bordes).
inline Image median3(const Image& s, int thr) {
    Image o = s;
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) {
            if (s.at(x, y)[3] < thr) continue;
            uint8_t v[3][9]; int n = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                int yy = y + dy; if (yy < 0 || yy >= s.h) continue;
                for (int dx = -1; dx <= 1; ++dx) {
                    int xx = x + dx; if (xx < 0 || xx >= s.w) continue;
                    const uint8_t* p = s.at(xx, yy);
                    if (p[3] < thr) continue;
                    v[0][n] = p[0]; v[1][n] = p[1]; v[2][n] = p[2]; ++n;
                }
            }
            uint8_t* q = o.at(x, y);
            for (int c = 0; c < 3; ++c) { std::nth_element(v[c], v[c] + n / 2, v[c] + n); q[c] = v[c][n / 2]; }
        }
    return o;
}

// ------------------------------------------------------------ capas --------

inline void normalizeRaster(Result& r, int W, int H) {
    if (r.rasterW > 0 && r.rasterH > 0 && (r.rasterW != W || r.rasterH != H)) {
        const float sx = float(W) / r.rasterW, sy = float(H) / r.rasterH;
        for (auto& s : r.shapes) { s.cx *= sx; s.cy *= sy; s.w *= sx; s.h *= sy; }
    }
    r.rasterW = W; r.rasterH = H; r.aspect = H > 0 ? float(W) / float(H) : 1.f;
}

inline size_t opaqueCount(const Image& im, int thr) {
    size_t n = 0;
    for (size_t i = 3; i < im.rgba.size(); i += 4) if (im.rgba[i] >= thr) ++n;
    return n;
}

// Traza una capa (imagen con huecos transparentes) con el trazador interno elegido.
inline Result traceLayer(const Image& img, int budget, const Options& base, int inner) {
    Result out; out.rasterW = img.w; out.rasterH = img.h; out.aspect = img.h > 0 ? float(img.w) / img.h : 1.f;
    if (opaqueCount(img, base.alphaThreshold) == 0) return out;
    Options o = base;
    o.budget = std::clamp(budget, 1, 80000);
    o.maxRaster = std::clamp(std::max(img.w, img.h), 32, 2048);
    o.grayscale = false; o.mainChannels = false; o.colorChannels = false; o.maxChannels = 0;
    auto run = [&](Method m) {
        Result r = convert(img, m, o, nullptr);
        normalizeRaster(r, img.w, img.h);
        r.mono = false; r.colored = false; r.textRows.clear();
        return r;
    };
    if (inner == 0) return run(Method::BSP);
    if (inner == 1) return run(Method::Regions);
    Result a = run(Method::BSP), b = run(Method::Regions);
    double pa = computePsnr(img, renderShapes(a.shapes, img.w, img.h), base.alphaThreshold);
    double pb = computePsnr(img, renderShapes(b.shapes, img.w, img.h), base.alphaThreshold);
    return pb > pa + 0.15 ? b : a;                     // BSP salvo que Regiones sea claramente mejor
}

inline void paintOver(Image& dst, const Image& layer) {
    for (size_t p = 0; p < dst.rgba.size(); p += 4)
        if (layer.rgba[p + 3] > 0) { dst.rgba[p] = layer.rgba[p]; dst.rgba[p + 1] = layer.rgba[p + 1]; dst.rgba[p + 2] = layer.rgba[p + 2]; dst.rgba[p + 3] = 255; }
}

inline int maxZ(const Result& r) { int m = 0; for (auto& s : r.shapes) m = std::max<int>(m, s.z); return m; }

// ---- paleta de canales: corte mediano ponderado por area + Lloyd; cada forma pasa al color de paleta mas cercano ----
inline uint32_t colKey(uint8_t r, uint8_t g, uint8_t b) { return (uint32_t(r) << 16) | (uint32_t(g) << 8) | b; }
inline double colDist(const double* a, const double* b) {
    double dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
    return 2.0 * dr * dr + 4.0 * dg * dg + 3.0 * db * db;
}

inline std::vector<std::array<uint8_t, 3>> buildPalette(const std::map<uint32_t, double>& hist, int K) {
    struct C { double c[3]; double w; };
    std::vector<C> cols; cols.reserve(hist.size());
    for (auto& [k, w] : hist) cols.push_back({{double(k >> 16 & 255), double(k >> 8 & 255), double(k & 255)}, w});
    std::vector<std::vector<int>> boxes(1);
    for (int i = 0; i < int(cols.size()); ++i) boxes[0].push_back(i);
    auto range = [&](const std::vector<int>& b, int& axis) {
        double lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
        for (int i : b) for (int c = 0; c < 3; ++c) { lo[c] = std::min(lo[c], cols[size_t(i)].c[c]); hi[c] = std::max(hi[c], cols[size_t(i)].c[c]); }
        double best = -1; axis = 0;
        for (int c = 0; c < 3; ++c) { double r = (hi[c] - lo[c]) * (c == 1 ? 1.4 : c == 2 ? 1.1 : 1.0); if (r > best) { best = r; axis = c; } }
        return best;
    };
    while (int(boxes.size()) < K) {
        int pick = -1, pa = 0; double pbest = 0;
        for (int bi = 0; bi < int(boxes.size()); ++bi) {
            if (boxes[size_t(bi)].size() < 2) continue;
            int ax; double r = range(boxes[size_t(bi)], ax);
            double w = 0; for (int i : boxes[size_t(bi)]) w += cols[size_t(i)].w;
            double score = r * std::sqrt(w + 1.0);
            if (r > 0 && score > pbest) { pbest = score; pick = bi; pa = ax; }
        }
        if (pick < 0) break;
        auto b = std::move(boxes[size_t(pick)]);
        std::sort(b.begin(), b.end(), [&](int x, int y) { return cols[size_t(x)].c[pa] < cols[size_t(y)].c[pa]; });
        double tot = 0; for (int i : b) tot += cols[size_t(i)].w;
        double acc = 0; size_t cut = 1;
        for (size_t i = 0; i < b.size(); ++i) { acc += cols[size_t(b[i])].w; if (acc >= tot * 0.5) { cut = std::clamp<size_t>(i + 1, 1, b.size() - 1); break; } }
        boxes[size_t(pick)] = std::vector<int>(b.begin(), b.begin() + long(cut));
        boxes.push_back(std::vector<int>(b.begin() + long(cut), b.end()));
    }
    std::vector<std::array<double, 3>> cen(boxes.size());
    auto recenter = [&](const std::vector<std::vector<int>>& bx) {
        for (size_t bi = 0; bi < bx.size(); ++bi) {
            double acc[3] = {0, 0, 0}, w = 0;
            for (int i : bx[bi]) { for (int c = 0; c < 3; ++c) acc[c] += cols[size_t(i)].c[c] * cols[size_t(i)].w; w += cols[size_t(i)].w; }
            for (int c = 0; c < 3; ++c) cen[bi][size_t(c)] = w > 0 ? acc[c] / w : 0;
        }
    };
    recenter(boxes);
    for (int it = 0; it < 5; ++it) {                                          // Lloyd
        std::vector<std::vector<int>> nb(boxes.size());
        for (int i = 0; i < int(cols.size()); ++i) {
            size_t bj = 0; double bd = 1e300;
            for (size_t j = 0; j < cen.size(); ++j) { double d = colDist(cols[size_t(i)].c, cen[j].data()); if (d < bd) { bd = d; bj = j; } }
            nb[bj].push_back(i);
        }
        std::vector<std::vector<int>> keep; std::vector<std::array<double, 3>> old = cen;
        for (size_t j = 0; j < nb.size(); ++j) if (!nb[j].empty()) keep.push_back(std::move(nb[j]));
        boxes = std::move(keep); cen.assign(boxes.size(), {0, 0, 0});
        recenter(boxes);
    }
    std::vector<std::array<uint8_t, 3>> out;
    for (auto& c : cen) out.push_back({uint8_t(std::clamp(c[0] + 0.5, 0.0, 255.0)), uint8_t(std::clamp(c[1] + 0.5, 0.0, 255.0)), uint8_t(std::clamp(c[2] + 0.5, 0.0, 255.0))});
    return out;
}

// Capa = base BSP + rectangulos rotados colocados por busqueda guiada por el error, DIRECTAMENTE sobre lo que ya se ve
// (lazo cerrado). Despues se recolorea cada forma con lo que realmente queda visible, se quitan las tapadas y ese
// presupuesto se reinvierte. `layerImg` = lo que debe cubrir la capa (con huecos); `target` = el frame completo (opaco);
// `baseCanvas` = composite de las capas de abajo (nullptr = lienzo plano de color medio).
inline Result traceHybrid(const Image& layerImg, const Image& target, const Image* baseCanvas, int budget,
                          const Options& base, double bspFrac, int quality, uint32_t seed) {
    const int W = target.w, H = target.h;
    budget = std::max(1, budget);
    Result out = traceLayer(layerImg, std::max(1, int(budget * bspFrac)), base, 0);
    if (budget < int(out.shapes.size()) + 3) return out;
    uint8_t mr = 128, mg = 128, mb = 128;
    {
        double r = 0, g = 0, b = 0; size_t n = size_t(W) * H;
        for (size_t i = 0; i < n; ++i) { r += target.rgba[i * 4]; g += target.rgba[i * 4 + 1]; b += target.rgba[i * 4 + 2]; }
        mr = uint8_t(r / std::max<size_t>(1, n) + 0.5); mg = uint8_t(g / std::max<size_t>(1, n) + 0.5); mb = uint8_t(b / std::max<size_t>(1, n) + 0.5);
    }
    Geo geo(target, seed);
    auto setCanvas = [&](const std::vector<Shape>& sh) {
        Image c;
        if (baseCanvas) c = *baseCanvas;
        else { c.w = W; c.h = H; c.rgba.resize(size_t(W) * H * 4); for (size_t i = 0; i < size_t(W) * H; ++i) { c.rgba[i*4] = mr; c.rgba[i*4+1] = mg; c.rgba[i*4+2] = mb; c.rgba[i*4+3] = 255; } }
        paintOver(c, renderShapes(sh, W, H));
        geo.setCanvasImage(c, mr, mg, mb);
    };
    setCanvas(out.shapes);
    auto extra = geo.run(budget - int(out.shapes.size()), 1, quality, nullptr, 0.f, 0.f);
    out.shapes.insert(out.shapes.end(), extra.begin(), extra.end());
    for (int round = 0; round < 3; ++round) {
        refitVisible(out.shapes, target, 255, true);                       // recolorea con lo visible y quita lo tapado
        int freed = budget - int(out.shapes.size());
        if (freed < std::max(4, budget / 150)) break;
        int zNext = 1; for (auto const& s : out.shapes) zNext = std::max<int>(zNext, s.z + 1);
        if (zNext > 31000) break;
        setCanvas(out.shapes);
        auto more = geo.run(freed, zNext, quality, nullptr, 0.f, 0.f);
        if (more.empty()) break;
        out.shapes.insert(out.shapes.end(), more.begin(), more.end());
    }
    refitVisible(out.shapes, target, 255, true);
    if (int(out.shapes.size()) > budget) out.shapes.resize(size_t(budget));
    confineShapes(out.shapes, W, H);
    compactZ(out.shapes, W, H);
    return out;
}

// Zona donde el objetivo difiere de lo que ya se ve: sin motas aisladas, con margen y sin agujeros de 1 px.
inline std::vector<uint8_t> changeMask(const Image& smT, const Image& smC, int tol, int grow, int thr) {
    const int W = smT.w, H = smT.h;
    std::vector<uint8_t> raw(size_t(W) * H, 0), m(size_t(W) * H, 0);
    for (size_t p = 0; p < raw.size(); ++p) raw[p] = pixDiff(&smT.rgba[p * 4], &smC.rgba[p * 4], thr) > tol ? 1 : 0;
    auto nb = [&](const std::vector<uint8_t>& v, int x, int y) {
        int n = 0;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            int xx = x + dx, yy = y + dy; if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
            n += v[size_t(yy) * W + xx];
        }
        return n;
    };
    std::vector<uint8_t> clean = raw;
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x)
        if (raw[size_t(y) * W + x] && nb(raw, x, y) == 0) clean[size_t(y) * W + x] = 0;      // mota aislada
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {                                 // dilatar
        bool on = false;
        for (int dy = -grow; dy <= grow && !on; ++dy) for (int dx = -grow; dx <= grow; ++dx) {
            int xx = x + dx, yy = y + dy; if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
            if (clean[size_t(yy) * W + xx]) { on = true; break; }
        }
        m[size_t(y) * W + x] = on;
    }
    std::vector<uint8_t> f = m;
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x)                                   // cerrar agujeros
        if (!m[size_t(y) * W + x] && nb(m, x, y) >= 6) f[size_t(y) * W + x] = 1;
    return f;
}

inline Image maskedImage(const Image& target, const std::vector<uint8_t>& mask) {
    Image o; o.w = target.w; o.h = target.h; o.rgba.assign(target.rgba.size(), 0);
    for (size_t p = 0; p < mask.size(); ++p) if (mask[p]) for (int c = 0; c < 4; ++c) o.rgba[p * 4 + size_t(c)] = target.rgba[p * 4 + size_t(c)];
    return o;
}

inline int areaBudget(size_t opaquePx, int cap) {
    return int(std::clamp<size_t>(opaquePx / 3, 6, size_t(std::max(6, cap))));
}

} // namespace lay

// ============================================================================
//  Calculo de las capas
// ============================================================================

inline LayeredAnim buildLayered(const GifData& g, const GifPlan& plan, const Options& base, const LayeredOptions& lo,
                                Progress* prog, int threads) {
    LayeredAnim A;
    const int N = plan.count();
    if (N < 2 || g.frames.empty()) { A.note = "Hacen falta al menos 2 frames"; return A; }
    const int thr = std::clamp(base.alphaThreshold, 1, 255);
    const int tol = std::clamp(lo.tol, 4, 120);
    const double te = std::clamp<double>(lo.timeExp, 0.0, 1.0);
    auto wt = [&](double n) { return std::pow(std::max(1.0, n), te); };      // peso por tiempo visible

    // --- raster de trabajo comun (limitado para no gastar demasiada memoria con muchos frames)
    int mr = std::clamp(base.maxRaster, 64, 1024), W, H;
    fitSize(g.w, g.h, mr, W, H);
    while (int64_t(W) * H * N > 9000000LL && mr > 64) { mr = int(mr * 0.88); fitSize(g.w, g.h, mr, W, H); }
    A.W = W; A.H = H; A.frames = N;

    std::vector<Image> orig; orig.resize(size_t(N));              // para medir el PSNR
    std::vector<Image> fr; fr.resize(size_t(N));                  // lo que se traza (con o sin mediana 3x3)
    lay::parallelFor(N, threads, [&](int i) {
        Image im = resizeArea(g.frames[size_t(plan.idx[size_t(i)])], W, H);
        if (base.grayscale) im = toGrayscale(im);
        fr[size_t(i)] = lo.denoise ? lay::median3(im, thr) : im;
        orig[size_t(i)] = std::move(im);
    });
    std::vector<Image> sm; sm.resize(size_t(N));
    lay::parallelFor(N, threads, [&](int i) { sm[size_t(i)] = lay::blur3(fr[size_t(i)], thr); });
    report(prog, 0.04f);
    if (isCancelled(prog)) { A.cancelled = true; return A; }

    // --- still: mediana temporal; lo que cambia de forma caotica (mas de la mitad de los frames) se rellena con el entorno
    Image med = lay::temporalMedian(fr, thr);
    {
        Image smRef = lay::blur3(med, thr);
        std::vector<uint16_t> dev(size_t(W) * H, 0);
        for (int i = 0; i < N; ++i)
            for (size_t p = 0; p < dev.size(); ++p)
                if (lay::pixDiff(&sm[size_t(i)].rgba[p * 4], &smRef.rgba[p * 4], thr) > tol) ++dev[p];
        std::vector<uint8_t> valid(dev.size(), 1);
        size_t nBad = 0;
        for (size_t p = 0; p < dev.size(); ++p)
            if (med.rgba[p * 4 + 3] >= thr && int(dev[p]) * 2 > N) { valid[p] = 0; ++nBad; }
        if (nBad) lay::pushPull(med, valid);
    }
    bool opaqueAll = true;
    for (int i = 0; i < N && opaqueAll; ++i) if (lay::opaqueCount(orig[size_t(i)], thr) != size_t(W) * H) opaqueAll = false;
    const bool usePainter = lo.painter != 0 && opaqueAll;
    const int pq = std::clamp(std::max(base.quality, 1), 0, 3);
    const double bf = std::clamp<double>(lo.bspFrac, 0.05, 1.0);
    const Image smMed = lay::blur3(med, thr);

    // --- estimacion barata (a baja resolucion) de cuanto hay que pintar en cada caso
    //     coste de una capa  ~  area x sqrt(frames en los que se ve)   (tasa-distorsion: densidad de objetos ~ sqrt(tiempo visible))
    int sw, sh; fitSize(W, H, 96, sw, sh);
    const double sc = double(W) * H / (double(sw) * sh);
    std::vector<Image> lowRes; lowRes.resize(size_t(N));
    lay::parallelFor(N, threads, [&](int i) { lowRes[size_t(i)] = lay::blur3(resizeArea(fr[size_t(i)], sw, sh), thr); });
    const Image smallMed = lay::blur3(resizeArea(med, sw, sh), thr);
    auto areaOf = [&](const Image& t, const Image& c) {
        auto m = lay::changeMask(t, c, tol, 0, thr);
        size_t n = 0; for (uint8_t v : m) n += v;
        return double(n) * sc;
    };
    std::vector<double> a0; a0.assign(size_t(N), 0.0);                       // parche de cada frame directamente sobre el still
    lay::parallelFor(N, threads, [&](int i) { a0[size_t(i)] = areaOf(lowRes[size_t(i)], smallMed); });
    report(prog, 0.1f);

    const std::vector<float> D = lay::frameDistances(fr);
    struct Plan { std::vector<int> med, as; std::vector<char> use; double S = 0; };
    auto evalK = [&](int K) {
        Plan pl; lay::kMedoids(D, N, K, pl.med, pl.as);
        const int KK = int(pl.med.size());
        pl.use.assign(size_t(KK), 0);
        std::vector<double> a1; a1.assign(size_t(N), 0.0);
        lay::parallelFor(N, threads, [&](int i) {
            const int m = pl.med[size_t(pl.as[size_t(i)])];
            a1[size_t(i)] = (i == m) ? 0.0 : areaOf(lowRes[size_t(i)], lowRes[size_t(m)]);
        });
        for (int k = 0; k < KK; ++k) {
            double with = a0[size_t(pl.med[size_t(k)])], wo = 0; int n = 0;
            for (int i = 0; i < N; ++i) if (pl.as[size_t(i)] == k) { with += a1[size_t(i)]; wo += a0[size_t(i)]; ++n; }
            with = a0[size_t(pl.med[size_t(k)])] * wt(n) + (with - a0[size_t(pl.med[size_t(k)])]);
            if (n >= 2 && with < 0.9 * wo) { pl.use[size_t(k)] = 1; pl.S += with; } else pl.S += wo;
        }
        return pl;
    };
    Plan best;
    {
        std::vector<int> cand;
        if (lo.restCount > 0) cand = {std::clamp(lo.restCount, 1, std::min(12, N))};
        else for (int k : {1, 2, 3, 4, 6, 8, 12}) if (k <= std::min(12, N)) cand.push_back(k);
        bool first = true;
        for (int K : cand) {
            Plan pl = evalK(K);
            if (first || pl.S < best.S * 0.97) { best = std::move(pl); first = false; }      // prefiere pocos rests salvo mejora clara
        }
        A.restCandidates = int(cand.size());
    }
    // rests realmente usados, numerados por orden de aparicion
    {
        std::vector<int> remap(best.med.size(), -1);
        A.restOf.assign(size_t(N), -1);
        for (int i = 0; i < N; ++i) {
            int k = best.as[size_t(i)];
            if (!best.use[size_t(k)]) continue;
            if (remap[size_t(k)] < 0) { remap[size_t(k)] = int(A.medoid.size()); A.medoid.push_back(best.med[size_t(k)]); }
            A.restOf[size_t(i)] = remap[size_t(k)];
        }
    }
    const int K = int(A.medoid.size());
    std::vector<int> nUse; nUse.assign(size_t(std::max(K, 1)), 0);
    for (int i = 0; i < N; ++i) if (A.restOf[size_t(i)] >= 0) ++nUse[size_t(A.restOf[size_t(i)])];
    // --- reutilizacion: un frame casi igual a otro (mismo rest) muestra el parche de ese otro y no gasta objetos propios
    A.patchOf.assign(size_t(N), -1);
    std::vector<int> keepers;
    {
        // relativo al CONTENIDO (pixeles opacos del frame mas lleno), no al lienzo: un sprite pequeno en un lienzo transparente
        // grande tenia todos los frames "casi iguales" y se quedaba sin objetos
        double contentArea = 1; for (int i = 0; i < N; ++i) contentArea = std::max(contentArea, double(lay::opaqueCount(orig[size_t(i)], thr)));
        const double thrArea = double(std::clamp(lo.reuse, 0.f, 0.2f)) * contentArea;
        for (int i = 0; i < N; ++i) {
            int bestJ = -1; double bestA = thrArea;
            if (thrArea > 0) {
                for (int j : keepers) {
                    if (A.restOf[size_t(j)] != A.restOf[size_t(i)]) continue;
                    double ar = areaOf(lowRes[size_t(i)], lowRes[size_t(j)]);
                    if (ar <= bestA) { bestA = ar; bestJ = j; }
                }
            }
            if (bestJ >= 0) A.patchOf[size_t(i)] = bestJ;
            else { A.patchOf[size_t(i)] = i; keepers.push_back(i); }
        }
    }
    report(prog, 0.14f);
    if (isCancelled(prog)) { A.cancelled = true; return A; }

    // --- presupuesto total (igual que el metodo normal: perFrame x frames) y reparto
    const int trigEst = 4 * N + 2 * K + 8;
    const double T = std::max(40.0, std::min(double(lo.perFrame) * N, double(lo.maxTotal) - trigEst));
    A.budgetTotal = int(T);
    double Sest = double(W) * H * wt(N);
    for (int k = 0; k < K; ++k) Sest += a0[size_t(A.medoid[size_t(k)])] * wt(nUse[size_t(k)]);
    for (int i = 0; i < N; ++i) {
        if (A.patchOf[size_t(i)] != i) continue;                                       // frames que reutilizan parche no cuestan
        int k = A.restOf[size_t(i)];
        Sest += k < 0 ? a0[size_t(i)] : std::max(1.0, a0[size_t(i)] * 0.5);        // los parches con rest son bastante menores
    }
    const double d0 = T / std::max(1.0, Sest);
    const int stillBudget = int(std::clamp(d0 * W * H * wt(N), 12.0, T * 0.6));

    A.still = usePainter ? lay::traceHybrid(med, med, nullptr, stillBudget, base, std::clamp<double>(lo.stillBsp, 0.05, 1.0), pq, base.seed)
                         : lay::traceLayer(med, stillBudget, base, lo.inner);
    if (isCancelled(prog)) { A.cancelled = true; return A; }
    report(prog, 0.3f);

    Image C0 = med; for (size_t p = 3; p < C0.rgba.size(); p += 4) C0.rgba[p] = 0;     // lo que se ve: solo las formas
    lay::paintOver(C0, renderShapes(A.still.shapes, W, H));
    const Image smC0 = lay::blur3(C0, thr);

    // --- rests: solo lo que difiere del still (se reparte con la densidad restante)
    A.rests.resize(size_t(K));
    std::vector<Image> Ck; Ck.assign(size_t(K), C0);
    {
        std::vector<std::vector<uint8_t>> rmask; rmask.resize(size_t(K));
        std::vector<double> rarea; rarea.assign(size_t(K), 0.0);
        for (int k = 0; k < K; ++k) {
            rmask[size_t(k)] = lay::changeMask(sm[size_t(A.medoid[size_t(k)])], smC0, tol, lo.grow, thr);
            for (uint8_t v : rmask[size_t(k)]) rarea[size_t(k)] += v;
        }
        const double T1 = T - double(A.still.shapes.size());
        double S1 = 0;
        for (int k = 0; k < K; ++k) S1 += rarea[size_t(k)] * wt(nUse[size_t(k)]);
        for (int i = 0; i < N; ++i) { if (A.patchOf[size_t(i)] != i) continue; int k = A.restOf[size_t(i)]; S1 += k < 0 ? a0[size_t(i)] : std::max(1.0, a0[size_t(i)] * 0.5); }
        const double d1 = std::max(0.0, T1) / std::max(1.0, S1);
        lay::parallelFor(K, threads, [&](int k) {
            if (isCancelled(prog)) return;
            Image layer = lay::maskedImage(fr[size_t(A.medoid[size_t(k)])], rmask[size_t(k)]);
            int b = int(std::clamp(d1 * rarea[size_t(k)] * wt(nUse[size_t(k)]), 6.0, std::max(6.0, rarea[size_t(k)] / 1.5)));
            A.rests[size_t(k)] = usePainter ? lay::traceHybrid(layer, fr[size_t(A.medoid[size_t(k)])], &C0, b, base, bf, pq, base.seed + 101u + uint32_t(k))
                                            : lay::traceLayer(layer, b, base, lo.inner);
            lay::paintOver(Ck[size_t(k)], renderShapes(A.rests[size_t(k)].shapes, W, H));
        });
    }
    if (isCancelled(prog)) { A.cancelled = true; return A; }
    report(prog, 0.42f);

    // --- parches: solo lo que difiere de lo que ya se ve (still + rest); se reparte lo que queda
    std::vector<Image> smCk; smCk.resize(size_t(K));
    lay::parallelFor(K, threads, [&](int k) { smCk[size_t(k)] = lay::blur3(Ck[size_t(k)], thr); });
    std::vector<std::vector<uint8_t>> pmask; pmask.resize(size_t(N));
    std::vector<double> parea; parea.assign(size_t(N), 0.0);
    lay::parallelFor(N, threads, [&](int i) {
        if (A.patchOf[size_t(i)] != i) return;
        const int k = A.restOf[size_t(i)];
        pmask[size_t(i)] = lay::changeMask(sm[size_t(i)], k >= 0 ? smCk[size_t(k)] : smC0, tol, lo.grow, thr);
        double n = 0; for (uint8_t v : pmask[size_t(i)]) n += v;
        parea[size_t(i)] = n;
    });
    size_t used = A.still.shapes.size();
    for (auto& r : A.rests) used += r.shapes.size();
    double Sp = 0; for (double v : parea) Sp += v;
    const double dp = std::max(0.0, T - double(used)) / std::max(1.0, Sp);

    A.patches.resize(size_t(N));
    A.psnr.assign(size_t(N), 0.0);
    for (auto& r : A.patches) { r.rasterW = W; r.rasterH = H; r.aspect = H > 0 ? float(W) / H : 1.f; }
    std::atomic<int> done{0};
    lay::parallelFor(N, threads, [&](int i) {
        if (isCancelled(prog)) return;
        if (A.patchOf[size_t(i)] != i) return;                                         // reutiliza el de otro frame
        const int k = A.restOf[size_t(i)];
        if (parea[size_t(i)] > 0) {
            Image layer = lay::maskedImage(fr[size_t(i)], pmask[size_t(i)]);
            int b = int(std::clamp(dp * parea[size_t(i)], 6.0, std::max(6.0, parea[size_t(i)] / 1.5)));
            const Image& under = k >= 0 ? Ck[size_t(k)] : C0;
            A.patches[size_t(i)] = usePainter ? lay::traceHybrid(layer, fr[size_t(i)], &under, b, base, bf, pq, base.seed + 1009u + uint32_t(i))
                                              : lay::traceLayer(layer, b, base, lo.inner);
        }
        report(prog, 0.42f + 0.5f * float(done.fetch_add(1) + 1) / float(std::max<size_t>(1, keepers.size())));
    });
    if (isCancelled(prog)) { A.cancelled = true; return A; }
    for (int i = 0; i < N; ++i)                                                        // un parche vacio no se muestra
        if (A.patchOf[size_t(i)] >= 0 && A.patches[size_t(A.patchOf[size_t(i)])].shapes.empty()) A.patchOf[size_t(i)] = -1;
    // --- colores en canales: una paleta comun a todas las capas (cada color = un canal; el canal 1 nunca)
    A.firstChannel = std::clamp(lo.firstChannel, 2, 998);
    if (lo.channels > 0) {
        const int K2 = std::clamp(lo.channels, 2, 999 - A.firstChannel + 1);
        std::map<uint32_t, double> hist;
        auto addHist = [&](const Result& r) { for (auto& sh : r.shapes) hist[lay::colKey(sh.r, sh.g, sh.b)] += double(sh.w) * sh.h + 1.0; };
        addHist(A.still); for (auto& r : A.rests) addHist(r); for (auto& r : A.patches) addHist(r);
        auto pal = lay::buildPalette(hist, K2);
        std::map<uint32_t, int> cache;
        std::vector<char> used(pal.size(), 0);
        auto nearest = [&](uint8_t r, uint8_t g, uint8_t b) {
            uint32_t k = lay::colKey(r, g, b);
            auto it = cache.find(k); if (it != cache.end()) return it->second;
            double c[3] = {double(r), double(g), double(b)}; int bi = 0; double bd = 1e300;
            for (size_t j = 0; j < pal.size(); ++j) { double pc[3] = {double(pal[j][0]), double(pal[j][1]), double(pal[j][2])}; double d = lay::colDist(c, pc); if (d < bd) { bd = d; bi = int(j); } }
            cache[k] = bi; return bi;
        };
        auto remap = [&](Result& r) {
            for (auto& sh : r.shapes) { int bi = nearest(sh.r, sh.g, sh.b); sh.r = pal[size_t(bi)][0]; sh.g = pal[size_t(bi)][1]; sh.b = pal[size_t(bi)][2]; used[size_t(bi)] = 1; }
            r.colored = true; r.mono = false;
        };
        remap(A.still); for (auto& r : A.rests) remap(r); for (auto& r : A.patches) remap(r);
        for (size_t j = 0; j < pal.size(); ++j) if (used[j]) A.palette.push_back(pal[j]);
    }
    lay::parallelFor(N, threads, [&](int i) {                                          // fidelidad real de cada frame montado
        const int k = A.restOf[size_t(i)], j = A.patchOf[size_t(i)];
        Image comp; comp.w = W; comp.h = H; comp.rgba.assign(size_t(W) * H * 4, 0);
        lay::paintOver(comp, renderShapes(A.still.shapes, W, H));
        if (k >= 0) lay::paintOver(comp, renderShapes(A.rests[size_t(k)].shapes, W, H));
        if (j >= 0) lay::paintOver(comp, renderShapes(A.patches[size_t(j)].shapes, W, H));
        A.psnr[size_t(i)] = computePsnr(orig[size_t(i)], comp, thr);
    });

    A.zRest = lay::maxZ(A.still) + 1;
    { int mz = 0; for (auto& r : A.rests) mz = std::max(mz, lay::maxZ(r)); A.zPatch = A.zRest + mz + 1; }
    double sum = 0, mn = 1e9;
    for (double p : A.psnr) { sum += p; mn = std::min(mn, p); }
    A.psnrAvg = sum / N; A.psnrMin = mn;
    report(prog, 1.f);
    return A;
}

// Frame `f` montado como un unico Result (still + rest + parche, con z-order por capa) para la vista previa.
inline Result layeredFrameResult(const LayeredAnim& a, int f) {
    Result r;
    r.rasterW = a.W; r.rasterH = a.H; r.aspect = a.H > 0 ? float(a.W) / a.H : 1.f;
    if (f < 0 || f >= a.frames) return r;
    int z = 0;
    auto add = [&](const std::vector<Shape>& sh) {                       // z global y unico: capa de abajo primero
        std::vector<Shape> v = sh;
        std::stable_sort(v.begin(), v.end(), [](const Shape& x, const Shape& y) { return x.z < y.z; });
        for (Shape s : v) { s.z = int16_t(std::min(z++, 32000)); r.shapes.push_back(s); }
    };
    add(a.still.shapes);
    if (a.restOf[size_t(f)] >= 0) add(a.rests[size_t(a.restOf[size_t(f)])].shapes);
    if (a.patchOf[size_t(f)] >= 0) add(a.patches[size_t(a.patchOf[size_t(f)])].shapes);
    r.psnr = f < int(a.psnr.size()) ? a.psnr[size_t(f)] : 0;
    return r;
}

// ============================================================================
//  Horario de triggers (que se muestra/oculta en cada frame) y recuento
// ============================================================================

struct LayerStep { int showPatch = -1, hidePatch = -1, showRest = -1, hideRest = -1;
                   int actions() const { return (showPatch >= 0) + (hidePatch >= 0) + (showRest >= 0) + (hideRest >= 0); } };

struct LayeredSchedule {
    std::vector<LayerStep> step;        // por frame; indices = frame (parche) o rest (rest)
    std::vector<int> restLayer;         // rest -> rest (si tiene objetos) o -1
    std::vector<int> patchLayer;        // frame -> frame (si tiene objetos) o -1
    std::vector<int> initHidePatch, initHideRest;
    int nRests = 0, nPatches = 0, nSteps = 0;
    int groups = 0, triggers = 0;
};

inline LayeredSchedule scheduleLayered(const LayeredAnim& a) {
    LayeredSchedule s;
    const int N = a.frames, K = int(a.rests.size());
    s.restLayer.assign(size_t(K), -1); s.patchLayer.assign(size_t(N), -1); s.step.assign(size_t(N), LayerStep{});
    for (int k = 0; k < K; ++k) if (!a.rests[size_t(k)].shapes.empty()) { s.restLayer[size_t(k)] = k; ++s.nRests; }
    for (int i = 0; i < N; ++i) {                                              // patchLayer = frame "representante" cuyo parche se muestra
        int j = a.patchOf[size_t(i)];
        if (j >= 0 && !a.patches[size_t(j)].shapes.empty()) { s.patchLayer[size_t(i)] = j; if (j == i) ++s.nPatches; }
    }
    auto restLayerOf = [&](int i) { int k = a.restOf[size_t(i)]; return k >= 0 ? s.restLayer[size_t(k)] : -1; };
    int visP = s.patchLayer[size_t(N - 1)];                                   // estado al final del bucle anterior
    int visR = restLayerOf(N - 1);
    for (int i = 0; i < N; ++i) {
        LayerStep& st = s.step[size_t(i)];
        const int curP = s.patchLayer[size_t(i)], curR = restLayerOf(i);
        if (curP != visP) { st.hidePatch = visP; st.showPatch = curP; visP = curP; }
        if (curR != visR) { st.hideRest = visR; st.showRest = curR; visR = curR; }
        if (st.actions() > 0) ++s.nSteps;
    }
    const int p0 = s.patchLayer[0], r0 = restLayerOf(0);
    for (int i = 0; i < N; ++i) if (s.patchLayer[size_t(i)] == i && i != p0) s.initHidePatch.push_back(i);
    for (int k = 0; k < K; ++k) if (s.restLayer[size_t(k)] >= 0 && s.restLayer[size_t(k)] != r0) s.initHideRest.push_back(k);
    const bool hasI = !s.initHidePatch.empty() || !s.initHideRest.empty();
    int acts = 0; for (auto& st : s.step) acts += st.actions();
    s.groups = s.nRests + s.nPatches + s.nSteps + 1 + (hasI ? 1 : 0);
    s.triggers = s.nSteps + 1 + acts + int(s.initHidePatch.size() + s.initHideRest.size()) + 1 + (hasI ? 1 : 0) + (hasI ? 1 : 0) + int(a.palette.size());     // + Spawn(I) por vuelta + un Color trigger por canal
    return s;
}

inline int layeredGroupCount(const LayeredAnim& a) { return scheduleLayered(a).groups; }
inline int layeredTriggerCount(const LayeredAnim& a) { return scheduleLayered(a).triggers; }

// ============================================================================
//  Cadena de GD
// ============================================================================

inline std::string buildLayeredString(const LayeredAnim& a, const GifPlan& plan, const GifExportParams& p, std::string* errmsg = nullptr) {
    auto fail = [&](const char* m) { if (errmsg) *errmsg = m; return std::string(); };
    if (!a.ok() || plan.count() != a.frames) return fail("No hay capas calculadas");
    const LayeredSchedule S = scheduleLayered(a);
    if (p.baseGroup < 1 || p.baseGroup + S.groups - 1 > 9999) return fail("No quedan grupos libres suficientes (max. 9999)");
    const bool useCh = !a.palette.empty();
    if (useCh && (a.firstChannel < 2 || a.firstChannel + int(a.palette.size()) - 1 > 999)) return fail("No caben los canales de color (2..999)");
    std::map<uint32_t, int> chOf;                                   // color -> canal (nunca el 1)
    for (size_t i = 0; i < a.palette.size(); ++i) chOf[lay::colKey(a.palette[i][0], a.palette[i][1], a.palette[i][2])] = a.firstChannel + int(i);
    const int N = a.frames, K = int(a.rests.size());

    // ids de grupo: rests, parches, pasos F, bucle L, inicio I
    int next = p.baseGroup;
    std::vector<int> gRest(size_t(K), 0), gPatch(size_t(N), 0), gStep(size_t(N), 0);
    for (int k = 0; k < K; ++k) if (S.restLayer[size_t(k)] >= 0) gRest[size_t(k)] = next++;
    for (int i = 0; i < N; ++i) if (S.patchLayer[size_t(i)] == i) gPatch[size_t(i)] = next++;
    for (int i = 0; i < N; ++i) if (S.step[size_t(i)].actions() > 0) gStep[size_t(i)] = next++;
    const int L = next++;
    const bool hasI = !S.initHidePatch.empty() || !S.initHideRest.empty();
    const int I = hasI ? next++ : 0;

    std::string out;
    out.reserve(a.objectCount() * 100 + size_t(S.triggers) * 90);

    auto emit = [&](const Result& r, int group, int zTier) {
        if (r.shapes.empty()) return;
        GdExportOptions eo = p.eo;
        eo.groupStr = group > 0 ? std::to_string(group) : std::string();
        eo.zBase = p.eo.zBase + zTier;
        eo.monoFastPath = false;
        if (useCh) {                                               // orden de aparicion de los colores de ESTA capa -> canal global
            ChannelMap cm = buildChannelMap(r);
            eo.channels.assign(cm.colors.size(), 0);
            for (size_t i = 0; i < cm.colors.size(); ++i) eo.channels[i] = chOf.at(lay::colKey(cm.colors[i][0], cm.colors[i][1], cm.colors[i][2]));
        }
        std::string s = toGdString(r, eo);
        if (!s.empty()) { out += s; out += ';'; }
    };
    // z-order: fondo < rests < parches, y cada rest/parche en su PROPIA banda de z (disjuntas; la mas alta = la primera). Asi las capas
    // que no estan visibles a la vez nunca se intercalan entre si, ni siquiera apiladas en el editor. Si no cabe (> 30000) se comparte.
    std::vector<int> zRestB(size_t(std::max(K, 0)), a.zRest), zPatchB(size_t(N), a.zPatch);
    {
        long total = a.zRest;
        for (int k = 0; k < K; ++k) total += lay::maxZ(a.rests[size_t(k)]) + 1;
        for (int i = 0; i < N; ++i) total += lay::maxZ(a.patches[size_t(i)]) + 1;
        if (total <= 30000) {
            long acc = a.zRest;
            for (int k = K - 1; k >= 0; --k) { zRestB[size_t(k)] = int(acc); acc += lay::maxZ(a.rests[size_t(k)]) + 1; }
            for (int i = N - 1; i >= 0; --i) { zPatchB[size_t(i)] = int(acc); acc += lay::maxZ(a.patches[size_t(i)]) + 1; }
        }
    }
    emit(a.still, 0, 0);
    for (int k = 0; k < K; ++k) emit(a.rests[size_t(k)], gRest[size_t(k)], zRestB[size_t(k)]);
    for (int i = 0; i < N; ++i) emit(a.patches[size_t(i)], gPatch[size_t(i)], zPatchB[size_t(i)]);

    // --- triggers (se mueven al principio de la cadena al final: ver triggersFirst)
    const size_t objEnd = out.size();
    GdSize sz = gdImageSize(a.still, p.eo);
    detail::TrigBoard board(detail::rowLeft(p.eo.originX - sz.w * 0.5, p.trigX), p.eo.originY + sz.h * 0.5 + 90.0);
    auto posA = [&](double& x, double& y) { board.atAlpha(x, y); };
    auto posS = [&](double& x, double& y) { board.atSpawn(x, y); };
    double x, y;

    double t = 0;                                              // bucle L
    for (int i = 0; i < N; ++i) {
        if (gStep[size_t(i)] > 0) { posS(x, y); detail::addSpawn(out, p, x, y, gStep[size_t(i)], t, L); }
        t += plan.delay[size_t(i)];
    }
    posS(x, y); detail::addSpawn(out, p, x, y, L, plan.total, L);
    if (hasI) { posS(x, y); detail::addSpawn(out, p, x, y, I, 0, L); }     // autocorreccion en cada vuelta
    board.newBlock();

    for (int i = 0; i < N; ++i) {                              // pasos F_i
        const LayerStep& st = S.step[size_t(i)];
        const int F = gStep[size_t(i)];
        if (F <= 0) continue;
        if (st.hidePatch >= 0) { posA(x, y); detail::addAlpha(out, p, x, y, gPatch[size_t(st.hidePatch)], 0, F); }
        if (st.showPatch >= 0) { posA(x, y); detail::addAlpha(out, p, x, y, gPatch[size_t(st.showPatch)], 1, F); }
        if (st.hideRest >= 0)  { posA(x, y); detail::addAlpha(out, p, x, y, gRest[size_t(st.hideRest)], 0, F); }
        if (st.showRest >= 0)  { posA(x, y); detail::addAlpha(out, p, x, y, gRest[size_t(st.showRest)], 1, F); }
    }
    board.newBlock();
    if (hasI) {                                                // inicio I: ocultar lo que no se ve en el frame 0
        for (int i : S.initHidePatch) { posA(x, y); detail::addAlpha(out, p, x, y, gPatch[size_t(i)], 0, I); }
        for (int k : S.initHideRest)  { posA(x, y); detail::addAlpha(out, p, x, y, gRest[size_t(k)], 0, I); }
    }
    board.newBlock();
    double sx = p.trigX, sy = board.nextY();
    detail::addSpawn(out, p, sx, board.rowY(), L, 0, 0);          // arranque: lanza L
    if (hasI) detail::addSpawn(out, p, sx, board.rowY() + 30.0, I, 0, 0);   // y lanza I, justo ENCIMA (los dos de arranque van apilados)

    if (useCh) {                                               // Color triggers (id 899) al inicio: fijan el color de cada canal
        for (size_t i = 0; i < a.palette.size(); ++i) {
            double cx, cy; detail::colorTriggerPos(int(i), p.trigX, sy, cx, cy);
            detail::addTriggerHead(out, 899, cx, cy);
            out += ",7,"; out += std::to_string(a.palette[i][0]);
            out += ",8,"; out += std::to_string(a.palette[i][1]);
            out += ",9,"; out += std::to_string(a.palette[i][2]);
            out += ",10,0,35,1,17,0,23,"; out += std::to_string(a.firstChannel + int(i));
            out += ';';
        }
    }
    detail::triggersFirst(out, objEnd);
    if (!out.empty() && out.back() == ';') out.pop_back();
    return out;
}

} // namespace gdx
