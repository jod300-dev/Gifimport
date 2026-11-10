// Prueba sin Geode: decodifica un GIF, convierte frames, genera la cadena y SIMULA los triggers.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "gifanim.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <sstream>
#include <set>
using namespace gdx;

struct Obj { std::map<int,std::string> kv; std::vector<int> groups; };
static std::vector<Obj> parse(const std::string& s) {
    std::vector<Obj> v; std::stringstream ss(s); std::string item;
    while (std::getline(ss, item, ';')) {
        Obj o; std::stringstream is(item); std::string a, b;
        while (std::getline(is, a, ',') && std::getline(is, b, ',')) o.kv[std::stoi(a)] = b;
        if (o.kv.count(57)) { std::stringstream gs(o.kv[57]); std::string g; while (std::getline(gs, g, '.')) o.groups.push_back(std::stoi(g)); }
        v.push_back(std::move(o));
    }
    return v;
}

int main(int argc, char** argv) {
    std::ifstream f(argc > 1 ? argv[1] : "tests/test.gif", std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    GifData g = decodeGif(bytes.data(), bytes.size());
    if (!g.ok()) { std::cout << "FAIL decode: " << g.error << "\n"; return 1; }
    std::cout << "GIF " << g.w << "x" << g.h << " frames=" << g.frames.size() << "\n";

    int fails = 0;
    for (int methodIdx : {2, 4, 6, 8, 12}) {           // BSP, Regiones, Rotados, Halftone, Bayer
        Method m = Method(methodIdx);
        GifPlan plan = planFrames(g, 12, 1.0, 0.02);
        Options o; o.maxRaster = 256; o.quality = 0; o.lightSpecialChannels = true; o.maxChannels = 0;
        o.budget = gifPerFrameBudget(300, plan.count(), 40000);
        auto res = convertFrames(g, plan, m, o, nullptr, 4);
        GifExportParams p; p.baseGroup = 100; p.eo.longSide = 600;
        std::string err; std::string s = buildGifString(res, plan, p, &err);
        if (s.empty()) { std::cout << "FAIL build: " << err << "\n"; return 1; }
        auto objs = parse(s);
        const int N = plan.count();

        // --- simulacion
        std::map<int,int> opacity;                        // grupo -> 0/1 (por defecto 1)
        auto op = [&](int gid) { return opacity.count(gid) ? opacity[gid] : 1; };
        std::map<int,std::vector<int>> inGroup;           // grupo -> triggers
        for (int i = 0; i < int(objs.size()); ++i) {
            int id = std::stoi(objs[i].kv[1]);
            if (id == 1007 || id == 1268) for (int gg : objs[i].groups) inGroup[gg].push_back(i);
        }
        struct Ev { double t; int seq; int obj; bool operator<(const Ev& o) const { return t > o.t || (t == o.t && seq > o.seq); } };
        std::priority_queue<Ev> q; int seq = 0;
        for (int i = 0; i < int(objs.size()); ++i) {
            int id = std::stoi(objs[i].kv[1]);
            if (id == 1268 && !objs[i].kv.count(62)) q.push({0, seq++, i});     // de posicion: t = 0
        }
        // registro de visibilidad: tras cada evento anotamos que frames son visibles
        std::vector<std::pair<double,std::vector<int>>> log;
        const double tEnd = plan.total * 2.5;
        while (!q.empty() && q.top().t <= tEnd) {
            Ev e = q.top(); q.pop();
            Obj& ob = objs[size_t(e.obj)]; int id = std::stoi(ob.kv[1]);
            int target = std::stoi(ob.kv[51]);
            if (id == 1007) {
                opacity[target] = std::stoi(ob.kv[35]);
            } else {
                double d = ob.kv.count(63) ? std::stod(ob.kv[63]) : 0.0;
                for (int ti : inGroup[target]) q.push({e.t + d, seq++, ti});
            }
            if (!q.empty() && q.top().t == e.t) continue;   // espera a terminar el instante
            std::vector<int> vis;
            for (int i = 0; i < N; ++i) if (op(100 + i) == 1) vis.push_back(i);
            log.push_back({e.t, vis});
        }
        // --- comprobaciones: tras t=0 solo el frame 0; luego el frame i desde t_i; y se repite
        bool ok = true; double t0 = 0; std::string why;
        for (int rep = 0; rep < 2 && ok; ++rep) {
            double ti = rep * plan.total;
            for (int i = 0; i < N && ok; ++i) {
                // estado justo despues de t = ti
                std::vector<int> vis;
                for (auto& [t, v] : log) if (t <= ti + 1e-9) vis = v;
                if (vis != std::vector<int>{i}) { ok = false; why = "rep " + std::to_string(rep) + " frame " + std::to_string(i) + " visibles=" + std::to_string(vis.size()) + (vis.empty() ? "" : " primero=" + std::to_string(vis[0])); }
                ti += plan.delay[size_t(i)];
            }
        }
        size_t shapes = 0; for (auto& r : res) shapes += r.shapes.size();
        std::cout << "metodo " << methodIdx << ": frames=" << N << " objetos=" << shapes << " triggers=" << gifTriggerCount(N)
                  << " cadena=" << s.size() / 1024 << "KB loop=" << plan.total << "s  secuencia " << (ok ? "OK" : "FALLA: " + why) << "\n";
        if (!ok) ++fails;
        if (methodIdx == 2) { std::ofstream("tests/out_sample.txt") << s; }
    }
    // plan con muestreo (menos frames que el GIF)
    GifPlan p6 = planFrames(g, 5, 1.0, 0.02);
    double orig = 0; for (int d : g.delaysMs) orig += d / 1000.0;
    std::cout << "muestreo 12->" << p6.count() << " duracion " << p6.total << "s (original " << orig << "s)\n";
    if (std::abs(p6.total - orig) > 1e-6) ++fails;
    std::cout << (fails ? "HAY FALLOS\n" : "TODO OK\n");
    return fails ? 1 : 0;
}
