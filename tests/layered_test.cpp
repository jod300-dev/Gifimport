// Prueba sin Geode del metodo "Capas": calcula las capas, genera la cadena de GD, SIMULA los triggers,
// rasteriza los objetos visibles en cada frame y mide el PSNR contra el GIF original.
// Tambien compara con el metodo normal (un frame = un grupo) usando el mismo numero total de objetos.
//   g++ -O2 -std=c++20 -pthread -Iinclude -Isrc/engine tests/layered_test.cpp -o layered_test
//   ./layered_test tests/test.gif [maxFrames] [objetosPorFrame] [inner 0|1|2] [rests] [tol] [denoise 0|1] [timeExp] [painter 0|1] [bspFrac] [reuse]
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "layered.hpp"
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
static void hsv2rgb(double h, double s, double v, uint8_t* o) {
    if (h < 0) h += 360; double c = v * s, x = c * (1 - std::abs(std::fmod(h / 60.0, 2) - 1)), m = v - c, r = 0, g = 0, b = 0;
    int k = int(h / 60.0) % 6;
    if (k == 0) { r = c; g = x; } else if (k == 1) { r = x; g = c; } else if (k == 2) { g = c; b = x; }
    else if (k == 3) { g = x; b = c; } else if (k == 4) { r = x; b = c; } else { r = c; b = x; }
    o[0] = uint8_t(std::clamp((r + m) * 255 + 0.5, 0.0, 255.0)); o[1] = uint8_t(std::clamp((g + m) * 255 + 0.5, 0.0, 255.0)); o[2] = uint8_t(std::clamp((b + m) * 255 + 0.5, 0.0, 255.0));
}

int main(int argc, char** argv) {
    std::ifstream f(argc > 1 ? argv[1] : "tests/test.gif", std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    GifData g = decodeGif(bytes.data(), bytes.size());
    if (!g.ok()) { std::cout << "FAIL decode: " << g.error << "\n"; return 1; }
    int maxFrames = argc > 2 ? std::atoi(argv[2]) : 40;
    LayeredOptions lo; lo.perFrame = argc > 3 ? std::atoi(argv[3]) : 300;
    lo.inner = argc > 4 ? std::atoi(argv[4]) : 0; lo.restCount = argc > 5 ? std::atoi(argv[5]) : 0;
    if (argc > 6) lo.tol = std::atoi(argv[6]);
    lo.maxTotal = 150000; if (argc > 7) lo.denoise = std::atoi(argv[7]) != 0; if (argc > 8) lo.timeExp = float(std::atof(argv[8])); if (argc > 9) lo.painter = std::atoi(argv[9]); if (argc > 10) lo.bspFrac = float(std::atof(argv[10])); if (argc > 11) lo.reuse = float(std::atof(argv[11])); if (argc > 12) lo.stillBsp = float(std::atof(argv[12]));
    std::cout << "GIF " << g.w << "x" << g.h << " frames=" << g.frames.size() << "\n";

    GifPlan plan = planFrames(g, maxFrames, 1.0, 0.03);
    const int N = plan.count();
    Options o; o.maxRaster = 512; o.quality = 0; /* argv[13] sobreescribe */ o.lightSpecialChannels = true; o.maxChannels = 0;
    if (argc > 13) o.quality = std::atoi(argv[13]);
    double t0 = nowMs();
    LayeredAnim A = buildLayered(g, plan, o, lo, nullptr, 4);
    double t1 = nowMs();
    if (!A.ok()) { std::cout << "FAIL build: " << A.note << "\n"; return 1; }
    size_t total = A.objectCount();
    LayeredSchedule S = scheduleLayered(A);
    { int mz = 0; for (auto& sh : A.still.shapes) mz = std::max<int>(mz, sh.z); std::cout << "z-order: still max=" << mz << " zRest=" << A.zRest << " zPatch=" << A.zPatch << "\n"; }
    std::cout << "CAPAS: frames=" << N << " rests=" << A.rests.size() << " parches=" << S.nPatches 
              << "  still=" << A.still.shapes.size() << " objetos=" << total << " triggers=" << S.triggers << " grupos=" << S.groups
              << "  PSNR medio=" << A.psnrAvg << " min=" << A.psnrMin << "  (" << (t1 - t0) << " ms)\n";

    // --- referencia: metodo normal (BSP) con el MISMO numero total de objetos
    {
        Options on = o; on.budget = std::max(1, int((total) / N));
        auto res = convertFrames(g, plan, Method::BSP, on, nullptr, 4);
        double s = 0, mn = 1e9; size_t cnt = 0;
        for (auto& r : res) { s += r.psnr; mn = std::min(mn, r.psnr); cnt += r.shapes.size(); }
        std::cout << "NORMAL (BSP, " << on.budget << " obj/frame): objetos=" << cnt << "  PSNR medio=" << s / N << " min=" << mn << "\n";
    }

    // --- cadena de GD + simulacion de triggers + rasterizado de lo visible
    GifExportParams p; p.baseGroup = 100; p.eo.longSide = 600;
    std::string err, str = buildLayeredString(A, plan, p, &err);
    if (str.empty()) { std::cout << "FAIL string: " << err << "\n"; return 1; }
    auto objs = parse(str);
    std::map<int,int> opacity; auto op = [&](int gid) { return opacity.count(gid) ? opacity[gid] : 1; };
    std::map<int,std::vector<int>> inGroup;
    for (int i = 0; i < int(objs.size()); ++i) { int id = std::stoi(objs[size_t(i)].kv[1]); if (id == 1007 || id == 1268) for (int gg : objs[size_t(i)].groups) inGroup[gg].push_back(i); }
    struct Ev { double t; int seq; int obj; bool operator<(const Ev& q) const { return t > q.t || (t == q.t && seq > q.seq); } };
    std::priority_queue<Ev> q; int seq = 0;
    for (int i = 0; i < int(objs.size()); ++i) if (std::stoi(objs[size_t(i)].kv[1]) == 1268 && !objs[size_t(i)].kv.count(62)) q.push({0, seq++, i});
    // estado de opacidad tras cada instante
    std::vector<std::pair<double, std::map<int,int>>> log;
    const double tEnd = plan.total * 2.5;
    while (!q.empty() && q.top().t <= tEnd) {
        Ev e = q.top(); q.pop(); Obj& ob = objs[size_t(e.obj)]; int id = std::stoi(ob.kv[1]); int target = std::stoi(ob.kv[51]);
        if (id == 1007) opacity[target] = std::stoi(ob.kv[35]);
        else { double d = ob.kv.count(63) ? std::stod(ob.kv[63]) : 0.0; for (int ti : inGroup[target]) q.push({e.t + d, seq++, ti}); }
        if (!q.empty() && q.top().t == e.t) continue;
        log.push_back({e.t, opacity});
    }
    // rasterizar el estado visible en el instante de cada frame (2 vueltas) y comparar con el GIF
    const GdSize sz = gdImageSize(A.still, p.eo);
    const double ppu = A.W / sz.w;                                  // pixeles de trabajo por unidad GD
    double sumP = 0, minP = 1e9; int fails = 0, checked = 0;
    for (int rep = 0; rep < 2; ++rep) {
        double ti = rep * plan.total;
        for (int i = 0; i < N; ++i) {
            std::map<int,int> st; for (auto& [t, m] : log) if (t <= ti + 1e-9) st = m;
            auto vis = [&](int gid) { return st.count(gid) ? st[gid] : 1; };
            std::vector<const Obj*> shown;
            for (auto& ob : objs) { int id = std::stoi(ob.kv.at(1)); if (id != 3092) continue; bool ok = true; for (int gg : ob.groups) if (!vis(gg)) ok = false; if (ok) shown.push_back(&ob); }
            std::stable_sort(shown.begin(), shown.end(), [](const Obj* a, const Obj* b) { return std::stoi(a->kv.at(25)) < std::stoi(b->kv.at(25)); });
            Image c; c.w = A.W; c.h = A.H; c.rgba.assign(size_t(A.W) * A.H * 4, 0);
            for (auto* ob : shown) {
                double x = std::stod(ob->kv.at(2)), y = std::stod(ob->kv.at(3)), w = std::stod(ob->kv.at(128)), h = std::stod(ob->kv.at(129));
                std::stringstream hs(ob->kv.at(43)); std::string tok; std::vector<double> hv; while (std::getline(hs, tok, 'a')) hv.push_back(std::stod(tok));
                uint8_t rgb[3]; hsv2rgb(hv[0], hv[1], hv[2], rgb);
                double cx = A.W * 0.5 + x * ppu, cy = A.H * 0.5 - y * ppu, hw = w * ppu * 0.5, hh = h * ppu * 0.5;
                const double rot = ob->kv.count(6) ? std::stod(ob->kv.at(6)) : 0.0;
                if (std::abs(rot) < 1e-6) {
                    int x0 = std::max(0, int(std::floor(cx - hw + 0.5))), x1 = std::min(A.W, int(std::floor(cx + hw + 0.5)));
                    int y0 = std::max(0, int(std::floor(cy - hh + 0.5))), y1 = std::min(A.H, int(std::floor(cy + hh + 0.5)));
                    for (int yy = y0; yy < y1; ++yy) for (int xx = x0; xx < x1; ++xx) { uint8_t* px = c.at(xx, yy); px[0] = rgb[0]; px[1] = rgb[1]; px[2] = rgb[2]; px[3] = 255; }
                } else {                                  // rotado (clave 6, grados en sentido horario, y de pantalla hacia abajo)
                    const double rad = rot * 3.14159265358979323846 / 180.0, cs = std::cos(rad), sn = std::sin(rad);
                    const double ex = std::abs(hw * cs) + std::abs(hh * sn), ey = std::abs(hw * sn) + std::abs(hh * cs);
                    int x0 = std::max(0, int(std::floor(cx - ex - 0.5))), x1 = std::min(A.W - 1, int(std::ceil(cx + ex + 0.5)));
                    int y0 = std::max(0, int(std::floor(cy - ey - 0.5))), y1 = std::min(A.H - 1, int(std::ceil(cy + ey + 0.5)));
                    for (int yy = y0; yy <= y1; ++yy) for (int xx = x0; xx <= x1; ++xx) {
                        double dx = xx + 0.5 - cx, dy = yy + 0.5 - cy, lx = dx * cs + dy * sn, ly = -dx * sn + dy * cs;
                        if (lx >= -hw - 1e-4 && lx < hw + 1e-4 && ly >= -hh - 1e-4 && ly < hh + 1e-4) { uint8_t* px = c.at(xx, yy); px[0] = rgb[0]; px[1] = rgb[1]; px[2] = rgb[2]; px[3] = 255; }
                    }
                }
            }
            Image ref = resizeArea(g.frames[size_t(plan.idx[size_t(i)])], A.W, A.H);
            double ps = computePsnr(ref, c, 128);
            if (rep == 0) { sumP += ps; minP = std::min(minP, ps); ++checked; }
            if (std::abs(ps - A.psnr[size_t(i)]) > 3.0) { if (fails < 5) std::cout << "  frame " << i << " rep " << rep << ": PSNR simulado " << ps << " vs calculado " << A.psnr[size_t(i)] << "\n"; ++fails; }
            if (rep == 0 && (i == 0 || i == N / 2)) {                // vuelca dos frames para mirarlos
                std::string name = "/tmp/lay_" + std::to_string(i) + ".ppm"; std::ofstream pf(name, std::ios::binary);
                pf << "P6\n" << c.w << " " << c.h << "\n255\n";
                for (size_t k = 0; k < size_t(c.w) * c.h; ++k) { uint8_t px[3] = {c.rgba[k*4], c.rgba[k*4+1], c.rgba[k*4+2]}; pf.write((char*)px, 3); }
                std::string name2 = "/tmp/ref_" + std::to_string(i) + ".ppm"; std::ofstream pf2(name2, std::ios::binary);
                pf2 << "P6\n" << ref.w << " " << ref.h << "\n255\n";
                for (size_t k = 0; k < size_t(ref.w) * ref.h; ++k) { uint8_t px[3] = {ref.rgba[k*4], ref.rgba[k*4+1], ref.rgba[k*4+2]}; pf2.write((char*)px, 3); }
            }
            ti += plan.delay[size_t(i)] + 1e-7;
        }
    }
    std::cout << "SIMULACION (triggers + rasterizado de objetos): PSNR medio=" << sumP / checked << " min=" << minP
              << "  desajustes con lo calculado=" << fails << "  cadena=" << str.size() / 1024 << "KB\n";
    std::cout << (fails ? "HAY FALLOS\n" : "TODO OK\n");
    return fails ? 1 : 0;
}
