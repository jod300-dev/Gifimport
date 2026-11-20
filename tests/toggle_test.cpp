// Ajuste "Usar Toggle triggers en vez de Alpha" (v0.5.11): el modo Toggle NO debe cambiar lo que se ve, solo el tipo de trigger.
// Para cada metodo representativo (planos, 18 capas, 19 arbol y 20-29 capas LSA) construye la cadena de GD dos veces (Alpha y Toggle) y comprueba:
//   1. la cadena Toggle no tiene ningun Alpha (1007) y la Alpha no tiene ningun Toggle (1049); mismo numero de triggers;
//   2. convirtiendo la cadena Alpha (id 1007 -> 1049 y ",35,v,10,0," -> ",56,v,") sale EXACTAMENTE la cadena Toggle (nada mas cambia);
//   3. el simulador de GD (gdsim.hpp, que ejecuta Alpha y Toggle como absolutos) dibuja los MISMOS pixeles en cada frame de 2 vueltas,
//      tanto en condiciones normales como con lag (saltos de 50 ms al reves) y triggers perdidos;
//   4. el valor de cada Toggle es 0 o 1 y las comprobaciones de estructura (filas de triggers, triggers antes que objetos) siguen cumpliendose.
//   g++ -O2 -std=c++23 -pthread -Iinclude -Isrc/engine -Itests tests/toggle_test.cpp -o toggle_test && ./toggle_test tests/test.gif [objetos/frame] [frames]
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "dispatch.hpp"
#include "chain_checks.hpp"
#include <fstream>
#include <iostream>
#include <regex>
using namespace gdx;

static std::string alphaToToggle(const std::string& s) {
    std::string t = std::regex_replace(s, std::regex("1,1007,"), "1,1049,");
    return std::regex_replace(t, std::regex(",35,([01]),10,0,62,1,87,1,57,"), ",56,$1,62,1,87,1,57,");
}
static size_t countSub(const std::string& s, const std::string& sub) { size_t c = 0, p = 0; while ((p = s.find(sub, p)) != std::string::npos) { ++c; p += sub.size(); } return c; }

int main(int argc, char** argv) {
    if (argc < 2) { std::puts("uso: toggle_test gif [objetos/frame] [frames]"); return 2; }
    std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    GifData g = decodeGif(b.data(), b.size());
    if (!g.ok()) { std::cout << "FALLO: GIF no valido\n"; return 1; }
    const int budget = argc > 2 ? atoi(argv[2]) : 200, nfr = argc > 3 ? atoi(argv[3]) : 8;
    GifPlan plan = planFrames(g, nfr, 1.0, 0.03);
    const int N = plan.count();
    const int W = 96, H = 96;
    int fails = 0, tested = 0;
    // 0..16 = metodos 1-17 (se prueban el 3 = BSP y el 8), 17 = capas (18), 18 = arbol (19), 19..28 = metodos 20-29
    for (int mi : {2, 7, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28}) {
        GifExportParams pa; pa.baseGroup = 100; pa.eo.longSide = 600; pa.trigX = -30;
        GifExportParams pt = pa; pt.useToggle = true;
        BuiltAnim A = buildForTest(g, plan, mi, budget, pa, 192), T = buildForTest(g, plan, mi, budget, pt, 192);
        char tag[32]; std::snprintf(tag, sizeof tag, "metodo %2d", mi + 1);
        if (A.chain.empty() || T.chain.empty()) { std::cout << tag << ": cadena vacia (" << A.err << " | " << T.err << ") - se salta\n"; continue; }
        ++tested;
        bool ok = true;
        const size_t a1007 = countSub(A.chain, "1,1007,"), a1049 = countSub(A.chain, "1,1049,"), t1007 = countSub(T.chain, "1,1007,"), t1049 = countSub(T.chain, "1,1049,");
        if (a1049 != 0 || t1007 != 0 || (a1007 == 0 && mi != 25) || a1007 != t1049)   // el metodo 26 (ciclos de color) no usa Alpha: 0 es lo esperado { std::cout << tag << ": FALLO conteo de triggers (alpha: " << a1007 << "/" << a1049 << " toggle: " << t1007 << "/" << t1049 << ")\n"; ok = false; }
        if (alphaToToggle(A.chain) != T.chain) { std::cout << tag << ": FALLO la cadena Toggle difiere de la Alpha en algo mas que el id y la clave\n"; ok = false; }
        for (const auto& kv : parse(T.chain)) {                       // valores de los Toggle
            if (!kv.count(1) || std::stoi(kv.at(1)) != 1049) continue;
            if (!kv.count(56) || (kv.at(56) != "0" && kv.at(56) != "1") || kv.count(35) || !kv.count(51)) { std::cout << tag << ": FALLO un Toggle mal formado\n"; ok = false; break; }
        }
        if (triggerOverlaps(T.chain) != 0) { std::cout << tag << ": FALLO triggers solapados en la cadena Toggle\n"; ok = false; }
        if (!triggersBeforeObjects(T.chain)) { std::cout << tag << ": FALLO hay triggers despues de los objetos\n"; ok = false; }

        struct Cond { const char* name; sim::Opt o; };
        sim::Opt normal; sim::Opt lag; lag.tick = 0.05; lag.reverseTick = true; lag.dropAlpha = 0.05; lag.dropSpawn = 0.02; lag.seed = 7;
        for (const Cond& c : {Cond{"normal", normal}, Cond{"lag+perdidas", lag}}) {
            sim::Sim SA, ST; SA.init(A.chain); ST.init(T.chain);
            std::mt19937 ra(c.o.seed), rt(c.o.seed);
            double t = 0; int diff = 0; double sum = 0; int cnt = 0;
            for (int rep = 0; rep < 2; ++rep) for (int i = 0; i < N; ++i) {
                t = rep * plan.total; for (int k = 0; k < i; ++k) t += plan.delay[size_t(k)];
                SA.run(t + 1e-4, c.o, ra); ST.run(t + 1e-4, c.o, rt);
                Image ia = SA.render(0, 0, 0.16, W, H, c.o, ra), it = ST.render(0, 0, 0.16, W, H, c.o, rt);
                if (ia.rgba != it.rgba) ++diff;
                Image tg = resizeArea(g.frames[size_t(plan.idx[size_t(i)])], W, H); sum += computePsnr(tg, it, 1); ++cnt;
            }
            if (diff) { std::cout << tag << " [" << c.name << "]: FALLO " << diff << " frames distintos entre Alpha y Toggle\n"; ok = false; }
            else std::printf("%s [%s]: identico a Alpha en %d frames (PSNR medio %.1f dB) | triggers %zu | cadena %zu KB\n", tag, c.name, 2 * N, sum / cnt, a1007, T.chain.size() / 1024);
        }
        if (!ok) ++fails;
    }
    if (tested == 0) { std::cout << "FALLO: no se pudo probar ningun metodo\n"; return 1; }
    std::cout << (fails ? "HAY FALLOS\n" : "TODO OK\n");
    return fails ? 1 : 0;
}
