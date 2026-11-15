// ============================================================================
//  Informe de diagnostico (v0.5.7). Sin dependencias de Geode: se puede probar en Linux.
//
//  Despues de colocar un GIF se guardan (si el ajuste esta activo) los datos necesarios para reconstruir EXACTAMENTE lo que hizo el mod y compararlo con lo que
//  se ve en el juego:
//    informe.txt     ajustes, GIF, plan de frames, resultado, estructura del metodo (capas, grupos, canales), estadisticas y COMPROBACIONES automaticas
//    cadena.txt      la cadena de objetos de GD tal cual se coloco en el editor (ground truth: con ella se reproduce todo en el simulador)
//    objetos.tsv     un renglon por objeto: id, posicion, escala, giro, z, capa, canal, grupos, HSV
//    triggers.tsv    un renglon por trigger: tipo, posicion, grupo objetivo, grupos propios, opacidad, fundido, retardo, color...
//    comparacion.png filas: frames originales / lo que calculo el mod / diferencia (x3)
//    detalle.png     dos frames a tamano grande: original | mod | diferencia
// ============================================================================
#pragma once
#include "deltatree.hpp"
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace gdx {
namespace diag {

// ------------------------------------------------------------------ PNG minimo (bloques "stored", sin compresion) ----
inline uint32_t crc32(const uint8_t* d, size_t n, uint32_t crc = 0) {
    static uint32_t T[256]; static bool init = false;
    if (!init) { for (uint32_t i = 0; i < 256; ++i) { uint32_t c = i; for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; T[i] = c; } init = true; }
    crc = ~crc; for (size_t i = 0; i < n; ++i) crc = T[(crc ^ d[i]) & 255u] ^ (crc >> 8); return ~crc;
}
inline void be32(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x >> 24)); v.push_back(uint8_t(x >> 16)); v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x)); }
inline void pngChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    be32(out, uint32_t(data.size()));
    std::vector<uint8_t> td(type, type + 4); td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end()); be32(out, crc32(td.data(), td.size()));
}
inline std::vector<uint8_t> encodePng(const std::vector<uint8_t>& rgb, int w, int h) {
    std::vector<uint8_t> out = {137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<uint8_t> ihdr; be32(ihdr, uint32_t(w)); be32(ihdr, uint32_t(h)); ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    pngChunk(out, "IHDR", ihdr);
    std::vector<uint8_t> raw; raw.reserve(size_t(h) * (size_t(w) * 3 + 1));
    for (int y = 0; y < h; ++y) { raw.push_back(0); raw.insert(raw.end(), rgb.begin() + std::ptrdiff_t(size_t(y) * w * 3), rgb.begin() + std::ptrdiff_t(size_t(y + 1) * w * 3)); }
    std::vector<uint8_t> z = {0x78, 0x01}; uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521u; b = (b + a) % 65521u; }
    for (size_t pos = 0; pos < raw.size() || pos == 0;) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos); const bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0); z.push_back(uint8_t(n & 255)); z.push_back(uint8_t(n >> 8)); z.push_back(uint8_t(~n & 255)); z.push_back(uint8_t((~n >> 8) & 255));
        z.insert(z.end(), raw.begin() + std::ptrdiff_t(pos), raw.begin() + std::ptrdiff_t(pos + n)); pos += n; if (last) break;
    }
    be32(z, (b << 16) | a);
    pngChunk(out, "IDAT", z); pngChunk(out, "IEND", {});
    return out;
}

// ------------------------------------------------------------------ imagenes ----
// RGBA -> RGB sobre gris oscuro (como la vista previa)
inline std::vector<uint8_t> flatten(const Image& im) {
    std::vector<uint8_t> o(size_t(im.w) * im.h * 3);
    for (size_t i = 0; i < size_t(im.w) * im.h; ++i) { const uint8_t* p = &im.rgba[i * 4]; const float a = p[3] / 255.f; for (int c = 0; c < 3; ++c) o[i * 3 + size_t(c)] = uint8_t(p[c] * a + 40 * (1.f - a)); }
    return o;
}
inline Image toRgbaOpaque(const std::vector<uint8_t>& rgb, int w, int h) { Image im; im.w = w; im.h = h; im.rgba.resize(size_t(w) * h * 4); for (size_t i = 0; i < size_t(w) * h; ++i) { for (int c = 0; c < 3; ++c) im.rgba[i * 4 + size_t(c)] = rgb[i * 3 + size_t(c)]; im.rgba[i * 4 + 3] = 255; } return im; }
inline Image renderResultImage(const Result& r) {
    if (!r.previewImg.rgba.empty() && r.previewImg.w > 0) return r.previewImg;
    return renderShapes(r.shapes, std::max(1, r.rasterW), std::max(1, r.rasterH));
}
inline double psnrRgb(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size() || a.empty()) return 0;
    double s = 0; for (size_t i = 0; i < a.size(); ++i) { const double d = double(a[i]) - b[i]; s += d * d; }
    const double mse = s / double(a.size()); return mse < 1e-9 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

struct Entry { Image orig, mod, diff; double psnr = 0, holes = 0; };   // holes: % de pixeles opacos del GIF que el render deja sin cubrir
inline Entry makeEntry(const GifData& g, int gifIdx, const Result& r) {
    Entry e; Image m = renderResultImage(r); const int W = m.w, H = m.h;
    Image o = g.frames[size_t(std::clamp(gifIdx, 0, int(g.frames.size()) - 1))];
    if (o.w != W || o.h != H) o = resizeArea(o, W, H);
    { size_t h = 0, tot = 0; for (size_t i = 0; i < size_t(W) * H; ++i) if (o.rgba[i * 4 + 3] >= 128) { ++tot; if (m.rgba[i * 4 + 3] == 0) ++h; } e.holes = tot ? 100.0 * double(h) / double(tot) : 0; }
    const std::vector<uint8_t> fo = flatten(o), fm = flatten(m);
    e.orig = toRgbaOpaque(fo, W, H); e.mod = toRgbaOpaque(fm, W, H); e.psnr = psnrRgb(fo, fm);
    std::vector<uint8_t> d(size_t(W) * H * 3); for (size_t i = 0; i < d.size(); ++i) d[i] = uint8_t(std::min(255, std::abs(int(fo[i]) - int(fm[i])) * 3));
    e.diff = toRgbaOpaque(d, W, H); return e;
}
// pega una imagen escalada a tile x tile (conservando la proporcion) en el lienzo RGB
inline void blit(std::vector<uint8_t>& cv, int cw, int x0, int y0, const Image& im, int tw, int th) {
    int nw = tw, nh = th; if (im.w * th > im.h * tw) nh = std::max(1, im.h * tw / im.w); else nw = std::max(1, im.w * th / im.h);
    const Image s = (im.w == nw && im.h == nh) ? im : resizeArea(im, nw, nh);
    for (int y = 0; y < nh; ++y) for (int x = 0; x < nw; ++x) for (int c = 0; c < 3; ++c) cv[(size_t(y0 + y) * size_t(cw) + size_t(x0 + x)) * 3 + size_t(c)] = s.rgba[(size_t(y) * size_t(s.w) + size_t(x)) * 4 + size_t(c)];
}

// ------------------------------------------------------------------ analisis de la cadena de GD ----
struct ChainStats {
    size_t objects = 0, blocks = 0, triggers = 0, bad = 0;
    std::map<int, size_t> byId;
    std::map<int, size_t> objsInGroup;            // grupo -> objetos (no triggers)
    std::set<int> targeted;                       // grupos que algun trigger controla (clave 51)
    std::set<int> ownGroups;                      // grupos propios de triggers "spawn triggered"
    std::set<int> trigGroups;                     // todos los grupos que llevan triggers
    std::set<int> chanUsed, chanColored;          // canales usados por bloques / fijados por Color triggers
    int zMin = 1 << 30, zMax = -(1 << 30);
    double xMin = 1e18, xMax = -1e18, yMin = 1e18, yMax = -1e18;
    size_t zeroZ = 0, hugeScale = 0, tinyScale = 0, nonFinite = 0;
    std::map<int, std::vector<std::string>> spawnsOf;     // grupo propio -> "->destino@retardo"
    std::map<int, std::pair<std::vector<int>, int>> alphaOf;   // grupo propio -> (grupos que muestra, n que oculta)
    std::vector<std::string> warnings;
};

inline const char* trigName(int id) {
    switch (id) { case 1007: return "Alpha"; case 1268: return "Spawn"; case 899: return "Color"; case 901: return "Move"; case 1049: return "Toggle"; case 1346: return "Rotate"; case 1006: return "Pulse"; case 1595: return "Touch"; default: return nullptr; }
}

// recorre la cadena objeto a objeto (sin guardarla entera) y escribe objetos.tsv / triggers.tsv
inline ChainStats scanChain(const std::string& chain, std::string& objTsv, std::string& trgTsv) {
    ChainStats S;
    objTsv = "n\tid\tx\ty\tescalaX\tescalaY\tgiro\tz\tcapa\tcanal\tgrupos\thsv\n";
    trgTsv = "n\ttipo\tid\tx\ty\tobjetivo\tgrupos_propios\tspawn_trig\tmulti\topacidad\tfundido\tretardo\tR\tG\tB\tcanal_objetivo\n";
    size_t pos = 0, n = 0;
    auto num = [](const std::string& s, double def = 0) { if (s.empty()) return def; char* e = nullptr; const double v = std::strtod(s.c_str(), &e); return e == s.c_str() ? def : v; };
    while (pos < chain.size()) {
        size_t end = chain.find(';', pos); if (end == std::string::npos) end = chain.size();
        if (end > pos) {
            std::map<int, std::string> kv; bool badObj = false;
            size_t p = pos;
            while (p < end) {
                size_t c1 = chain.find(',', p); if (c1 == std::string::npos || c1 >= end) { badObj = true; break; }
                size_t c2 = chain.find(',', c1 + 1); if (c2 == std::string::npos || c2 > end) c2 = end;
                const std::string ks = chain.substr(p, c1 - p);
                char* e = nullptr; const long k = std::strtol(ks.c_str(), &e, 10); if (e == ks.c_str()) { badObj = true; break; }
                kv[int(k)] = chain.substr(c1 + 1, c2 - c1 - 1); p = c2 + 1;
            }
            ++n; ++S.objects;
            if (badObj || !kv.count(1)) { ++S.bad; }
            else {
                const int id = int(num(kv[1])); ++S.byId[id];
                const double x = num(kv[2]), y = num(kv[3]);
                if (!std::isfinite(x) || !std::isfinite(y)) ++S.nonFinite;
                else { S.xMin = std::min(S.xMin, x); S.xMax = std::max(S.xMax, x); S.yMin = std::min(S.yMin, y); S.yMax = std::max(S.yMax, y); }
                std::vector<int> groups; { const std::string& g = kv.count(57) ? kv[57] : std::string(); size_t a = 0; while (a < g.size()) { size_t b = g.find('.', a); if (b == std::string::npos) b = g.size(); if (b > a) groups.push_back(int(num(g.substr(a, b - a)))); a = b + 1; } }
                const char* tn = trigName(id);
                const bool isTrig = tn != nullptr;
                std::string gs; for (size_t i = 0; i < groups.size(); ++i) { if (i) gs += '.'; gs += std::to_string(groups[i]); }
                char line[512];
                if (isTrig) {
                    ++S.triggers;
                    const int tgt = kv.count(51) ? int(num(kv[51])) : 0; const bool spawnTrig = kv.count(62) && num(kv[62]) != 0;
                    for (int g : groups) { S.trigGroups.insert(g); if (spawnTrig) S.ownGroups.insert(g); }
                    if ((id == 1007 || id == 1268 || id == 1049) && tgt > 0) S.targeted.insert(tgt);
                    if (id == 899 && kv.count(23)) S.chanColored.insert(int(num(kv[23])));
                    const int own = groups.empty() ? 0 : groups[0];
                    if (id == 1268) { char b2[64]; std::snprintf(b2, sizeof b2, "%d@%s", tgt, kv.count(63) ? kv[63].c_str() : "0"); S.spawnsOf[own].push_back(b2); }
                    if (id == 1007) { auto& a = S.alphaOf[own]; if (kv.count(35) && num(kv[35]) >= 0.5) a.first.push_back(tgt); else ++a.second; }
                    std::snprintf(line, sizeof line, "%zu\t%s\t%d\t%.3f\t%.3f\t%d\t%s\t%d\t%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", n, tn, id, x, y, tgt, gs.c_str(), spawnTrig ? 1 : 0, kv.count(87) ? int(num(kv[87])) : 0,
                                  kv.count(35) ? kv[35].c_str() : "", kv.count(10) ? kv[10].c_str() : "", kv.count(63) ? kv[63].c_str() : "", kv.count(7) ? kv[7].c_str() : "", kv.count(8) ? kv[8].c_str() : "", kv.count(9) ? kv[9].c_str() : "", kv.count(23) ? kv[23].c_str() : "");
                    trgTsv += line;
                } else {
                    ++S.blocks;
                    for (int g : groups) ++S.objsInGroup[g];
                    const double sx = kv.count(128) ? num(kv[128], 1) : 1, sy = kv.count(129) ? num(kv[129], 1) : 1;
                    if (!std::isfinite(sx) || !std::isfinite(sy)) ++S.nonFinite; else { if (sx > 2000 || sy > 2000) ++S.hugeScale; if (sx < 0.002 || sy < 0.002) ++S.tinyScale; }
                    const int z = kv.count(25) ? int(num(kv[25])) : 0; if (z == 0) ++S.zeroZ; S.zMin = std::min(S.zMin, z); S.zMax = std::max(S.zMax, z);
                    if (kv.count(21)) S.chanUsed.insert(int(num(kv[21])));
                    std::snprintf(line, sizeof line, "%zu\t%d\t%.3f\t%.3f\t%s\t%s\t%s\t%d\t%s\t%s\t%s\t%s\n", n, id, x, y, kv.count(128) ? kv[128].c_str() : "1", kv.count(129) ? kv[129].c_str() : "1",
                                  kv.count(6) ? kv[6].c_str() : "0", z, kv.count(24) ? kv[24].c_str() : "", kv.count(21) ? kv[21].c_str() : "", gs.c_str(), kv.count(43) ? kv[43].c_str() : "");
                    objTsv += line;
                }
            }
        }
        pos = end + 1;
    }
    // ---- comprobaciones ----
    auto warn = [&](const std::string& s) { S.warnings.push_back(s); };
    if (S.objects > 200000) warn("MAS de 200000 objetos (" + std::to_string(S.objects) + "): GD corta la cadena");
    if (S.bad) warn(std::to_string(S.bad) + " objetos con formato invalido (sin id o claves rotas)");
    if (S.nonFinite) warn(std::to_string(S.nonFinite) + " valores no finitos (NaN/inf) en posicion o escala");
    if (S.hugeScale) warn(std::to_string(S.hugeScale) + " objetos con escala > 2000");
    if (S.tinyScale) warn(std::to_string(S.tinyScale) + " objetos con escala < 0,002 (casi invisibles: pueden dibujarse como un punto o no verse)");
    if (S.zeroZ) warn(std::to_string(S.zeroZ) + " objetos con z-order 0 (GD los dibuja ENCIMA de los z 1..N)");
    std::vector<int> dangling; for (int t : S.targeted) if (!S.objsInGroup.count(t) && !S.trigGroups.count(t)) dangling.push_back(t);
    if (!dangling.empty()) { std::string s = std::to_string(dangling.size()) + " grupos objetivo de un trigger no tienen ningun objeto ni trigger (el trigger no hace nada): "; for (size_t i = 0; i < dangling.size() && i < 12; ++i) s += std::to_string(dangling[i]) + " "; warn(s); }
    std::vector<int> orphan; for (int g : S.ownGroups) if (!S.targeted.count(g)) orphan.push_back(g);
    if (!orphan.empty()) { std::string s = std::to_string(orphan.size()) + " grupos de triggers 'spawn triggered' que nadie lanza (esos triggers nunca se ejecutan): "; for (size_t i = 0; i < orphan.size() && i < 12; ++i) s += std::to_string(orphan[i]) + " "; warn(s); }
    std::vector<int> nocol; for (int c : S.chanUsed) if (c != 1010 && c != 1011 && c != 1004 && !S.chanColored.count(c)) nocol.push_back(c);
    if (!nocol.empty()) { std::string s = std::to_string(nocol.size()) + " canales de color usados por objetos que ningun Color trigger fija (se veran con el color por defecto): "; for (size_t i = 0; i < nocol.size() && i < 12; ++i) s += std::to_string(nocol[i]) + " "; warn(s); }
    if (S.chanUsed.count(1)) warn("algun objeto usa el canal 1 (debia quedar siempre libre)");
    size_t uncontrolled = 0; for (auto& kv : S.objsInGroup) if (!S.targeted.count(kv.first)) uncontrolled += kv.second;
    if (uncontrolled) warn("informativo: " + std::to_string(uncontrolled) + " objetos estan en grupos que ningun trigger controla (siempre visibles; normal para el fondo/capas fijas)");
    return S;
}

// ------------------------------------------------------------------ entrada / salida ----
struct Input {
    const std::string* chain = nullptr;
    const GifData* gif = nullptr;
    const GifPlan* plan = nullptr;
    const std::vector<Result>* frames = nullptr;     // un Result por frame del plan (lo que el mod calculo)
    const LayeredAnim* layered = nullptr;            // metodos 18-29
    std::string settingsText, methodName, modVersion, when, platformText, note;
    double calcSeconds = -1;
    int editorRoom = -1, threads = 0, baseGroup = 0;
    double originX = 0, originY = 0, trigX = 0, longSide = 0;
    bool channels = false, gray = false;
};
struct Output { std::string informe, objetos, triggers; std::vector<uint8_t> comparacion, detalle; };

inline Output build(const Input& in) {
    Output O; std::ostringstream R; R.setf(std::ios::fixed);
    const GifData& g = *in.gif; const GifPlan& plan = *in.plan; const int N = plan.count();
    R << "=== INFORME DE DIAGNOSTICO - GIF Importer " << in.modVersion << " ===\n" << in.when << "\n" << in.platformText << "\n\n";
    R << "## Metodo y ajustes\nmetodo: " << in.methodName << "\ncanales (sin HSV): " << (in.channels ? "si" : "no") << "   escala de grises: " << (in.gray ? "si" : "no") << "\n"
      << "origen en el editor: (" << std::setprecision(1) << in.originX << ", " << in.originY << ")  lado largo: " << in.longSide << " u  trigX: " << in.trigX << "  primer grupo: " << in.baseGroup << "\n"
      << "hilos: " << in.threads << "  tiempo de calculo: ";
    if (in.calcSeconds >= 0) R << std::setprecision(2) << in.calcSeconds << " s\n"; else R << "(reutilizado de la vista previa)\n";
    R << in.settingsText << "\n";
    R << "## GIF\narchivo: " << g.srcW << "x" << g.srcH << "  guardado a " << g.w << "x" << g.h << "  frames: " << g.frames.size() << "\nretardos (ms): ";
    for (int d : g.delaysMs) R << d << ' ';
    R << "\n## Plan de frames usados (" << N << ")\nindice_gif:duracion_s  ";
    for (int i = 0; i < N; ++i) R << plan.idx[size_t(i)] << ":" << std::setprecision(3) << plan.delay[size_t(i)] << "  ";
    R << "\nduracion del bucle: " << std::setprecision(3) << plan.total << " s\n\n";

    // ---- cadena ----
    ChainStats S = scanChain(*in.chain, O.objetos, O.triggers);
    R << "## Cadena de GD\nbytes: " << in.chain->size() << "  objetos totales: " << S.objects << "  bloques: " << S.blocks << "  triggers: " << S.triggers << "  (limite del editor 200000; sitio libre antes de colocar: " << in.editorRoom << ")\n";
    R << "por id: "; for (auto& kv : S.byId) R << kv.first << "=" << kv.second << " "; R << "\n";
    if (S.blocks) R << "caja: x " << std::setprecision(1) << S.xMin << " .. " << S.xMax << "   y " << S.yMin << " .. " << S.yMax << "   z-order " << S.zMin << " .. " << S.zMax << "\n";
    R << "grupos con objetos: " << S.objsInGroup.size() << "  grupos con triggers: " << S.trigGroups.size() << "  canales de color usados por bloques: " << S.chanUsed.size() << "  fijados por Color triggers: " << S.chanColored.size() << "\n\n";
    R << "## COMPROBACIONES automaticas\n";
    if (S.warnings.empty()) R << "(sin avisos)\n"; else for (auto& w : S.warnings) R << "- " << w << "\n";
    R << "\n";

    // ---- estructura del metodo ----
    R << "## Estructura del metodo\n";
    if (in.layered && in.layered->lsa) {
        const LsaAnim& A = *in.layered->lsa; const lsa::Sched Sc = lsa::schedule(A);
        R << "capas LSA (metodo " << (19 + A.kind) << "): capas " << A.layers.size() << "  dinamicas " << Sc.dyn.size() << "  pasos " << Sc.nSteps << "  grupos " << Sc.groups << "  triggers " << Sc.triggers
          << "  estado completo por paso: " << (Sc.full ? "si" : "no (reafirma los ultimos pasos)") << "  grupo de inicio I: " << (Sc.hasI ? "si" : "no") << "  z total " << Sc.zTotal << "\n";
        R << "canales de color del metodo 26: " << A.colorChannels << " (desde el " << A.firstChannel << ")  paleta global: " << A.palette.size() << "\nnota: " << A.note << "\n";
        R << "id  depth  siempre  formas  frames_visible\n";
        std::vector<int> cnt(A.layers.size(), 0); for (auto& v : A.vis) for (int id : v) if (id >= 0 && id < int(cnt.size())) ++cnt[size_t(id)];
        for (size_t l = 0; l < A.layers.size() && l < 800; ++l) R << l << "  " << A.layers[l].depth << "  " << (A.layers[l].always ? 1 : 0) << "  " << A.layers[l].shapes.size() << "  " << cnt[l] << "\n";
        if (A.layers.size() > 800) R << "... (" << A.layers.size() - 800 << " capas mas)\n";
        R << "capas visibles por frame (dinamicas):\n";
        for (int f = 0; f < A.frames && f < 200; ++f) { R << "f" << f << ": "; for (size_t i = 0; i < A.vis[size_t(f)].size() && i < 60; ++i) R << A.vis[size_t(f)][i] << ' '; if (A.vis[size_t(f)].size() > 60) R << "... (" << A.vis[size_t(f)].size() << ")"; R << "\n"; }
        if (A.colorChannels > 0 && !A.colorAt.empty()) { R << "color de los primeros 12 canales en los primeros 6 frames (R,G,B):\n"; for (int f = 0; f < std::min(6, A.frames); ++f) { R << "f" << f << ": "; for (int c = 0; c < std::min(12, A.colorChannels); ++c) { auto& col = A.colorAt[size_t(f)][size_t(c)]; R << int(col[0]) << "," << int(col[1]) << "," << int(col[2]) << " "; } R << "\n"; } }
    } else if (in.layered) {
        const LayeredAnim& A = *in.layered;
        R << (A.tree ? "arbol de deltas (19)" : "capas (18)") << ": rests " << A.rests.size() << "  parches con formas " << std::count_if(A.patches.begin(), A.patches.end(), [](const Result& r) { return !r.shapes.empty(); })
          << "  formas del fondo " << A.still.shapes.size() << "  zRest " << A.zRest << " zPatch " << A.zPatch << "  profundidad " << A.depthMax << "  paleta " << A.palette.size() << " (desde el canal " << A.firstChannel << ")\n";
        R << "restOf: "; for (int v : A.restOf) R << v << ' '; R << "\npatchOf: "; for (int v : A.patchOf) R << v << ' '; R << "\nmedoid: "; for (int v : A.medoid) R << v << ' ';
        if (A.tree) { R << "\nparent: "; for (int v : A.parent) R << v << ' '; }
        R << "\nnota: " << A.note << "\n";
    } else R << "metodo de imagen por frames planos: un grupo por frame; los triggers y grupos estan en triggers.tsv\n";
    R << "\n";

    // ---- lineas de tiempo derivadas de los triggers ----
    R << "## Triggers por grupo propio (lo que lanza o muestra cada grupo)\n";
    int shown = 0;
    for (auto& kv : S.spawnsOf) { if (shown++ >= 120) { R << "...\n"; break; } R << "grupo " << kv.first << " lanza: "; for (size_t i = 0; i < kv.second.size() && i < 40; ++i) R << kv.second[i] << ' '; if (kv.second.size() > 40) R << "... (" << kv.second.size() << ")"; R << "\n"; }
    shown = 0;
    for (auto& kv : S.alphaOf) { if (shown++ >= 120) { R << "...\n"; break; } R << "grupo " << kv.first << " alpha: muestra "; for (size_t i = 0; i < kv.second.first.size() && i < 20; ++i) R << kv.second.first[i] << ' '; R << " oculta " << kv.second.second << " grupos\n"; }
    R << "\n";

    // ---- comparacion con el GIF ----
    R << "## Lo que calculo el mod frente al GIF (PSNR medido ahora, mismo raster, sobre gris)\n";
    // Solo se guardan en memoria los frames que salen en las imagenes (los demas solo aportan su PSNR): con 100 frames no se acumulan cientos de MB.
    const int nf = in.frames ? std::min<int>(N, int(in.frames->size())) : 0;
    const int cols = std::min(10, std::max(1, nf));
    std::vector<int> ovIdx; for (int c = 0; c < cols; ++c) ovIdx.push_back(nf <= 1 ? 0 : int(std::llround(double(c) * double(nf - 1) / std::max(1, cols - 1))));
    const int dtIdx[2] = {0, nf / 2};
    std::map<int, Entry> keep; double sum = 0, mn = 1e9; int counted = 0;
    for (int i = 0; i < nf; ++i) {
        const Result& r = (*in.frames)[size_t(i)];
        if (r.rasterW <= 0 && r.previewImg.w <= 0) { R << "f" << i << ": sin imagen\n"; continue; }
        Entry e = makeEntry(g, plan.idx[size_t(i)], r); sum += e.psnr; mn = std::min(mn, e.psnr); ++counted;
        R << "f" << i << " (gif " << plan.idx[size_t(i)] << "): formas " << r.shapes.size() << "  PSNR " << std::setprecision(1) << e.psnr << " dB  huecos " << std::setprecision(3) << e.holes << "%" << (e.holes > 0.05 ? "  <-- AVISO: pixeles sin cubrir (se vera el fondo del nivel)" : "") << (r.note.empty() ? "" : "  | " + r.note) << "\n";
        bool want = i == dtIdx[0] || i == dtIdx[1]; for (int v : ovIdx) want |= v == i;
        if (want) keep[i] = std::move(e);
    }
    if (counted) R << "media " << std::setprecision(2) << sum / double(counted) << " dB   minimo " << mn << " dB\n";
    if (in.layered) { R << "PSNR que calculo el metodo: media " << std::setprecision(2) << in.layered->psnrAvg << "  minimo " << in.layered->psnrMin << "\n"; }
    if (!in.note.empty()) R << "nota: " << in.note << "\n";
    O.informe = R.str();

    // ---- imagenes ----
    if (!keep.empty()) {
        const int T = 120, pad = 2, cw = cols * (T + pad), ch = 3 * (T + pad);
        std::vector<uint8_t> cv(size_t(cw) * ch * 3, 30);
        for (int c = 0; c < cols; ++c) {
            auto it = keep.find(ovIdx[size_t(c)]); if (it == keep.end()) continue; const Entry& e = it->second;
            blit(cv, cw, c * (T + pad), 0, e.orig, T, T); blit(cv, cw, c * (T + pad), T + pad, e.mod, T, T); blit(cv, cw, c * (T + pad), 2 * (T + pad), e.diff, T, T);
        }
        O.comparacion = encodePng(cv, cw, ch);
        const int nr = nf > 1 ? 2 : 1, D = 380, dw = 3 * (D + pad), dh = nr * (D + pad); std::vector<uint8_t> dv(size_t(dw) * dh * 3, 30);
        for (int r = 0; r < nr; ++r) { auto it = keep.find(dtIdx[r]); if (it == keep.end()) continue; const Entry& e = it->second; blit(dv, dw, 0, r * (D + pad), e.orig, D, D); blit(dv, dw, D + pad, r * (D + pad), e.mod, D, D); blit(dv, dw, 2 * (D + pad), r * (D + pad), e.diff, D, D); }
        O.detalle = encodePng(dv, dw, dh);
    }
    return O;
}

} // namespace diag
} // namespace gdx
