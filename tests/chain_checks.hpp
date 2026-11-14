// Comprobaciones sobre una CADENA de GD (copiadas de sanity_test.cpp): parseo, triggers que se pisan, triggers antes que objetos, empates de z.
#pragma once
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "core.hpp"
#include "sanitize.hpp"
using namespace gdx;
static std::vector<std::map<int, std::string>> parse(const std::string& s) {
    std::vector<std::map<int, std::string>> v; std::stringstream ss(s); std::string item;
    while (std::getline(ss, item, ';')) { std::map<int, std::string> kv; std::stringstream is(item); std::string a, b; while (std::getline(is, a, ',') && std::getline(is, b, ',')) kv[std::stoi(a)] = b; v.push_back(kv); }
    return v;
}
// Triggers de frames: DOS filas (Alpha arriba, Spawn debajo), cada una avanzando hacia la derecha uno tras otro (x estrictamente
// creciente en orden de creacion, misma y), y los 2 Spawn de arranque (de posicion) apilados en la misma x. Los Color triggers (899) estan exentos.
static int triggerOverlaps(const std::string& s) {
    int bad = 0; double lastA = -1e18, lastS = -1e18, yA = 0, yS = 0; bool fa = true, fs = true;
    std::vector<std::pair<double, double>> start;
    for (auto& kv : parse(s)) {
        if (!kv.count(1)) continue;
        int id = std::stoi(kv.at(1));
        if (id != 1007 && id != 1268) continue;
        double x = std::stod(kv.at(2)), y = std::stod(kv.at(3));
        if (!kv.count(62)) { start.push_back({x, y}); continue; }
        if (id == 1007) { if (fa) { yA = y; fa = false; } if (x <= lastA || std::abs(y - yA) > 0.01) ++bad; lastA = x; }
        else            { if (fs) { yS = y; fs = false; } if (x <= lastS || std::abs(y - yS) > 0.01) ++bad; lastS = x; }
    }
    if (!fa && !fs && !(yA > yS)) ++bad;                          // Alpha ARRIBA, Spawn DEBAJO
    for (size_t i = 0; i < start.size(); ++i) for (size_t j = i + 1; j < start.size(); ++j)
        if (std::abs(start[i].first - start[j].first) > 0.01 || std::abs(start[i].second - start[j].second) < 29.0) ++bad;   // apilados
    return bad;
}

// Color triggers: cuadricula COMPACTA (paso 30 u en x e y, sin huecos). Cada y tiene columnas consecutivas.
static int colorGridGaps(const std::string& s) {
    std::map<long, std::vector<long>> rows; int bad = 0;
    for (auto& kv : parse(s)) {
        if (!kv.count(1) || std::stoi(kv.at(1)) != 899) continue;
        rows[std::lround(std::stod(kv.at(3)))].push_back(std::lround(std::stod(kv.at(2))));
    }
    long prevY = 0; bool first = true;
    for (auto& r : rows) {
        auto xs = r.second; std::sort(xs.begin(), xs.end());
        for (size_t i = 1; i < xs.size(); ++i) if (xs[i] - xs[i - 1] != 30) ++bad;      // huecos entre columnas
        if (!first && r.first - prevY != 30) ++bad;                                     // huecos entre filas
        prevY = r.first; first = false;
    }
    return bad;
}
// z-order: los frames (grupos) usan bandas de z DISJUNTAS
static int zBandOverlaps(const std::string& s) {
    std::map<int, std::pair<int, int>> zr;
    for (auto& kv : parse(s)) {
        if (!kv.count(1) || std::stoi(kv.at(1)) != 3092 || !kv.count(57)) continue;
        int g = std::stoi(kv.at(57)), z = std::stoi(kv.at(25));
        auto it = zr.find(g); if (it == zr.end()) zr[g] = {z, z}; else { it->second.first = std::min(it->second.first, z); it->second.second = std::max(it->second.second, z); }
    }
    std::vector<std::pair<int, int>> v; for (auto& k : zr) v.push_back(k.second); std::sort(v.begin(), v.end());
    int bad = 0; for (size_t i = 1; i < v.size(); ++i) if (v[i].first <= v[i - 1].second) ++bad;
    return bad;
}
// los triggers van ANTES que los objetos (un recorte por el limite del editor nunca debe dejar la animacion sin triggers)
static bool triggersBeforeObjects(const std::string& s) {
    bool seenObj = false;
    for (auto& kv : parse(s)) {
        if (!kv.count(1)) continue;
        int id = std::stoi(kv.at(1));
        if (id == 3092) seenObj = true;
        else if (seenObj && (id == 1007 || id == 1268 || id == 899)) return false;
    }
    return true;
}

// pares que se solapan (> 0.1 px) y comparten z: en GD su orden relativo seria arbitrario
static long tiedOverlaps(const std::vector<Shape>& sh) {
    long n = 0;
    for (size_t i = 0; i < sh.size(); ++i)
        for (size_t j = i + 1; j < sh.size(); ++j) {
            if (sh[i].z != sh[j].z) continue;
            if (std::abs(sh[i].cx - sh[j].cx) > 90 || std::abs(sh[i].cy - sh[j].cy) > 90) continue;
            if (rectPenetration(sh[i], sh[j]) > 0.1) ++n;
        }
    return n;
}

