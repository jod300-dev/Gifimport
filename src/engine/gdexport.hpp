// ============================================================================
//  Convierte un Result en la cadena de objetos de LevelEditorLayer::createObjectsFromString
//  (formato clave,valor;...).
//
//  Objeto base 3092 (cuadrado "pixel" 1x1 u, escalable con las claves 128/129).
//  Color: HSV absoluto sobre un canal base (1010 = negro).
//  El z-order (clave 25) se emite SIEMPRE y los objetos pueden llevar grupo (clave 57).
// ============================================================================
#pragma once
#include <string>
#include "core.hpp"

namespace gdx {

struct GdExportOptions {
    double originX = 0, originY = 0;   // centro de la imagen en el editor (unidades GD)
    double longSide = 600;             // lado largo de la imagen en unidades GD (30 u = 1 bloque)
    int colorChannel = 1010;           // canal de color base para HSV (1010 = negro)
    int objectId = 3092;               // objeto cuadrado 1x1 u
    double bleed = 0.15;               // solape (u) para evitar costuras entre rectangulos
    int zBase = 0;                     // z-order base
    std::string groupStr;              // grupo(s) de los objetos (clave 57, "12" o "12.13"). Vacio = sin grupo
};

namespace detail {
inline void appendNum(std::string& s, double v, int decimals) {
    char buf[40];
    int n = std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    while (n > 0 && buf[n - 1] == '0') --n;
    if (n > 0 && buf[n - 1] == '.') --n;
    if (n == 0 || (n == 1 && buf[0] == '-')) { s += '0'; return; }
    if (n == 2 && buf[0] == '-' && buf[1] == '0') { s += '0'; return; }
    s.append(buf, size_t(n));
}
} // namespace detail

// RGB -> cadena HSV de GD "h a s a v a 1 a 1" (saturacion y brillo absolutos).
inline void appendGdHsv(std::string& s, uint8_t R, uint8_t G, uint8_t B) {
    float r = R / 255.f, g = G / 255.f, b = B / 255.f;
    float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b)), d = mx - mn;
    float hue = 0, sat = 0, val = mx;
    if (d > 0) {
        sat = d / mx;
        if (mx == r) hue = 60.f * ((g - b) / d);
        else if (mx == g) hue = 60.f * (((b - r) / d) + 2.f);
        else hue = 60.f * (((r - g) / d) + 4.f);
    }
    if (hue < 0) hue += 360.f;
    if (hue >= 180.f) hue -= 360.f;                    // GD usa -180..180
    if (std::abs(hue) < 0.005f) hue = 0.05f;           // el tono 0 exacto da problemas en GD
    detail::appendNum(s, hue, 2); s += 'a';
    detail::appendNum(s, sat, 3); s += 'a';
    detail::appendNum(s, val, 3); s += "a1a1";
}

struct GdSize { double w, h; };
inline GdSize gdImageSize(const Result& r, const GdExportOptions& o) {
    double a = r.aspect > 0 ? r.aspect : 1.0;
    return a >= 1.0 ? GdSize{o.longSide, o.longSide / a} : GdSize{o.longSide * a, o.longSide};
}

inline std::string toGdString(const Result& r, const GdExportOptions& o) {
    std::string out;
    if (r.shapes.empty() || r.rasterW <= 0 || r.rasterH <= 0) return out;
    out.reserve(r.shapes.size() * 100);
    GdSize sz = gdImageSize(r, o);
    const double ux = sz.w / r.rasterW, uy = sz.h / r.rasterH;
    for (const Shape& s : r.shapes) {
        double x = o.originX + (s.cx - r.rasterW * 0.5) * ux;
        double y = o.originY - (s.cy - r.rasterH * 0.5) * uy;
        double sx = s.w * ux + o.bleed, sy = s.h * uy + o.bleed;
        out += "1,"; out += std::to_string(o.objectId);
        out += ",2,"; detail::appendNum(out, x, 3);
        out += ",3,"; detail::appendNum(out, y, 3);
        out += ",21,"; out += std::to_string(o.colorChannel);
        out += ",41,1,43,"; appendGdHsv(out, s.r, s.g, s.b);
        out += ",25,"; out += std::to_string(o.zBase + s.z);                  // SIEMPRE explicito
        if (!o.groupStr.empty()) { out += ",57,"; out += o.groupStr; }
        out += ",128,"; detail::appendNum(out, sx, 3);
        out += ",129,"; detail::appendNum(out, sy, 3);
        out += ';';
    }
    if (!out.empty() && out.back() == ';') out.pop_back();   // createObjectsFromString no necesita el ultimo ';'
    return out;
}

} // namespace gdx
