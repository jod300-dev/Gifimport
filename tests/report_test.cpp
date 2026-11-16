// Prueba del informe de diagnostico: genera los archivos de un render y los escribe en una carpeta.
//   g++ -O2 -std=c++20 -pthread -Iinclude -Isrc/engine -Itests tests/report_test.cpp -o report_test && ./report_test gif metodo(1..29) carpeta [obj/frame] [frames] [canales]
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "chanframes.hpp"
#include "deltatree.hpp"
#include "diag.hpp"
#include <fstream>
#include <iostream>
using namespace gdx;
static void wr(const std::string& p, const std::string& s) { std::ofstream(p, std::ios::binary) << s; }
int main(int argc, char** argv) {
    if (argc < 4) { std::puts("uso: report_test gif metodo carpeta [obj/frame] [frames] [canales]"); return 2; }
    std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    GifData g = decodeGif(b.data(), b.size()); if (!g.ok()) { std::puts("DECODE-FAIL"); return 1; }
    const int m = atoi(argv[2]); const std::string dir = argv[3];
    const int per = argc > 4 ? atoi(argv[4]) : 300, nfr = argc > 5 ? atoi(argv[5]) : 12, ch = argc > 6 ? atoi(argv[6]) : 0;
    GifPlan plan = planFrames(g, nfr, 1.0, 0.03); const int N = plan.count();
    Options o; o.maxRaster = getenv("RASTER") ? atoi(getenv("RASTER")) : 256; o.quality = 1; o.budget = per; o.lightSpecialChannels = true; o.maxChannels = 0; o.colorChannels = false; o.mainChannels = false;
    GifExportParams p; p.baseGroup = 100; p.eo.longSide = 600; p.trigX = -30; std::string err, chain; std::vector<Result> frames; std::shared_ptr<LayeredAnim> A;
    if (m >= 18) {
        LayeredOptions lo; if (getenv("POSTER")) lo.posterize = atoi(getenv("POSTER")); if (getenv("EDGEW")) lo.edgeWeight = float(atof(getenv("EDGEW"))); if (getenv("SIZEK")) lo.sizeFloor = float(atof(getenv("SIZEK"))); lo.perFrame = per; if (getenv("EFFORT")) lo.effort = atoi(getenv("EFFORT")); lo.tree = m == 19; lo.maxTotal = 100000; lo.channels = ch ? 64 : 0; lo.firstChannel = 2; lo.method = m >= 20 ? m - 19 : 0;
        A = std::make_shared<LayeredAnim>(buildAnim(g, plan, o, lo, nullptr, 2)); if (!A->ok()) { std::puts("anim no ok"); return 1; }
        chain = buildAnimString(*A, plan, p, &err); for (int i = 0; i < N; ++i) frames.push_back(animFrameResult(*A, i));
    } else {
        auto res = convertFrames(g, plan, Method(m - 1), o, nullptr, 2); chain = buildGifString(res, plan, p, &err); frames = res;
    }
    if (chain.empty()) { std::printf("cadena vacia: %s\n", err.c_str()); return 1; }
    diag::Input in; in.chain = &chain; in.gif = &g; in.plan = &plan; in.frames = &frames; in.layered = A.get(); in.settingsText = "[ajustes]\nclave = valor\n"; in.methodName = "metodo " + std::to_string(m);
    in.modVersion = "test"; in.when = "ahora"; in.platformText = "linux"; in.calcSeconds = 1.5; in.editorRoom = 200000; in.threads = 2; in.baseGroup = p.baseGroup; in.longSide = 600; in.channels = ch != 0;
    diag::Output O = diag::build(in);
    system(("mkdir -p " + dir).c_str());
    wr(dir + "/informe.txt", O.informe); wr(dir + "/cadena.txt", chain); wr(dir + "/objetos.tsv", O.objetos); wr(dir + "/triggers.tsv", O.triggers);
    wr(dir + "/comparacion.png", std::string(O.comparacion.begin(), O.comparacion.end())); wr(dir + "/detalle.png", std::string(O.detalle.begin(), O.detalle.end()));
    std::printf("ok: informe %zu B, objetos %zu B, triggers %zu B, png %zu/%zu B\n", O.informe.size(), O.objetos.size(), O.triggers.size(), O.comparacion.size(), O.detalle.size());
    return 0;
}
