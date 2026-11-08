// Prueba de SANIDAD sin Geode: los errores reales que se vieron en el juego no deben repetirse en ningun metodo.
//   1. ninguna forma fuera del lienzo (en GD no se recorta: se ve como astillas alrededor de la imagen)
//   2. z-order compacto (no miles de valores distintos)
//   3. nunca mas formas que el presupuesto
//   4. en la CADENA exportada: todo objeto dentro del rectangulo de la imagen (+ solape), escalas > 0, ids/grupos validos
//   5. editor limpio: el frame 0 esta por encima del resto (z-order mayor)
//   5b. ningun par de formas que se solapan de verdad (SAT, geometria continua) comparte z: en GD los empates NO tienen orden garantizado
//   5c. hoja de frames: sin grupos ni triggers, cada frame dentro de su casilla
//   6. metodos 18/19: igual sobre cada capa
//   g++ -O2 -std=c++20 -pthread -Iinclude -Isrc/engine tests/sanity_test.cpp -o sanity_test && ./sanity_test tests/test.gif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "chanframes.hpp"
#include "deltatree.hpp"
#include "sheet.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace gdx;
static std::vector<std::map<int, std::string>> parse(const std::string& s) {
    std::vector<std::map<int, std::string>> v; std::stringstream ss(s); std::string item;
    while (std::getline(ss, item, ';')) { std::map<int, std::string> kv; std::stringstream is(item); std::string a, b; while (std::getline(is, a, ',') && std::getline(is, b, ',')) kv[std::stoi(a)] = b; v.push_back(kv); }
    return v;
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
        GdSize sz = gdImageSize(A.still, p.eo);
        for (auto& kv : parse(str)) {
            if (std::stoi(kv.at(1)) != p.eo.objectId) continue;
            double x = std::stod(kv.at(2)), y = std::stod(kv.at(3)), w = std::stod(kv.at(128)), h = std::stod(kv.at(129));
            double rot = kv.count(6) ? std::stod(kv.at(6)) : 0.0, rad = rot * 3.14159265358979 / 180.0;
            double ex = std::abs(w * 0.5 * std::cos(rad)) + std::abs(h * 0.5 * std::sin(rad)), ey = std::abs(w * 0.5 * std::sin(rad)) + std::abs(h * 0.5 * std::cos(rad));
            double tol = 0.3 * (sz.w / A.W) + p.eo.bleed;
            if (x - ex < p.eo.originX - sz.w * 0.5 - tol || x + ex > p.eo.originX + sz.w * 0.5 + tol || y - ey < p.eo.originY - sz.h * 0.5 - tol || y + ey > p.eo.originY + sz.h * 0.5 + tol) { fail(tag + ": objeto exportado fuera de la imagen"); break; }
        }
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
                ++shapesOut; if (kv.count(57)) grp = true; if (useCh && (!kv.count(21) || std::stoi(kv.at(21)) < 2)) ch1 = true;
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
    std::cout << checks << " comprobaciones: " << (bad ? std::to_string(bad) + " FALLOS\n" : std::string("TODO OK\n"));
    return bad ? 1 : 0;
}
