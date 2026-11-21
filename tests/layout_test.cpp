// Layout de la ventana del importador (src/ui/Layout.hpp + src/ui/FitText.hpp), sin Geode:
//  A) las descripciones y nombres REALES de los 29 metodos caben en su caja con una etiqueta simulada, tanto si GD cuenta el ancho de ajuste
//     en unidades de la ventana como en unidades del texto (no sabemos cual; fitWrapped mide la caja real y no depende de ello);
//  B) la geometria (fila de botones, boton de info, columnas, cajas de texto) no solapa nada y esta alineada. Los anchos de texto de los botones
//     y etiquetas son los MEDIDOS en un volcado real de la ventana (ui_20261120_140836_manual.txt: GD 2.2081, Windows).
//   g++ -O2 -std=c++23 -pthread -Iinclude -Isrc/engine -Isrc/ui -Itests tests/layout_test.cpp -o layout_test && ./layout_test
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "dispatch.hpp"
#include "Layout.hpp"
#include "FitText.hpp"
#include "uidump.hpp"
#include <cstdio>
#include <iostream>
#include <sstream>
namespace lay = gdx::layout;
using gdx::ui::Node;

// ---- etiqueta simulada: ajuste de linea voraz por palabras ----
struct MockLabel {
    int semantics;                       // 0: el ancho de ajuste esta en unidades de la ventana (visual); 1: en unidades del texto
    float charW, lineH, scale = 1.f, wrapW = 1e9f; std::string str;
    struct Sz { float width, height; };
    void setScale(float s) { scale = s; }
    void setWidth(float w) { wrapW = w; }
    void setString(const char* t) { str = t; }
    Sz getContentSize() const {
        const float limit = semantics == 0 ? wrapW / scale : wrapW;
        std::istringstream in(str); std::string w; float line = 0, widest = 0; int lines = 1;
        while (in >> w) {
            const float ww = float(w.size()) * charW, sp = charW;
            if (line > 0 && line + sp + ww > limit) { widest = std::max(widest, line); line = ww; ++lines; } else line += (line > 0 ? sp : 0) + ww;
        }
        widest = std::max(widest, line);
        return {widest, float(lines) * lineH};
    }
};

static Node box(const char* path, const char* kind, float x0, float y0, float x1, float y1, const char* text = "") {
    Node n; n.path = path; n.kind = kind; n.cls = std::string("cocos2d::") + kind; n.text = text; n.hasBox = true; n.bx0 = x0; n.by0 = y0; n.bx1 = x1; n.by1 = y1;
    n.x = x0; n.y = y0; n.w = x1 - x0; n.h = y1 - y0; n.interactive = std::string(kind) == "boton" || std::string(kind) == "toggler"; n.depth = 0; return n;
}
static int bad = 0;
#define CHECK(c, msg) do { if (!(c)) { std::cout << "FALLO: " << msg << "\n"; ++bad; } } while (0)

int main() {
    // ---------- A) textos reales ----------
    std::vector<std::pair<std::string, std::string>> items;   // nombre, descripcion
    for (int i = 0; i < int(gdx::Method::Ascii); ++i) { auto const& m = gdx::methodInfo(gdx::Method(i)); items.push_back({m.name, m.desc}); }
    items.push_back({gdx::layeredName(), gdx::layeredDesc()});
    items.push_back({gdx::treeName(), gdx::treeDesc()});
    for (int k = 1; k <= 10; ++k) items.push_back({gdx::lsa::methodName(k), gdx::lsa::methodDesc(k)});
    CHECK(items.size() == 29, "debia haber 29 metodos y hay " << items.size());
    float worstDesc = 9.f, worstReal = 9.f; size_t longest = 0;
    for (int sem = 0; sem < 2; ++sem) for (auto const& it : items) {
        MockLabel d{sem, 7.8f, 18.f};
        const float s = gdx::fit::fitWrapped(&d, it.second, lay::kDescMaxW, lay::kDescMaxH, 0.6f, 0.3f);
        const auto c = d.getContentSize();
        CHECK(c.width * s <= lay::kDescMaxW + 0.6f && c.height * s <= lay::kDescMaxH + 0.6f, "descripcion fuera de la caja (" << it.first << "): " << c.width * s << "x" << c.height * s);
        worstDesc = std::min(worstDesc, s); longest = std::max(longest, it.second.size());
        // semantica 0 = la real en GD 2.2081 (el volcado midio 512 de contenido con ancho de ajuste 380: no puede ser unidades del texto): debe leerse bien
        if (sem == 0) CHECK(s >= 0.34f, "descripcion demasiado pequena (escala " << s << "): " << it.first);
        if (sem == 0) worstReal = std::min(worstReal, s);
    }
    std::printf("descripciones: %zu, la mas larga %zu caracteres; escala minima necesaria %.2f con el ajuste real de GD (%.2f en la hipotesis contraria; maxima 0.60)\n", items.size(), longest, worstReal, worstDesc);
    // nombres: limitLabelWidth(kTitleMaxW, 0.5, 0.2) con bigFont (18.1 unidades por caracter segun el volcado)
    for (auto const& it : items) {
        const float w = float(it.first.size()) * 18.1f, s = std::max(0.2f, std::min(0.5f, lay::kTitleMaxW / w));
        CHECK(w * s <= lay::kTitleMaxW + 0.5f, "nombre demasiado largo para su hueco (" << it.first << ")");
        CHECK(s >= 0.25f, "nombre de metodo demasiado pequeno (" << it.first << ", escala " << s << ")");
    }
    // nota (izquierda): caja 150 x 22
    for (const char* note : {"El GIF tiene 12 frames; se usaran 35.", "Se recorta: el tope total de objetos de los ajustes es 40000 y se necesitan 52000 (baja los objetos por frame).", ""}) {
        MockLabel n{0, 7.8f, 18.f}; const float s = gdx::fit::fitWrapped(&n, note, lay::kNoteMaxW, lay::kNoteMaxH, 0.6f, 0.4f); const auto c = n.getContentSize();
        CHECK(c.width * s <= lay::kNoteMaxW + 0.6f && c.height * s <= lay::kNoteMaxH + 0.6f, "nota fuera de su caja: " << c.width * s << "x" << c.height * s);
    }

    // ---------- B) geometria ----------
    const float kPad = 16.f;                                           // relleno del ButtonSprite (medido: ancho del boton = ancho del texto + 16)
    auto btnW = [&](float textWAt1) { return textWAt1 * lay::kBtnTextScale + kPad; };   // anchos del texto a escala 1: Ajustes 138, UI 40, GIF 62, Colocar 147 (volcado)
    const float wSet = btnW(138.f), wUi = btnW(40.f), wGif = btnW(62.f), wPlace = btnW(147.f);
    const float cy = lay::kBtnRowY, h = lay::kBtnH;
    const float setR = lay::kLeftL + wSet, uiL = setR + lay::kBtnGap, uiR = uiL + wUi, placeL = lay::kRightR - wPlace, gifR = placeL - lay::kBtnGap, gifL = gifR - wGif;
    std::vector<Node> ns;
    auto add = [&](Node n) { n.path = "0." + std::to_string(ns.size()); n.idx = int(ns.size()); ns.push_back(n); };
    Node bSet = box("", "boton", lay::kLeftL, cy - h / 2, setR, cy + h / 2); bSet.inner = "Ajustes";
    Node bUi = box("", "boton", uiL, cy - h / 2, uiR, cy + h / 2); bUi.inner = "UI";
    Node bGif = box("", "boton", gifL, cy - h / 2, gifR, cy + h / 2); bGif.inner = "GIF";
    Node bPlace = box("", "boton", placeL, cy - h / 2, lay::kRightR, cy + h / 2); bPlace.inner = "Colocar";
    Node info = box("", "boton", lay::kInfoCx - lay::kInfoSize / 2, lay::kInfoCy - lay::kInfoSize / 2, lay::kInfoCx + lay::kInfoSize / 2, lay::kInfoCy + lay::kInfoSize / 2); info.inner = "(i)";
    add(bSet); add(bUi); add(bGif); add(bPlace); add(info);
    add(box("", "texto", 126.1f, 9.9f, 303.9f, 30.1f, "GIF Importer (beta)"));                                       // titulo (volcado)
    add(box("", "boton", lay::kArrowCxL - lay::kArrowW / 2, lay::kArrowCy - 10, lay::kArrowCxL + lay::kArrowW / 2, lay::kArrowCy + 10));     // flechas 16.5 x 20 (volcado)
    add(box("", "boton", lay::kArrowCxR - lay::kArrowW / 2, lay::kArrowCy - 10, lay::kArrowCxR + lay::kArrowW / 2, lay::kArrowCy + 10));
    add(box("", "texto", lay::kRightCx - lay::kTitleMaxW / 2, lay::kArrowCy - 7, lay::kRightCx + lay::kTitleMaxW / 2, lay::kArrowCy + 7, "nombre del metodo (ancho maximo)"));
    add(box("", "texto", lay::kRightL, lay::kDescTop, lay::kRightR, lay::kDescBottom, "descripcion (caja entera)"));
    add(box("", "texto", lay::kRightL, 102.1f, lay::kRightL + 111.2f, 113.9f, "Objetos por frame"));                // etiquetas medidas en el volcado
    add(box("", "texto", lay::kRightR - 113.6f, 140.1f, lay::kRightR, 151.9f, "210 u (7.0 bloques)"));
    add(box("", "texto", lay::kRightL, 140.1f, lay::kRightL + 67.7f, 151.9f, "Lado largo"));
    add(box("", "texto", lay::kRightL, 178.1f, lay::kRightL + 93.8f, 189.9f, "Frames maximos"));
    add(box("", "toggler", 156.5f, 206.5f, 191.5f, 241.5f)); add(box("", "texto", 186.f, 219.1f, 268.9f, 228.9f, "Escala de grises"));
    add(box("", "toggler", 282.5f, 206.5f, 317.5f, 241.5f)); add(box("", "texto", 312.f, 219.1f, 398.5f, 228.9f, "Canales (sin HSV)"));
    add(box("", "texto", lay::kLeftCx - 75.f, lay::kStatACy - 7.65f, lay::kLeftCx + 75.f, lay::kStatACy + 7.65f, "Elige un GIF animado"));
    add(box("", "texto", lay::kLeftCx - 75.f, lay::kStatBCy - 6.75f, lay::kLeftCx + 75.f, lay::kStatBCy + 6.75f, "estado B (hasta 150 x 13.5)"));
    add(box("", "texto", lay::kLeftCx - lay::kNoteMaxW / 2, lay::kNoteCy - lay::kNoteMaxH / 2, lay::kLeftCx + lay::kNoteMaxW / 2, lay::kNoteCy + lay::kNoteMaxH / 2, "nota (caja entera)"));
    // el boton de cerrar de Geode (volcado): (-15.4,-16.2)-(21.4,22.2), sobresale a proposito; no se mete en el analisis
    auto issues = gdx::ui::analyze(ns, lay::kW, lay::kH);
    for (auto const& l : issues) { std::cout << l << "\n"; if (l.rfind("INFO", 0) != 0) ++bad; }   // INFO (flechas de 16.5 x 20) no es un fallo

    CHECK(bSet.by0 == bUi.by0 && bUi.by0 == bGif.by0 && bGif.by0 == bPlace.by0 && bSet.by1 == bPlace.by1, "los 4 botones de la fila no comparten Y y altura");
    CHECK(bSet.bx0 == lay::kLeftL && lay::kLeftL == 14.f, "Ajustes no esta pegado al borde izquierdo de la vista previa");
    CHECK(bPlace.bx1 == lay::kRightR && lay::kRightR == 416.f, "Colocar no esta pegado al borde derecho de la columna");
    CHECK(std::fabs((bPlace.bx0 - bGif.bx1) - lay::kBtnGap) < 1e-3f && std::fabs((bUi.bx0 - bSet.bx1) - lay::kBtnGap) < 1e-3f, "huecos entre botones distintos");
    CHECK(uiR + 12.f < gifL, "UI y GIF demasiado juntos (" << gifL - uiR << " px)");
    CHECK(bSet.by1 <= lay::kH - 8.f && bSet.by1 > lay::kH - 12.f, "margen inferior de la fila (" << lay::kH - bSet.by1 << ")");
    CHECK(info.bx1 <= lay::kW - 5.f && info.by0 >= 5.f && info.bx0 > 304.f + 20.f, "el boton de info no esta bien en la esquina superior derecha");
    CHECK(std::fabs((lay::kW - info.bx1) - info.by0) < 1e-3f, "el boton de info no esta a la misma distancia del borde derecho y del superior");
    CHECK(lay::kNoteCy + lay::kNoteMaxH / 2 < bSet.by0 - 1.5f, "la nota llega a la fila de botones");
    CHECK(lay::kNoteCy - lay::kNoteMaxH / 2 > lay::kStatBCy + 6.75f, "la nota pisa el estado B");
    CHECK(lay::kDescTop >= lay::kArrowCy + 7.f + 3.f && lay::kDescBottom <= 102.1f - 3.f, "la descripcion no cabe entre el nombre del metodo y los sliders");
    CHECK(lay::kRightCx - lay::kTitleMaxW / 2 - (lay::kArrowCxL + lay::kArrowW / 2) >= lay::kTitleGap - 1e-3f, "el nombre del metodo toca la flecha");
    CHECK(lay::kRightL - lay::kLeftR >= 12.f, "las columnas estan demasiado juntas");
    std::printf("fila de botones: Ajustes %.1f..%.1f | UI %.1f..%.1f | GIF %.1f..%.1f | Colocar %.1f..%.1f   (y %.1f..%.1f)\n", bSet.bx0, bSet.bx1, bUi.bx0, bUi.bx1, bGif.bx0, bGif.bx1, bPlace.bx0, bPlace.bx1, bSet.by0, bSet.by1);
    std::cout << (bad ? "HAY FALLOS\n" : "TODO OK\n"); return bad ? 1 : 0;
}
