// ============================================================================
//  Métodos de partición del plano en rectángulos:
//    1) Cuadrícula uniforme
//    2) Quadtree adaptativo (mejor-primero, llena el presupuesto exacto)
//    3) BSP / k-d adaptativo (corte óptimo por eje)
//    4) Pirámide de capas con residuos dispersos (pintor)
// ============================================================================
#pragma once
#include <queue>
#include <unordered_map>
#include "core.hpp"
#include "refit.hpp"

namespace gdx {

struct Rect { int x0, y0, x1, y1; };

inline Shape makeRectShape(const Rect& r, uint8_t R, uint8_t G, uint8_t B, int z = 0) {
    Shape s;
    s.cx = (r.x0 + r.x1) * 0.5f; s.cy = (r.y0 + r.y1) * 0.5f;
    s.w = float(r.x1 - r.x0);    s.h = float(r.y1 - r.y0);
    s.rot = 0; s.r = R; s.g = G; s.b = B; s.z = int16_t(z);
    return s;
}

// ------------------------------------------------------ 1) cuadrícula ------

inline void gridDims(int sw, int sh, int budget, int& cols, int& rows) {
    double a = double(sw) / sh;
    int c0 = std::max(1, int(std::sqrt(double(budget) * a)));
    double bestScore = -1e9; cols = 1; rows = 1;
    for (int c = std::max(1, c0 - 4); c <= c0 + 4; ++c) {
        int r = std::max(1, budget / c);
        int cc = std::min(c, sw), rr = std::min(r, sh);
        if (cc == sw) rr = std::min(sh, std::max(1, budget / cc));
        if (rr == sh) cc = std::min(sw, std::max(1, budget / rr));
        if (int64_t(cc) * rr > budget) continue;
        double dev = std::abs(std::log((double(cc) / rr) / a));
        double score = double(cc) * rr / budget - dev * 2.0;
        if (score > bestScore) { bestScore = score; cols = cc; rows = rr; }
    }
}

// Devuelve las formas y el tamaño del raster (cols x rows).
inline std::vector<Shape> gridShapes(const Image& src, int budget, int alphaThr, int& outW, int& outH) {
    int cols, rows;
    gridDims(src.w, src.h, std::max(1, budget), cols, rows);
    Image g = resizeArea(src, cols, rows);
    outW = cols; outH = rows;
    std::vector<Shape> out;
    out.reserve(size_t(cols) * rows);
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < cols; ++x) {
            const uint8_t* p = g.at(x, y);
            if (p[3] < alphaThr) continue;
            out.push_back(makeRectShape({x, y, x + 1, y + 1}, p[0], p[1], p[2]));
        }
    return out;
}

// ------------------------------------------- 2) y 3) árboles mejor-primero --

struct SplitInfo { Rect kids[4]; int nk = 0; double gain = 0; };

// Cuadtree: parte por la mitad en x e y.
struct QuadSplitter {
    const Stats& st;
    SplitInfo operator()(const Rect& r) const {
        SplitInfo s;
        int w = r.x1 - r.x0, h = r.y1 - r.y0;
        if (w <= 1 && h <= 1) return s;
        int xm = w > 1 ? r.x0 + w / 2 : r.x1, ym = h > 1 ? r.y0 + h / 2 : r.y1;
        int xs[3] = {r.x0, xm, r.x1}, ys[3] = {r.y0, ym, r.y1};
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i) {
                if (xs[i] >= xs[i + 1] || ys[j] >= ys[j + 1]) continue;
                s.kids[s.nk++] = {xs[i], ys[j], xs[i + 1], ys[j + 1]};
            }
        double e = 0;
        for (int k = 0; k < s.nk; ++k) e += st.sse(s.kids[k].x0, s.kids[k].y0, s.kids[k].x1, s.kids[k].y1);
        s.gain = st.sse(r.x0, r.y0, r.x1, r.y1) - e;
        return s;
    }
};

// BSP: prueba cortes en x e y y se queda con el de menor error total.
struct BspSplitter {
    const Stats& st;
    SplitInfo operator()(const Rect& r) const {
        SplitInfo s;
        int w = r.x1 - r.x0, h = r.y1 - r.y0;
        if (w <= 1 && h <= 1) return s;
        double best = 1e300; int bestPos = -1; bool bestVert = true;
        if (w > 1) {
            int step = std::max(1, (w - 1) / 192);
            for (int x = r.x0 + 1; x < r.x1; x += step) {
                double e = st.sse(r.x0, r.y0, x, r.y1) + st.sse(x, r.y0, r.x1, r.y1);
                if (e < best) { best = e; bestPos = x; bestVert = true; }
            }
        }
        if (h > 1) {
            int step = std::max(1, (h - 1) / 192);
            for (int y = r.y0 + 1; y < r.y1; y += step) {
                double e = st.sse(r.x0, r.y0, r.x1, y) + st.sse(r.x0, y, r.x1, r.y1);
                if (e < best) { best = e; bestPos = y; bestVert = false; }
            }
        }
        if (bestPos < 0) return s;
        if (bestVert) { s.kids[0] = {r.x0, r.y0, bestPos, r.y1}; s.kids[1] = {bestPos, r.y0, r.x1, r.y1}; }
        else          { s.kids[0] = {r.x0, r.y0, r.x1, bestPos}; s.kids[1] = {r.x0, bestPos, r.x1, r.y1}; }
        s.nk = 2;
        s.gain = st.sse(r.x0, r.y0, r.x1, r.y1) - best;
        return s;
    }
};

// Algoritmo mejor-primero: siempre divide la hoja con más prioridad, mientras el nº de objetos no supere el
// presupuesto. La prioridad mira dos niveles: un corte con poca ganancia inmediata pero que abre otro muy
// valioso (bordes, texturas) sube en la cola en vez de quedarse atrás.
// (Se probó además una fase de intercambios fusionar+dividir: no mejoraba, el voraz ya es casi óptimo aquí.)
template <class Splitter>
std::vector<Shape> bestFirstTree(const Stats& st, int budget, Splitter splitter, Progress* prog) {
    struct TNode { Rect r; SplitInfo sp; int emit; bool split; };
    std::vector<TNode> nodes;
    nodes.reserve(size_t(budget) * 2 + 16);
    std::priority_queue<std::pair<double, int>> pq;
    std::unordered_map<uint64_t, SplitInfo> cache;
    cache.reserve(size_t(budget) * 3 + 16);

    auto keyOf = [](const Rect& r) { return (uint64_t(r.x0) << 48) | (uint64_t(r.y0) << 32) | (uint64_t(r.x1) << 16) | uint64_t(r.y1); };
    auto splitCached = [&](const Rect& r) -> const SplitInfo& {
        auto it = cache.find(keyOf(r));
        if (it == cache.end()) it = cache.emplace(keyOf(r), splitter(r)).first;
        return it->second;
    };
    auto emitOf = [&](const Rect& r) { return Stats::mostlyOpaque(st.query(r.x0, r.y0, r.x1, r.y1)) ? 1 : 0; };
    auto makeNode = [&](const Rect& r) {
        TNode n; n.r = r; n.sp = splitCached(r); n.emit = emitOf(r); n.split = false;
        nodes.push_back(n);
        int i = int(nodes.size()) - 1;
        const SplitInfo& sp = nodes[i].sp;
        if (sp.nk > 0) {
            double g2 = 0;
            for (int k = 0; k < sp.nk; ++k) g2 = std::max(g2, splitCached(sp.kids[k]).gain);
            // Con corte fijo (quadtree) la ganancia del primer nivel puede ser EXACTAMENTE 0 aunque la zona sea mixta (p. ej. un
            // sprite simetrico respecto al centro): se mira tambien un nivel mas abajo para no dejarlo sin dividir nunca.
            if (sp.gain > 1e-9 || g2 > 1e-9) pq.push({std::max(sp.gain, 0.5 * (sp.gain + g2)), i});
        }
        return i;
    };

    int root = makeNode({0, 0, st.w, st.h});
    int leaves = nodes[root].emit;
    int iter = 0;
    while (!pq.empty()) {
        int i = pq.top().second; pq.pop();
        SplitInfo sp = nodes[i].sp;
        int d = -nodes[i].emit;
        for (int k = 0; k < sp.nk; ++k) d += emitOf(sp.kids[k]);
        if (leaves + d > budget) continue;             // no cabe: se queda como hoja
        nodes[i].split = true;
        leaves += d;
        for (int k = 0; k < sp.nk; ++k) makeNode(sp.kids[k]);
        if ((++iter & 255) == 0) {
            report(prog, std::min(1.f, float(leaves) / std::max(1, budget)));
            if (isCancelled(prog)) break;
        }
    }
    std::vector<Shape> out;
    out.reserve(size_t(leaves));
    for (auto& n : nodes) {
        if (n.split || !n.emit) continue;
        auto q = st.query(n.r.x0, n.r.y0, n.r.x1, n.r.y1);
        uint8_t R, G, B; Stats::mean(q, R, G, B);
        out.push_back(makeRectShape(n.r, R, G, B));
    }
    return out;
}

// ------------------------------------------------ 4) pirámide de capas ------
// Se pinta un rectángulo grande con el color medio y, encima, sólo los
// sub-bloques cuyo color se aparta del que ya se ve (residuos dispersos).
// Respecto a un quadtree, los hijos "parecidos" al padre no cuestan objetos.

struct PyrCtx {
    const Stats* st;
    double lam = 1;                      // "precio" de un objeto, en error cuadrático total
    size_t budget = 0, count = 0;
    bool record = false, abort = false;
    std::vector<Shape>* out = nullptr;
};
struct VisCol { double r = 0, g = 0, b = 0; bool has = false; };

// Criterio rate-distortion: un bloque se pinta si el error que elimina
// (n * |media - color visible|^2) supera lambda, y se sigue bajando mientras
// el error restante del bloque supere lambda.
inline void pyrVisit(PyrCtx& c, const Rect& node, VisCol vis, int level) {
    if (c.abort) return;
    int w = node.x1 - node.x0, h = node.y1 - node.y0;
    if (w <= 1 && h <= 1) return;
    int xm = w > 1 ? node.x0 + w / 2 : node.x1, ym = h > 1 ? node.y0 + h / 2 : node.y1;
    int xs[3] = {node.x0, xm, node.x1}, ys[3] = {node.y0, ym, node.y1};
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
            if (xs[i] >= xs[i + 1] || ys[j] >= ys[j + 1]) continue;
            Rect k{xs[i], ys[j], xs[i + 1], ys[j + 1]};
            auto q = c.st->query(k.x0, k.y0, k.x1, k.y1);
            if (q.n == 0) continue;                               // vacío
            if (q.n < q.area) { pyrVisit(c, k, vis, level + 1); if (c.abort) return; continue; } // mezcla opaco/transparente: dividir sin pintar
            double mr = q.sr / q.n, mg = q.sg / q.n, mb = q.sb / q.n;
            double within = q.sq - (q.sr * q.sr + q.sg * q.sg + q.sb * q.sb) / q.n;   // error interno
            if (within < 0) within = 0;
            double dv = 0;
            if (vis.has) dv = (mr - vis.r) * (mr - vis.r) + (mg - vis.g) * (mg - vis.g) + (mb - vis.b) * (mb - vis.b);
            if (vis.has && within + q.n * dv <= c.lam) continue;   // hereda el color visible
            VisCol nv = vis;
            if (!vis.has || q.n * dv > c.lam) {                     // vale la pena pintar este bloque
                if (++c.count > c.budget) { c.abort = true; return; }
                if (c.record) {
                    uint8_t R, G, B; Stats::mean(q, R, G, B);
                    c.out->push_back(makeRectShape(k, R, G, B, level + 1));
                }
                nv = {mr, mg, mb, true};
            }
            double dn = (nv.r - mr) * (nv.r - mr) + (nv.g - mg) * (nv.g - mg) + (nv.b - mb) * (nv.b - mb);
            if (q.area > 1 && within + q.n * dn > c.lam) {
                pyrVisit(c, k, nv, level + 1);
                if (c.abort) return;
            }
        }
}

inline size_t pyrRun(const Stats& st, double lam, size_t budget, bool record, std::vector<Shape>* out) {
    PyrCtx c; c.st = &st; c.lam = lam; c.budget = budget; c.record = record; c.out = out;
    Rect root{0, 0, st.w, st.h};
    auto q = st.query(root.x0, root.y0, root.x1, root.y1);
    VisCol vis;
    if (q.n == q.area && q.n > 0) {                                // imagen totalmente opaca: fondo único
        double mr = q.sr / q.n, mg = q.sg / q.n, mb = q.sb / q.n;
        ++c.count;
        if (record) { uint8_t R, G, B; Stats::mean(q, R, G, B); out->push_back(makeRectShape(root, R, G, B, 0)); }
        vis = {mr, mg, mb, true};
    }
    pyrVisit(c, root, vis, 0);
    return c.abort ? budget + 1 : c.count;
}

inline std::vector<Shape> treeShapes(const Image& raster, int budget, int alphaThr, bool bsp, Progress* prog);
inline std::vector<Shape> pyramidShapes(const Image& raster, int budget, int alphaThr, Progress* prog) {
    Stats st; st.build(raster, alphaThr);
    size_t B = size_t(std::max(1, budget));
    double lo = 0.5, hi = 3.0 * 255 * 255 * double(raster.w) * raster.h;
    // Con transparencia, los bloques opacos del quadtree que quedan sin "color visible" se pintan SIEMPRE (no dependen de lambda): si
    // solo esos ya superan el presupuesto, ninguna lambda sirve (antes: bucle infinito). Se cae a BSP, que respeta cualquier presupuesto.
    if (pyrRun(st, hi, B, false, nullptr) > B) return treeShapes(raster, budget, alphaThr, true, prog);
    if (pyrRun(st, lo, B, false, nullptr) <= B) hi = lo;         // cabe incluso sin pérdida
    else {
        for (int it = 0; it < 44 && hi / lo > 1.004; ++it) {
            double mid = std::sqrt(lo * hi);
            if (pyrRun(st, mid, B, false, nullptr) <= B) hi = mid; else lo = mid;
            report(prog, 0.5f * float(it + 1) / 44.f);
            if (isCancelled(prog)) break;
        }
        for (int g = 0; g < 400 && pyrRun(st, hi, B, false, nullptr) > B; ++g) hi *= 1.02;  // seguridad (acotada)
    }
    // Segunda fase: los bloques padre totalmente tapados por sus hijos no se ven. Se cuentan solo las formas
    // visibles, así que se puede bajar lambda (más detalle) hasta llenar el presupuesto de verdad.
    auto visible = [&](double lam, std::vector<Shape>* keep) {
        std::vector<Shape> v;
        pyrRun(st, lam, B * 2, true, &v);
        refitVisible(v, raster, alphaThr, true);
        size_t n = v.size();
        if (keep) *keep = std::move(v);
        return n;
    };
    if (hi > lo) {
        double top = hi, bot = hi * 0.25;
        while (visible(bot, nullptr) <= B && bot > lo) { top = bot; bot *= 0.5; }      // cota inferior que ya no cabe
        if (bot < lo) bot = lo;
        for (int it = 0; it < 13 && top / bot > 1.01; ++it) {
            double mid = std::sqrt(top * bot);
            if (visible(mid, nullptr) <= B) top = mid; else bot = mid;
            report(prog, 0.5f + 0.45f * float(it + 1) / 13.f);
            if (isCancelled(prog)) break;
        }
        hi = top;
    }
    std::vector<Shape> out;
    visible(hi, &out);
    for (int g = 0; g < 400 && out.size() > B; ++g) { hi *= 1.03; visible(hi, &out); }       // seguridad (acotada)
    if (out.size() > B) return treeShapes(raster, budget, alphaThr, true, prog);
    return out;
}

// ------------------------------------------------------------ envoltorios ---

inline std::vector<Shape> treeShapes(const Image& raster, int budget, int alphaThr, bool bsp, Progress* prog) {
    Stats st; st.build(raster, alphaThr);
    if (bsp) return bestFirstTree(st, budget, BspSplitter{st}, prog);
    return bestFirstTree(st, budget, QuadSplitter{st}, prog);
}

} // namespace gdx
