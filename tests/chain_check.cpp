// Reproduce una CADENA real (cadena.txt del informe de diagnostico) con el simulador y la compara con el GIF original.
//   ./chain_check original.gif cadena.txt origenX origenY ladoLargo [maxFrames]
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "chanframes.hpp"
#include "deltatree.hpp"
#include "diag.hpp"
#include "gdsim.hpp"
#include <fstream>
using namespace gdx;
int main(int argc, char** argv) {
    if (argc < 6) { std::puts("uso: chain_check gif cadena ox oy lado [frames]"); return 2; }
    std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {}); GifData g = decodeGif(b.data(), b.size());
    std::ifstream cf(argv[2], std::ios::binary); std::string chain((std::istreambuf_iterator<char>(cf)), {});
    const double ox = atof(argv[3]), oy = atof(argv[4]), side = atof(argv[5]); const int nfr = argc > 6 ? atoi(argv[6]) : 32;
    GifPlan plan = planFrames(g, nfr, 1.0, argc > 7 ? atof(argv[7]) : 0.07); const int N = plan.count();
    const double ppu = 360.0 / side; const int W = 360, H = int(std::lround(360.0 * g.h / g.w));
    sim::Opt so; sim::Sim S; S.init(chain); std::mt19937 rng(1); double t = 0, sum = 0, mn = 1e9; int cnt = 0;
    for (int i = 0; i < N; ++i) {
        S.run(t + plan.delay[size_t(i)] * 0.5, so, rng);
        Image r = S.render(ox, oy, ppu, W, H, so, rng); Image tg = resizeArea(g.frames[size_t(plan.idx[size_t(i)])], W, H);
        const double v = diag::psnrRgb(diag::flatten(tg), diag::flatten(r)); sum += v; mn = std::min(mn, v); ++cnt;
        std::printf("f%d %.1f  ", i, v); t += plan.delay[size_t(i)];
        if (i == N / 2 || i == 0) { auto fl = diag::flatten(r); (void)fl; }
    }
    std::printf("\nPSNR simulado de la cadena contra el GIF: media %.2f min %.2f\n", sum / cnt, mn);
}
