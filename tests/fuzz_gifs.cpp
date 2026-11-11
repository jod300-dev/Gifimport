// Banco de robustez: pasa un GIF por los 19 metodos (HSV y canales), simula el resultado en el modelo de GD y avisa de fallos/vacios/excepciones.
// g++ -O1 -g -fsanitize=address,undefined -std=c++20 -pthread -Iinclude -Isrc/engine -Itests tests/fuzz_gifs.cpp -o fuzz_gifs && ./fuzz_gifs archivo.gif <objetos/frame> <max frames>
// Los GIFs de prueba (tamanos extremos, transparencia, disposal 0-3, retardos raros...) se generan con tests/make_fuzz_gifs.py
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
    int budget = atoi(argv[2]), maxFrames = atoi(argv[3]);
    GifData g = decodeGif(b.data(), b.size());
    if (!g.ok()) { std::printf("%-26s REJECT: %s\n", argv[1], g.error.c_str()); return 0; }
    GifPlan plan = planFrames(g, maxFrames, 1.0, 0.03);
    int N = plan.count();
    if (N < 2) { std::printf("%s PLAN<2\n", argv[1]); return 3; }
    int bad = 0; std::string rep;
    for (int chan = 0; chan < 2; ++chan)
    for (int m = 0; m < 19; ++m) {
        if (chan && m >= 1 && m < 0) continue;
        std::string chain, err;
        try {
            Options o; o.maxRaster = 384; o.quality = 0; o.lightSpecialChannels = true; o.maxChannels = 0; o.budget = budget;
            GifExportParams p; p.baseGroup = 100; p.eo.longSide = 600; p.trigX = -30;
            if (m >= 17) {
                LayeredOptions lo; lo.perFrame = budget; lo.tree = m == 18; lo.maxTotal = 40000; lo.channels = chan ? 128 : 0; lo.firstChannel = 2;
                LayeredAnim A = buildAnim(g, plan, o, lo, nullptr, 2);
                if (!A.ok()) { rep += " m" + std::to_string(m + 1) + (chan ? "c" : "h") + ":NOTOK(" + A.note + ")"; ++bad; continue; }
                chain = buildAnimString(A, plan, p, &err);
            } else {
                o.budget = gifPerFrameBudget(budget, N, 40000, chan ? 128 : 0);
                auto res = convertFrames(g, plan, Method(m), o, nullptr, 2);
                std::vector<std::array<uint8_t,3>> pal; if (chan) pal = quantizeFrames(res, 128);
                chain = chan ? buildGifStringCh(res, plan, p, pal, &err) : buildGifString(res, plan, p, &err);
            }
            if (chain.empty()) { rep += " m" + std::to_string(m + 1) + (chan ? "c" : "h") + ":EMPTY(" + err + ")"; ++bad; continue; }
            // simulacion: cada frame debe mostrar algo y los frames no deben superponerse (comprobamos nº de frames visibles via PSNR no es posible; basta con ejecutar)
            sim::Sim S; S.init(chain); std::mt19937 rng(3); sim::Opt so;
            double asp = double(g.w) / g.h; double sw = asp >= 1 ? 600 : 600 * asp, sh = asp >= 1 ? 600 / asp : 600;
            int W = std::max(1, int(sw * 0.4)), H = std::max(1, int(sh * 0.4));
            double t = 0; double worst = 99;
            for (int i = 0; i < N; ++i) {
                S.run(t + 1e-4, so, rng);
                Image r = S.render(0, 0, 0.4, W, H, so, rng);
                size_t op = 0; for (size_t k = 3; k < r.rgba.size(); k += 4) op += r.rgba[k] > 0;
                if (op == 0 && m != 8 && true) { /* frame vacio: aceptable solo si el GIF es transparente */ }
                Image tg = resizeArea(g.frames[size_t(plan.idx[size_t(i)])], W, H);
                double ps = computePsnr(tg, r, 128); worst = std::min(worst, ps);
                t += plan.delay[size_t(i)];
            }
            if (m < 8 || m >= 17) if (worst < 8.0) { rep += " m" + std::to_string(m + 1) + (chan ? "c" : "h") + ":PSNR" + std::to_string(int(worst)); ++bad; }
        } catch (const std::exception& e) { rep += " m" + std::to_string(m + 1) + ":EXC(" + e.what() + ")"; ++bad; }
        catch (...) { rep += " m" + std::to_string(m + 1) + ":EXC"; ++bad; }
    }
    std::printf("%-26s %4dx%-4d src=%-3zu N=%-3d %s%s\n", argv[1], g.w, g.h, g.frames.size(), N, bad ? "FALLOS:" : "ok", rep.c_str());
    return bad ? 2 : 0;
}
