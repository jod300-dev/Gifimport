// ============================================================================
//  LSA: horario de triggers, cadena de GD, render de un frame y tope de objetos (metodos 20-29)
// ============================================================================
#pragma once
#include "layered.hpp"

namespace gdx {
namespace lsa {

struct Sched {
    std::vector<int> dyn;                       // capas dinamicas (con grupo): visibles solo en algunos frames
    std::vector<char> isDyn;                    // por capa
    std::vector<std::vector<int>> changed;      // por frame: ids que cambian respecto al frame anterior (capa: id; canal de color: nL + canal)
    std::vector<std::vector<int>> heal;         // por frame (!full): ids a reafirmar
    std::vector<int> initHide;                  // capas dinamicas que no se ven en el frame 0
    bool full = false, hasI = false;
    int nSteps = 0, groups = 0, triggers = 0;
    std::vector<int> zBase;                     // por capa: z base de su banda
    long zTotal = 0;
};

inline Sched schedule(const LsaAnim& A) {
    Sched s; const int N = A.frames, nL = int(A.layers.size());
    s.isDyn.assign(size_t(nL), 0);
    std::vector<int> count(size_t(nL), 0);
    for (int f = 0; f < N; ++f) for (int id : A.vis[size_t(f)]) if (id >= 0 && id < nL) ++count[size_t(id)];
    for (int l = 0; l < nL; ++l)
        if (!A.layers[size_t(l)].always && !A.layers[size_t(l)].shapes.empty() && count[size_t(l)] > 0 && count[size_t(l)] < N) { s.isDyn[size_t(l)] = 1; s.dyn.push_back(l); }
    s.changed.assign(size_t(N), {});
    for (int f = 0; f < N; ++f) {
        const int pf = (f + N - 1) % N;
        std::set<int> a, b;
        for (int id : A.vis[size_t(f)]) if (id >= 0 && id < nL && s.isDyn[size_t(id)]) a.insert(id);
        for (int id : A.vis[size_t(pf)]) if (id >= 0 && id < nL && s.isDyn[size_t(id)]) b.insert(id);
        for (int id : a) if (!b.count(id)) s.changed[size_t(f)].push_back(id);
        for (int id : b) if (!a.count(id)) s.changed[size_t(f)].push_back(id);
        for (int c = 0; c < A.colorChannels; ++c)
            if (A.colorAt[size_t(f)][size_t(c)] != A.colorAt[size_t(pf)][size_t(c)]) s.changed[size_t(f)].push_back(nL + c);
        if (!s.changed[size_t(f)].empty()) ++s.nSteps;
    }
    if (N < 2) s.nSteps = 0;
    s.full = A.colorChannels == 0 && int(s.dyn.size()) <= kFullStateMaxLayers;
    int acts = 0;
    if (s.full) acts = s.nSteps * int(s.dyn.size());
    else { s.heal = healSets(s.changed, kHealWindow); for (auto& h : s.heal) acts += int(h.size()); }
    std::set<int> v0; for (int id : A.vis[0]) v0.insert(id);
    for (int l : s.dyn) if (!v0.count(l)) s.initHide.push_back(l);
    s.hasI = !s.initHide.empty() || A.colorChannels > 0;
    s.groups = int(s.dyn.size()) + s.nSteps + 1 + (s.hasI ? 1 : 0);
    s.triggers = 2 * s.nSteps + 1 + (s.hasI ? 1 : 0) + acts + int(s.initHide.size()) + A.colorChannels + 1 + (s.hasI ? 1 : 0);
    // bandas de z: una por valor de depth (capas con la misma depth comparten banda)
    std::map<int, int> span;
    for (int l = 0; l < nL; ++l) { if (A.layers[size_t(l)].shapes.empty()) continue; int mz = 0; for (auto& sh : A.layers[size_t(l)].shapes) mz = std::max<int>(mz, sh.z);
        int& sp = span[A.layers[size_t(l)].depth]; sp = std::max(sp, mz + 1); }
    std::map<int, long> base; long acc = 0;
    for (auto& kv : span) { base[kv.first] = acc; acc += kv.second; }
    s.zTotal = acc;
    s.zBase.assign(size_t(nL), 0);
    for (int l = 0; l < nL; ++l) s.zBase[size_t(l)] = int(base[A.layers[size_t(l)].depth]);
    return s;
}

// Un frame montado como Result (vista previa y PSNR): capas siempre visibles + las visibles del frame, con el z remapeado a sus bandas.
inline Result frameResult(const LsaAnim& A, const Sched& S, int f) {
    Result r; r.rasterW = A.W; r.rasterH = A.H; r.aspect = A.H > 0 ? float(A.W) / A.H : 1.f;
    auto add = [&](int l) {
        const LsaLayer& L = A.layers[size_t(l)];
        for (Shape sh : L.shapes) {
            sh.z = int16_t(std::min<long>(32000, S.zBase[size_t(l)] + sh.z));
            if (A.colorChannels > 0) { int idx = sh.r | (sh.g << 8); idx = std::min(idx, A.colorChannels - 1); const auto& c = A.colorAt[size_t(f)][size_t(idx)]; sh.r = c[0]; sh.g = c[1]; sh.b = c[2]; }
            r.shapes.push_back(sh);
        }
    };
    for (int l = 0; l < int(A.layers.size()); ++l) if (A.layers[size_t(l)].always) add(l);
    std::set<int> seen;
    for (int l = 0; l < int(A.layers.size()); ++l) if (!A.layers[size_t(l)].always && !S.isDyn[size_t(l)] && !A.layers[size_t(l)].shapes.empty()) {
        // visible en todos los frames (la planificacion la trata como siempre visible)
        bool all = true; for (int g = 0; g < A.frames && all; ++g) { auto& v = A.vis[size_t(g)]; all = std::find(v.begin(), v.end(), l) != v.end(); }
        if (all) add(l);
    }
    for (int l : A.vis[size_t(f)]) if (l >= 0 && l < int(A.layers.size()) && S.isDyn[size_t(l)] && seen.insert(l).second) add(l);
    std::stable_sort(r.shapes.begin(), r.shapes.end(), [](const Shape& a, const Shape& b) { return a.z < b.z; });
    if (f < int(A.psnr.size())) r.psnr = A.psnr[size_t(f)];
    return r;
}

} // namespace lsa

inline int lsaTriggerCount(const LsaAnim& A) { return lsa::schedule(A).triggers; }
inline int lsaGroupCount(const LsaAnim& A) { return lsa::schedule(A).groups; }

inline std::string buildLsaString(const LsaAnim& A, const GifPlan& plan, const GifExportParams& p, std::string* errmsg = nullptr) {
    auto fail = [&](const char* m) { if (errmsg) *errmsg = m; return std::string(); };
    if (!A.ok() || plan.count() != A.frames) return fail("No hay capas calculadas");
    const lsa::Sched S = lsa::schedule(A);
    if (p.baseGroup < 1 || p.baseGroup + S.groups - 1 > 9999) return fail("No quedan grupos libres suficientes (max. 9999)");
    if (S.zTotal > 30000) return fail("Demasiadas capas de z (mas de 30000)");
    const bool palMode = !A.palette.empty(), colMode = A.colorChannels > 0, useCh = palMode || colMode;
    if (palMode && (A.firstChannel < 2 || A.firstChannel + int(A.palette.size()) - 1 > 999)) return fail("No caben los canales de color (2..999)");
    if (colMode && (A.firstChannel < 2 || A.firstChannel + A.colorChannels - 1 > 999)) return fail("No caben los canales de color (2..999)");
    std::map<uint32_t, int> chOf;
    for (size_t i = 0; i < A.palette.size(); ++i) chOf[lay::colKey(A.palette[i][0], A.palette[i][1], A.palette[i][2])] = A.firstChannel + int(i);
    const int N = A.frames, nL = int(A.layers.size());

    int next = p.baseGroup;
    std::vector<int> gLayer(size_t(nL), 0), gStep(size_t(N), 0);
    for (int l : S.dyn) gLayer[size_t(l)] = next++;
    for (int f = 0; f < N; ++f) if (!S.changed[size_t(f)].empty() && N >= 2) gStep[size_t(f)] = next++;
    const int L = next++;
    const int I = S.hasI ? next++ : 0;

    std::string out; out.reserve(A.objectCount() * 100 + size_t(S.triggers) * 90);
    // objetos
    std::set<int> v0all;
    for (int l = 0; l < nL; ++l) {
        const LsaLayer& ly = A.layers[size_t(l)];
        if (ly.shapes.empty()) continue;
        const bool visibleSomewhere = ly.always || S.isDyn[size_t(l)] || [&] { for (int f = 0; f < N; ++f) { auto& v = A.vis[size_t(f)]; if (std::find(v.begin(), v.end(), l) != v.end()) return true; } return false; }();
        if (!visibleSomewhere) continue;                                  // capa que nunca se ve: no se emite (sin grupo seria visible siempre)
        Result r; r.rasterW = A.W; r.rasterH = A.H; r.aspect = float(A.W) / float(A.H); r.shapes = ly.shapes; r.colored = useCh;
        GdExportOptions eo = p.eo;
        eo.groupStr = gLayer[size_t(l)] > 0 ? std::to_string(gLayer[size_t(l)]) : std::string();
        eo.zBase = p.eo.zBase + S.zBase[size_t(l)];
        eo.monoFastPath = false;
        if (useCh) {
            ChannelMap cm = buildChannelMap(r);
            eo.channels.assign(cm.colors.size(), 0);
            for (size_t i = 0; i < cm.colors.size(); ++i) {
                if (colMode) eo.channels[i] = A.firstChannel + std::min<int>(A.colorChannels - 1, int(cm.colors[i][0]) | (int(cm.colors[i][1]) << 8));
                else eo.channels[i] = chOf.at(lay::colKey(cm.colors[i][0], cm.colors[i][1], cm.colors[i][2]));
            }
        }
        std::string s = toGdString(r, eo);
        if (!s.empty()) { out += s; out += ';'; }
    }
    const size_t objEnd = out.size();
    Result dim; dim.aspect = float(A.W) / float(A.H);
    GdSize sz = gdImageSize(dim, p.eo);
    detail::TrigBoard board(detail::rowLeft(p.eo.originX - sz.w * 0.5, p.trigX), p.eo.originY + sz.h * 0.5 + 90.0);
    double x, y;
    auto colorTrig = [&](int ch, const std::array<uint8_t, 3>& c, int ownGroup) {
        board.atAlpha(x, y);
        detail::addTriggerHead(out, 899, x, y);
        out += ",7,"; out += std::to_string(c[0]); out += ",8,"; out += std::to_string(c[1]); out += ",9,"; out += std::to_string(c[2]);
        out += ",10,0,35,1,17,0,23,"; out += std::to_string(A.firstChannel + ch);
        out += ",62,1,87,1,57,"; out += std::to_string(ownGroup); out += ';';
    };
    double t = 0; std::vector<double> tStart(size_t(N), 0.0);
    for (int f = 0; f < N; ++f) { tStart[size_t(f)] = t; if (gStep[size_t(f)] > 0) { board.atSpawn(x, y); detail::addSpawn(out, p, x, y, gStep[size_t(f)], t, L); } t += plan.delay[size_t(f)]; }
    for (int f = 0; f < N; ++f) if (gStep[size_t(f)] > 0) { board.atSpawn(x, y); detail::addSpawn(out, p, x, y, gStep[size_t(f)], tStart[size_t(f)] + reassertDelay(plan.delay[size_t(f)]), L); }
    board.atSpawn(x, y); detail::addSpawn(out, p, x, y, L, plan.total, L);
    if (S.hasI) { board.atSpawn(x, y); detail::addSpawn(out, p, x, y, I, 0, L); }

    for (int f = 0; f < N; ++f) {
        const int F = gStep[size_t(f)]; if (F <= 0) continue;
        std::set<int> vis; for (int id : A.vis[size_t(f)]) vis.insert(id);
        if (S.full) {
            for (int l : S.dyn) if (vis.count(l)) { board.atAlpha(x, y); detail::addAlpha(out, p, x, y, gLayer[size_t(l)], 1, F); }
            for (int l : S.dyn) if (!vis.count(l)) { board.atAlpha(x, y); detail::addAlpha(out, p, x, y, gLayer[size_t(l)], 0, F); }
        } else {
            for (int id : S.heal[size_t(f)]) if (id < nL && vis.count(id)) { board.atAlpha(x, y); detail::addAlpha(out, p, x, y, gLayer[size_t(id)], 1, F); }
            for (int id : S.heal[size_t(f)]) if (id < nL && !vis.count(id)) { board.atAlpha(x, y); detail::addAlpha(out, p, x, y, gLayer[size_t(id)], 0, F); }
            for (int id : S.heal[size_t(f)]) if (id >= nL) colorTrig(id - nL, A.colorAt[size_t(f)][size_t(id - nL)], F);
        }
    }
    if (S.hasI) {
        for (int l : S.initHide) { board.atAlpha(x, y); detail::addAlpha(out, p, x, y, gLayer[size_t(l)], 0, I); }
        for (int c = 0; c < A.colorChannels; ++c) colorTrig(c, A.colorAt[0][size_t(c)], I);
    }
    double sx = p.trigX; (void)board.nextY();
    detail::addSpawn(out, p, sx, board.rowY(), L, 0, 0);
    if (S.hasI) detail::addSpawn(out, p, sx, board.rowY() + 30.0, I, 0, 0);
    detail::triggersFirst(out, objEnd);
    if (!out.empty() && out.back() == ';') out.pop_back();
    return out;
}

// Tope duro: objetos + triggers <= maxTotal; si se pasa, se recortan los ultimos (mas finos) objetos de las capas mas grandes.
inline void capLsa(LsaAnim& A, int maxTotal) {
    if (!A.ok()) return;
    for (int pass = 0; pass < 4; ++pass) {
        const long total = long(A.objectCount()) + long(lsaTriggerCount(A));
        if (total <= maxTotal) return;
        const long excess = total - maxTotal; long trimmable = 0;
        for (auto& l : A.layers) if (l.shapes.size() > 1) trimmable += long(l.shapes.size()) - 1;
        if (trimmable <= 0) return;
        const double f = std::min(1.0, double(excess) / double(trimmable) + 0.002);
        for (auto& l : A.layers) if (l.shapes.size() > 1) { long cut = long(std::ceil(double(long(l.shapes.size()) - 1) * f)); l.shapes.resize(size_t(std::max<long>(1, long(l.shapes.size()) - cut))); }
        A.note += A.note.empty() ? "Recortado al tope de objetos." : " Recortado al tope de objetos.";
    }
}

} // namespace gdx
