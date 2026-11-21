// Encaje de texto en una caja (sin dependencias de Geode: GifPopup lo usa con CCLabelBMFont y tests/layout_test.cpp con una etiqueta simulada).
#pragma once
#include <algorithm>
#include <string>

namespace gdx::fit {

// CCLabelBMFont::setWidth (ancho de ajuste de linea): si el binding no lo trae, simplemente no se ajusta el ancho.
template <class T> void setWrapWidth(T* l, float w) {
    if constexpr (requires { l->setWidth(w); }) l->setWidth(w);
}

// Texto de varias lineas dentro de una caja maxW x maxH (unidades de la ventana). No se fia de como cuente GD el ancho de ajuste
// (en unidades del texto o de la ventana): prueba escalas de mayor a menor y MIDE la caja real (contentSize * escala);
// si ni a la escala minima cabe, encoge el nodo hasta que quepa. Devuelve la escala final.
template <class Label> float fitWrapped(Label* l, std::string const& text, float maxW, float maxH, float maxScale, float minScale) {
    if (!l) return maxScale;
    float s = maxScale, vw = 0.f, vh = 0.f;
    for (int i = 0; i < 60; ++i) {
        l->setScale(s);
        setWrapWidth(l, maxW);
        l->setString(text.c_str());
        const auto c = l->getContentSize();
        vw = c.width * s; vh = c.height * s;
        if ((vw <= maxW + 0.5f && vh <= maxH + 0.5f) || s <= minScale + 1e-4f) break;
        s = std::max(minScale, s - 0.02f);
    }
    if (vw > maxW + 0.5f || vh > maxH + 0.5f) {
        s *= std::min(maxW / std::max(vw, 0.01f), maxH / std::max(vh, 0.01f));
        l->setScale(s);
    }
    return s;
}

} // namespace gdx::fit
