// Banco de pruebas: un GIF x un metodo -> una fila (objetos, triggers, PSNR limpio, % de fallos con triggers perdidos, tiempos).
// g++ -O2 -std=c++20 -pthread -Iinclude -Isrc/engine -Itests tests/corpus_test.cpp -o corpus_test
// ./corpus_test <gif> <metodo 0..N> <objetos/frame> <maxFrames> [semillas]      (metodo: 0-16 planos, 17 capas, 18 arbol, 19+ nuevos)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "dispatch.hpp"
#include "gdsim.hpp"
#include <fstream>
#include <iostream>
using namespace gdx;
int main(int argc, char** argv) {
    if (argc < 5) { std::puts("uso: corpus_test gif metodo objetos/frame maxFrames [semillas]"); return 2; }
    std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    GifData g = decodeGif(b.data(), b.size());
    const char* nm = std::strrchr(argv[1], '/'); nm = nm ? nm + 1 : argv[1];
    const int mi = atoi(argv[2]), budget = atoi(argv[3]), nfr = atoi(argv[4]); const int seeds = argc > 5 ? atoi(argv[5]) : 6;
    if (g.frames.empty()) { std::printf("%-18s m%-2d DECODE-FAIL\n", nm, mi + 1); return 1; }
    GifPlan plan = planFrames(g, nfr, 1.0, 0.03); const int N = plan.count();
    GifExportParams p; p.baseGroup = 100; p.eo.longSide = 600; p.trigX = -30;
    BuiltAnim ba; double t0 = nowMs();
    try { ba = buildForTest(g, plan, mi, budget, p, 256); } catch (const std::exception& e) { std::printf("%-18s m%-2d EXCEPCION %s\n", nm, mi + 1, e.what()); return 1; }
    double buildMs = nowMs() - t0;
    if (ba.chain.empty()) { std::printf("%-18s m%-2d N=%-3d FALLO: %s\n", nm, mi + 1, N, ba.err.c_str()); return 1; }
    const double ppu = 0.18; double asp = double(g.w) / g.h; double sw = asp >= 1 ? 600 : 600 * asp, sh = asp >= 1 ? 600 / asp : 600;
    const int W = std::max(8, int(sw * ppu)), H = std::max(8, int(sh * ppu));
    std::vector<Image> tgt; for (int i = 0; i < N; ++i) tgt.push_back(resizeArea(g.frames[size_t(plan.idx[size_t(i)])], W, H));
    auto coverage = [&](const Image& r) { size_t c = 0; for (size_t k = 0; k < size_t(r.w) * r.h; ++k) if (r.rgba[k * 4 + 3] > 0) ++c; return double(c) / double(r.w * r.h); };
    // limpio
    double sum = 0, mn = 1e9; int worst = 0;
    { sim::Opt so; sim::Sim S; S.init(ba.chain); std::mt19937 rng(1); double t = 0;
      for (int rep = 0; rep < 2; ++rep) for (int i = 0; i < N; ++i) {
          S.run(t + plan.delay[size_t(i)] * 0.7, so, rng); Image r = S.render(0, 0, ppu, W, H, so, rng);
          if (rep == 1) { double ps = computePsnr(tgt[size_t(i)], r, 1); sum += ps; if (ps < mn) { mn = ps; worst = i; } }
          t += plan.delay[size_t(i)]; } }
    const double avg = sum / N;
    // con fallos: se cuenta una muestra como fallo si su PSNR cae > 4 dB respecto a la corrida limpia del MISMO frame
    std::vector<double> ref(size_t(N), 0);
    { sim::Opt so; sim::Sim S; S.init(ba.chain); std::mt19937 rng(1); double t = 0;
      for (int rep = 0; rep < 2; ++rep) for (int i = 0; i < N; ++i) { S.run(t + plan.delay[size_t(i)] * 0.7, so, rng); Image r = S.render(0, 0, ppu, W, H, so, rng); if (rep == 1) ref[size_t(i)] = computePsnr(tgt[size_t(i)], r, 1); t += plan.delay[size_t(i)]; } }
    int bad = 0, tot = 0;
    for (int sd = 1; sd <= seeds; ++sd) {
        sim::Opt so; so.seed = unsigned(sd); so.tick = 1.0 / 30; so.reverseTick = true; so.dropAlpha = 0.03; so.dropSpawn = 0.03;
        sim::Sim S; S.init(ba.chain); std::mt19937 rng(unsigned(sd) * 7919u); double t = 0;
        for (int rep = 0; rep < 3; ++rep) for (int i = 0; i < N; ++i) {
            S.run(t + plan.delay[size_t(i)] * 0.7, so, rng); Image r = S.render(0, 0, ppu, W, H, so, rng);
            if (rep >= 1) { ++tot; double ps = computePsnr(tgt[size_t(i)], r, 1); if (coverage(r) < 0.3 * coverage(tgt[size_t(i)]) || ps < ref[size_t(i)] - 4.0) ++bad; }
            t += plan.delay[size_t(i)]; }
    }
    const int lim = 200000;
    std::printf("%-18s m%-2d N=%-3d obj=%-6zu trig=%-5d grp=%-4d %5.0fms str=%5zuKB | PSNR avg %5.1f min %5.1f(f%d) | fallos %5.1f%%%s\n",
                nm, mi + 1, N, ba.objects, ba.triggers, ba.groups, buildMs, ba.chain.size() / 1024, avg, mn, worst, 100.0 * bad / std::max(1, tot),
                (ba.objects + size_t(ba.triggers) > size_t(lim)) ? "  [SUPERA 200k]" : "");
    if (getenv("NOTES") && !ba.note.empty()) std::printf("      nota: %s\n", ba.note.c_str());
    return 0;
}
