// Pruebas de los metodos 20-29 (LSA) sin Geode: invariantes de la cadena de GD + coherencia con el simulador.
//   g++ -O2 -std=c++20 -pthread -Iinclude -Isrc/engine -Itests tests/lsa_test.cpp -o lsa_test && ./lsa_test <gif> [objetos/frame] [maxFrames]
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "chain_checks.hpp"
#include "chanframes.hpp"
#include "deltatree.hpp"
#include "gdsim.hpp"
#include <fstream>
#include <iostream>
int main(int argc, char** argv) {
    std::ifstream f(argc > 1 ? argv[1] : "tests/test.gif", std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    GifData g = decodeGif(bytes.data(), bytes.size());
    if (!g.ok()) { std::cout << "FAIL decode\n"; return 1; }
    const int per = argc > 2 ? atoi(argv[2]) : 200, mf = argc > 3 ? atoi(argv[3]) : 40;
    GifPlan plan = planFrames(g, mf, 1.0, 0.03); const int N = plan.count();
    int bad = 0, checks = 0; auto fail = [&](int k, const std::string& m) { std::cout << "  FALLO metodo " << (19 + k) << ": " << m << "\n"; ++bad; };
    for (int k = 1; k <= 10; ++k) {
        Options o; o.maxRaster = 192; o.quality = 1; o.budget = per;
        LayeredOptions lo; lo.perFrame = per; lo.method = k; lo.maxTotal = per * N + 400;
        LayeredAnim A = buildAnim(g, plan, o, lo, nullptr, 2);
        if (!A.ok()) { std::cout << "  (metodo " << 19 + k << " no aplicable: " << A.note << ")\n"; continue; }
        ++checks;
        GifExportParams p; p.baseGroup = 10; p.eo.longSide = 300; p.eo.originX = 1000; p.eo.originY = 500; std::string err;
        std::string s = buildAnimString(A, plan, p, &err);
        if (s.empty()) { fail(k, "cadena vacia: " + err); continue; }
        std::string s2 = buildAnimString(A, plan, p, &err);
        if (s != s2) fail(k, "la cadena no es determinista");
        if (int ov = triggerOverlaps(s)) fail(k, std::to_string(ov) + " pares de triggers se pisan");
        if (!triggersBeforeObjects(s)) fail(k, "hay triggers DESPUES de los objetos");
        size_t objs = 0, trig = 0; int maxGroup = 0, minGroup = 1 << 30;
        for (auto& kv : parse(s)) {
            if (!kv.count(1)) { fail(k, "objeto sin id"); continue; }
            int id = std::stoi(kv.at(1));
            if (id == 3092) {
                ++objs;
                if (!kv.count(25) || std::stoi(kv.at(25)) < 1) fail(k, "z-order < 1 o ausente");
                if (!kv.count(128) || !kv.count(129) || std::stod(kv.at(128)) <= 0 || std::stod(kv.at(129)) <= 0) fail(k, "escala no positiva");
                if (kv.count(21) && std::stoi(kv.at(21)) < 1) fail(k, "canal < 1");
            } else ++trig;
            for (const char* key : {"51", "57"}) { (void)key; }
            if (kv.count(51)) { int t = std::stoi(kv.at(51)); maxGroup = std::max(maxGroup, t); minGroup = std::min(minGroup, t); }
        }
        if (objs + trig > size_t(lo.maxTotal)) fail(k, "supera el tope (" + std::to_string(objs + trig) + " > " + std::to_string(lo.maxTotal) + ")");
        if (int(trig) != animTriggerCount(A)) fail(k, "animTriggerCount=" + std::to_string(animTriggerCount(A)) + " pero la cadena tiene " + std::to_string(trig) + " (cota)");
        if (maxGroup > 9999) fail(k, "grupo > 9999");
        for (int fi = 0; fi < N; ++fi) if (animFrameResult(A, fi).shapes.empty() && A.psnrAvg > 0) { fail(k, "frame " + std::to_string(fi) + " vacio"); break; }
        // coherencia con el simulador de GD (triggers perfectos): el PSNR medido en el juego simulado debe coincidir con el calculado
        {
            double asp = double(g.w) / g.h; double sw = asp >= 1 ? 300 : 300 * asp; const int W = A.W, H = A.H; const double ppu = double(W) / sw;   // mismo raster que el calculo: PSNR comparable
            sim::Opt so; sim::Sim S; S.init(s); std::mt19937 rng(1); double t = 0, sum = 0; int cnt = 0;
            for (int rep = 0; rep < 2; ++rep) for (int i = 0; i < N; ++i) {
                S.run(t + plan.delay[size_t(i)] * 0.7, so, rng);
                if (rep == 1) { Image r = S.render(1000, 500, ppu, W, H, so, rng); Image tg = resizeArea(g.frames[size_t(plan.idx[size_t(i)])], W, H); sum += computePsnr(tg, r, 128); ++cnt; }
                t += plan.delay[size_t(i)];
            }
            const double simAvg = sum / cnt;
            if (A.lsa && A.lsa->colorChannels == 0 && std::abs(std::min(60.0, simAvg) - std::min(60.0, A.psnrAvg)) > 2.5) fail(k, "PSNR simulado " + std::to_string(simAvg) + " dB muy distinto del calculado " + std::to_string(A.psnrAvg));
            std::printf("  metodo %d: obj=%zu trig=%zu capas=%zu PSNR calc %.1f / sim %.1f  (%s)\n", 19 + k, objs, trig, A.lsa->layers.size(), A.psnrAvg, simAvg, A.note.c_str());
        }
    }
    std::cout << checks << " metodos comprobados: " << (bad ? std::to_string(bad) + " FALLOS\n" : std::string("TODO OK\n"));
    return bad ? 1 : 0;
}
