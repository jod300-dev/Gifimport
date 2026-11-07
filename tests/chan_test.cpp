// Prueba sin Geode: los 17 metodos de imagen con colores en CANALES (sin HSV, canal 1 nunca, empezando por el 2).
//   g++ -O2 -std=c++20 -pthread -Iinclude -Isrc/engine tests/chan_test.cpp -o chan_test && ./chan_test tests/test.gif [K]
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "chanframes.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace gdx;
int main(int argc, char** argv) {
    std::ifstream f(argc > 1 ? argv[1] : "tests/test.gif", std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    GifData g = decodeGif(bytes.data(), bytes.size());
    if (!g.ok()) { std::cout << "FAIL decode\n"; return 1; }
    const int K = argc > 2 ? std::atoi(argv[2]) : 64;
    GifPlan plan = planFrames(g, 12, 1.0, 0.03);
    int bad = 0;
    for (int m = 0; m < int(Method::Ascii); ++m) {
        Options o; o.budget = 120; o.maxRaster = 256; o.quality = 0; o.lightSpecialChannels = true; o.maxChannels = 0;
        auto res = convertFrames(g, plan, Method(m), o, nullptr, 2);
        // referencia HSV (no debe cambiar nada)
        GifExportParams p0; p0.baseGroup = 10; p0.eo.longSide = 300;
        std::string hsv = buildGifString(res, plan, p0, nullptr);
        auto pal = quantizeFrames(res, K);
        std::string err; GifExportParams p; p.baseGroup = 10; p.eo.longSide = 300;
        std::string str = buildGifStringCh(res, plan, p, pal, &err);
        if (str.empty()) { std::cout << "metodo " << m + 1 << ": FALLO " << err << "\n"; ++bad; continue; }
        // parsear y comprobar
        std::vector<std::map<int, std::string>> objs; std::stringstream ss(str); std::string item;
        while (std::getline(ss, item, ';')) { std::map<int, std::string> kv; std::stringstream is(item); std::string a, b; while (std::getline(is, a, ',') && std::getline(is, b, ',')) kv[std::stoi(a)] = b; objs.push_back(kv); }
        std::map<int, std::array<int, 3>> chCol; int nShape = 0, hsvN = 0, ch1 = 0, minCh = 9999, maxCh = 0, colTrig = 0;
        std::vector<int> shapeCh;
        for (auto& kv : objs) {
            int id = std::stoi(kv.at(1));
            if (id == 899) { ++colTrig; int c = std::stoi(kv.at(23)); chCol[c] = {std::stoi(kv.at(7)), std::stoi(kv.at(8)), std::stoi(kv.at(9))}; if (c == 1) ++ch1; minCh = std::min(minCh, c); maxCh = std::max(maxCh, c); }
            if (id == 3092) { ++nShape; if (kv.count(41) || kv.count(43)) ++hsvN; int c = std::stoi(kv.at(21)); if (c == 1) ++ch1; if (c >= 1000) ++ch1; shapeCh.push_back(c); }
        }
        // cada objeto (en orden: frames, formas) debe tener el color de su forma
        int mism = 0; size_t idx = 0;
        for (auto& r : res) for (auto& s : r.shapes) {
            if (idx >= shapeCh.size()) { ++mism; continue; }
            auto it = chCol.find(shapeCh[idx++]);
            if (it == chCol.end() || it->second[0] != s.r || it->second[1] != s.g || it->second[2] != s.b) ++mism;
        }
        bool ok = hsvN == 0 && ch1 == 0 && mism == 0 && minCh == 2 && colTrig == int(pal.size()) && !hsv.empty();
        std::cout << "metodo " << (m + 1) << ": objetos=" << nShape << " HSV=" << hsvN << " canal1/especiales=" << ch1 << " canales=" << minCh << ".." << maxCh
                  << " color-triggers=" << colTrig << " colores-mal=" << mism << (ok ? "  OK" : "  FALLO") << "\n";
        if (!ok) ++bad;
    }
    std::cout << (bad ? "HAY FALLOS\n" : "TODO OK\n");
    return bad ? 1 : 0;
}
