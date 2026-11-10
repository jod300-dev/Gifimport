// Simulador de GD (modelo) de punta a punta: genera la cadena de un metodo (1-19), la "ejecuta" (HSV/canales, Alpha/Spawn, z-order con empates al azar) y mide el PSNR contra el GIF.
// g++ -O2 -std=c++20 -pthread -Iinclude -Isrc/engine -Itests tests/fullsim_test.cpp -o fullsim && ./fullsim tests/test.gif <metodo 0..18> <objetos/frame> <frames> [canales 0|1] [tope total]
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
    int methodIdx = atoi(argv[2]);             // 0..16 normales (1-17), 17 = capas (18), 18 = arbol (19)
    int budget = atoi(argv[3]), nfr = atoi(argv[4]), chan = argc > 5 ? atoi(argv[5]) : 0;
    int maxTotal = argc > 6 ? atoi(argv[6]) : 40000;
    GifPlan plan = planFrames(g, nfr, 1.0, 0.03);
    const int N = plan.count();
    Options o; o.maxRaster = 384; o.quality = 0; o.lightSpecialChannels = true; o.maxChannels = 0; o.colorChannels = false; o.mainChannels = false;
    GifExportParams p; p.baseGroup = 100; p.eo.longSide = 600; p.trigX = -30;
    std::string chain, err; double previewPsnr = 0; size_t nobj = 0, ntrig = 0;
    double t0 = nowMs();
    if (methodIdx >= 17) {
        LayeredOptions lo; lo.perFrame = budget; lo.tree = methodIdx == 18; lo.maxTotal = maxTotal; lo.channels = chan ? 128 : 0; lo.firstChannel = 2;
        o.budget = budget;
        LayeredAnim A = buildAnim(g, plan, o, lo, nullptr, 4);
        if (!A.ok()) { std::cout << "FAIL: capas no ok " << A.note << "\n"; return 1; }
        chain = buildAnimString(A, plan, p, &err); previewPsnr = A.psnrAvg; nobj = A.objectCount(); ntrig = animTriggerCount(A);
    } else {
        o.budget = gifPerFrameBudget(budget, N, maxTotal);
        auto res = convertFrames(g, plan, Method(methodIdx), o, nullptr, 4);
        std::vector<std::array<uint8_t,3>> pal;
        if (chan) pal = quantizeFrames(res, 128);
        for (auto& r : res) { nobj += r.shapes.size(); previewPsnr += r.psnr / N; }
        chain = chan ? buildGifStringCh(res, plan, p, pal, &err) : buildGifString(res, plan, p, &err);
        ntrig = gifTriggerCount(N);
    }
    double tms = nowMs() - t0;
    if (chain.empty()) { std::cout << "FAIL build: " << err << "\n"; return 1; }
    const double ppu = 0.6;
    GdSize sz = gdImageSize(Result{}, p.eo); (void)sz;
    // tamano de imagen: aspecto del gif
    double asp = double(g.w) / g.h; double sw = asp >= 1 ? 600 : 600 * asp, sh = asp >= 1 ? 600 / asp : 600;
    int W = int(sw * ppu), H = int(sh * ppu);
    auto run = [&](const sim::Opt& so, double& avg, double& mn, int& worst) {
        sim::Sim S; S.init(chain); std::mt19937 rng(so.seed);
        avg = 0; mn = 1e9; worst = -1; int cnt = 0;
        double t = 0;
        for (int rep = 0; rep < 2; ++rep) {
            t = rep * plan.total;
            for (int i = 0; i < N; ++i) {
                S.run(t + 1e-4, so, rng);
                Image r = S.render(0, 0, ppu, W, H, so, rng);
                Image tgt = resizeArea(g.frames[size_t(plan.idx[size_t(i)])], W, H);
                double ps = computePsnr(tgt, r, 1);
                if (rep == 1) { avg += ps; ++cnt; if (ps < mn) { mn = ps; worst = i; } }
                if (rep == 1 && i == (argc > 7 ? atoi(argv[7]) : -1)) {
                    std::ofstream o2("/tmp/sim_frame.ppm", std::ios::binary); o2 << "P6\n" << r.w << " " << r.h << "\n255\n";
                    for (size_t k = 0; k < size_t(r.w) * r.h; ++k) { bool a = r.rgba[k*4+3] > 0; o2.put(char(a ? r.rgba[k*4] : 255)); o2.put(char(a ? r.rgba[k*4+1] : 0)); o2.put(char(a ? r.rgba[k*4+2] : 255)); }
                }
                t += plan.delay[size_t(i)];
            }
        }
        avg /= std::max(1, cnt);
    };
    double a1, m1, a2, m2, a3, m3; int w1, w2, w3;
    sim::Opt o1; run(o1, a1, m1, w1);
    sim::Opt o2; o2.randomTies = true; o2.seed = 7; run(o2, a2, m2, w2);
    sim::Opt o3; o3.randomTies = true; o3.shuffleSameTick = true; o3.seed = 11; run(o3, a3, m3, w3);
    std::printf("m%-2d N=%-3d obj=%-7zu trig=%-5zu str=%zuKB build=%.0fms | preview %.1f dB | GDsim avg %.1f min %.1f(f%d) | randTies avg %.1f min %.1f | +shuffleTick min %.1f\n",
        methodIdx + 1, N, nobj, ntrig, chain.size() / 1024, tms, previewPsnr, a1, m1, w1, a2, m2, m3);
}
