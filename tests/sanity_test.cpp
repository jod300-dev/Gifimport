// Prueba de SANIDAD sin Geode: los errores reales que se vieron en el juego no deben repetirse en ningun metodo.
//   1. ninguna forma fuera del lienzo (en GD no se recorta: se ve como astillas alrededor de la imagen)
//   2. z-order compacto (no miles de valores distintos)
//   3. nunca mas formas que el presupuesto
//   4. en la CADENA exportada: todo objeto dentro del rectangulo de la imagen (+ solape), escalas > 0, ids/grupos validos
//   5. editor limpio: el frame 0 esta por encima del resto (z-order mayor)
//   5b. ningun par de formas que se solapan de verdad (SAT, geometria continua) comparte z: en GD los empates NO tienen orden garantizado
//   5c. hoja de frames: sin grupos ni triggers, cada frame dentro de su casilla
//   5d. NUNCA se emite z-order 0 (GD lo trata como \"por defecto\" y lo dibuja ENCIMA de los z>=1: tapaba los rotados de 7/8/18/19)
//   5e. triggers de frames en UNA fila, uno tras otro hacia la derecha, sin ninguno encima/debajo de otro
//   6. metodos 18/19: igual sobre cada capa
//   g++ -O2 -std=c++20 -pthread -Iinclude -Isrc/engine tests/sanity_test.cpp -o sanity_test && ./sanity_test tests/test.gif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "chanframes.hpp"
#include "deltatree.hpp"
#include "calib.hpp"
#include "sheet.hpp"
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
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

int main(int argc, char** argv) {
    std::ifstream f(argc > 1 ? argv[1] : "tests/test.gif", std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    GifData g = decodeGif(bytes.data(), bytes.size());
    if (!g.ok()) { std::cout << "FAIL decode\n"; return 1; }
    GifPlan plan = planFrames(g, 6, 1.0, 0.03);
    int bad = 0, checks = 0;
    auto fail = [&](const std::string& m) { std::cout << "  FALLO: " << m << "\n"; ++bad; };
    for (int budget : {300, 1150, 2500}) {
        for (int m = 0; m < int(Method::Ascii); ++m) {
            Options o; o.budget = budget; o.maxRaster = 256; o.quality = 0; o.lightSpecialChannels = true; o.maxChannels = 0;
            auto res = convertFrames(g, plan, Method(m), o, nullptr, 2);
            for (size_t i = 0; i < res.size(); ++i) {
                auto& r = res[i];
                ++checks;
                if (int(r.shapes.size()) > budget) fail("metodo " + std::to_string(m + 1) + " presup " + std::to_string(budget) + ": mas formas que el presupuesto");
                double worst = 0; for (auto& s : r.shapes) worst = std::max(worst, shapeOverhang(s, r.rasterW, r.rasterH));
                if (worst > 0.2) fail("metodo " + std::to_string(m + 1) + ": forma fuera del lienzo por " + std::to_string(worst) + " px");
                int mz = 0; for (auto& s : r.shapes) mz = std::max<int>(mz, s.z);
                if (!r.mono && mz > 64) fail("metodo " + std::to_string(m + 1) + ": z-order disperso (max " + std::to_string(mz) + ")");
                if (i == 0 && !r.mono) { long ties = tiedOverlaps(r.shapes); if (ties) fail("metodo " + std::to_string(m + 1) + ": " + std::to_string(ties) + " pares solapados con el mismo z"); }
            }
            // cadena exportada (HSV): dentro del rectangulo de la imagen
            GifExportParams p; p.baseGroup = 10; p.eo.longSide = 300; p.eo.originX = 1000; p.eo.originY = 500;
            std::string str = buildGifString(res, plan, p, nullptr);
            if (str.empty()) { fail("metodo " + std::to_string(m + 1) + ": cadena vacia"); continue; }
            if (int ov = triggerOverlaps(str)) fail("metodo " + std::to_string(m + 1) + ": " + std::to_string(ov) + " pares de triggers se pisan");
            if (!triggersBeforeObjects(str)) fail("metodo " + std::to_string(m + 1) + ": hay triggers DESPUES de los objetos");
            GdSize sz = gdImageSize(res[0], p.eo);
            double zf0 = -1e9, zOthers = -1e9;
            for (auto& kv : parse(str)) {
                int id = std::stoi(kv.at(1));
                if (id != p.eo.objectId) continue;
                double x = std::stod(kv.at(2)), y = std::stod(kv.at(3)), w = std::stod(kv.at(128)), h = std::stod(kv.at(129));
                double rot = kv.count(6) ? std::stod(kv.at(6)) : 0.0, rad = rot * 3.14159265358979 / 180.0;
                double ex = std::abs(w * 0.5 * std::cos(rad)) + std::abs(h * 0.5 * std::sin(rad)), ey = std::abs(w * 0.5 * std::sin(rad)) + std::abs(h * 0.5 * std::cos(rad));
                double tol = 0.3 * (sz.w / res[0].rasterW) + p.eo.bleed;           // 0.3 px del raster + solape
                if (x - ex < p.eo.originX - sz.w * 0.5 - tol || x + ex > p.eo.originX + sz.w * 0.5 + tol || y - ey < p.eo.originY - sz.h * 0.5 - tol || y + ey > p.eo.originY + sz.h * 0.5 + tol) { fail("metodo " + std::to_string(m + 1) + ": objeto exportado fuera de la imagen"); break; }
                if (w <= 0 || h <= 0) { fail("metodo " + std::to_string(m + 1) + ": escala no positiva"); break; }
                int grp = std::stoi(kv.at(57)), z = std::stoi(kv.at(25));
                if (z <= 0) { fail("metodo " + std::to_string(m + 1) + ": se emitio z-order " + std::to_string(z) + " (GD lo trata como por defecto y lo dibuja encima)"); break; }
                if (grp == p.baseGroup) zf0 = std::max<double>(zf0, z); else zOthers = std::max<double>(zOthers, z);
            }
            if (!res[0].shapes.empty() && zOthers > -1e8 && !(zf0 > zOthers)) fail("metodo " + std::to_string(m + 1) + ": el frame 0 no queda encima en el editor");
            ++checks;
        }
    }
    // metodos 18 y 19, con y sin canales
    for (int tree = 0; tree < 2; ++tree) for (int ch : {0, 64}) {
        Options o; o.maxRaster = 256; o.quality = 0; o.lightSpecialChannels = true; o.maxChannels = 0;
        LayeredOptions lo; lo.perFrame = 600; lo.maxTotal = 100000; lo.tree = tree; lo.channels = ch;
        LayeredAnim A = buildAnim(g, plan, o, lo, nullptr, 2);
        std::string tag = std::string(tree ? "arbol" : "capas") + (ch ? "+canales" : "+HSV");
        if (!A.ok()) { fail(tag + ": no se calcularon las capas"); continue; }
        auto chk = [&](const Result& r, const char* nm) {
            double worst = 0; for (auto& s : r.shapes) worst = std::max(worst, shapeOverhang(s, A.W, A.H));
            if (worst > 0.2) fail(tag + " " + nm + ": forma fuera del lienzo por " + std::to_string(worst) + " px");
            int mz = 0; for (auto& s : r.shapes) mz = std::max<int>(mz, s.z);
            if (mz > 128) fail(tag + " " + nm + ": z-order disperso");
            long ties = tiedOverlaps(r.shapes); if (ties) fail(tag + " " + nm + ": " + std::to_string(ties) + " pares solapados con el mismo z");
        };
        chk(A.still, "fondo"); for (auto& r : A.rests) chk(r, "rest"); for (auto& r : A.patches) chk(r, "parche");
        GifExportParams p; p.baseGroup = 10; p.eo.longSide = 300; p.eo.originX = 1000; p.eo.originY = 500;
        std::string err, str = buildAnimString(A, plan, p, &err);
        if (str.empty()) { fail(tag + ": cadena vacia " + err); continue; }
        if (int ov = triggerOverlaps(str)) fail(tag + ": " + std::to_string(ov) + " pares de triggers se pisan");
        if (!triggersBeforeObjects(str)) fail(tag + ": hay triggers DESPUES de los objetos");
        GdSize sz = gdImageSize(A.still, p.eo);
        for (auto& kv : parse(str)) {
            if (std::stoi(kv.at(1)) != p.eo.objectId) continue;
            if (std::stoi(kv.at(25)) <= 0) { fail(tag + ": se emitio z-order <= 0"); break; }
            double x = std::stod(kv.at(2)), y = std::stod(kv.at(3)), w = std::stod(kv.at(128)), h = std::stod(kv.at(129));
            double rot = kv.count(6) ? std::stod(kv.at(6)) : 0.0, rad = rot * 3.14159265358979 / 180.0;
            double ex = std::abs(w * 0.5 * std::cos(rad)) + std::abs(h * 0.5 * std::sin(rad)), ey = std::abs(w * 0.5 * std::sin(rad)) + std::abs(h * 0.5 * std::cos(rad));
            double tol = 0.3 * (sz.w / A.W) + p.eo.bleed;
            if (x - ex < p.eo.originX - sz.w * 0.5 - tol || x + ex > p.eo.originX + sz.w * 0.5 + tol || y - ey < p.eo.originY - sz.h * 0.5 - tol || y + ey > p.eo.originY + sz.h * 0.5 + tol) { fail(tag + ": objeto exportado fuera de la imagen"); break; }
        }
        ++checks;
    }
    // tope duro de objetos en 18/19 (objetos + triggers <= maxTotal)
    for (int tree = 0; tree < 2; ++tree) {
        Options o; o.maxRaster = 256; o.quality = 0; o.lightSpecialChannels = true; o.maxChannels = 0;
        LayeredOptions lo; lo.perFrame = 5000; lo.maxTotal = 4000; lo.tree = tree; lo.channels = 0;
        LayeredAnim A = buildAnim(g, plan, o, lo, nullptr, 2);
        long tot = A.ok() ? long(A.objectCount()) + animTriggerCount(A) : 0;
        if (!A.ok() || tot > lo.maxTotal) fail(std::string(tree ? "arbol" : "capas") + ": supera el tope duro (" + std::to_string(tot) + " > 4000)");
        ++checks;
    }
    // 7 y 8 NO son el mismo metodo: el 7 es puro (un fondo plano + girados), el 8 tiene base BSP (mas formas alineadas a ejes)
    {
        Options o; o.budget = 400; o.maxRaster = 256; o.quality = 0;
        Result a = convert(g.frames[0], Method::Geometrize, o), b = convert(g.frames[0], Method::Hybrid, o);
        auto axisCount = [](const Result& r) { int n = 0; for (auto& s : r.shapes) if (std::fmod(std::abs(s.rot), 90.f) < 0.05f) ++n; return n; };
        if (axisCount(a) >= axisCount(b) && !b.note.empty() == false) fail("7 y 8: el 7 tiene tantas formas alineadas como el 8 (no hay diferencia)");
        ++checks;
    }
    // hoja de frames (diagnostico): sin grupos ni triggers; cada frame dentro de su casilla
    {
        Options o; o.budget = 300; o.maxRaster = 256; o.quality = 0; o.lightSpecialChannels = true; o.maxChannels = 0;
        auto res = convertFrames(g, plan, Method::BSP, o, nullptr, 2);
        for (int useCh = 0; useCh < 2; ++useCh) {
            std::vector<std::array<uint8_t, 3>> pal; auto frames = res;
            if (useCh) pal = quantizeFrames(frames, 32);
            GifExportParams p; p.baseGroup = 10; p.eo.longSide = 300; p.eo.originX = 1000; p.eo.originY = 500;
            std::string err, s = buildFrameSheet(frames, p, pal, kFirstColorChannel, false, &err);
            if (s.empty()) { fail("hoja: cadena vacia " + err); continue; }
            size_t shapesOut = 0, trig = 0; bool grp = false, ch1 = false;
            GdSize sz = gdImageSize(res[0], p.eo); const int cols = std::clamp(int(std::ceil(std::sqrt(double(res.size())))), 1, 8); const double gap = sz.w * 0.12;
            for (auto& kv : parse(s)) {
                int id = std::stoi(kv.at(1));
                if (id == 899) { ++trig; if (std::stoi(kv.at(23)) < 2) ch1 = true; continue; }
                if (id != p.eo.objectId) { fail("hoja: objeto inesperado id " + std::to_string(id) + " (no debe haber triggers)"); break; }
                if (std::stoi(kv.at(25)) <= 0) fail("hoja: z-order <= 0"); ++shapesOut; if (kv.count(57)) grp = true; if (useCh && (!kv.count(21) || std::stoi(kv.at(21)) < 2)) ch1 = true;
                double x = std::stod(kv.at(2)), y = std::stod(kv.at(3));
                int col = int(std::floor((x - (p.eo.originX - sz.w * 0.5) + 0.5) / (sz.w + gap))), row = int(std::floor(((p.eo.originY + sz.h * 0.5) - y) / (sz.h + gap)));
                if (col < 0 || col >= cols || row < 0 || row > int(res.size()) / cols) { fail("hoja: objeto fuera de la cuadricula"); break; }
            }
            size_t expect = 0; for (auto& r : res) expect += r.shapes.size();
            if (grp) fail("hoja: hay objetos con grupo"); if (ch1) fail("hoja: usa el canal 1 o falta canal"); if (shapesOut != expect) fail("hoja: numero de objetos distinto");
            if (useCh && trig != pal.size()) fail("hoja: color-triggers != paleta");
            ++checks;
        }
    }
    // patron de calibracion: cadena bien formada, 115 objetos HSV + 4 de canal (2..5) + 4 Color triggers, z=300 presente, sin canal 1
    {
        GdExportOptions eo; eo.originX = 1000; eo.originY = 500;
        auto cr = buildCalibration(eo, -30);
        int hsv = 0, chs = 0, trig = 0, zmax = 0, bad1 = 0, malformed = 0;
        std::set<int> chans;
        for (auto& kv : parse(cr.str)) {
            if (!kv.count(1)) { ++malformed; continue; }
            int id = std::stoi(kv.at(1));
            if (id == 899) { ++trig; chans.insert(std::stoi(kv.at(23))); continue; }
            if (id != eo.objectId) { ++malformed; continue; }
            if (kv.count(43)) ++hsv; else { ++chs; if (std::stoi(kv.at(21)) < 2) ++bad1; }
            zmax = std::max(zmax, std::stoi(kv.at(25)));
            if (!kv.count(2) || !kv.count(3) || !kv.count(128) || !kv.count(129)) ++malformed;
        }
        if (malformed) fail("calibracion: " + std::to_string(malformed) + " objetos mal formados");
        if (hsv != 115 || chs != 4) fail("calibracion: numero de objetos inesperado (HSV " + std::to_string(hsv) + ", canal " + std::to_string(chs) + ")");
        if (trig != 4 || chans != std::set<int>({2, 3, 4, 5})) fail("calibracion: Color triggers/canales incorrectos");
        if (zmax < 300) fail("calibracion: falta el z-order 300"); if (bad1) fail("calibracion: usa canal < 2");
        ++checks;
    }
    std::cout << checks << " comprobaciones: " << (bad ? std::to_string(bad) + " FALLOS\n" : std::string("TODO OK\n"));
    return bad ? 1 : 0;
}
