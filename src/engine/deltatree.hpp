// ============================================================================
//  gdx :: Metodo 19 "Arbol de deltas"  --  codificacion predictiva con un arbol de frames
//  Sin dependencias de Geode. No modifica ningun otro metodo (reutiliza helpers de layered.hpp).
//
//  Cada frame se predice del frame MAS PARECIDO (arbol de expansion minima sobre el area que cambia entre pares de
//  frames; la raiz es el fondo). Un frame solo pinta una DELTA: lo que difiere del composite real (en lazo cerrado)
//  de su padre. Lo que se ve en el frame i es el camino  fondo -> ... -> padre -> i : las deltas se apilan encima
//  (z creciente) y solo se ocultan cuando se cambia de rama. Pintar solo el ~15-25 % del area permite gastar muchisimos
//  mas objetos por pixel que cambia.  El reparto sigue la regla del metodo 18: densidad ~ (frames que ven la capa)^0,3.
// ============================================================================
#pragma once
#include <cstdio>
#include <set>
#include "layered.hpp"

namespace gdx {
namespace tree {

// ---- paleta de canales para un conjunto de capas (igual que en el metodo 18) ----
inline std::vector<std::array<uint8_t, 3>> quantizeResults(const std::vector<Result*>& rs, int K) {
    std::map<uint32_t, double> hist;
    for (auto* r : rs) for (auto& sh : r->shapes) hist[lay::colKey(sh.r, sh.g, sh.b)] += double(sh.w) * sh.h + 1.0;
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
    for (auto* r : rs) {
        for (auto& sh : r->shapes) { int bi = nearest(sh.r, sh.g, sh.b); sh.r = pal[size_t(bi)][0]; sh.g = pal[size_t(bi)][1]; sh.b = pal[size_t(bi)][2]; used[size_t(bi)] = 1; }
        r->colored = true; r->mono = false;
    }
    for (size_t j = 0; j < pal.size(); ++j) if (used[j]) out.push_back(pal[j]);
    return out;
}

// ---- horario: capas visibles en cada frame = camino raiz -> frame (solo capas con objetos) ----
struct Sched {
    std::vector<std::vector<int>> path, hide, show;       // por frame
    std::vector<int> initHide;                            // capas que no se ven en el frame 0
    std::vector<int> layerOf;                             // frame -> su propia capa (el mismo indice) o -1 si la delta esta vacia
    int nLayers = 0, nSteps = 0, groups = 0, triggers = 0;
};

inline Sched schedule(const LayeredAnim& a) {
    Sched s; const int N = a.frames;
    s.path.assign(size_t(N), {}); s.hide.assign(size_t(N), {}); s.show.assign(size_t(N), {}); s.layerOf.assign(size_t(N), -1);
    for (int i = 0; i < N; ++i) if (!a.patches[size_t(i)].shapes.empty()) { s.layerOf[size_t(i)] = i; ++s.nLayers; }
    std::vector<char> done; done.assign(size_t(N), 0);
    std::vector<int> stack;
    for (int i = 0; i < N; ++i) {                                   // camino = camino del padre + la propia capa
        stack.clear(); int c = i;
        while (c >= 0 && !done[size_t(c)]) { stack.push_back(c); c = a.parent[size_t(c)]; }
        while (!stack.empty()) {
            int n = stack.back(); stack.pop_back();
            int p = a.parent[size_t(n)];
            if (p >= 0) s.path[size_t(n)] = s.path[size_t(p)];
            if (s.layerOf[size_t(n)] >= 0) s.path[size_t(n)].push_back(n);
            done[size_t(n)] = 1;
        }
    }
    auto diff = [](const std::vector<int>& x, const std::vector<int>& y) {       // x \ y
        std::set<int> sy(y.begin(), y.end()); std::vector<int> o;
        for (int v : x) if (!sy.count(v)) o.push_back(v);
        return o;
    };
    int acts = 0;
    for (int i = 0; i < N; ++i) {
        const std::vector<int>& prev = s.path[size_t(i == 0 ? N - 1 : i - 1)];
        s.hide[size_t(i)] = diff(prev, s.path[size_t(i)]);
        s.show[size_t(i)] = diff(s.path[size_t(i)], prev);
        int n = int(s.hide[size_t(i)].size() + s.show[size_t(i)].size());
        if (n > 0) { ++s.nSteps; acts += n; }
    }
    s.initHide = diff([&] { std::vector<int> all; for (int i = 0; i < N; ++i) if (s.layerOf[size_t(i)] >= 0) all.push_back(i); return all; }(), s.path[0]);
    const bool hasI = !s.initHide.empty();
    s.groups = s.nLayers + s.nSteps + 1 + (hasI ? 1 : 0);
    s.triggers = s.nSteps + 1 + acts + int(s.initHide.size()) + 1 + (hasI ? 2 : 0) + int(a.palette.size());
    return s;
}

} // namespace tree

// ============================================================================
//  Calculo del arbol
// ============================================================================
inline LayeredAnim buildTree(const GifData& g, const GifPlan& plan, const Options& base, const LayeredOptions& lo,
                             Progress* prog, int threads) {
    LayeredAnim A; A.tree = true;
    const int N = plan.count();
    if (N < 2 || g.frames.empty()) { A.note = "Hacen falta al menos 2 frames"; return A; }
    const int thr = std::clamp(base.alphaThreshold, 1, 255);
    const int tol = std::clamp(lo.tol, 4, 120);
    const double te = std::clamp<double>(lo.timeExp, 0.0, 1.0);
    auto wt = [&](double n) { return std::pow(std::max(1.0, n), te); };

    int mr = std::clamp(base.maxRaster, 64, 1024), W, H;
    fitSize(g.w, g.h, mr, W, H);
    while (int64_t(W) * H * N > 10000000LL && mr > 64) { mr = int(mr * 0.88); fitSize(g.w, g.h, mr, W, H); }
    A.W = W; A.H = H; A.frames = N;

    std::vector<Image> orig; orig.resize(size_t(N));
    std::vector<Image> fr; fr.resize(size_t(N));
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

    // --- fondo (raiz del arbol): mediana temporal; lo caotico se rellena con el entorno
    Image med = lay::temporalMedian(fr, thr);
    {
        Image smRef = lay::blur3(med, thr);
        std::vector<uint16_t> dev(size_t(W) * H, 0);
        for (int i = 0; i < N; ++i)
            for (size_t p = 0; p < dev.size(); ++p)
                if (lay::pixDiff(&sm[size_t(i)].rgba[p * 4], &smRef.rgba[p * 4], thr) > tol) ++dev[p];
        std::vector<uint8_t> valid(dev.size(), 1); size_t nBad = 0;
        for (size_t p = 0; p < dev.size(); ++p) if (med.rgba[p * 4 + 3] >= thr && int(dev[p]) * 2 > N) { valid[p] = 0; ++nBad; }
        if (nBad) lay::pushPull(med, valid);
    }
    bool opaqueAll = true;
    for (int i = 0; i < N && opaqueAll; ++i) if (lay::opaqueCount(orig[size_t(i)], thr) != size_t(W) * H) opaqueAll = false;
    const bool usePainter = lo.painter != 0 && opaqueAll;
    const int pq = std::clamp(std::max(base.quality, 1), 0, 3);
    const double bf = std::clamp<double>(lo.bspFrac, 0.05, 1.0);

    // --- area que cambia entre cada par de frames (a baja resolucion) -> arbol de expansion minima (Prim) con raiz = fondo
    int sw, sh; fitSize(W, H, N > 120 ? 64 : 96, sw, sh);
    const double sc = double(W) * H / (double(sw) * sh);
    std::vector<Image> low; low.resize(size_t(N));
    lay::parallelFor(N, threads, [&](int i) { low[size_t(i)] = lay::blur3(resizeArea(fr[size_t(i)], sw, sh), thr); });
    const Image lowMed = lay::blur3(resizeArea(med, sw, sh), thr);
    auto diffArea = [&](const Image& a, const Image& b) {
        size_t n = 0;
        for (size_t p = 0; p < size_t(sw) * sh; ++p) if (lay::pixDiff(&a.rgba[p * 4], &b.rgba[p * 4], thr) > tol) ++n;
        return double(n) * sc;
    };
    std::vector<double> a0(size_t(N), 0.0), D(size_t(N) * N, 0.0);
    lay::parallelFor(N, threads, [&](int i) {
        a0[size_t(i)] = diffArea(low[size_t(i)], lowMed);
        for (int j = i + 1; j < N; ++j) D[size_t(i) * N + j] = D[size_t(j) * N + i] = diffArea(low[size_t(i)], low[size_t(j)]);
    });
    report(prog, 0.12f);
    A.parent.assign(size_t(N), -1);
    std::vector<int> order; order.reserve(size_t(N));
    std::vector<double> best; best.assign(size_t(N), 0.0); std::vector<char> inT; inT.assign(size_t(N), 0);
    for (int i = 0; i < N; ++i) best[size_t(i)] = a0[size_t(i)];
    for (int it = 0; it < N; ++it) {
        int pick = -1; double bv = 1e300;
        for (int k = 0; k < N; ++k) if (!inT[size_t(k)] && best[size_t(k)] < bv) { bv = best[size_t(k)]; pick = k; }
        inT[size_t(pick)] = 1; order.push_back(pick);
        for (int k = 0; k < N; ++k) {
            if (inT[size_t(k)]) continue;
            double w = D[size_t(pick) * N + k] + 1e-3 * std::abs(pick - k);          // desempate: el frame mas cercano en el tiempo
            if (w < best[size_t(k)]) { best[size_t(k)] = w; A.parent[size_t(k)] = pick; }
        }
    }
    bool opaqueTree = true;
    for (int i = 0; i < N && opaqueTree; ++i) if (lay::opaqueCount(orig[size_t(i)], thr) != size_t(W) * H) opaqueTree = false;
    if (!opaqueTree) {                       // con transparencia una delta no puede borrar lo del padre: cada frame va sobre el fondo
        for (int i = 0; i < N; ++i) { A.parent[size_t(i)] = -1; order[size_t(i)] = i; }
    }
    std::vector<int> depth; depth.assign(size_t(N), 1); std::vector<int> sub; sub.assign(size_t(N), 1);
    for (int i : order) depth[size_t(i)] = A.parent[size_t(i)] < 0 ? 1 : depth[size_t(A.parent[size_t(i)])] + 1;
    for (int k = N - 1; k >= 0; --k) { int i = order[size_t(k)]; if (A.parent[size_t(i)] >= 0) sub[size_t(A.parent[size_t(i)])] += sub[size_t(i)]; }
    int maxD = 0; for (int d : depth) maxD = std::max(maxD, d);
    A.depthMax = maxD;
    report(prog, 0.16f);
    if (isCancelled(prog)) { A.cancelled = true; return A; }

    // --- presupuesto total: reserva el espacio para Color Triggers antes de trazar.
    const int trigEst = 6 * N + 16;
    A.firstChannel = std::clamp(lo.firstChannel, 2, 998);
    const int paletteBudget = lo.channels >= 2
        ? std::clamp(std::min(lo.channels, lo.maxTotal - trigEst - 40), 0, 999 - A.firstChannel + 1)
        : 0;
    if (lo.channels >= 2 && paletteBudget < lo.channels)
        A.note = "Paleta limitada a " + std::to_string(paletteBudget) + " canales para respetar el tope total.";
    const double shapeLimit = std::max(40.0, double(lo.maxTotal) - trigEst - paletteBudget);
    const double T = std::min(double(lo.perFrame) * N, shapeLimit);
    A.budgetTotal = int(T);
    const double reuseArea = double(std::clamp(lo.reuse, 0.f, 0.2f)) * double(W) * H;
    A.patches.resize(size_t(N));
    for (auto& r : A.patches) { r.rasterW = W; r.rasterH = H; r.aspect = H > 0 ? float(W) / H : 1.f; }
    std::vector<Image> comp; comp.resize(size_t(N));
    std::vector<std::vector<int>> levels; levels.resize(size_t(maxD) + 1);
    for (int i : order) levels[size_t(depth[size_t(i)])].push_back(i);

    // Un recorrido por niveles (padres antes que hijos; en paralelo dentro de cada nivel), SIEMPRE en lazo cerrado:
    // la delta de un frame es lo que difiere del composite REAL de su padre (incluido el error de aproximacion de este).
    // finalPass=false: trazado rapido (solo BSP) para MEDIR las areas reales; true: trazado final con presupuesto repartido.
    std::vector<double> area1; area1.assign(size_t(N), 0.0);
    auto runPass = [&](bool finalPass, const Image& stillComp, double dens) {
        for (auto& r : A.patches) { r.shapes.clear(); }
        for (int d = 1; d <= maxD; ++d) {
            lay::parallelFor(int(levels[size_t(d)].size()), threads, [&](int li) {
                if (isCancelled(prog)) return;
                const int i = levels[size_t(d)][size_t(li)], p = A.parent[size_t(i)];
                const Image& under = p < 0 ? stillComp : comp[size_t(p)];
                auto mask = lay::changeMask(sm[size_t(i)], lay::blur3(under, thr), tol, lo.grow, thr);
                double area = 0; for (uint8_t v : mask) area += v;
                comp[size_t(i)] = under;
                if (!finalPass) area1[size_t(i)] = area;
                if (area <= std::max(reuseArea, 1.0)) return;                      // casi igual que el padre: sin delta, cero objetos
                Image layer = lay::maskedImage(fr[size_t(i)], mask);
                if (!finalPass) {
                    int b = int(std::clamp(0.025 * area, 6.0, std::max(6.0, area / 1.5)));
                    A.patches[size_t(i)] = lay::traceLayer(layer, b, base, 0);
                } else {
                    int b = int(std::clamp(dens * std::max(area1[size_t(i)], area * 0.5) * wt(sub[size_t(i)]), 6.0, std::max(6.0, area / 1.5)));
                    A.patches[size_t(i)] = usePainter ? lay::traceHybrid(layer, fr[size_t(i)], &under, b, base, bf, pq, base.seed + 1009u + uint32_t(i))
                                                      : lay::traceLayer(layer, b, base, lo.inner);
                }
                lay::paintOver(comp[size_t(i)], renderShapes(A.patches[size_t(i)].shapes, W, H));
            });
            if (isCancelled(prog)) return;
        }
    };

    // pasada 0/1: fondo provisional (BSP) + medicion de areas reales
    {
        Image pm = med;
        Result still0 = lay::traceLayer(med, int(std::max(40.0, T * 0.3)), base, 0);
        Image C00 = pm; for (size_t q = 3; q < C00.rgba.size(); q += 4) C00.rgba[q] = 0;
        lay::paintOver(C00, renderShapes(still0.shapes, W, H));
        runPass(false, C00, 0.0);
        if (isCancelled(prog)) { A.cancelled = true; return A; }
    }
    report(prog, 0.3f);
    double Sest = double(W) * H * wt(N), Sd = 0;
    for (int i = 0; i < N; ++i) if (area1[size_t(i)] > std::max(reuseArea, 1.0)) Sd += area1[size_t(i)] * wt(sub[size_t(i)]);
    const double d0 = T / std::max(1.0, Sest + Sd);
    const int stillBudget = int(std::clamp(d0 * W * H * wt(N), 12.0, T * 0.6));
    A.still = usePainter ? lay::traceHybrid(med, med, nullptr, stillBudget, base, std::clamp<double>(lo.stillBsp, 0.05, 1.0), pq, base.seed)
                         : lay::traceLayer(med, stillBudget, base, lo.inner);
    if (isCancelled(prog)) { A.cancelled = true; return A; }
    report(prog, 0.45f);
    Image C0 = med; for (size_t q = 3; q < C0.rgba.size(); q += 4) C0.rgba[q] = 0;
    lay::paintOver(C0, renderShapes(A.still.shapes, W, H));
    // pasada final: lo que no gasto el fondo (BSP puede quedarse corto) se reparte entre las deltas
    const double dens = std::max(0.0, T - double(A.still.shapes.size())) / std::max(1.0, Sd);
    runPass(true, C0, dens);
    if (isCancelled(prog)) { A.cancelled = true; return A; }
    report(prog, 0.9f);

    // --- colores en canales (paleta comun, el canal 1 nunca)
    if (paletteBudget >= 2) {
        std::vector<Result*> all{&A.still};
        for (auto& r : A.patches) all.push_back(&r);
        A.palette = tree::quantizeResults(all, paletteBudget);
    }
    // --- z base de cada delta: encima de todo su camino
    A.zNode.assign(size_t(N), 0);
    {
        const int zStill = lay::maxZ(A.still) + 1;
        for (int i : order) {
            const int p = A.parent[size_t(i)];
            A.zNode[size_t(i)] = p < 0 ? zStill : A.zNode[size_t(p)] + (A.patches[size_t(p)].shapes.empty() ? 0 : lay::maxZ(A.patches[size_t(p)]) + 1);
        }
        A.zRest = zStill; A.zPatch = zStill;
    }
    // --- fidelidad real de cada frame ya montado (con los colores de paleta)
    A.psnr.assign(size_t(N), 0.0);
    {
        std::vector<Image> c2; c2.resize(size_t(N));
        Image base0; base0.w = W; base0.h = H; base0.rgba.assign(size_t(W) * H * 4, 0);
        lay::paintOver(base0, renderShapes(A.still.shapes, W, H));
        for (int i : order) {
            const int p = A.parent[size_t(i)];
            c2[size_t(i)] = p < 0 ? base0 : c2[size_t(p)];
            if (!A.patches[size_t(i)].shapes.empty()) lay::paintOver(c2[size_t(i)], renderShapes(A.patches[size_t(i)].shapes, W, H));
        }
        lay::parallelFor(N, threads, [&](int i) { A.psnr[size_t(i)] = computePsnr(orig[size_t(i)], c2[size_t(i)], thr); });
    }
    double sum = 0, mn = 1e9; for (double p : A.psnr) { sum += p; mn = std::min(mn, p); }
    A.psnrAvg = sum / N; A.psnrMin = mn;
    report(prog, 1.f);
    return A;
}

// ============================================================================
//  Cadena de GD del arbol
// ============================================================================
inline std::string buildTreeString(const LayeredAnim& a, const GifPlan& plan, const GifExportParams& p, std::string* errmsg = nullptr) {
    auto fail = [&](const char* m) { if (errmsg) *errmsg = m; return std::string(); };
    if (!a.ok() || plan.count() != a.frames) return fail("No hay capas calculadas");
    const tree::Sched S = tree::schedule(a);
    if (p.baseGroup < 1 || p.baseGroup + S.groups - 1 > 9999) return fail("No quedan grupos libres suficientes (max. 9999)");
    const bool useCh = !a.palette.empty();
    if (useCh && (a.firstChannel < 2 || a.firstChannel + int(a.palette.size()) - 1 > 999)) return fail("No caben los canales de color (2..999)");
    std::map<uint32_t, int> chOf;
    for (size_t i = 0; i < a.palette.size(); ++i) chOf[lay::colKey(a.palette[i][0], a.palette[i][1], a.palette[i][2])] = a.firstChannel + int(i);
    const int N = a.frames;
    int next = p.baseGroup;
    std::vector<int> gLayer(size_t(N), 0), gStep(size_t(N), 0);
    for (int i = 0; i < N; ++i) if (S.layerOf[size_t(i)] >= 0) gLayer[size_t(i)] = next++;
    for (int i = 0; i < N; ++i) if (!S.hide[size_t(i)].empty() || !S.show[size_t(i)].empty()) gStep[size_t(i)] = next++;
    const int L = next++;
    const bool hasI = !S.initHide.empty();
    const int I = hasI ? next++ : 0;

    std::string out; out.reserve(a.objectCount() * 100 + size_t(S.triggers) * 90);
    auto emit = [&](const Result& r, int group, int zTier) {
        if (r.shapes.empty()) return;
        GdExportOptions eo = p.eo;
        eo.groupStr = group > 0 ? std::to_string(group) : std::string();
        eo.zBase = p.eo.zBase + zTier; eo.monoFastPath = false;
        if (useCh) {
            ChannelMap cm = buildChannelMap(r);
            eo.channels.assign(cm.colors.size(), 0);
            for (size_t i = 0; i < cm.colors.size(); ++i) eo.channels[i] = chOf.at(lay::colKey(cm.colors[i][0], cm.colors[i][1], cm.colors[i][2]));
        }
        std::string s = toGdString(r, eo);
        if (!s.empty()) { out += s; out += ';'; }
    };
    emit(a.still, 0, 0);
    for (int i = 0; i < N; ++i) emit(a.patches[size_t(i)], gLayer[size_t(i)], a.zNode[size_t(i)]);

    GdSize sz = gdImageSize(a.still, p.eo);
    detail::TrigBoard board(detail::rowLeft(p.eo.originX - sz.w * 0.5, p.trigX), p.eo.originY + sz.h * 0.5 + 90.0);
    auto pos = [&](double& x, double& y) { board.at(x, y); };
    double x, y, t = 0;
    for (int i = 0; i < N; ++i) {                               // bucle L
        if (gStep[size_t(i)] > 0) { pos(x, y); detail::addSpawn(out, p, x, y, gStep[size_t(i)], t, L); }
        t += plan.delay[size_t(i)];
    }
    pos(x, y); detail::addSpawn(out, p, x, y, L, plan.total, L);
    if (hasI) { pos(x, y); detail::addSpawn(out, p, x, y, I, 0, L); }     // autocorreccion en cada vuelta
    board.newBlock();
    for (int i = 0; i < N; ++i) {                               // pasos: ocultar lo que sale del camino, mostrar lo que entra
        const int F = gStep[size_t(i)]; if (F <= 0) continue;
        for (int l : S.hide[size_t(i)]) { pos(x, y); detail::addAlpha(out, p, x, y, gLayer[size_t(l)], 0, F); }
        for (int l : S.show[size_t(i)]) { pos(x, y); detail::addAlpha(out, p, x, y, gLayer[size_t(l)], 1, F); }
    }
    board.newBlock();
    if (hasI) for (int l : S.initHide) { pos(x, y); detail::addAlpha(out, p, x, y, gLayer[size_t(l)], 0, I); }
    board.newBlock();
    double sx = p.trigX, sy = board.nextY();
    detail::addSpawn(out, p, sx, board.rowY(), L, 0, 0);          // arranque en la fila de triggers, a la izquierda de la imagen
    if (hasI) detail::addSpawn(out, p, sx - 30.0, board.rowY(), I, 0, 0);   // I justo a la izquierda de L: nunca encima/debajo de otro
    if (useCh) {
        for (size_t i = 0; i < a.palette.size(); ++i) {
            const double cx = p.trigX - 48.0 * double(1 + int(i) % 20), cy = sy + 90.0 + 52.0 * double(int(i) / 20);
            detail::addTriggerHead(out, 899, cx, cy);
            out += ",7,"; out += std::to_string(a.palette[i][0]); out += ",8,"; out += std::to_string(a.palette[i][1]);
            out += ",9,"; out += std::to_string(a.palette[i][2]);
            out += ",10,0,35,1,17,0,23,"; out += std::to_string(a.firstChannel + int(i)); out += ';';
        }
    }
    if (!out.empty() && out.back() == ';') out.pop_back();
    return out;
}

inline Result treeFrameResult(const LayeredAnim& a, int f) {
    Result r; r.rasterW = a.W; r.rasterH = a.H; r.aspect = a.H > 0 ? float(a.W) / a.H : 1.f;
    if (f < 0 || f >= a.frames) return r;
    int z = 0;
    auto add = [&](const std::vector<Shape>& sh) {
        std::vector<Shape> v = sh;
        std::stable_sort(v.begin(), v.end(), [](const Shape& x, const Shape& y) { return x.z < y.z; });
        for (Shape s : v) { s.z = int16_t(std::min(z++, 32000)); r.shapes.push_back(s); }
    };
    add(a.still.shapes);
    std::vector<int> chain; for (int c = f; c >= 0; c = a.parent[size_t(c)]) chain.push_back(c);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) add(a.patches[size_t(*it)].shapes);
    r.psnr = f < int(a.psnr.size()) ? a.psnr[size_t(f)] : 0;
    return r;
}

// ============================================================================
//  Punto de entrada unico para la ventana: metodo 18 (rests + parches) o 19 (arbol de deltas)
// ============================================================================
inline const char* treeName() { return "19. Arbol de deltas (prediccion)"; }
inline const char* treeDesc() {
    return "Cada frame se predice del frame mas parecido y solo pinta lo que cambia (arbol de expansion minima). "
           "Maxima nitidez con los mismos objetos. 'Objetos por frame' x frames = total.";
}

inline Result animFrameResult(const LayeredAnim& a, int f) { return a.tree ? treeFrameResult(a, f) : layeredFrameResult(a, f); }
inline int animTriggerCount(const LayeredAnim& a) { return a.tree ? tree::schedule(a).triggers : layeredTriggerCount(a); }
inline int animGroupCount(const LayeredAnim& a) { return a.tree ? tree::schedule(a).groups : layeredGroupCount(a); }

// Ultima defensa del limite: los presupuestos por capa son aproximados y el
// numero de triggers depende de las capas que realmente quedaron vacias.
// Si hace falta, elimina primero las formas de menor area (parches antes que
// rests y fondo), compacta la paleta y vuelve a medir el resultado.
inline size_t enforceAnimObjectLimit(LayeredAnim& a, const GifData& g, const GifPlan& plan,
                                     const Options& base, int maxTotal) {
    if (!a.ok() || maxTotal < 1) return 0;
    size_t total = a.objectCount() + size_t(animTriggerCount(a));
    if (total <= size_t(maxTotal)) return 0;

    struct Candidate { std::vector<Shape>* layer; size_t index; double score; };
    std::vector<Candidate> candidates;
    auto collect = [&](std::vector<Shape>& shapes, double keepWeight) {
        for (size_t i = 0; i < shapes.size(); ++i) {
            const Shape& sh = shapes[i];
            const double area = std::max(0.01, double(sh.w) * double(sh.h));
            candidates.push_back({&shapes, i, area * keepWeight});
        }
    };
    collect(a.still.shapes, 2.0);
    for (auto& r : a.rests) collect(r.shapes, 1.35);
    for (auto& r : a.patches) collect(r.shapes, 1.0);
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& x, const Candidate& y) {
        return x.score < y.score;
    });

    std::map<std::vector<Shape>*, std::vector<uint8_t>> keep;
    auto maskFor = [&](std::vector<Shape>* v) -> std::vector<uint8_t>& {
        auto [it, inserted] = keep.try_emplace(v);
        if (inserted) it->second.assign(v->size(), 1);
        return it->second;
    };
    size_t stillLeft = a.still.shapes.size();
    const size_t toRemove = total - size_t(maxTotal);
    size_t removed = 0;
    for (const Candidate& c : candidates) {
        if (removed >= toRemove) break;
        if (c.layer == &a.still.shapes && stillLeft <= 1) continue;
        auto& mask = maskFor(c.layer);
        if (!mask[c.index]) continue;
        mask[c.index] = 0;
        if (c.layer == &a.still.shapes) --stillLeft;
        ++removed;
    }
    auto compact = [&](std::vector<Shape>& v) {
        auto it = keep.find(&v);
        if (it == keep.end()) return;
        size_t w = 0;
        for (size_t i = 0; i < v.size(); ++i) if (it->second[i]) v[w++] = v[i];
        v.resize(w);
    };
    compact(a.still.shapes);
    for (auto& r : a.rests) compact(r.shapes);
    for (auto& r : a.patches) compact(r.shapes);

    if (!a.palette.empty()) {
        std::set<uint32_t> used;
        auto scan = [&](const Result& r) { for (const Shape& sh : r.shapes) used.insert(lay::colKey(sh.r, sh.g, sh.b)); };
        scan(a.still); for (const auto& r : a.rests) scan(r); for (const auto& r : a.patches) scan(r);
        a.palette.erase(std::remove_if(a.palette.begin(), a.palette.end(), [&](const auto& c) {
            return !used.count(lay::colKey(c[0], c[1], c[2]));
        }), a.palette.end());
    }

    total = a.objectCount() + size_t(animTriggerCount(a));
    if (total > size_t(maxTotal) || a.still.shapes.empty()) {
        a.still.shapes.clear();
        a.note = "El tope total es demasiado bajo para los triggers y objetos minimos de esta animacion.";
        return removed;
    }

    if (removed) {
        if (!a.note.empty()) a.note += " ";
        a.note += "Tope total aplicado: se recortaron " + std::to_string(removed) + " formas pequenas para incluir todos los triggers.";
        a.psnr.assign(size_t(a.frames), 0.0);
        double sum = 0.0, mn = 1e9;
        const int thr = std::clamp(base.alphaThreshold, 1, 255);
        for (int i = 0; i < a.frames; ++i) {
            Image reference = resizeArea(g.frames[size_t(plan.idx[size_t(i)])], a.W, a.H);
            if (base.grayscale) reference = toGrayscale(reference);
            Result visible = animFrameResult(a, i);
            const double value = computePsnr(reference, renderShapes(visible.shapes, a.W, a.H), thr);
            a.psnr[size_t(i)] = value;
            sum += value; mn = std::min(mn, value);
        }
        a.psnrAvg = sum / std::max(1, a.frames);
        a.psnrMin = mn;
    }
    return removed;
}

inline std::string buildAnimString(const LayeredAnim& a, const GifPlan& plan, const GifExportParams& p, std::string* err = nullptr) {
    return a.tree ? buildTreeString(a, plan, p, err) : buildLayeredString(a, plan, p, err);
}

inline LayeredAnim buildAnim(const GifData& g, const GifPlan& plan, const Options& base, const LayeredOptions& lo, Progress* prog, int threads) {
    LayeredAnim a = lo.tree ? buildTree(g, plan, base, lo, prog, threads) : buildLayered(g, plan, base, lo, prog, threads);
    if (a.ok()) enforceAnimObjectLimit(a, g, plan, base, lo.maxTotal);
    return a;
}

} // namespace gdx
