// ============================================================================
//  Volcado de la interfaz (ajuste "Diagnostico de la interfaz"): modulo SIN dependencias de Geode.
//  El recolector (src/ui/UiCollect.hpp) recorre los nodos de cocos2d y rellena una lista de `Node`; aqui se da formato
//  (arbol literal, tabla de textos, tabla de botones) y se hacen las COMPROBACIONES AUTOMATICAS (textos solapados,
//  elementos fuera de la ventana, botones tapados, tamanos raros...) para poder arreglar la UI solo con el volcado.
//
//  Coordenadas de la "caja" (bx0,by0)-(bx1,by1): las mismas que usa el codigo de la ventana al colocar cosas, es decir,
//  X desde la izquierda e Y desde ARRIBA de la ventana (unidades de la ventana, ya descontada su escala).
// ============================================================================
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace gdx::ui {

struct Node {
    int idx = -1, parent = -1, depth = 0;
    std::string path;            // "0.3.2": posicion en el arbol (hijo 3 del hijo 0 ...)
    std::string cls;             // clase C++ (desmangled)
    std::string kind;            // texto | boton | toggler | menu | slider | sprite | fondo | capa | nodo
    std::string id;              // setID()
    std::string text, font;      // etiquetas
    std::string extra;           // datos propios del tipo (enabled, toggled, rect de textura, ...)
    std::string inner;           // botones: texto de la primera etiqueta que lleva dentro
    int tag = 0, z = 0;
    bool visible = true;         // la bandera del propio nodo
    bool effVisible = true;      // visible Y todos sus padres visibles
    float x = 0, y = 0, ax = 0, ay = 0, w = 0, h = 0, sx = 1, sy = 1, rot = 0, skx = 0, sky = 0;
    bool hasBox = false;
    float bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;   // caja en coordenadas de la ventana (Y hacia abajo)
    float wsx = 1, wsy = 1;                      // escala acumulada hasta la pantalla
    bool hasColor = false; int r = 255, g = 255, b = 255, op = 255;
    bool interactive = false, enabled = true;    // items de menu
    int childCount = 0, skipped = 0;             // hijos reales / hijos que no se listan
};

inline std::string esc(std::string const& s) {
    std::string o; o.reserve(s.size() + 4);
    for (char c : s) { if (c == '\n') o += "\\n"; else if (c == '\r') {} else if (c == '"') o += "\\\""; else o += c; }
    return o;
}
inline std::string sfmt(const char* f, ...) {
    char buf[1024]; va_list ap; va_start(ap, f); std::vsnprintf(buf, sizeof buf, f, ap); va_end(ap); return buf;
}

inline float bw(Node const& n) { return n.bx1 - n.bx0; }
inline float bh(Node const& n) { return n.by1 - n.by0; }
inline bool isText(Node const& n) { return n.kind == "texto"; }
inline bool isBtn(Node const& n) { return n.kind == "boton" || n.kind == "toggler"; }
inline bool shown(Node const& n) { return n.effVisible && n.hasBox && n.op > 0; }
inline bool isAnc(Node const& a, Node const& b) { return b.path.size() > a.path.size() && b.path.compare(0, a.path.size(), a.path) == 0 && b.path[a.path.size()] == '.'; }
inline float interArea(Node const& a, Node const& b, float* iw = nullptr, float* ih = nullptr) {
    const float w = std::min(a.bx1, b.bx1) - std::max(a.bx0, b.bx0), h = std::min(a.by1, b.by1) - std::max(a.by0, b.by0);
    if (iw) *iw = std::max(0.f, w);
    if (ih) *ih = std::max(0.f, h);
    return (w > 0 && h > 0) ? w * h : 0.f;
}

// Una linea por nodo, con sangria por profundidad. TODO lo que se sabe del nodo.
inline std::string formatTree(std::vector<Node> const& ns, bool worldBox = false) {
    std::string o;
    for (auto const& n : ns) {
        o.append(size_t(n.depth) * 2, ' ');
        o += sfmt("[%s] %s <%s>", n.path.c_str(), n.cls.c_str(), n.kind.c_str());
        if (!n.id.empty()) o += " id=\"" + esc(n.id) + "\"";
        o += sfmt(" z=%d tag=%d vis=%d/%d pos=(%.2f,%.2f) anc=(%.2f,%.2f) tam=(%.2f,%.2f) esc=(%.3f,%.3f)", n.z, n.tag, n.visible ? 1 : 0, n.effVisible ? 1 : 0, n.x, n.y, n.ax, n.ay, n.w, n.h, n.sx, n.sy);
        if (std::fabs(n.rot) > 1e-4f) o += sfmt(" giro=%.2f", n.rot);
        if (std::fabs(n.skx) > 1e-4f || std::fabs(n.sky) > 1e-4f) o += sfmt(" skew=(%.2f,%.2f)", n.skx, n.sky);
        if (n.hasBox) o += sfmt(" %s=(%.1f,%.1f)-(%.1f,%.1f)[%.1fx%.1f] escPantalla=(%.3f,%.3f)", worldBox ? "mundo" : "ventana", n.bx0, n.by0, n.bx1, n.by1, bw(n), bh(n), n.wsx, n.wsy);
        if (n.hasColor) o += sfmt(" color=(%d,%d,%d) op=%d", n.r, n.g, n.b, n.op);
        if (!n.font.empty()) o += " fuente=" + n.font;
        if (isText(n)) o += " texto=\"" + esc(n.text) + "\"";
        if (n.interactive) o += sfmt(" activo=%d", n.enabled ? 1 : 0);
        if (!n.extra.empty()) o += " | " + n.extra;
        if (n.childCount > 0) o += sfmt(" | hijos=%d", n.childCount);
        if (n.skipped > 0) o += sfmt(" (%d sin listar)", n.skipped);
        o += "\n";
    }
    return o;
}

// Tabla de TODOS los textos visibles: lo mas util para detectar textos cortados, solapados o con fuente/escala rara.
inline std::string formatTexts(std::vector<Node> const& ns) {
    std::string o = "ruta\tvisible\ttexto\tfuente\tcaja(ventana)\ttamano_en_pantalla\tescala_pantalla\tcolor\n";
    for (auto const& n : ns) if (isText(n)) {
        o += sfmt("%s\t%d\t\"%s\"\t%s\t(%.1f,%.1f)-(%.1f,%.1f)\t%.1fx%.1f\t%.3f\t(%d,%d,%d)/%d\n", n.path.c_str(), n.effVisible ? 1 : 0, esc(n.text).c_str(), n.font.c_str(),
                 n.bx0, n.by0, n.bx1, n.by1, bw(n), bh(n), n.wsx, n.r, n.g, n.b, n.op);
    }
    return o;
}

inline std::string formatButtons(std::vector<Node> const& ns) {
    std::string o = "ruta\tclase\tid\tvisible\tactivo\ttexto_interior\tcaja(ventana)\ttamano\textra\n";
    for (auto const& n : ns) if (n.interactive || isBtn(n)) {
        o += sfmt("%s\t%s\t%s\t%d\t%d\t\"%s\"\t(%.1f,%.1f)-(%.1f,%.1f)\t%.1fx%.1f\t%s\n", n.path.c_str(), n.cls.c_str(), n.id.c_str(), n.effVisible ? 1 : 0, n.enabled ? 1 : 0, esc(n.inner).c_str(),
                 n.bx0, n.by0, n.bx1, n.by1, bw(n), bh(n), n.extra.c_str());
    }
    return o;
}

// Comprobaciones automaticas sobre la caja de la ventana (rootW x rootH). Cada linea empieza por PROBLEMA / AVISO / INFO.
inline std::vector<std::string> analyze(std::vector<Node> const& ns, float rootW, float rootH) {
    std::vector<std::string> out;
    auto name = [](Node const& n) { return sfmt("[%s] %s%s%s", n.path.c_str(), n.cls.c_str(), n.id.empty() ? "" : " id=", n.id.c_str()); };
    auto label = [&](Node const& n) { return name(n) + (isText(n) ? " \"" + esc(n.text) + "\"" : (n.inner.empty() ? std::string() : " \"" + esc(n.inner) + "\"")); };
    const size_t N = ns.size();
    for (size_t i = 0; i < N; ++i) {
        auto const& a = ns[i];
        if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.w) || !std::isfinite(a.h) || !std::isfinite(a.sx) || !std::isfinite(a.sy) ||
            (a.hasBox && (!std::isfinite(a.bx0) || !std::isfinite(a.by0) || !std::isfinite(a.bx1) || !std::isfinite(a.by1))))
            out.push_back("PROBLEMA valor no finito (NaN/inf) en " + name(a));
        const bool inToggler = a.parent >= 0 && size_t(a.parent) < N && ns[size_t(a.parent)].kind == "toggler";   // el check lleva dentro dos botones y siempre oculta uno: es normal
        if (a.interactive && a.enabled && !a.effVisible && !inToggler) out.push_back("AVISO boton ACTIVO pero oculto (recibe toques sin verse): " + label(a));
        if (!(isText(a) || isBtn(a)) || !shown(a)) continue;
        if (isText(a) && a.text.empty()) continue;
        if (bw(a) <= 0.01f || bh(a) <= 0.01f) out.push_back("PROBLEMA tamano cero: " + label(a));
        if (isText(a) && bh(a) < 6.f && bh(a) > 0.01f) out.push_back(sfmt("AVISO texto muy pequeno (alto %.1f px): ", bh(a)) + label(a));
        if (isBtn(a) && std::min(bw(a), bh(a)) < 20.f && bw(a) > 0.01f) out.push_back(sfmt("INFO boton pequeno para tocar (%.1fx%.1f): ", bw(a), bh(a)) + label(a));
        const float m = 1.f;   // margen de tolerancia
        const float over = std::max(std::max(-a.bx0, -a.by0), std::max(a.bx1 - rootW, a.by1 - rootH));
        if (over > m) {
            if (isText(a))
                out.push_back(sfmt("PROBLEMA fuera de la ventana (%.1f,%.1f)-(%.1f,%.1f) y la ventana mide %.0fx%.0f (sobresale %.1f px): ", a.bx0, a.by0, a.bx1, a.by1, rootW, rootH, over) + label(a));
            else if (over <= 20.f)   // el boton de cerrar de los popups de Geode sobresale a proposito
                out.push_back(sfmt("INFO boton sobresale %.1f px de la ventana (normal en el boton de cerrar): ", over) + label(a));
            else
                out.push_back(sfmt("AVISO boton fuera de la ventana (%.1f,%.1f)-(%.1f,%.1f), sobresale %.1f px: ", a.bx0, a.by0, a.bx1, a.by1, over) + label(a));
        }
    }
    for (size_t i = 0; i < N; ++i) for (size_t j = i + 1; j < N; ++j) {
        auto const& a = ns[i]; auto const& b = ns[j];
        if (!(isText(a) || isBtn(a)) || !(isText(b) || isBtn(b)) || !shown(a) || !shown(b)) continue;
        if ((isText(a) && a.text.empty()) || (isText(b) && b.text.empty())) continue;
        if (isAnc(a, b) || isAnc(b, a)) continue;                                   // una etiqueta dentro de su boton no cuenta
        float iw = 0, ih = 0; const float ia = interArea(a, b, &iw, &ih);
        if (ia <= 0) continue;
        const float minA = std::max(0.01f, std::min(bw(a) * bh(a), bw(b) * bh(b))), ratio = ia / minA;
        const char* what = (isText(a) && isText(b)) ? "textos" : (isBtn(a) && isBtn(b)) ? "botones" : "texto y boton";
        if (ratio >= 0.30f) out.push_back(sfmt("PROBLEMA %s SOLAPADOS (%.0f%% de la caja menor, zona %.1fx%.1f): ", what, ratio * 100.f, iw, ih) + label(a) + "  <->  " + label(b));
        else if (ratio >= 0.08f && !(isBtn(a) && isBtn(b))) out.push_back(sfmt("AVISO %s se rozan (%.0f%%, zona %.1fx%.1f; las cajas de texto incluyen aire: puede verse bien): ", what, ratio * 100.f, iw, ih) + label(a) + "  <->  " + label(b));
    }
    return out;
}

} // namespace gdx::ui
