// Banco de velocidad/calidad de los metodos 18-29 (sin Geode). Imprime tiempo de calculo, objetos, triggers, PSNR y un HASH de la cadena de GD:
// si una optimizacion es "sin perdida de calidad" el hash NO cambia.
//   g++ -O2 -std=c++20 -pthread -Iinclude -Isrc/engine -Itests tests/bench_lsa.cpp -o bench_lsa
//   ./bench_lsa <gif> <metodo 18..29> [objetos/frame=600] [frames=36] [maxRaster=384] [calidad 0..3 =0] [canales(0/1)=0] [hilos=1] [duracion 0..3 =1]
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "chanframes.hpp"
#include "deltatree.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include "diag.hpp"
using namespace gdx;
int main(int argc, char** argv) {
    if (argc < 3) { std::puts("uso: bench_lsa gif metodo [obj/frame] [frames] [maxRaster] [calidad] [canales] [hilos] [duracion]"); return 2; }
    std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    GifData g = decodeGif(b.data(), b.size());
    if (g.frames.empty()) { std::puts("DECODE-FAIL"); return 1; }
    const int m = atoi(argv[2]);                                  // 18..29
    const int per = argc > 3 ? atoi(argv[3]) : 600, nfr = argc > 4 ? atoi(argv[4]) : 36, mr = argc > 5 ? atoi(argv[5]) : 384;
    const int q = argc > 6 ? atoi(argv[6]) : 0, ch = argc > 7 ? atoi(argv[7]) : 0, th = argc > 8 ? atoi(argv[8]) : 1;
    const int dur = argc > 9 ? atoi(argv[9]) : 1;
    GifPlan plan = planFrames(g, nfr, 1.0, 0.03); const int N = plan.count();
    Options o; o.maxRaster = mr; o.quality = q; o.budget = per; o.lightSpecialChannels = true; o.maxChannels = 0; o.colorChannels = false; o.mainChannels = false;
    LayeredOptions lo; lo.perFrame = per; lo.tree = m == 19; lo.maxTotal = 200000; lo.channels = ch ? 128 : 0; lo.firstChannel = 2; lo.method = m >= 20 ? m - 19 : 0;
#ifdef HAS_EFFORT
    lo.effort = dur;
#else
    (void)dur;
#endif
    if (getenv("EDGEW")) lo.edgeWeight = float(atof(getenv("EDGEW"))); if (getenv("SIZEK")) lo.sizeFloor = float(atof(getenv("SIZEK")));
    if (getenv("DMODE")) lo.denoiseMode = atoi(getenv("DMODE"));
    if (getenv("DENOISE")) lo.denoise = atoi(getenv("DENOISE")) != 0;
    auto t0 = std::chrono::steady_clock::now();
    LayeredAnim A = buildAnim(g, plan, o, lo, nullptr, th);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!A.ok()) { std::printf("%-12s m%d NO-OK: %s\n", argv[1], m, A.note.c_str()); return 1; }
    GifExportParams p; p.baseGroup = 100; p.eo.longSide = 600; p.trigX = -30; std::string err;
    std::string chain = buildAnimString(A, plan, p, &err);
    if (chain.empty()) { std::printf("m%d cadena vacia: %s\n", m, err.c_str()); return 1; }
    size_t objs = 0, colorTrig = 0; for (char c : chain) if (c == ';') ++objs;
    for (size_t pos = 0; (pos = chain.find("1,899,", pos)) != std::string::npos; ++pos) ++colorTrig;
    uint64_t h = 1469598103934665603ULL; for (unsigned char c : chain) { h ^= c; h *= 1099511628211ULL; }
    std::printf("m%-2d N=%-3d %6.0fms obj+trig=%-6zu trig=%-5d colorTrig=%-4zu PSNR avg %5.1f min %5.1f hash=%016llx | %s\n", m, N, ms, objs, animTriggerCount(A), colorTrig,
                A.psnrAvg, A.psnrMin, (unsigned long long)h, A.note.c_str());
    if (const char* cd = getenv("CLEAN")) {                       // PSNR frente a los frames LIMPIOS (sin dithering): lo que se ve de verdad
        double sum = 0, mn = 1e9; int cnt = 0;
        for (int fr = 0; fr < N; ++fr) {
            Result r = animFrameResult(A, fr); char nm[512]; std::snprintf(nm, sizeof nm, "%s/c_%03d.png", cd, plan.idx[size_t(fr)] + 1);
            std::ifstream pf(nm, std::ios::binary); std::vector<uint8_t> pb((std::istreambuf_iterator<char>(pf)), {}); if (pb.empty()) continue;
            int w, h, c; uint8_t* px = stbi_load_from_memory(pb.data(), int(pb.size()), &w, &h, &c, 4); if (!px) continue;
            Image cl; cl.w = w; cl.h = h; cl.rgba.assign(px, px + size_t(w) * h * 4); stbi_image_free(px);
            Image m = renderShapes(r.shapes, r.rasterW, r.rasterH); if (cl.w != m.w || cl.h != m.h) cl = resizeArea(cl, m.w, m.h);
            const double v = diag::psnrRgb(diag::flatten(cl), diag::flatten(m)); sum += v; mn = std::min(mn, v); ++cnt;
        }
        if (cnt) std::printf("  PSNR vs LIMPIO: avg %.2f min %.2f\n", sum / cnt, mn);
    }
    {   // huecos: pixeles del GIF (opaco) que el render deja SIN cubrir (se veria el fondo del nivel)
        double worst = 0, tot = 0; int wf = 0;
        for (int fr = 0; fr < N; ++fr) {
            Result r = animFrameResult(A, fr); Image m = renderShapes(r.shapes, r.rasterW, r.rasterH); size_t holes = 0;
            for (size_t i = 0; i < size_t(m.w) * m.h; ++i) if (m.rgba[i * 4 + 3] == 0) ++holes;
            const double pc = 100.0 * double(holes) / double(m.w * m.h); tot += pc; if (pc > worst) { worst = pc; wf = fr; }
        }
        std::printf("  huecos sin cubrir: media %.3f%%  peor %.3f%% (f%d)\n", tot / N, worst, wf);
    }
    if (getenv("PSNRS")) { std::printf("  psnr/frame:"); for (double v : A.psnr) std::printf(" %.1f", v); std::printf("\n"); }
    return 0;
}
