// Robustez de los triggers frente a fallos de GD (modelo hostil): lag con eventos en orden inverso, triggers perdidos.
// Mide, en el punto medio de cada frame de 3 vueltas, si lo que se ve es un frame LIMPIO (se parece a algun frame del GIF) o un fallo:
//   vacio = casi nada visible;   mezcla = se ve algo que no se parece a ningun frame (frames encimados / restos);  (un frame tardio por lag NO cuenta)
// g++ -O2 -std=c++20 -pthread -Iinclude -Isrc/engine -Itests tests/robust_test.cpp -o robust && ./robust tests/test.gif <metodo 0..18> <objetos/frame> <frames> [semillas]
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "chanframes.hpp"
#include "deltatree.hpp"
#include "gdsim.hpp"
#include <fstream>
#include <iostream>
using namespace gdx;
int main(int argc, char** argv) {
    std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    GifData g = decodeGif(b.data(), b.size());
    int mi = atoi(argv[2]), budget = atoi(argv[3]), nfr = atoi(argv[4]); int seeds = argc > 5 ? atoi(argv[5]) : 20;
    GifPlan plan = planFrames(g, nfr, 1.0, 0.03); const int N = plan.count();
    Options o; o.maxRaster = 256; o.quality = 0; o.lightSpecialChannels = true; o.maxChannels = 0; o.colorChannels = false; o.mainChannels = false; o.budget = budget;
    GifExportParams p; p.baseGroup = 100; p.eo.longSide = 600; p.trigX = -30; std::string chain, err;
    if (mi >= 17) { LayeredOptions lo; lo.perFrame = budget; lo.tree = mi == 18; lo.maxTotal = 60000; lo.channels = 0; lo.firstChannel = 2; LayeredAnim A = buildAnim(g, plan, o, lo, nullptr, 4); chain = buildAnimString(A, plan, p, &err); }
    else { auto res = convertFrames(g, plan, Method(mi), o, nullptr, 4); chain = buildGifString(res, plan, p, &err); }
    if (chain.empty()) { std::cout << "FAIL: " << err << "\n"; return 1; }
    const double ppu = 0.18; double asp = double(g.w) / g.h; double sw = asp >= 1 ? 600 : 600 * asp, sh = asp >= 1 ? 600 / asp : 600;
    const int W = int(sw * ppu), H = int(sh * ppu);
    std::vector<Image> tgt; for (int i = 0; i < N; ++i) tgt.push_back(resizeArea(g.frames[size_t(plan.idx[size_t(i)])], W, H));
    auto bestPsnr = [&](const Image& r, int& arg) { double bp = -1; arg = -1; for (int j = 0; j < N; ++j) { double ps = computePsnr(tgt[size_t(j)], r, 1); if (ps > bp) { bp = ps; arg = j; } } return bp; };
    auto coverage = [&](const Image& r) { size_t c = 0; for (size_t k = 0; k < size_t(r.w) * r.h; ++k) if (r.rgba[k * 4 + 3] > 0) ++c; return double(c) / double(r.w * r.h); };
    // referencia sin fallos
    double refBest = 0; {
        sim::Opt so; sim::Sim S; S.init(chain); std::mt19937 rng(1); double t = 0; int cnt = 0;
        for (int rep = 0; rep < 2; ++rep) for (int i = 0; i < N; ++i) {
            S.run(t + plan.delay[size_t(i)] * 0.7, so, rng); Image r = S.render(0, 0, ppu, W, H, so, rng); int a; double bp = bestPsnr(r, a);
            if (rep == 1) { refBest += bp; ++cnt; } t += plan.delay[size_t(i)];
        }
        refBest /= cnt;
    }
    struct Model { const char* name; sim::Opt o; };
    std::vector<Model> models;
    { sim::Opt x; x.tick = 1.0 / 60; x.reverseTick = true; models.push_back({"lag 17ms, orden inverso", x}); }
    { sim::Opt x; x.tick = 1.0 / 30; x.reverseTick = true; models.push_back({"lag 33ms, orden inverso", x}); }
    { sim::Opt x; x.tick = 1.0 / 15; x.reverseTick = true; models.push_back({"lag 67ms, orden inverso", x}); }
    { sim::Opt x; x.dropAlpha = 0.03; models.push_back({"3% de Alpha perdidos   ", x}); }
    { sim::Opt x; x.dropSpawn = 0.03; models.push_back({"3% de Spawn perdidos   ", x}); }
    { sim::Opt x; x.tick = 1.0 / 30; x.reverseTick = true; x.dropAlpha = 0.02; x.dropSpawn = 0.02; models.push_back({"lag 33ms + 2% perdidos  ", x}); }
    std::printf("m%-2d N=%d (PSNR limpio %.1f dB). Muestras con FALLO (vacio / mezcla) por modelo:\n", mi + 1, N, refBest);
    for (auto& m : models) {
        int bad = 0, empty = 0, mixed = 0, tot = 0;
        for (int sd = 1; sd <= seeds; ++sd) {
            sim::Opt so = m.o; so.seed = unsigned(sd); sim::Sim S; S.init(chain); std::mt19937 rng(unsigned(sd) * 7919u); double t = 0;
            for (int rep = 0; rep < 3; ++rep) for (int i = 0; i < N; ++i) {
                S.run(t + plan.delay[size_t(i)] * 0.7, so, rng);
                Image r = S.render(0, 0, ppu, W, H, so, rng);
                if (rep >= 1) {
                    ++tot; int a; double bp;
                    if (coverage(r) < 0.5) { ++empty; ++bad; }
                    else { bp = bestPsnr(r, a); if (bp < refBest - 3.0) { ++mixed; ++bad; } }
                }
                t += plan.delay[size_t(i)];
            }
        }
        std::printf("   %s : %5.1f %%  (vacio %d, mezcla %d de %d)\n", m.name, 100.0 * bad / tot, empty, mixed, tot);
    }
}
