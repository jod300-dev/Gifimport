// ============================================================================
//  Metodo 26 (v0.5.6): ANIMACION POR COLORES con teselado espacio-temporal.
//
//  Las formas NO cambian durante la animacion: cada una es un rectangulo fijo ligado a un CANAL DE COLOR y cada frame solo recolorea canales
//  con Color triggers. Por eso el reto es doble: (1) que los rectangulos sean pocos y limpios, (2) que cada canal tenga el color exacto en cada frame.
//
//  Version anterior: k-means de la trayectoria de cada pixel + descomposicion "golosa" del mapa de etiquetas en rectangulos. Con movimiento real
//  (un personaje que se mueve) el mapa de etiquetas es ruido: miles de rectangulos finos, colores mezclados y rayas.
//
//  Ahora, en tres pasos:
//   1. BSP ESPACIO-TEMPORAL: se parte el lienzo con cortes rectos (como el metodo BSP normal) pero el error de una hoja se mide en TODOS los frames a la vez
//      (suma de la varianza de color de la hoja en cada frame; imagenes integrales por frame). Cada hoja es un rectangulo EXACTO: las hojas teselan el
//      lienzo sin huecos ni solapes (no hay "objetos encimados") y los cortes caen justo donde algo cambia en algun momento.
//   2. CANALES: las trayectorias de color de las hojas (3N valores) se agrupan en K canales con k-means ponderado por area. El color de un canal en
//      cada frame es la media (ponderada por area) de sus hojas: el optimo en minimos cuadrados. K y el numero de hojas se reparten con un modelo de coste
//      (objetos + Color triggers): se prueban varios K y se queda el que menos error total da con el presupuesto.
//   3. FUSION: dos hojas hermanas con el mismo canal vuelven a ser un solo rectangulo (menos objetos), recursivamente.
//  Para GIF de ciclos de color/destellos (donde pocas trayectorias distintas lo explican todo) sale una descomposicion exacta con muy pocos objetos.
// ============================================================================
#pragma once
#include "lsa_core.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <vector>

namespace gdx {
namespace lsa {

// ------------------------------------------------------------------ imagenes integrales de todos los frames ----
struct SttVolume {
    int W = 0, H = 0, N = 0; size_t PW = 0;
    std::vector<int32_t> s3;      // [(y * PW + x) * N + f] * 3 + canal   (suma de R, G, B del rectangulo [0,x) x [0,y))
    std::vector<int64_t> sq;      // [(y * PW + x) * N + f]               (suma de R^2 + G^2 + B^2)
    void build(const std::vector<Image>& fr, int w, int h) {
        W = w; H = h; N = int(fr.size()); PW = size_t(W) + 1;
        const size_t nc = PW * size_t(H + 1);
        s3.assign(nc * size_t(N) * 3, 0); sq.assign(nc * size_t(N), 0);
        for (int f = 0; f < N; ++f) {
            const Image& im = fr[size_t(f)];
            for (int y = 0; y < H; ++y) {
                int64_t rr = 0, gg = 0, bb = 0, qq = 0;
                for (int x = 0; x < W; ++x) {
                    const uint8_t* p = im.at(x, y);
                    rr += p[0]; gg += p[1]; bb += p[2]; qq += int64_t(p[0]) * p[0] + int64_t(p[1]) * p[1] + int64_t(p[2]) * p[2];
                    const size_t i = (size_t(y + 1) * PW + size_t(x + 1)) * size_t(N) + size_t(f), up = (size_t(y) * PW + size_t(x + 1)) * size_t(N) + size_t(f);
                    s3[i * 3] = int32_t(s3[up * 3] + rr); s3[i * 3 + 1] = int32_t(s3[up * 3 + 1] + gg); s3[i * 3 + 2] = int32_t(s3[up * 3 + 2] + bb);
                    sq[i] = sq[up] + qq;
                }
            }
        }
    }
    // error (suma de cuadrados respecto a la media de la hoja en cada frame) del rectangulo [x0,x1) x [y0,y1) sobre todos los frames
    double err(int x0, int y0, int x1, int y1) const {
        const double n = double(x1 - x0) * double(y1 - y0); if (n <= 0) return 0;
        const size_t a = (size_t(y1) * PW + size_t(x1)) * size_t(N), b = (size_t(y0) * PW + size_t(x1)) * size_t(N),
                     cc = (size_t(y1) * PW + size_t(x0)) * size_t(N), d = (size_t(y0) * PW + size_t(x0)) * size_t(N);
        double acc = 0;
        for (int f = 0; f < N; ++f) {
            const size_t fa = (a + size_t(f)), fb = (b + size_t(f)), fc = (cc + size_t(f)), fd = (d + size_t(f));
            const double r = double(int64_t(s3[fa * 3]) - s3[fb * 3] - s3[fc * 3] + s3[fd * 3]);
            const double g = double(int64_t(s3[fa * 3 + 1]) - s3[fb * 3 + 1] - s3[fc * 3 + 1] + s3[fd * 3 + 1]);
            const double bl = double(int64_t(s3[fa * 3 + 2]) - s3[fb * 3 + 2] - s3[fc * 3 + 2] + s3[fd * 3 + 2]);
            const double q = double(sq[fa] - sq[fb] - sq[fc] + sq[fd]);
            acc += q - (r * r + g * g + bl * bl) / n;
        }
        return std::max(0.0, acc);
    }
    // media de color por frame de un rectangulo (3N valores)
    void mean(int x0, int y0, int x1, int y1, float* out) const {
        const double n = std::max(1.0, double(x1 - x0) * double(y1 - y0));
        const size_t a = (size_t(y1) * PW + size_t(x1)) * size_t(N), b = (size_t(y0) * PW + size_t(x1)) * size_t(N),
                     cc = (size_t(y1) * PW + size_t(x0)) * size_t(N), d = (size_t(y0) * PW + size_t(x0)) * size_t(N);
        for (int f = 0; f < N; ++f) for (int k = 0; k < 3; ++k)
            out[size_t(f) * 3 + size_t(k)] = float(double(int64_t(s3[(a + size_t(f)) * 3 + size_t(k)]) - s3[(b + size_t(f)) * 3 + size_t(k)] - s3[(cc + size_t(f)) * 3 + size_t(k)] + s3[(d + size_t(f)) * 3 + size_t(k)]) / n);
    }
};

// ------------------------------------------------------------------ BSP espacio-temporal ----
struct SttNode { int x0, y0, x1, y1, parent = -1, c0 = -1, c1 = -1, splitTime = -1, axis = -1, pos = 0; double err = 0, gain = 0, e0 = 0, e1 = 0; };

struct SttTree {
    std::vector<SttNode> nd; int splits = 0;
    const SttVolume* V = nullptr;
    void findSplit(int i) {
        SttNode& n = nd[size_t(i)]; n.gain = 0; n.axis = -1;
        if (n.err <= 1e-9) return;
        double best = n.err; int ba = -1, bp = 0; double b0 = 0, b1 = 0;
        for (int xm = n.x0 + 1; xm < n.x1; ++xm) { const double e0 = V->err(n.x0, n.y0, xm, n.y1), e1 = V->err(xm, n.y0, n.x1, n.y1); if (e0 + e1 < best - 1e-9) { best = e0 + e1; ba = 0; bp = xm; b0 = e0; b1 = e1; } }
        for (int ym = n.y0 + 1; ym < n.y1; ++ym) { const double e0 = V->err(n.x0, n.y0, n.x1, ym), e1 = V->err(n.x0, ym, n.x1, n.y1); if (e0 + e1 < best - 1e-9) { best = e0 + e1; ba = 1; bp = ym; b0 = e0; b1 = e1; } }
        if (ba >= 0) { n.axis = ba; n.pos = bp; n.e0 = b0; n.e1 = b1; n.gain = n.err - best; }
    }
    // construye hasta `maxLeaves` hojas (o hasta que no quede error)
    void build(const SttVolume& vol, int maxLeaves, Progress* prog) {
        V = &vol; nd.clear(); splits = 0;
        SttNode r; r.x0 = 0; r.y0 = 0; r.x1 = vol.W; r.y1 = vol.H; r.err = vol.err(0, 0, vol.W, vol.H); nd.push_back(r);
        struct QE { double g; int i; bool operator<(const QE& o) const { return g != o.g ? g < o.g : i > o.i; } };
        std::priority_queue<QE> pq; findSplit(0); if (nd[0].axis >= 0) pq.push({nd[0].gain, 0});
        int leaves = 1;
        while (!pq.empty() && leaves < maxLeaves) {
            if ((splits & 255) == 0 && isCancelled(prog)) break;
            const int i = pq.top().i; pq.pop();
            SttNode p = nd[size_t(i)];
            SttNode a, b; a.parent = b.parent = i;
            if (p.axis == 0) { a.x0 = p.x0; a.y0 = p.y0; a.x1 = p.pos; a.y1 = p.y1; b.x0 = p.pos; b.y0 = p.y0; b.x1 = p.x1; b.y1 = p.y1; }
            else { a.x0 = p.x0; a.y0 = p.y0; a.x1 = p.x1; a.y1 = p.pos; b.x0 = p.x0; b.y0 = p.pos; b.x1 = p.x1; b.y1 = p.y1; }
            a.err = p.e0; b.err = p.e1;
            const int ia = int(nd.size()); nd.push_back(a); const int ib = int(nd.size()); nd.push_back(b);
            nd[size_t(i)].c0 = ia; nd[size_t(i)].c1 = ib; nd[size_t(i)].splitTime = splits++;
            findSplit(ia); findSplit(ib);
            if (nd[size_t(ia)].axis >= 0) pq.push({nd[size_t(ia)].gain, ia});
            if (nd[size_t(ib)].axis >= 0) pq.push({nd[size_t(ib)].gain, ib});
            ++leaves;
        }
    }
    int leafCount() const { return splits + 1; }
    // hojas tras las `s` primeras particiones (s = hojas - 1)
    std::vector<int> leavesAt(int s) const {
        std::vector<int> out; s = std::clamp(s, 0, splits);
        std::vector<int> st{0};
        while (!st.empty()) { int i = st.back(); st.pop_back(); const SttNode& n = nd[size_t(i)];
            if (n.splitTime < 0 || n.splitTime >= s) out.push_back(i); else { st.push_back(n.c1); st.push_back(n.c0); } }
        return out;
    }
};

// ------------------------------------------------------------------ k-means ponderado ----
struct WPoints { int L = 0, D = 0; std::vector<float> m; std::vector<double> w; };

inline double wkmeans(const WPoints& P, int K, uint32_t seed, int iters, std::vector<float>& cen, std::vector<int>& as, bool keepInit) {
    const int L = P.L; const size_t D = size_t(P.D);
    K = std::max(1, std::min(K, L));
    if (!keepInit || int(cen.size()) != int(size_t(K) * D)) {
        cen.assign(size_t(K) * D, 0.f); Rng rng(seed);
        std::vector<double> d2(size_t(L), 1e300);
        int first = 0; for (int l = 1; l < L; ++l) if (P.w[size_t(l)] > P.w[size_t(first)]) first = l;
        std::memcpy(&cen[0], &P.m[size_t(first) * D], D * sizeof(float));
        for (int k = 1; k < K; ++k) {                                                            // k-means++ ponderado
            double sum = 0;
            for (int l = 0; l < L; ++l) { double d = 0; const float* a = &P.m[size_t(l) * D]; const float* b = &cen[size_t(k - 1) * D];
                for (size_t t = 0; t < D; ++t) { double x = double(a[t]) - b[t]; d += x * x; } d2[size_t(l)] = std::min(d2[size_t(l)], d); sum += d2[size_t(l)] * P.w[size_t(l)]; }
            if (sum <= 0) { std::memcpy(&cen[size_t(k) * D], &cen[0], D * sizeof(float)); continue; }
            const double r = double(rng.next() % 1000003u) / 1000003.0 * sum; double acc = 0; int pick = L - 1;
            for (int l = 0; l < L; ++l) { acc += d2[size_t(l)] * P.w[size_t(l)]; if (acc >= r) { pick = l; break; } }
            std::memcpy(&cen[size_t(k) * D], &P.m[size_t(pick) * D], D * sizeof(float));
        }
    }
    as.assign(size_t(L), 0); double err = 0;
    std::vector<double> acc(size_t(K) * D, 0.0); std::vector<double> cw(size_t(K), 0.0); std::vector<double> dmin(size_t(L), 0.0);
    for (int it = 0; it < iters; ++it) {
        err = 0;
        for (int l = 0; l < L; ++l) {
            double bd = 1e300; int bk = 0; const float* a = &P.m[size_t(l) * D];
            for (int k = 0; k < K; ++k) { const float* b = &cen[size_t(k) * D]; double d = 0;
                for (size_t t = 0; t < D; ++t) { double x = double(a[t]) - b[t]; d += x * x; if (d >= bd) break; }
                if (d < bd) { bd = d; bk = k; } }
            as[size_t(l)] = bk; dmin[size_t(l)] = bd; err += bd * P.w[size_t(l)];
        }
        std::fill(acc.begin(), acc.end(), 0.0); std::fill(cw.begin(), cw.end(), 0.0);
        for (int l = 0; l < L; ++l) { const int k = as[size_t(l)]; cw[size_t(k)] += P.w[size_t(l)]; for (size_t t = 0; t < D; ++t) acc[size_t(k) * D + t] += P.w[size_t(l)] * P.m[size_t(l) * D + t]; }
        for (int k = 0; k < K; ++k) {
            if (cw[size_t(k)] > 0) for (size_t t = 0; t < D; ++t) cen[size_t(k) * D + t] = float(acc[size_t(k) * D + t] / cw[size_t(k)]);
            else { int worst = 0; double bw = -1; for (int l = 0; l < L; ++l) if (dmin[size_t(l)] * P.w[size_t(l)] > bw) { bw = dmin[size_t(l)] * P.w[size_t(l)]; worst = l; }     // cluster vacio: se re-siembra en el punto peor servido
                   std::memcpy(&cen[size_t(k) * D], &P.m[size_t(worst) * D], D * sizeof(float)); dmin[size_t(worst)] = 0; }
        }
    }
    // asignacion final y error con los centroides definitivos
    err = 0;
    for (int l = 0; l < L; ++l) {
        double bd = 1e300; int bk = 0; const float* a = &P.m[size_t(l) * D];
        for (int k = 0; k < K; ++k) { const float* b = &cen[size_t(k) * D]; double d = 0; for (size_t t = 0; t < D; ++t) { double x = double(a[t]) - b[t]; d += x * x; if (d >= bd) break; } if (d < bd) { bd = d; bk = k; } }
        as[size_t(l)] = bk; err += bd * P.w[size_t(l)];
    }
    return err;
}

// ------------------------------------------------------------------ metodo 26 ----
inline bool buildColorAnim(const Ctx& c, LsaAnim& A) {
    if (!c.opaqueAll) { A.note = "La animacion por colores necesita un GIF sin transparencia"; return false; }
    const int N = c.N, W = c.W, H = c.H; const size_t np = size_t(W) * H;
    // raster de analisis: las imagenes integrales de N frames cuestan 20 bytes por pixel y frame -> tope de 4 M pixeles-frame
    int aw = W, ah = H;
    if (int64_t(W) * H * N > 4000000LL) { const double f = std::sqrt(4000000.0 / (double(W) * H * N)); aw = std::max(8, int(W * f)); ah = std::max(8, int(H * f)); }
    std::vector<Image> src; const std::vector<Image>* use = &c.fr;
    if (aw != W || ah != H) { src.resize(size_t(N)); for (int i = 0; i < N; ++i) src[size_t(i)] = resizeArea(c.fr[size_t(i)], aw, ah); use = &src; }
    SttVolume vol; vol.build(*use, aw, ah);
    report(c.prog, 0.15f);

    const long total = c.total;
    const long B = std::max<long>(8, total - total / 40 - 2L * N - 8);             // objetos + Color triggers disponibles
    const int Kcap = int(std::clamp<long>(long(0.72 * double(B) / std::max(1, N)), 4, 256));
    SttTree tree; tree.build(vol, int(std::min<long>(B, long(aw) * ah)), c.prog);
    if (isCancelled(c.prog)) return false;
    report(c.prog, 0.55f);
    const int Lmax = tree.leafCount();
    const double nPix = double(aw) * ah;

    // hojas -> puntos (trayectoria de color de 3N valores y area)
    auto makePoints = [&](const std::vector<int>& leaves, WPoints& P) {
        P.L = int(leaves.size()); P.D = N * 3; P.m.assign(size_t(P.L) * size_t(P.D), 0.f); P.w.assign(size_t(P.L), 0.0);
        for (int l = 0; l < P.L; ++l) { const SttNode& n = tree.nd[size_t(leaves[size_t(l)])]; vol.mean(n.x0, n.y0, n.x1, n.y1, &P.m[size_t(l) * size_t(P.D)]); P.w[size_t(l)] = double(n.x1 - n.x0) * (n.y1 - n.y0); }
    };

    // eleccion de (hojas S, canales K) con un modelo de coste. Error total = error del BSP con S hojas (acumulado de las ganancias de cada corte)
    // + error de fusionar las trayectorias en K canales (medido sobre un conjunto de hojas de referencia). Coste = S + K*N (peor caso: todos los canales
    // cambian en todos los frames). Se elige el plan MAS BARATO cuyo PSNR este a menos de `tol` dB del mejor posible con el presupuesto: no se gasta
    // un objeto que no mejore de verdad la imagen.
    std::vector<double> cumErr(size_t(Lmax) + 1, 0.0);                                  // cumErr[s] = error del BSP tras s particiones
    {
        std::vector<double> g(size_t(tree.splits), 0.0);
        for (const SttNode& n : tree.nd) if (n.splitTime >= 0) g[size_t(n.splitTime)] = n.err - (tree.nd[size_t(n.c0)].err + tree.nd[size_t(n.c1)].err);
        cumErr[0] = tree.nd[0].err; for (int t = 0; t < tree.splits; ++t) cumErr[size_t(t) + 1] = std::max(0.0, cumErr[size_t(t)] - g[size_t(t)]);
    }
    auto psnrOf = [&](double sse) { const double mse = sse / (3.0 * nPix * N); return mse < 1e-9 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse); };
    const int iters = c.quality >= 3 ? 8 : c.quality == 2 ? 6 : 5;
    const int Kmin = 1;
    // error de fusionar las hojas de un plan (S hojas) en K canales, medido sobre una MUESTRA sistematica de esas hojas (con pesos reescalados)
    const int Msub = c.quality >= 3 ? 3000 : c.quality == 2 ? 2200 : 1500;
    auto estimateMerge = [&](int S, int K) {
        const std::vector<int> lv = tree.leavesAt(S - 1); const int L = int(lv.size()); const int stride = std::max(1, (L + Msub - 1) / Msub);
        WPoints Pr; Pr.D = N * 3; double wAll = 0, wSub = 0;
        for (int l = 0; l < L; ++l) { const SttNode& n = tree.nd[size_t(lv[size_t(l)])]; wAll += double(n.x1 - n.x0) * (n.y1 - n.y0); }
        for (int l = 0; l < L; l += stride) {
            const SttNode& n = tree.nd[size_t(lv[size_t(l)])]; const size_t o = Pr.m.size(); Pr.m.resize(o + size_t(Pr.D));
            vol.mean(n.x0, n.y0, n.x1, n.y1, &Pr.m[o]); Pr.w.push_back(double(n.x1 - n.x0) * (n.y1 - n.y0)); wSub += Pr.w.back();
        }
        Pr.L = int(Pr.w.size()); const double sc = wSub > 0 ? wAll / wSub : 1.0; for (double& w : Pr.w) w *= sc;
        std::vector<float> cen0; std::vector<int> as0; return wkmeans(Pr, K, c.seed + uint32_t(K) * 7u, std::max(3, iters - 1), cen0, as0, false);
    };
    struct Plan { int K = 0, S = 0; double cost = 0, psnr = 0; };
    std::vector<Plan> plans; double bestPsnr = -1;
    std::set<std::pair<int, int>> seen;
    const int Kcap2 = std::max(Kmin, Kcap);
    for (double cost = double(B); cost >= 60.0; cost /= 1.6) {
        if (isCancelled(c.prog)) return false;
        for (double share : {0.1, 0.22, 0.4, 0.6}) {
            const int K = std::clamp(int(share * cost / N + 0.5), 1, Kcap2);
            const int S = int(std::clamp<double>(cost - double(K) * N, 4.0, double(Lmax)));
            if (!seen.insert({K, S}).second) continue;
            Plan p; p.K = K; p.S = S; p.cost = double(S) + double(K) * N;
            if (p.cost > double(B) + 1) continue;
            const double em = estimateMerge(S, K);
            p.psnr = psnrOf(cumErr[size_t(S) - 1] + em); plans.push_back(p); bestPsnr = std::max(bestPsnr, p.psnr);
        }
    }
    report(c.prog, 0.7f);
    if (plans.empty()) { A.note = "No se pudo calcular el metodo"; return false; }
    const double tol = c.quality >= 3 ? 0.4 : c.quality == 2 ? 0.7 : 1.0;
    const double target = std::min(bestPsnr, 42.0) - tol;
    Plan pick = plans.front(); double bc = 1e300;
    for (const Plan& p : plans) if (p.psnr >= target && p.cost < bc) { bc = p.cost; pick = p; }

    const int K = pick.K;
    std::vector<float> cen; std::vector<int> as; std::vector<int> leaves; WPoints P;
    leaves = tree.leavesAt(pick.S - 1); makePoints(leaves, P);
    wkmeans(P, K, c.seed + uint32_t(K), iters + 3, cen, as, false);
    // colores por frame y canal = media ponderada de las hojas del canal
    auto channelColors = [&](std::vector<std::vector<std::array<uint8_t, 3>>>& out) {
        const size_t D = size_t(P.D); std::vector<double> acc(size_t(K) * D, 0.0); std::vector<double> cw(size_t(K), 0.0);
        for (int l = 0; l < P.L; ++l) { const int k = as[size_t(l)]; cw[size_t(k)] += P.w[size_t(l)]; for (size_t t = 0; t < D; ++t) acc[size_t(k) * D + t] += P.w[size_t(l)] * P.m[size_t(l) * D + t]; }
        out.assign(size_t(N), std::vector<std::array<uint8_t, 3>>(size_t(K), {0, 0, 0}));
        for (int k = 0; k < K; ++k) for (int f = 0; f < N; ++f) for (int t = 0; t < 3; ++t)
            out[size_t(f)][size_t(k)][size_t(t)] = cw[size_t(k)] > 0 ? uint8_t(std::clamp(acc[size_t(k) * D + size_t(f) * 3 + size_t(t)] / cw[size_t(k)] + 0.5, 0.0, 255.0)) : 0;
    };
    // fusion de hojas hermanas con el mismo canal (menos objetos)
    std::vector<int> chOfNode(tree.nd.size(), -1);
    {
        std::map<int, int> leafIdx; for (int l = 0; l < P.L; ++l) leafIdx[leaves[size_t(l)]] = l;
        for (auto& kv : leafIdx) chOfNode[size_t(kv.first)] = as[size_t(kv.second)];
    }
    std::vector<int> out;                                                                // nodos finales (hojas fusionadas)
    {
        std::vector<char> isLeafNow(tree.nd.size(), 0); for (int i : leaves) isLeafNow[size_t(i)] = 1;
        std::function<int(int)> uni = [&](int i) -> int {                               // canal comun del subarbol (o -1 si hay mas de uno)
            if (isLeafNow[size_t(i)]) return chOfNode[size_t(i)];
            const SttNode& n = tree.nd[size_t(i)]; if (n.c0 < 0) return chOfNode[size_t(i)];
            const int a = uni(n.c0), b = uni(n.c1); return (a >= 0 && a == b) ? a : -1;
        };
        std::function<void(int)> emit = [&](int i) {
            if (isLeafNow[size_t(i)] || tree.nd[size_t(i)].c0 < 0) { out.push_back(i); return; }
            const int u = uni(i); if (u >= 0) { chOfNode[size_t(i)] = u; out.push_back(i); return; }
            emit(tree.nd[size_t(i)].c0); emit(tree.nd[size_t(i)].c1);
        };
        emit(0);
    }
    // forma final: rectangulos del raster de analisis escalados al raster de trabajo
    A.colorChannels = K; A.firstChannel = std::clamp(c.lo.firstChannel, 2, 999 - K); A.vis.assign(size_t(N), {});
    channelColors(A.colorAt);
    const float sx = float(W) / float(aw), sy = float(H) / float(ah);
    LsaLayer L; L.depth = 1; L.always = true;
    for (int i : out) {
        const SttNode& n = tree.nd[size_t(i)]; const int ch = chOfNode[size_t(i)] < 0 ? 0 : chOfNode[size_t(i)];
        Shape s; s.cx = 0.5f * float(n.x0 + n.x1) * sx; s.cy = 0.5f * float(n.y0 + n.y1) * sy; s.w = float(n.x1 - n.x0) * sx; s.h = float(n.y1 - n.y0) * sy; s.rot = 0;
        s.r = uint8_t(ch & 255); s.g = uint8_t(ch >> 8); s.b = 0; s.z = 0; L.shapes.push_back(s);
    }
    A.layers.push_back(std::move(L));
    (void)np;
    A.note = "canales " + std::to_string(K) + ", rectangulos " + std::to_string(A.layers[0].shapes.size()) + " (teselado espacio-temporal)";
    return !A.layers[0].shapes.empty();
}

} // namespace lsa
} // namespace gdx
