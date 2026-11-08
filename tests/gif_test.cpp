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

static int triggerSchemaErrors(const std::vector<Obj>& objs, int frames) {
    int errors = 0, alpha = 0, spawn = 0, startup = 0, loopSelf = 0;
    double lastSpawnX = -1e18, spawnRowY = 0.0; bool firstSpawn = true;
    std::vector<double> startupX;
    for (const Obj& ob : objs) {
        if (!ob.kv.count(1)) continue;
        const int id = std::stoi(ob.kv.at(1));
        if (id == 1007) {
            ++alpha;
            if (!ob.kv.count(51) || !ob.kv.count(35) || !ob.kv.count(10) ||
                !ob.kv.count(62) || !ob.kv.count(87) || !ob.kv.count(57) ||
                std::stoi(ob.kv.at(62)) != 1 || std::stoi(ob.kv.at(87)) != 1 ||
                std::stoi(ob.kv.at(10)) != 0) ++errors;
        } else if (id == 1268) {
            ++spawn;
            if (!ob.kv.count(51)) { ++errors; continue; }
            const double x = std::stod(ob.kv.at(2)), y = std::stod(ob.kv.at(3));
            if (ob.kv.count(62)) {
                if (std::stoi(ob.kv.at(62)) != 1 || !ob.kv.count(87) ||
                    std::stoi(ob.kv.at(87)) != 1 || !ob.kv.count(57)) ++errors;
                if (x <= lastSpawnX || (!firstSpawn && std::abs(y - spawnRowY) > 0.01)) ++errors;
                if (firstSpawn) { firstSpawn = false; spawnRowY = y; }
                lastSpawnX = x;
                if (std::stoi(ob.kv.at(51)) == std::stoi(ob.kv.at(57)) && ob.kv.count(63) && std::stod(ob.kv.at(63)) > 0.0) ++loopSelf;
            } else {
                ++startup;
                startupX.push_back(x);
                if (ob.kv.count(87) || ob.kv.count(57)) ++errors;
            }
        }
    }
    if (alpha != 3 * frames - 1) ++errors; // 2N Alpha de frame + N-1 de inicializacion
    if (spawn != frames + 4) ++errors;     // N+2 dentro del loop + 2 de arranque
    if (startup != 2 || startupX.size() != 2 || std::abs(std::abs(startupX[0] - startupX[1]) - 30.0) > 0.01) ++errors;
    if (loopSelf != 1) ++errors;
    if (alpha + spawn != gifTriggerCount(frames)) ++errors;
    return errors;
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
        const int schemaErrors = triggerSchemaErrors(objs, N);
        if (schemaErrors) { std::cout << "FAIL: " << schemaErrors << " errores en flags/orden/count de triggers\n"; ++fails; }

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
    // El tope normal tambien debe incluir la paleta y los triggers.
    for (int n : {2, 34, 300}) {
        const int palette = gifPaletteBudget(900, n, 2000);
        const int perFrame = gifPerFrameBudget(80000, n, 2000, palette);
        const long long used = 1LL * n * perFrame + gifTriggerCount(n) + palette;
        if (used > 2000 || perFrame < 1 || palette < 0 || palette > 900) {
            std::cout << "FAIL: limite total para " << n << " frames = " << used << "\n"; ++fails;
        }
    }
    // plan con muestreo (menos frames que el GIF)
    GifPlan p6 = planFrames(g, 5, 1.0, 0.02);
    double orig = 0; for (int d : g.delaysMs) orig += d / 1000.0;
    std::cout << "muestreo 12->" << p6.count() << " duracion " << p6.total << "s (original " << orig << "s)\n";
    if (std::abs(p6.total - orig) > 1e-6) ++fails;
    std::cout << (fails ? "HAY FALLOS\n" : "TODO OK\n");
    return fails ? 1 : 0;
}
