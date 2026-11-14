// ============================================================================
//  Metodos 20-29 (capas LSA). Ver lsa.hpp / lsa_core.hpp.
//   20 Vidas de formas            reciclaje: una forma vive mientras siga valiendo; solo se trazan huecos y errores nuevos
//   21 Biblioteca de teselas      rejilla de teselas; cada tesela tiene pocas variantes y los frames comparten variantes
//   22 Keyframes + residuos       keyframes completos + residuo por tesela con biblioteca de variantes
//   23 Optimizador                un trazado por frame con arranque temporal y busqueda local por forma (calidad maxima)
//   24 Reparto RD + dedup         presupuesto por frame segun cuanto mejora cada frame al anadir objetos; frames repetidos comparten capa
//   25 Regiones de movimiento     fondo quieto + zonas que se mueven (componentes conexas) con variantes por zona
//   26 Animacion por colores      formas fijas y Color triggers: ideal para destellos, degradados que cambian, ciclos de color
//   27 Vidas + optimizador        como el 20, y cada tanda de formas nuevas se refina con busqueda local
//   28 Auto                       prueba varios metodos y se queda con el mejor
//   29 Cuadtree temporal          celdas grandes donde nada se mueve y pequenas donde si; variantes por celda
// ============================================================================
#pragma once
#include "lsa_core.hpp"

namespace gdx {
namespace lsa {

inline const char* methodName(int k) {
    static const char* n[] = {"", "20. Vidas de formas (reciclaje)", "21. Biblioteca de teselas", "22. Keyframes + residuos por tesela", "23. Optimizador por forma (calidad)",
                              "24. Reparto RD + frames repetidos", "25. Fondo + regiones de movimiento", "26. Animacion por colores (Color triggers)",
                              "27. Vidas + optimizador", "28. Auto (elige el mejor)", "29. Cuadtree temporal"};
    return n[std::clamp(k, 0, 10)];
}
inline const char* methodDesc(int k) {
    static const char* d[] = {"",
        "Cada forma vive mientras siga valiendo en los frames siguientes (un grupo por 'vida'): se trazan solo los huecos y errores nuevos. Muchos menos objetos en movimientos lentos.",
        "Divide la imagen en teselas; cada tesela guarda pocas variantes y los frames comparten variantes casi iguales. Muy bueno con bucles y movimientos locales.",
        "Pocos frames clave completos y, encima, solo las teselas que cambian, con una biblioteca de parches compartidos entre frames.",
        "Un trazado por frame, partiendo del anterior y afinando posicion, tamano y giro de cada forma. Es lento pero saca la maxima calidad por objeto.",
        "Reparte los objetos entre frames segun lo que mejora cada uno y reutiliza la capa de frames identicos o casi (ping-pong, pausas).",
        "Fondo quieto y, encima, solo las zonas que se mueven (cada una con sus variantes). Ideal para personajes sobre fondo fijo.",
        "Las formas no cambian: cada frame solo recolorea canales con Color triggers. Para destellos, brillos y ciclos de color (sin movimiento). Usa canales 2+.",
        "Como el 20, pero cada tanda de formas nuevas se afina con busqueda local para que cada objeto rinda mas.",
        "Prueba varios metodos (20, 21, 22, 24, 25, 29 y 26 si encaja) y se queda con el de mejor calidad con ese presupuesto. Lento.",
        "Celdas grandes donde nada cambia y pequenas donde hay movimiento; cada celda tiene sus variantes compartidas entre frames."};
    return d[std::clamp(k, 0, 10)];
}

inline long objBudget(const Ctx& c, const LsaAnim& structure) {
    const long T = triggerEstimate(structure);
    return std::max<long>(long(double(c.total) * 0.3), c.total - T - c.total / 40);
}

inline double diffFrac(const Ctx& c, int i, int j) {                 // fraccion de pixeles opacos de la union que difieren (mas que tol)
    size_t n = 0, bad = 0; const size_t np = size_t(c.W) * c.H;
    for (size_t p = 0; p < np; ++p) {
        const uint8_t* a = &c.sm[size_t(i)].rgba[p * 4]; const uint8_t* b = &c.sm[size_t(j)].rgba[p * 4];
        if (a[3] < c.thr && b[3] < c.thr) continue;
        ++n; if (lay::pixDiff(a, b, c.thr) > c.tol) ++bad;
    }
    return n ? double(bad) / double(n) : 0.0;
}

inline std::vector<float> activityMap(const Ctx& c, const Image& med) {
    const size_t np = size_t(c.W) * c.H; std::vector<float> a(np, 0.f);
    Image sm = lay::blur3(med, c.thr);
    for (int i = 0; i < c.N; ++i) for (size_t p = 0; p < np; ++p) if (lay::pixDiff(&c.sm[size_t(i)].rgba[p * 4], &sm.rgba[p * 4], c.thr) > c.tol) a[p] += 1.f;
    for (auto& v : a) v /= float(c.N);
    return a;
}

inline Image stillImage(const Ctx& c) {                               // mediana temporal con los pixeles "caoticos" rellenados con su entorno
    Image med = lay::temporalMedian(c.fr, c.thr);
    Image smRef = lay::blur3(med, c.thr);
    std::vector<uint16_t> dev(size_t(c.W) * c.H, 0);
    for (int i = 0; i < c.N; ++i) for (size_t p = 0; p < dev.size(); ++p) if (lay::pixDiff(&c.sm[size_t(i)].rgba[p * 4], &smRef.rgba[p * 4], c.thr) > c.tol) ++dev[p];
    std::vector<uint8_t> valid(dev.size(), 1); size_t bad = 0;
    for (size_t p = 0; p < dev.size(); ++p) if (med.rgba[p * 4 + 3] >= c.thr && int(dev[p]) * 2 > c.N) { valid[p] = 0; ++bad; }
    if (bad) lay::pushPull(med, valid);
    return med;
}

inline std::vector<Region> gridRegions(int W, int H, int G) {
    std::vector<Region> out; const int gx = std::max(1, std::min(G, W / 4)), gy = std::max(1, std::min(G, H / 4));
    for (int j = 0; j < gy; ++j) for (int i = 0; i < gx; ++i) { Region r; r.x0 = i * W / gx; r.x1 = (i + 1) * W / gx; r.y0 = j * H / gy; r.y1 = (j + 1) * H / gy; if (r.w() > 0 && r.h() > 0) out.push_back(r); }
    return out;
}

inline void qtSplit(const std::vector<float>& act, int W, int x0, int y0, int x1, int y1, int minCell, double thrAct, int depth, std::vector<Region>& out) {
    const int w = x1 - x0, h = y1 - y0; size_t n = 0, dyn = 0, stat = 0;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) { float a = act[size_t(y) * W + x]; ++n; if (a >= thrAct) ++dyn; else if (a < thrAct * 0.25) ++stat; }
    const double fd = n ? double(dyn) / n : 0, fs = n ? double(stat) / n : 0;
    const bool mixed = fd > 0.10 && fs > 0.10;
    const bool canSplit = (w >= 2 * minCell || h >= 2 * minCell) && depth < 7;
    if (canSplit && (mixed || (fd > 0.10 && std::max(w, h) > 3 * minCell))) {
        if (w >= h) { int xm = x0 + w / 2; qtSplit(act, W, x0, y0, xm, y1, minCell, thrAct, depth + 1, out); qtSplit(act, W, xm, y0, x1, y1, minCell, thrAct, depth + 1, out); }
        else { int ym = y0 + h / 2; qtSplit(act, W, x0, y0, x1, ym, minCell, thrAct, depth + 1, out); qtSplit(act, W, x0, ym, x1, y1, minCell, thrAct, depth + 1, out); }
        return;
    }
    Region r; r.x0 = x0; r.y0 = y0; r.x1 = x1; r.y1 = y1; out.push_back(r);
}

// ------------------------------------------------------------------ seleccion de (regiones, theta) por modelo de coste ----
// Modelo de error (en MSE aproximado): un pixel mal asignado cuesta ~3600 (60 niveles de color al cuadrado); el error de ajuste crece al bajar
// la densidad de objetos: 163 (26 dB) x (dBase/d)^0.8, con d = objetos por pixel de capa y dBase = lo que da trazar cada frame entero.
struct Pick { double E = 1e18, theta = 0.03; VariantPlan plan; long Bobj = 0; };
inline void considerPlan(const Ctx& c, const VariantPlan& P, double theta, double extraArea, int depth, double dBase, Pick& best, size_t nReg) {
    if (P.nVars == 0) return;
    LsaAnim st = planStructure(c, P, depth);
    if (triggerEstimate(st) > long(0.40 * double(c.total))) return;                // demasiados triggers: no queda presupuesto para objetos
    const long Bobj = objBudget(c, st);
    const double area = P.areaSum + extraArea;
    const double d = double(Bobj) / std::max(1.0, area);
    // partir la imagen en regiones cuesta calidad aunque no se comparta nada (medido: x1.6 con 4 regiones, x3 con 9, x4.5 con 16 a igual presupuesto)
    const double split = 1.0 + 0.2 * double(nReg > 0 ? nReg - 1 : 0);
    const double E = 3600.0 * theta + 163.0 * split * std::pow(std::max(1e-6, dBase / d), 0.8);
    if (E < best.E) { best.E = E; best.theta = theta; best.plan = P; best.Bobj = Bobj; }
}

inline bool buildGridTiles(const Ctx& c, LsaAnim& A, bool quad) {
    const double dBase = double(std::max(1, c.lo.perFrame)) / (double(c.W) * c.H);
    Pick best; std::vector<Region> bestRegs; 
    std::vector<std::vector<Region>> cands;
    cands.push_back(gridRegions(c.W, c.H, 1));                                      // reserva: una sola region (= frames unicos con dedup)
    if (!quad) { for (int G : {3, 4, 6}) cands.push_back(gridRegions(c.W, c.H, G)); }
    else {
        Image med = lay::temporalMedian(c.fr, c.thr); std::vector<float> act = activityMap(c, med);
        for (int div : {16, 10, 6}) { std::vector<Region> r; qtSplit(act, c.W, 0, 0, c.W, c.H, std::max(6, std::min(c.W, c.H) / div), 0.06, 0, r); cands.push_back(std::move(r)); }
    }
    for (auto& regs : cands) {
        if (isCancelled(c.prog)) return false;
        auto RD = prepRegions(c, regs, nullptr);
        for (double th : {0.004, 0.01, 0.02, 0.04, 0.08}) { VariantPlan P = planVariants(c, regs, RD, nullptr, th); Pick before = best; considerPlan(c, P, th, 0, 1, dBase, best, regs.size()); if (best.E < before.E) bestRegs = regs; }
    }
    if (bestRegs.empty()) return false;
    A.vis.assign(size_t(c.N), {});
    fitVariants(c, A, bestRegs, best.plan, nullptr, 1, best.Bobj);
    A.note = std::string(quad ? "celdas " : "teselas ") + std::to_string(bestRegs.size()) + ", variantes " + std::to_string(best.plan.nVars) + ", theta " + std::to_string(int(best.theta * 100)) + "%";
    return A.objectCount() > 0;
}

inline bool buildKeyframesResidual(const Ctx& c, LsaAnim& A) {
    const int N = c.N; const double dBase = double(std::max(1, c.lo.perFrame)) / (double(c.W) * c.H);
    const std::vector<float> D = lay::frameDistances(c.fr);
    struct Cand { int K; std::vector<int> med, as; };
    std::vector<Cand> cands;
    for (int K : {1, 2, 4}) if (K <= N) { Cand cd; cd.K = K; lay::kMedoids(D, N, K, cd.med, cd.as); cands.push_back(std::move(cd)); }
    Pick best; Cand bestC; std::vector<Region> bestRegs; BaseSet bestBase; bool any = false;
    for (auto& cd : cands) {
        BaseSet bs; bs.sm = c.sm; bs.fr = c.fr; bs.of.assign(size_t(N), 0);
        for (int i = 0; i < N; ++i) bs.of[size_t(i)] = cd.med[size_t(cd.as[size_t(i)])];
        for (int G : {3, 5}) {
            if (isCancelled(c.prog)) return false;
            auto regs = gridRegions(c.W, c.H, G); auto RD = prepRegions(c, regs, &bs);
            for (double th : {0.008, 0.02, 0.05}) {
                VariantPlan P = planVariants(c, regs, RD, &bs, th);
                Pick before = best; considerPlan(c, P, th, double(cd.K) * c.contentArea, 2, dBase, best, regs.size() + size_t(cd.K));
                if (best.E < before.E) { bestC = cd; bestRegs = regs; bestBase = bs; any = true; }
            }
        }
    }
    if (!any) return false;
    A.vis.assign(size_t(N), {});
    const double areaKf = double(bestC.K) * c.contentArea, areaRes = std::max(1.0, best.plan.areaSum);
    const long Bkf = long(double(best.Bobj) * areaKf * 1.3 / (areaKf * 1.3 + areaRes)), Bres = best.Bobj - Bkf;
    {   // keyframes completos (profundidad 0)
        std::vector<Result> kr(size_t(bestC.K));
        lay::parallelFor(bestC.K, c.threads, [&](int k) {
            const int m = bestC.med[size_t(k)]; Image full = c.fr[size_t(m)];
            kr[size_t(k)] = fitImage(c, full, full, nullptr, int(std::max<long>(6, Bkf / bestC.K)), c.seed + uint32_t(k) * 31u);
        });
        for (int k = 0; k < bestC.K; ++k) {
            LsaLayer L; L.shapes = std::move(kr[size_t(k)].shapes); L.depth = 0; const int id = int(A.layers.size());
            if (L.shapes.empty()) continue;
            A.layers.push_back(std::move(L));
            for (int i = 0; i < N; ++i) if (bestC.as[size_t(i)] == k) A.vis[size_t(i)].push_back(id);
        }
    }
    fitVariants(c, A, bestRegs, best.plan, &bestBase, 2, Bres);
    A.note = "keyframes " + std::to_string(bestC.K) + ", variantes de residuo " + std::to_string(best.plan.nVars);
    return A.objectCount() > 0;
}

inline bool buildRdFlat(const Ctx& c, LsaAnim& A);
inline bool buildMotionRegions(const Ctx& c, LsaAnim& A) {
    const int W = c.W, H = c.H, N = c.N; const size_t np = size_t(W) * H;
    Image med = stillImage(c); std::vector<float> act = activityMap(c, med);
    std::vector<Region> regs;
    for (int attempt = 0; attempt < 4 && regs.empty(); ++attempt) {
        const int grow = 1 + attempt * 2;
        std::vector<uint8_t> m(np, 0), md(np, 0);
        const float actThr = 0.99f / float(N);                                         // cualquier pixel que se salga de la mediana en AL MENOS UN frame
        for (size_t p = 0; p < np; ++p) m[p] = act[p] >= actThr ? 1 : 0;
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            bool on = false;
            for (int dy = -grow; dy <= grow && !on; ++dy) for (int dx = -grow; dx <= grow; ++dx) { int xx = x + dx, yy = y + dy; if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue; if (m[size_t(yy) * W + xx]) { on = true; break; } }
            md[size_t(y) * W + x] = on ? 1 : 0;
        }
        std::vector<int> lab(np, -1); std::vector<Region> comps; std::vector<std::vector<int>> pix;
        for (size_t s0 = 0; s0 < np; ++s0) {
            if (!md[s0] || lab[s0] >= 0) continue;
            const int id = int(comps.size()); std::vector<int> st{int(s0)}, mine; lab[s0] = id;
            int x0 = W, y0 = H, x1 = 0, y1 = 0;
            while (!st.empty()) {
                int p = st.back(); st.pop_back(); mine.push_back(p); int x = p % W, y = p / W;
                x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x + 1); y1 = std::max(y1, y + 1);
                const int nb[4] = {x > 0 ? p - 1 : -1, x + 1 < W ? p + 1 : -1, y > 0 ? p - W : -1, y + 1 < H ? p + W : -1};
                for (int q : nb) if (q >= 0 && md[size_t(q)] && lab[size_t(q)] < 0) { lab[size_t(q)] = id; st.push_back(q); }
            }
            Region r; r.x0 = x0; r.y0 = y0; r.x1 = x1; r.y1 = y1; comps.push_back(r); pix.push_back(std::move(mine));
        }
        const double minArea = std::max(12.0, 0.002 * double(np));
        std::vector<size_t> keep; for (size_t i = 0; i < comps.size(); ++i) if (double(pix[i].size()) >= minArea) keep.push_back(i);
        std::sort(keep.begin(), keep.end(), [&](size_t a, size_t b) { return pix[a].size() > pix[b].size(); });
        if (keep.size() > 10 && attempt < 3) continue;                              // demasiadas: se agrandan los margenes y se funden
        if (keep.size() > 10) keep.resize(10);
        for (size_t k : keep) {
            Region r = comps[k]; const int w = r.w(), h = r.h(); r.mask.assign(size_t(w) * h, 0);
            for (int p : pix[k]) r.mask[size_t(p / W - r.y0) * w + size_t(p % W - r.x0)] = 1;
            regs.push_back(std::move(r));
        }
    }
    A.vis.assign(size_t(N), {});
    BaseSet bs; Image medSm = lay::blur3(med, c.thr); bs.sm.push_back(medSm); bs.fr.push_back(med); bs.of.assign(size_t(N), 0);
    const double dBase = double(std::max(1, c.lo.perFrame)) / (double(W) * H);
    Pick best; std::vector<RegionData> RD;
    if (!regs.empty()) RD = prepRegions(c, regs, &bs);
    for (double th : {0.004, 0.01, 0.02, 0.04, 0.08}) if (!regs.empty()) { VariantPlan P = planVariants(c, regs, RD, &bs, th); considerPlan(c, P, th, c.contentArea, 2, dBase, best, regs.size()); }
    if (!regs.empty() && best.Bobj == 0) { A.layers.clear(); A.vis.clear(); return buildRdFlat(c, A); }   // todos los planes pedian demasiados triggers
    const long Btot = regs.empty() ? c.total - c.total / 40 : best.Bobj;
    const double areaStill = c.contentArea, areaDyn = regs.empty() ? 0.0 : std::max(1.0, best.plan.areaSum);
    const long Bstill = long(double(Btot) * areaStill / (areaStill + areaDyn * 1.5));
    {   // fondo quieto (profundidad 0), visible siempre
        Image layerImg = med; if (!c.opaqueAll) for (size_t p = 0; p < np; ++p) if (med.rgba[p * 4 + 3] < c.thr) layerImg.rgba[p * 4 + 3] = 0;
        Result r = fitImage(c, layerImg, med, nullptr, int(std::max<long>(6, Bstill)), c.seed);
        LsaLayer L; L.shapes = std::move(r.shapes); L.depth = 0; L.always = true;
        if (!L.shapes.empty()) A.layers.push_back(std::move(L));
    }
    if (!regs.empty()) fitVariants(c, A, regs, best.plan, &bs, 2, Btot - Bstill);
    A.note = "zonas de movimiento " + std::to_string(regs.size()) + (regs.empty() ? " (GIF casi quieto)" : ", variantes " + std::to_string(best.plan.nVars));
    return A.objectCount() > 0;
}

// ------------------------------------------------------------------ 24: reparto RD + dedup ----
inline bool buildRdFlat(const Ctx& c, LsaAnim& A) {
    const int N = c.N;
    std::vector<int> rep(size_t(N), -1), uniq;
    for (int i = 0; i < N; ++i) {
        for (int u : uniq) if (diffFrac(c, i, u) <= 0.003) { rep[size_t(i)] = u; break; }
        if (rep[size_t(i)] < 0) { rep[size_t(i)] = i; uniq.push_back(i); }
    }
    const int U = int(uniq.size());
    A.vis.assign(size_t(N), {});
    LsaAnim st; st.W = c.W; st.H = c.H; st.frames = N; st.vis.assign(size_t(N), {}); st.layers.resize(size_t(U));
    for (int k = 0; k < U; ++k) { st.layers[size_t(k)].shapes.assign(1, Shape{}); }
    for (int i = 0; i < N; ++i) { int k = int(std::find(uniq.begin(), uniq.end(), rep[size_t(i)]) - uniq.begin()); if (U > 1) st.vis[size_t(i)].push_back(k); }
    const long Bobj = objBudget(c, st); const double b0 = double(Bobj) / U;
    std::vector<double> slope(size_t(U), 1.0);
    lay::parallelFor(U, c.threads, [&](int k) {
        const Image& tg = c.fr[size_t(uniq[size_t(k)])]; double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
        for (double f : {0.25, 0.5, 1.0, 2.0}) {
            Result r = lay::traceLayer(tg, std::max(4, int(b0 * f)), c.base, 0);
            double ps = std::min(60.0, computePsnr(tg, renderShapes(r.shapes, c.W, c.H), c.thr)), x = std::log(std::max(4.0, b0 * f));
            sx += x; sy += ps; sxx += x * x; sxy += x * ps; ++n;
        }
        const double den = n * sxx - sx * sx; slope[size_t(k)] = den > 1e-9 ? std::max(0.3, (n * sxy - sx * sy) / den) : 1.0;
    });
    const double bmin = std::max(4.0, 0.25 * b0), bmax = 3.0 * b0;
    double lo = 1e-4, hi = 1e4;
    for (int it = 0; it < 60; ++it) {
        double lam = std::sqrt(lo * hi), s = 0; for (int k = 0; k < U; ++k) s += std::clamp(slope[size_t(k)] / lam, bmin, bmax);
        if (s > double(Bobj)) lo = lam; else hi = lam;
    }
    const double lam = std::sqrt(lo * hi);
    std::vector<Result> res; res.resize(size_t(U));
    lay::parallelFor(U, c.threads, [&](int k) {
        const Image& tg = c.fr[size_t(uniq[size_t(k)])]; const int b = int(std::clamp(slope[size_t(k)] / lam, bmin, bmax));
        res[size_t(k)] = fitImage(c, tg, tg, nullptr, b, c.seed + uint32_t(k) * 131u);
    });
    for (int k = 0; k < U; ++k) { LsaLayer L; L.shapes = std::move(res[size_t(k)].shapes); L.depth = 1; L.always = U == 1; A.layers.push_back(std::move(L)); }
    for (int i = 0; i < N; ++i) { int k = int(std::find(uniq.begin(), uniq.end(), rep[size_t(i)]) - uniq.begin()); A.vis[size_t(i)].push_back(k); }
    A.note = "frames unicos " + std::to_string(U) + " de " + std::to_string(N);
    return A.objectCount() > 0;
}

// ------------------------------------------------------------------ 23: optimizador con arranque temporal ----
inline bool buildOptimizer(const Ctx& c, LsaAnim& A) {
    const int N = c.N, W = c.W, H = c.H;
    A.vis.assign(size_t(N), {});
    LsaAnim st; st.W = W; st.H = H; st.frames = N; st.vis.assign(size_t(N), {}); st.layers.resize(size_t(N));
    for (int i = 0; i < N; ++i) { st.layers[size_t(i)].shapes.assign(1, Shape{}); st.vis[size_t(i)].push_back(i); }
    const long Bobj = objBudget(c, st); const int per = int(std::max<long>(6, Bobj / N));
    const int rounds = std::clamp(c.quality, 1, 3);
    std::vector<Shape> prev;
    for (int i = 0; i < N; ++i) {
        if (isCancelled(c.prog)) return false;
        report(c.prog, 0.1f + 0.85f * float(i) / float(N));
        const Image& tg = c.fr[size_t(i)];
        auto improve = [&](std::vector<Shape>& sh, bool warm) {
            refineShapes(sh, tg, nullptr, c.thr, rounds, c.seed + uint32_t(i) + (warm ? 5u : 0u), 30000L + long(per) * 500L);
            // huecos / errores que quedan: se rellenan con formas nuevas encima si queda presupuesto
            const int room = per - int(sh.size());
            if (room >= 4) {
                Image canvas = renderShapes(sh, W, H);
                std::vector<uint8_t> mask = lay::changeMask(c.sm[size_t(i)], canvas, c.tol, c.lo.grow, c.thr);
                size_t cnt = 0; for (uint8_t v : mask) cnt += v;
                if (cnt > 0) {
                    Image under = canvas; std::vector<uint8_t> valid(size_t(W) * H, 0); bool any = false;
                    for (size_t p = 0; p < valid.size(); ++p) { valid[p] = canvas.rgba[p * 4 + 3] > 0; any |= valid[p] != 0; }
                    const Image* up = nullptr; if (any && c.opaqueAll) { lay::pushPull(under, valid); up = &under; }
                    Result add = fitImage(c, lay::maskedImage(tg, mask), tg, up, std::min(room, lay::areaBudget(cnt, 100000)), c.seed + 7u + uint32_t(i));
                    int zTop = 0; for (auto& s2 : sh) zTop = std::max<int>(zTop, s2.z);
                    for (Shape s2 : add.shapes) { s2.z = int16_t(std::min(30000, zTop + 1 + s2.z)); sh.push_back(s2); }
                }
            }
            if (int(sh.size()) > per) {                                                  // demasiadas: se quitan las de menos area
                std::stable_sort(sh.begin(), sh.end(), [](const Shape& a, const Shape& b) { return a.w * a.h > b.w * b.h; });
                sh.resize(size_t(per)); std::stable_sort(sh.begin(), sh.end(), [](const Shape& a, const Shape& b) { return a.z < b.z; });
            }
        };
        // candidato A: trazado nuevo + afinado.  candidato B (si hay frame anterior): formas del frame anterior seguidas hasta este frame.
        std::vector<Shape> sh = fitImage(c, tg, tg, nullptr, per, c.seed + uint32_t(i) * 13u).shapes;
        improve(sh, false);
        if (i > 0 && !prev.empty()) {
            std::vector<Shape> w = prev; refitVisible(w, tg, c.thr, true); improve(w, true);
            const double pa = computePsnr(tg, renderShapes(sh, W, H), c.thr), pb = computePsnr(tg, renderShapes(w, W, H), c.thr);
            if (pb > pa) sh = std::move(w);
        }
        confineShapes(sh, W, H); compactZ(sh, W, H);
        prev = sh;
        LsaLayer L; L.shapes = std::move(sh); L.depth = 1; L.always = N == 1; A.layers.push_back(std::move(L));
        A.vis[size_t(i)].push_back(i);
    }
    A.note = "afinado por forma, " + std::to_string(rounds) + " rondas";
    return A.objectCount() > 0;
}

// ------------------------------------------------------------------ 20 / 27: vidas de formas ----
struct Live { Shape s; int birth = 0; };
struct Fin { Shape s; int birth = 0, death = 0; };

inline bool lifetimesRun(const Ctx& c, double tau0, long Bobj, bool refine, std::vector<Fin>& fin, bool& cancelled) {
    const int N = c.N, W = c.W, H = c.H; const size_t np = size_t(W) * H;
    std::vector<double> wt(size_t(N), 0.0); double mean = 0;
    for (int i = 1; i < N; ++i) { wt[size_t(i)] = diffFrac(c, i, i - 1) * double(np) + 8.0; mean += wt[size_t(i)]; }
    mean = N > 1 ? mean / (N - 1) : 1.0; wt[0] = std::max(c.contentArea * 0.5, 2.0 * mean);
    std::vector<double> cum(size_t(N) + 1, 0.0); for (int i = N - 1; i >= 0; --i) cum[size_t(i)] = cum[size_t(i) + 1] + wt[size_t(i)];
    const double totW = cum[0];
    std::vector<Live> alive; fin.clear(); double tau = tau0; long pool = Bobj, used = 0; double planned = 0;
    const int rounds = std::clamp(c.quality, 1, 3);
    for (int i = 0; i < N; ++i) {
        if (isCancelled(c.prog)) { cancelled = true; return false; }
        report(c.prog, 0.05f + 0.9f * float(i) / float(N));
        const Image& tg = c.fr[size_t(i)];
        auto orderOf = [&]() { std::vector<size_t> o(alive.size()); for (size_t k = 0; k < o.size(); ++k) o[k] = k;
            std::stable_sort(o.begin(), o.end(), [&](size_t a, size_t b) { return alive[a].birth != alive[b].birth ? alive[a].birth < alive[b].birth : alive[a].s.z < alive[b].s.z; }); return o; };
        if (i > 0 && !alive.empty()) {
            for (int pass = 0; pass < 3; ++pass) {
                std::vector<Shape> sh; sh.reserve(alive.size()); for (auto& l : alive) sh.push_back(l.s);
                const std::vector<size_t> ord = orderOf(); const std::vector<int32_t> ids = idBuffer(sh, ord, W, H);
                std::vector<int> nvis(alive.size(), 0), nbad(alive.size(), 0);
                for (size_t p = 0; p < np; ++p) {
                    const int32_t id = ids[p]; if (id < 0) continue; ++nvis[size_t(id)];
                    const uint8_t* t = &c.sm[size_t(i)].rgba[p * 4]; const Shape& s = sh[size_t(id)];
                    if (t[3] < c.thr || std::max({std::abs(int(t[0]) - s.r), std::abs(int(t[1]) - s.g), std::abs(int(t[2]) - s.b)}) > c.tol) ++nbad[size_t(id)];
                }
                std::vector<Live> keep; bool drop = false;
                for (size_t k = 0; k < alive.size(); ++k) {
                    if (nvis[k] == 0 || double(nbad[k]) > tau * double(nvis[k])) { Fin f; f.s = alive[k].s; f.birth = alive[k].birth; f.death = i - 1; fin.push_back(f); drop = true; }
                    else keep.push_back(alive[k]);
                }
                alive = std::move(keep); if (!drop) break;
            }
        }
        // lienzo con lo que sobrevive; huecos y errores -> mascara
        Image canvas; canvas.w = W; canvas.h = H; canvas.rgba.assign(np * 4, 0);
        if (!alive.empty()) {
            std::vector<Shape> sh; const auto ord = orderOf(); sh.reserve(alive.size());
            for (size_t r = 0; r < ord.size(); ++r) { Shape s = alive[ord[r]].s; s.z = int16_t(std::min<size_t>(32000, r)); sh.push_back(s); }
            canvas = renderShapes(sh, W, H);
        }
        std::vector<uint8_t> mask = lay::changeMask(c.sm[size_t(i)], canvas, c.tol, c.lo.grow, c.thr);
        if (i == 0) for (size_t p = 0; p < np; ++p) mask[p] = tg.rgba[p * 4 + 3] >= c.thr ? 1 : 0;
        size_t cnt = 0; for (uint8_t v : mask) cnt += v;
        int b = int(std::max(0.0, double(pool) * wt[size_t(i)] / std::max(1e-9, cum[size_t(i)])));
        b = std::min(b, lay::areaBudget(cnt, 100000)); if (cnt > 0) b = std::max(b, std::min<int>(4, int(pool)));
        if (cnt > 0 && b > 0 && pool > 0) {
            Image under = canvas; std::vector<uint8_t> valid(np, 0); bool any = false;
            for (size_t p = 0; p < np; ++p) { valid[p] = canvas.rgba[p * 4 + 3] > 0; any |= valid[p] != 0; }
            const Image* up = nullptr; if (any && c.opaqueAll) { lay::pushPull(under, valid); up = &under; }
            Result res = fitImage(c, lay::maskedImage(tg, mask), tg, up, b, c.seed + uint32_t(i) * 17u);
            if (refine && !res.shapes.empty()) refineShapes(res.shapes, tg, up, c.thr, rounds, c.seed + uint32_t(i), 20000L + long(res.shapes.size()) * 400L);
            for (const Shape& s : res.shapes) { Live l; l.s = s; l.birth = i; alive.push_back(l); }
            pool -= long(res.shapes.size()); used += long(res.shapes.size());
        }
        planned += double(Bobj) * wt[size_t(i)] / totW;                               // ritmo: si se gasta mas de lo previsto, tolerancia mas laxa (mas vida)
        tau = std::clamp(tau0 * std::exp(8.0 * (double(used) - planned) / std::max(1.0, double(Bobj))), 0.02, 0.6);
    }
    for (auto& l : alive) { Fin f; f.s = l.s; f.birth = l.birth; f.death = N - 1; fin.push_back(f); }
    return true;
}

inline bool buildLifetimes(const Ctx& c, LsaAnim& A, bool refine) {
    const int N = c.N; std::vector<Fin> fin; bool cancelled = false;
    double tau0 = 0.04;
    for (int attempt = 0; attempt < 4; ++attempt) {
        // presupuesto: se reserva un 6 % para triggers (se ajusta al final con el tope duro)
        const long Bobj = long(double(c.total) * (0.94 - 0.04 * attempt));
        if (!lifetimesRun(c, tau0, Bobj, refine, fin, cancelled)) return false;
        std::set<std::pair<int, int>> keys; for (auto& f : fin) keys.insert({f.birth, f.death});
        if (int(keys.size()) <= 420 || attempt == 3) break;
        tau0 *= 2.2;                                                                    // demasiadas capas (triggers): vidas mas largas
    }
    A.vis.assign(size_t(N), {});
    std::map<std::pair<int, int>, int> idOf;
    for (auto& f : fin) {
        auto key = std::make_pair(f.birth, f.death); auto it = idOf.find(key);
        if (it == idOf.end()) {
            LsaLayer L; L.depth = f.birth + 1; L.always = (f.birth == 0 && f.death == N - 1);
            const int id = int(A.layers.size()); A.layers.push_back(std::move(L)); it = idOf.emplace(key, id).first;
            for (int g = f.birth; g <= f.death; ++g) A.vis[size_t(g)].push_back(id);
        }
        A.layers[size_t(it->second)].shapes.push_back(f.s);
    }
    A.note = "vidas " + std::to_string(A.layers.size()) + " capas";
    return A.objectCount() > 0;
}

// ------------------------------------------------------------------ 26: animacion por colores ----
inline bool buildColorAnim(const Ctx& c, LsaAnim& A) {
    if (!c.opaqueAll) { A.note = "La animacion por colores necesita un GIF sin transparencia"; return false; }
    const int N = c.N, W = c.W, H = c.H; const size_t np = size_t(W) * H;
    int sw, sh; fitSize(W, H, 96, sw, sh);
    std::vector<Image> low; low.resize(size_t(N)); for (int i = 0; i < N; ++i) low[size_t(i)] = resizeArea(c.fr[size_t(i)], sw, sh);
    const size_t ns = size_t(sw) * sh, dims = size_t(N) * 3;
    std::vector<float> feat(ns * dims);
    for (size_t p = 0; p < ns; ++p) for (int i = 0; i < N; ++i) for (int k = 0; k < 3; ++k) feat[p * dims + size_t(i) * 3 + size_t(k)] = float(low[size_t(i)].rgba[p * 4 + size_t(k)]);
    const int Kmax = std::clamp(int(0.3 * double(c.total) / (2.0 * N)), 4, 64);
    Rng rng(c.seed);
    auto kmeans = [&](int K, std::vector<float>& cen, std::vector<int>& as) -> double {
        cen.assign(size_t(K) * dims, 0.f); as.assign(ns, 0);
        std::vector<double> d2(ns, 1e30); size_t first = rng.next() % ns;
        std::memcpy(&cen[0], &feat[first * dims], dims * sizeof(float));
        for (int k = 1; k < K; ++k) {                                                  // k-means++
            double sum = 0;
            for (size_t p = 0; p < ns; ++p) { double d = 0; const float* a = &feat[p * dims]; const float* b = &cen[size_t(k - 1) * dims]; for (size_t t = 0; t < dims; ++t) { double x = a[t] - b[t]; d += x * x; } d2[p] = std::min(d2[p], d); sum += d2[p]; }
            double r = double(rng.next() % 1000003) / 1000003.0 * sum, acc = 0; size_t pick = ns - 1;
            for (size_t p = 0; p < ns; ++p) { acc += d2[p]; if (acc >= r) { pick = p; break; } }
            std::memcpy(&cen[size_t(k) * dims], &feat[pick * dims], dims * sizeof(float));
        }
        double err = 0;
        for (int it = 0; it < 8; ++it) {
            err = 0;
            for (size_t p = 0; p < ns; ++p) { double bd = 1e30; int bk = 0; const float* a = &feat[p * dims];
                for (int k = 0; k < K; ++k) { const float* b = &cen[size_t(k) * dims]; double d = 0; for (size_t t = 0; t < dims; ++t) { double x = a[t] - b[t]; d += x * x; if (d >= bd) break; } if (d < bd) { bd = d; bk = k; } }
                as[p] = bk; err += bd; }
            std::vector<double> acc(size_t(K) * dims, 0.0); std::vector<int> cn(size_t(K), 0);
            for (size_t p = 0; p < ns; ++p) { ++cn[size_t(as[p])]; for (size_t t = 0; t < dims; ++t) acc[size_t(as[p]) * dims + t] += feat[p * dims + t]; }
            for (int k = 0; k < K; ++k) if (cn[size_t(k)]) for (size_t t = 0; t < dims; ++t) cen[size_t(k) * dims + t] = float(acc[size_t(k) * dims + t] / cn[size_t(k)]);
        }
        return std::sqrt(err / double(ns * dims));
    };
    std::vector<float> cen, cenBest; std::vector<int> as, asBest; double eMax = -1; int K = Kmax;
    {   // error de referencia con Kmax; se elige el menor K cuyo error no supera 1.3x ese (o 6 niveles de color)
        std::vector<int> ks; for (int k : {4, 8, 16, 32, 48, 64}) if (k <= Kmax) ks.push_back(k); if (ks.empty() || ks.back() != Kmax) ks.push_back(Kmax);
        std::vector<double> es; std::vector<std::vector<float>> cs; std::vector<std::vector<int>> asl;
        for (int k : ks) { std::vector<float> ce; std::vector<int> a; es.push_back(kmeans(k, ce, a)); cs.push_back(std::move(ce)); asl.push_back(std::move(a)); }
        eMax = es.back(); size_t pick = ks.size() - 1;
        for (size_t i = 0; i < ks.size(); ++i) if (es[i] <= std::max(1.15 * eMax, 2.5)) { pick = i; break; }
        K = ks[pick]; cenBest = cs[pick]; asBest = asl[pick];
    }
    // asignacion a resolucion completa con una muestra de frames
    const int Nf = std::min(N, 16); std::vector<int> fs; for (int k = 0; k < Nf; ++k) fs.push_back(int(int64_t(k) * N / Nf));
    std::vector<int> lab(np, 0);
    lay::parallelFor(H, c.threads, [&](int y) {
        for (int x = 0; x < W; ++x) {
            const size_t p = size_t(y) * W + x; double bd = 1e30; int bk = 0;
            for (int k = 0; k < K; ++k) { double d = 0; for (int f : fs) { const uint8_t* q = &c.fr[size_t(f)].rgba[p * 4]; const float* ce = &cenBest[size_t(k) * dims + size_t(f) * 3];
                    double d0 = q[0] - ce[0], d1 = q[1] - ce[1], d2 = q[2] - ce[2]; d += d0 * d0 + d1 * d1 + d2 * d2; if (d >= bd) break; } if (d < bd) { bd = d; bk = k; } }
            lab[p] = bk;
        }
    });
    {   // filtro de moda 3x3 (quita motas)
        std::vector<int> o = lab;
        for (int y = 1; y + 1 < H; ++y) for (int x = 1; x + 1 < W; ++x) { int cnt[64] = {0}; for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) ++cnt[lab[size_t(y + dy) * W + x + dx]];
            int bk = lab[size_t(y) * W + x], bc = cnt[bk]; for (int k = 0; k < K; ++k) if (cnt[k] > bc + 1) { bc = cnt[k]; bk = k; } o[size_t(y) * W + x] = bk; }
        lab = std::move(o);
    }
    // colores por frame y canal (media de los pixeles del canal a resolucion completa)
    std::vector<std::array<uint8_t, 3>> colF; A.colorAt.assign(size_t(N), std::vector<std::array<uint8_t, 3>>(size_t(K), {0, 0, 0}));
    {
        std::vector<int> cn(size_t(K), 0); for (size_t p = 0; p < np; ++p) ++cn[size_t(lab[p])];
        for (int i = 0; i < N; ++i) { std::vector<double> acc(size_t(K) * 3, 0.0);
            for (size_t p = 0; p < np; ++p) for (int k = 0; k < 3; ++k) acc[size_t(lab[p]) * 3 + size_t(k)] += c.fr[size_t(i)].rgba[p * 4 + size_t(k)];
            for (int k = 0; k < K; ++k) for (int t = 0; t < 3; ++t) A.colorAt[size_t(i)][size_t(k)][size_t(t)] = cn[size_t(k)] ? uint8_t(std::clamp(acc[size_t(k) * 3 + size_t(t)] / cn[size_t(k)] + 0.5, 0.0, 255.0)) : 0; }
    }
    A.colorChannels = K; A.firstChannel = std::clamp(c.lo.firstChannel, 2, 999 - K);
    A.vis.assign(size_t(N), {});
    LsaAnim st = A; st.layers.resize(1); st.layers[0].always = true; st.layers[0].shapes.assign(1, Shape{});
    const long Bobj = std::max<long>(long(double(c.total) * 0.3), c.total - lsaTriggerCount(st) - c.total / 40);
    // 1) descomposicion exacta en rectangulos (greedy: ancho maximo y luego alto maximo): perfecta para formas rectilineas
    std::vector<Shape> exact; bool exactOk = true;
    {
        std::vector<uint8_t> cov(np, 0);
        for (int y = 0; y < H && exactOk; ++y) for (int x = 0; x < W; ++x) {
            const size_t p0 = size_t(y) * W + x; if (cov[p0]) continue;
            const int l = lab[p0]; int w = 1; while (x + w < W && !cov[p0 + size_t(w)] && lab[p0 + size_t(w)] == l) ++w;
            int h = 1; bool grow = true;
            while (y + h < H && grow) { for (int xx = x; xx < x + w; ++xx) { const size_t q = size_t(y + h) * W + xx; if (cov[q] || lab[q] != l) { grow = false; break; } } if (grow) ++h; }
            for (int yy = y; yy < y + h; ++yy) for (int xx = x; xx < x + w; ++xx) cov[size_t(yy) * W + xx] = 1;
            Shape sh2; sh2.cx = x + 0.5f * w; sh2.cy = y + 0.5f * h; sh2.w = float(w); sh2.h = float(h); sh2.rot = 0; sh2.r = uint8_t(l & 255); sh2.g = uint8_t(l >> 8); sh2.b = 0; sh2.z = 0;
            exact.push_back(sh2);
            if (long(exact.size()) > Bobj) { exactOk = false; break; }
        }
    }
    if (exactOk) {
        LsaLayer L; L.depth = 1; L.always = true; L.shapes = std::move(exact); A.layers.push_back(std::move(L));
        A.note = "canales " + std::to_string(K) + " (error de color " + std::to_string(int(eMax)) + "), rectangulos exactos";
        return true;
    }
    // 2) si no caben: cuadtree sobre el mapa de etiquetas: se divide la celda con mas pixeles que no son de su etiqueta mayoritaria
    struct Cell { int x0, y0, x1, y1, maj; long err; };
    auto stats = [&](int x0, int y0, int x1, int y1) { int cnt[64] = {0}; for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) ++cnt[lab[size_t(y) * W + x]];
        int bk = 0; for (int k = 1; k < K; ++k) if (cnt[k] > cnt[bk]) bk = k; return Cell{x0, y0, x1, y1, bk, long(x1 - x0) * (y1 - y0) - cnt[bk]}; };
    auto cmp = [](const Cell& a, const Cell& b) { return a.err < b.err; };
    std::priority_queue<Cell, std::vector<Cell>, decltype(cmp)> pq(cmp);
    std::vector<Cell> leaves; pq.push(stats(0, 0, W, H));
    long count = 1;
    while (!pq.empty() && count < Bobj) {
        Cell t = pq.top(); pq.pop();
        if (t.err == 0 || (t.x1 - t.x0 <= 1 && t.y1 - t.y0 <= 1)) { leaves.push_back(t); continue; }
        const int w = t.x1 - t.x0, h = t.y1 - t.y0;
        if (w >= 2 && h >= 2) { int xm = t.x0 + w / 2, ym = t.y0 + h / 2; pq.push(stats(t.x0, t.y0, xm, ym)); pq.push(stats(xm, t.y0, t.x1, ym)); pq.push(stats(t.x0, ym, xm, t.y1)); pq.push(stats(xm, ym, t.x1, t.y1)); count += 3; }
        else if (w >= 2) { int xm = t.x0 + w / 2; pq.push(stats(t.x0, t.y0, xm, t.y1)); pq.push(stats(xm, t.y0, t.x1, t.y1)); count += 1; }
        else { int ym = t.y0 + h / 2; pq.push(stats(t.x0, t.y0, t.x1, ym)); pq.push(stats(t.x0, ym, t.x1, t.y1)); count += 1; }
    }
    while (!pq.empty()) { leaves.push_back(pq.top()); pq.pop(); }
    LsaLayer L; L.depth = 1; L.always = true;
    for (const Cell& t : leaves) { Shape s; s.cx = 0.5f * float(t.x0 + t.x1); s.cy = 0.5f * float(t.y0 + t.y1); s.w = float(t.x1 - t.x0); s.h = float(t.y1 - t.y0); s.rot = 0; s.r = uint8_t(t.maj & 255); s.g = uint8_t(t.maj >> 8); s.b = 0; s.z = 0; L.shapes.push_back(s); }
    A.layers.push_back(std::move(L));
    A.note = "canales " + std::to_string(K) + " (error de color " + std::to_string(int(eMax)) + ")";
    return !A.layers[0].shapes.empty();
}

// ------------------------------------------------------------------ 28: auto ----
inline bool buildAuto(const Ctx& c, LsaAnim& A, int& chosen);

} // namespace lsa

// ============================================================================
//  Entrada: una funcion por kind (1..10), cierre y envoltorio LayeredAnim
// ============================================================================
namespace lsa {
inline bool buildKind(const Ctx& c, LsaAnim& A, int kind) {
    switch (kind) {
        case 1: return buildLifetimes(c, A, false);
        case 2: return buildGridTiles(c, A, false) || buildRdFlat(c, A);
        case 3: return buildKeyframesResidual(c, A) || buildRdFlat(c, A);
        case 4: return buildOptimizer(c, A);
        case 5: return buildRdFlat(c, A);
        case 6: return buildMotionRegions(c, A);
        case 7: return buildColorAnim(c, A);
        case 8: return buildLifetimes(c, A, true);
        case 10: return buildGridTiles(c, A, true) || buildRdFlat(c, A);
        default: return false;
    }
}
inline bool buildAuto(const Ctx& c, LsaAnim& A, int& chosen) {
    std::vector<int> kinds = {1, 2, 3, 5, 6, 10};
    if (c.opaqueAll) kinds.push_back(7);
    if (c.N <= 24 && int64_t(c.W) * c.H <= 200000) kinds.push_back(4);
    double bestScore = -1e9; LsaAnim bestA; chosen = 0;
    for (int k : kinds) {
        if (isCancelled(c.prog)) return false;
        LsaAnim t; t.W = c.W; t.H = c.H; t.frames = c.N; t.kind = k;
        if (!buildKind(c, t, k) || !t.ok()) continue;
        finish(c, t);
        const long tot = long(t.objectCount()) + lsaTriggerCount(t);
        double score = 0.7 * t.psnrAvg + 0.3 * t.psnrMin - (tot > c.total * 1.02 ? 6.0 : 0.0) - 0.0003 * double(tot);
        if (score > bestScore) { bestScore = score; bestA = std::move(t); chosen = k; }
    }
    if (chosen == 0) return false;
    A = std::move(bestA); A.note = std::string("elegido: ") + methodName(chosen) + (A.note.empty() ? "" : " | " + A.note);
    return true;
}
} // namespace lsa

inline LayeredAnim buildLsaAnim(const GifData& g, const GifPlan& plan, const Options& base, const LayeredOptions& lo, Progress* prog, int threads) {
    LayeredAnim R;
    const int N = plan.count();
    if (N < 1 || g.frames.empty()) { R.note = "No hay frames"; return R; }
    lsa::Ctx c = lsa::makeCtx(g, plan, base, lo, prog, threads);
    report(prog, 0.04f);
    auto A = std::make_shared<LsaAnim>(); A->W = c.W; A->H = c.H; A->frames = N; A->kind = lo.method;
    bool ok = false; int chosen = 0;
    if (lo.method == 9) { ok = lsa::buildAuto(c, *A, chosen); }
    else { ok = lsa::buildKind(c, *A, lo.method); if (ok) lsa::finish(c, *A); }
    if (isCancelled(prog)) { R.cancelled = true; return R; }
    R.W = c.W; R.H = c.H; R.frames = N;
    R.still.rasterW = c.W; R.still.rasterH = c.H; R.still.aspect = float(c.W) / float(c.H);
    if (!ok || !A->ok()) { R.note = A->note.empty() ? "No se pudo calcular el metodo" : A->note; return R; }
    R.psnr = A->psnr; R.psnrAvg = A->psnrAvg; R.psnrMin = A->psnrMin; R.note = A->note;
    R.firstChannel = A->firstChannel; R.palette = A->palette;
    R.lsa = A;
    report(prog, 1.f);
    return R;
}

} // namespace gdx
