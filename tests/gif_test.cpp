// Prueba sin Geode: decodifica un GIF, calcula fondo + parches, genera la cadena de GD y SIMULA los triggers.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "gifpatch.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <sstream>
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

// Simula GD: triggers de posicion en t=0, Spawn (retardo) y Alpha. Devuelve false si la secuencia de frames no es la esperada.
static bool simulate(const std::string& s, const GifPlan& plan, int baseGroup, int expectStatic, int expectTriggers, std::string& why) {
    auto objs = parse(s);
    const int N = plan.count();
    std::map<int,int> opacity;                         // grupo -> 0/1 (por defecto 1)
    auto op = [&](int gid) { return opacity.count(gid) ? opacity[gid] : 1; };
    std::map<int,std::vector<int>> inGroup;
    int staticObjs = 0, triggers = 0, positional = 0;
    for (int i = 0; i < int(objs.size()); ++i) {
        int id = std::stoi(objs[i].kv[1]);
        if (id == 1007 || id == 1268) {
            ++triggers;
            for (int gg : objs[i].groups) inGroup[gg].push_back(i);
            if (id == 1268 && !objs[i].kv.count(62)) ++positional;
            if (id == 1268 && objs[i].kv.count(62) && !objs[i].kv.count(87)) { why = "spawn sin multi-trigger"; return false; }
        } else if (objs[i].groups.empty()) ++staticObjs;
    }
    if (staticObjs != expectStatic) { why = "objetos sin grupo=" + std::to_string(staticObjs) + " esperado " + std::to_string(expectStatic); return false; }
    if (triggers != expectTriggers) { why = "triggers=" + std::to_string(triggers) + " esperado " + std::to_string(expectTriggers); return false; }
    if (positional != 1) { why = "triggers de posicion=" + std::to_string(positional) + " (debe ser 1)"; return false; }
    for (auto& o : objs) {                              // la x de los triggers de posicion debe ser >= trigX y no estar en un grupo
        int id = std::stoi(o.kv[1]);
        if (id == 1268 && !o.kv.count(62) && !o.groups.empty()) { why = "trigger de posicion dentro de un grupo"; return false; }
    }
    struct Ev { double t; int seq; int obj; bool operator<(const Ev& o) const { return t > o.t || (t == o.t && seq > o.seq); } };
    std::priority_queue<Ev> q; int seq = 0;
    for (int i = 0; i < int(objs.size()); ++i) {
        int id = std::stoi(objs[i].kv[1]);
        if (id == 1268 && !objs[i].kv.count(62)) q.push({0, seq++, i});
    }
    std::vector<std::pair<double,std::vector<int>>> log;
    const double tEnd = plan.total * 3.5;
    while (!q.empty() && q.top().t <= tEnd) {
        Ev e = q.top(); q.pop();
        Obj& ob = objs[size_t(e.obj)]; int id = std::stoi(ob.kv[1]);
        int target = std::stoi(ob.kv[51]);
        if (id == 1007) opacity[target] = std::stoi(ob.kv[35]);
        else {
            double d = ob.kv.count(63) ? std::stod(ob.kv[63]) : 0.0;
            for (int ti : inGroup[target]) q.push({e.t + d, seq++, ti});
        }
        if (!q.empty() && q.top().t == e.t) continue;
        std::vector<int> vis;
        for (int i = 0; i < N; ++i) if (op(baseGroup + i) == 1) vis.push_back(i);
        log.push_back({e.t, vis});
    }
    for (int rep = 0; rep < 3; ++rep) {                 // 3 vueltas seguidas
        double ti = rep * plan.total;
        for (int i = 0; i < N; ++i) {
            std::vector<int> vis;
            for (auto& [t, v] : log) if (t <= ti + 1e-9) vis = v;
            if (vis != std::vector<int>{i}) { why = "vuelta " + std::to_string(rep) + " frame " + std::to_string(i) + ": visibles=" + std::to_string(vis.size()); return false; }
            ti += plan.delay[size_t(i)];
        }
    }
    return true;
}

static double psnrFrame(const Image& target, const std::vector<Shape>& shapes, int rw, int rh) {
    return computePsnr(target, renderShapes(shapes, rw, rh), 128);
}

int main(int argc, char** argv) {
    std::ifstream f(argc > 1 ? argv[1] : "tests/test.gif", std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    GifData g = decodeGif(bytes.data(), bytes.size());
    if (!g.ok()) { std::cout << "FAIL decode: " << g.error << "\n"; return 1; }
    std::cout << "GIF " << g.w << "x" << g.h << " frames=" << g.frames.size() << "\n";
    int fails = 0;
    for (int perFrame : {60, 200}) for (bool gray : {false, true}) {
        GifPlan plan = planFrames(g, 12, 1.0, 0.03);
        const int N = plan.count();
        Options o; o.maxRaster = 256; o.grayscale = gray;
        o.budget = patchPerFrameBudget(perFrame, N, 40000, perFrame * 6);
        PatchParams pp; pp.tolerance = 28; pp.baseBudget = perFrame * 6; pp.threads = 4;
        PatchSet ps = convertPatches(g, plan, o, pp, nullptr);
        GifExportParams p; p.baseGroup = 100; p.eo.longSide = 600; p.trigX = 0;
        std::string err, why;
        std::string str = buildGifPatchString(ps, plan, p, &err);
        if (str.empty()) { std::cout << "FAIL build: " << err << "\n"; ++fails; continue; }
        size_t pobj = 0; for (auto& r : ps.patches) pobj += r.shapes.size();
        bool ok = simulate(str, plan, 100, int(ps.base.shapes.size()), gifTriggerCount(N), why);
        // calidad: fondo + parche_i frente al frame original
        double psnr = 0; int rw = ps.rasterW, rh = ps.rasterH, zo = patchMaxZ(ps.base) + 1;
        for (int i = 0; i < N; ++i) {
            const Image& orig = g.frames[size_t(plan.idx[size_t(i)])];
            Image tgt = (orig.w == rw && orig.h == rh) ? orig : resizeArea(orig, rw, rh);
            if (gray) tgt = toGrayscale(tgt);
            std::vector<Shape> comp = ps.base.shapes;
            for (Shape s : ps.patches[size_t(i)].shapes) { s.z = int16_t(s.z + zo); comp.push_back(s); }
            psnr += psnrFrame(tgt, comp, rw, rh);
        }
        std::cout << "limite " << perFrame << (gray ? " gris " : " color") << ": frames=" << N << " base=" << ps.base.shapes.size()
                  << " parches=" << pobj << " triggers=" << gifTriggerCount(N) << " grupos=" << gifGroupCount(N)
                  << " cadena=" << str.size() / 1024 << "KB PSNR=" << psnr / N << "dB  secuencia " << (ok ? "OK" : "FALLA: " + why) << "\n";
        if (!ok) ++fails;
    }
    GifPlan p6 = planFrames(g, 5, 1.0, 0.02);
    double orig = 0; for (int d : g.delaysMs) orig += d / 1000.0;
    std::cout << "muestreo " << g.frames.size() << "->" << p6.count() << " duracion " << p6.total << "s (original " << orig << "s)\n";
    if (std::abs(p6.total - orig) > 1e-6) ++fails;
    std::cout << (fails ? "HAY FALLOS\n" : "TODO OK\n");
    return fails ? 1 : 0;
}
