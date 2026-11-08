// ============================================================================
//  gdx :: Hoja de frames (modo de diagnostico, SIN triggers)
//  Coloca los frames lado a lado como objetos estaticos. Sirve para ver en el juego la calidad REAL de cada frame, sin
//  que los triggers ni el apilado de frames influyan. Si la hoja se ve bien y la animacion no, el fallo esta en los
//  triggers/ordenes; si la hoja se ve mal, esta en la conversion o en como GD dibuja los objetos.
//  Sin dependencias de Geode.
// ============================================================================
#pragma once
#include "chanframes.hpp"

namespace gdx {

// frames: un Result por frame (con z unico/compacto). palette vacia = HSV; si no, canales palette[i] -> firstChannel + i.
inline std::string buildFrameSheet(std::vector<Result> frames, const GifExportParams& p,
                                   const std::vector<std::array<uint8_t, 3>>& palette, int firstChannel,
                                   bool onlyFirst, std::string* errmsg = nullptr) {
    auto fail = [&](const char* m) { if (errmsg) *errmsg = m; return std::string(); };
    if (frames.empty() || frames[0].shapes.empty()) return fail("No hay frames que colocar");
    const bool useCh = !palette.empty();
    if (useCh && (firstChannel < 2 || firstChannel + int(palette.size()) - 1 > 999)) return fail("No caben los canales de color (2..999)");
    std::map<uint32_t, int> chOf;
    for (size_t i = 0; i < palette.size(); ++i) chOf[lay::colKey(palette[i][0], palette[i][1], palette[i][2])] = firstChannel + int(i);
    const int N = onlyFirst ? 1 : int(frames.size());
    const GdSize sz = gdImageSize(frames[0], p.eo);
    const int cols = onlyFirst ? 1 : std::clamp(int(std::ceil(std::sqrt(double(N)))), 1, 8);
    const double gap = sz.w * 0.12;
    std::string out; size_t total = 0;
    for (int i = 0; i < N; ++i) total += frames[size_t(i)].shapes.size();
    out.reserve(total * 100);
    for (int i = 0; i < N; ++i) {
        Result& r = frames[size_t(i)];
        if (r.shapes.empty()) continue;
        GdExportOptions eo = p.eo;
        eo.originX = p.eo.originX + (i % cols) * (sz.w + gap);
        eo.originY = p.eo.originY - (i / cols) * (sz.h + gap);
        eo.groupStr.clear();                                      // sin grupos: objetos estaticos
        eo.monoFastPath = false;
        if (useCh) {
            r.colored = true;
            ChannelMap cm = buildChannelMap(r);
            eo.channels.assign(cm.colors.size(), 0);
            for (size_t k = 0; k < cm.colors.size(); ++k) {
                auto it = chOf.find(lay::colKey(cm.colors[k][0], cm.colors[k][1], cm.colors[k][2]));
                if (it == chOf.end()) return fail("Color fuera de la paleta");
                eo.channels[k] = it->second;
            }
        }
        std::string s = toGdString(r, eo);
        if (!s.empty()) { out += s; out += ';'; }
    }
    if (useCh) appendColorTriggers(out, palette, firstChannel, p.trigX, p.eo.originY + sz.h * 0.5 + 60.0);
    if (!out.empty() && out.back() == ';') out.pop_back();
    return out;
}

} // namespace gdx
