// Simulador de GD (modelo): parsea la cadena exportada, aplica HSV/canales/Color triggers/Alpha/Spawn y rasteriza con z-order.
#pragma once
#include <map>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include "core.hpp"
namespace sim {
using gdx::Shape; using gdx::Image;
struct Obj {
    std::map<int, std::string> kv; std::vector<int> groups; int id = 0; double x = 0, y = 0; int order = 0;
    double geti(int k, double d = 0) const { auto it = kv.find(k); return it == kv.end() ? d : std::stod(it->second); }
    bool has(int k) const { return kv.count(k) > 0; }
};
inline std::vector<Obj> parse(const std::string& s) {
    std::vector<Obj> v; std::stringstream ss(s); std::string item; int ord = 0;
    while (std::getline(ss, item, ';')) {
        Obj o; std::stringstream is(item); std::string a, b;
        while (std::getline(is, a, ',') && std::getline(is, b, ',')) o.kv[std::stoi(a)] = b;
        if (!o.has(1)) continue;
        o.id = int(o.geti(1)); o.x = o.geti(2); o.y = o.geti(3); o.order = ord++;
        if (o.has(57)) { std::stringstream gs(o.kv[57]); std::string g; while (std::getline(gs, g, '.')) o.groups.push_back(std::stoi(g)); }
        v.push_back(std::move(o));
    }
    return v;
}
inline void hsv2rgb(double h, double s, double v, double& r, double& g, double& b) {
    h = std::fmod(h, 360.0); if (h < 0) h += 360.0;
    double c = v * s, x = c * (1 - std::abs(std::fmod(h / 60.0, 2.0) - 1)), m = v - c, rr = 0, gg = 0, bb = 0;
    if (h < 60) { rr = c; gg = x; } else if (h < 120) { rr = x; gg = c; } else if (h < 180) { gg = c; bb = x; }
    else if (h < 240) { gg = x; bb = c; } else if (h < 300) { rr = x; bb = c; } else { rr = c; bb = x; }
    r = rr + m; g = gg + m; b = bb + m;
}
struct Opt { bool randomTies = false; bool shuffleSameTick = false; unsigned seed = 1; };
struct Sim {
    std::vector<Obj> objs;
    std::map<int, std::array<uint8_t,3>> chColor;       // canal -> color (de Color triggers)
    std::map<int, int> opacity;                          // grupo -> 0/1
    std::map<int, std::vector<int>> trigsIn;             // grupo -> indices de triggers (alpha/spawn) spawn-triggered
    struct Ev { double t; long seq; int obj; };
    struct Cmp { bool operator()(const Ev& a, const Ev& b) const { return a.t > b.t || (a.t == b.t && a.seq > b.seq); } };
    std::priority_queue<Ev, std::vector<Ev>, Cmp> q; long seq = 0;
    // visibilidad de cada objeto = min opacidad de sus grupos
    bool visible(const Obj& o) const { for (int g : o.groups) { auto it = opacity.find(g); if (it != opacity.end() && it->second == 0) return false; } return true; }
    std::array<uint8_t,3> color(const Obj& o) const {
        int ch = int(o.geti(21, 1));
        double br = 255, bg = 255, bb = 255;                     // canal sin definir: blanco
        if (ch == 1010) br = bg = bb = 0; else if (ch == 1011) br = bg = bb = 255;
        else if (chColor.count(ch)) { br = chColor.at(ch)[0]; bg = chColor.at(ch)[1]; bb = chColor.at(ch)[2]; }
        if (o.has(43) && o.geti(41) == 1) {
            std::vector<double> p; { std::stringstream hs(o.kv.at(43)); std::string t; while (std::getline(hs, t, 'a')) p.push_back(std::stod(t)); }
            // base -> hsv
            double r = br / 255, g = bg / 255, b = bb / 255, mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn, H = 0, S = mx > 0 ? d / mx : 0, V = mx;
            if (d > 0) { if (mx == r) H = 60 * std::fmod((g - b) / d, 6.0); else if (mx == g) H = 60 * ((b - r) / d + 2); else H = 60 * ((r - g) / d + 4); }
            H += p[0]; S = p[3] > 0.5 ? S + p[1] : S * p[1]; V = p[4] > 0.5 ? V + p[2] : V * p[2];
            S = std::clamp(S, 0.0, 1.0); V = std::clamp(V, 0.0, 1.0);
            double rr, gg, bb2; hsv2rgb(H, S, V, rr, gg, bb2);
            return {uint8_t(std::clamp(rr * 255 + 0.5, 0.0, 255.0)), uint8_t(std::clamp(gg * 255 + 0.5, 0.0, 255.0)), uint8_t(std::clamp(bb2 * 255 + 0.5, 0.0, 255.0))};
        }
        return {uint8_t(br), uint8_t(bg), uint8_t(bb)};
    }
    void init(const std::string& chain) {
        objs = parse(chain);
        for (size_t i = 0; i < objs.size(); ++i) {
            const Obj& o = objs[i];
            if (o.id == 899) chColor[int(o.geti(23))] = {uint8_t(o.geti(7)), uint8_t(o.geti(8)), uint8_t(o.geti(9))};
            if ((o.id == 1007 || o.id == 1268) && o.has(62)) for (int g : o.groups) trigsIn[g].push_back(int(i));
        }
        // triggers de posicion: se ejecutan al empezar (t=0), de izquierda a derecha
        std::vector<int> pos;
        for (size_t i = 0; i < objs.size(); ++i) if ((objs[i].id == 1268 || objs[i].id == 1007) && !objs[i].has(62)) pos.push_back(int(i));
        std::stable_sort(pos.begin(), pos.end(), [&](int a, int b) { return objs[size_t(a)].x < objs[size_t(b)].x; });
        for (int i : pos) q.push({0, seq++, i});
    }
    // Procesa todos los eventos con t <= tUntil.  Los eventos del mismo instante se ejecutan por orden de x (izq -> der) o barajados.
    void run(double tUntil, const Opt& opt, std::mt19937& rng) {
        while (!q.empty() && q.top().t <= tUntil + 1e-9) {
            double t = q.top().t; std::vector<Ev> batch;
            while (!q.empty() && std::abs(q.top().t - t) < 1e-9) { batch.push_back(q.top()); q.pop(); }
            // una "ronda" del mismo instante: lo que dispare un spawn con retardo 0 entra en la misma ronda
            while (!batch.empty()) {
                if (opt.shuffleSameTick) std::shuffle(batch.begin(), batch.end(), rng);
                else std::stable_sort(batch.begin(), batch.end(), [&](const Ev& a, const Ev& b) { return objs[size_t(a.obj)].x < objs[size_t(b.obj)].x; });
                std::vector<Ev> next;
                for (const Ev& e : batch) {
                    const Obj& ob = objs[size_t(e.obj)];
                    int target = int(ob.geti(51));
                    if (ob.id == 1007) opacity[target] = int(ob.geti(35, 1));
                    else if (ob.id == 1268) {
                        double d = ob.geti(63, 0);
                        for (int ti : trigsIn[target]) { if (d <= 1e-9) next.push_back({t, seq++, ti}); else q.push({t + d, seq++, ti}); }
                    }
                }
                batch = std::move(next);
            }
        }
    }
    // Rasteriza lo visible: pxPerUnit px por unidad de GD; origen (ox,oy) = centro; W,H en px.
    Image render(double ox, double oy, double pxPerUnit, int W, int H, const Opt& opt, std::mt19937& rng) const {
        std::vector<const Obj*> v;
        for (auto& o : objs) if (o.id == 3092 && visible(o)) v.push_back(&o);
        std::vector<double> tie(objs.size(), 0.0);
        if (opt.randomTies) for (auto& t : tie) t = std::uniform_real_distribution<double>(0, 1)(rng);
        std::stable_sort(v.begin(), v.end(), [&](const Obj* a, const Obj* b) {
            double za = a->geti(25, 0), zb = b->geti(25, 0);
            if (za != zb) return za < zb;
            return opt.randomTies ? tie[size_t(a->order)] < tie[size_t(b->order)] : a->order < b->order;
        });
        std::vector<Shape> sh;
        for (const Obj* o : v) {
            Shape s; s.cx = float((o->x - ox) * pxPerUnit + W * 0.5); s.cy = float((oy - o->y) * pxPerUnit + H * 0.5);
            s.w = float(o->geti(128, 1) * pxPerUnit); s.h = float(o->geti(129, 1) * pxPerUnit); s.rot = float(o->geti(6, 0));
            auto c = color(*o); s.r = c[0]; s.g = c[1]; s.b = c[2]; s.z = 0;
            sh.push_back(s);
        }
        return gdx::renderShapes(sh, W, H);        // ya en orden
    }
};
} // namespace sim
