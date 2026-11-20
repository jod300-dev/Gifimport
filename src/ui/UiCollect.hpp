// ============================================================================
//  Recolector del volcado de UI (parte que SI depende de Geode / cocos2d-x).
//  Recorre un arbol de CCNode y rellena gdx::ui::Node (ver engine/uidump.hpp).
//
//  Las llamadas cuya existencia depende de los bindings (fuente de un label, estado de un toggler, rect de textura, tamano del
//  framebuffer...) van dentro de `if constexpr (requires {...})`, igual que maxUsedGroup() en GifPopup.cpp: si un binding no existe
//  ese dato simplemente no sale en el volcado, pero el mod sigue compilando.
// ============================================================================
#pragma once
#include <Geode/Geode.hpp>
#include <cstdlib>
#include <typeinfo>
#include "uidump.hpp"
#if defined(__has_include)
#  if __has_include(<cxxabi.h>)
#    include <cxxabi.h>
#    define GDX_HAS_CXXABI 1
#  endif
#endif

namespace gdx::uic {
using namespace geode::prelude;

inline std::string className(CCObject* o) {
    if (!o) return "?";
    const char* raw = typeid(*o).name();
    std::string r = raw ? raw : "?";
#ifdef GDX_HAS_CXXABI
    int st = 0; char* d = abi::__cxa_demangle(raw, nullptr, nullptr, &st);
    if (st == 0 && d) r = d;
    std::free(d);
#else
    for (const char* pre : {"class ", "struct "}) if (r.rfind(pre, 0) == 0) r.erase(0, std::string(pre).size());
#endif
    return r;
}

inline bool contains(std::string const& s, const char* sub) { return s.find(sub) != std::string::npos; }

inline std::string kindOf(std::string const& c) {
    if (contains(c, "CCLabelBMFont") || contains(c, "CCLabelTTF") || contains(c, "CCLabelAtlas")) return "texto";
    if (contains(c, "MenuItemToggler")) return "toggler";
    if (contains(c, "CCMenuItem")) return "boton";
    if (contains(c, "SliderThumb") || contains(c, "Slider")) return "slider";
    if (contains(c, "Menu")) return "menu";
    if (contains(c, "Scale9Sprite")) return "fondo";
    if (contains(c, "Sprite")) return "sprite";
    if (contains(c, "CCLayer") || contains(c, "FLAlertLayer") || contains(c, "Popup")) return "capa";
    return "nodo";
}

// ---- datos opcionales (dependen de los bindings) ----
template <class T> std::string fntOf(T* l) {
    if constexpr (requires { l->getFntFile(); }) { const char* s = l->getFntFile(); return s ? s : ""; }
    else return "";
}
template <class T> void rgbaOf(T* p, ui::Node& o) {
    if constexpr (requires { p->getColor(); p->getOpacity(); }) {
        auto c = p->getColor(); o.r = int(c.r); o.g = int(c.g); o.b = int(c.b); o.op = int(p->getOpacity()); o.hasColor = true;
    }
}
template <class T> std::string spriteExtra(T* s) {
    std::string e;
    if constexpr (requires { s->getTextureRect(); }) { auto r = s->getTextureRect(); e += ui::sfmt("texRect=(%.0f,%.0f,%.0f,%.0f) ", r.origin.x, r.origin.y, r.size.width, r.size.height); }
    if constexpr (requires { s->isFlipX(); }) { if (s->isFlipX()) e += "flipX "; }
    if constexpr (requires { s->isFlipY(); }) { if (s->isFlipY()) e += "flipY "; }
    if constexpr (requires { s->getTexture()->getContentSize(); }) { if (auto* t = s->getTexture()) { auto sz = t->getContentSize(); e += ui::sfmt("textura=%.0fx%.0f ", sz.width, sz.height); } }
    return e;
}
template <class T> std::string itemExtra(T* m) {
    std::string e;
    if constexpr (requires { m->isSelected(); }) e += ui::sfmt("seleccionado=%d ", m->isSelected() ? 1 : 0);
    return e;
}
template <class T> std::string togglerExtra(T* t) {
    if (!t) return "";
    if constexpr (requires { t->isToggled(); }) return ui::sfmt("marcado=%d ", t->isToggled() ? 1 : 0);
    else return "";
}
template <class T> std::string sliderExtra(T* s) {
    if (!s) return "(no existe) ";
    if constexpr (requires { s->getThumb()->getValue(); }) { if (auto* th = s->getThumb()) return ui::sfmt("valor=%.4f ", double(th->getValue())); }
    return "";
}

// primera etiqueta que lleva dentro un nodo (texto de un boton)
inline std::string firstLabelText(CCNode* n, int depth = 0) {
    if (!n || depth > 6) return "";
    if (auto* l = typeinfo_cast<CCLabelBMFont*>(n)) { const char* s = l->getString(); return s ? s : ""; }
    if (auto* ch = n->getChildren()) for (unsigned i = 0; i < ch->count(); ++i) {
        auto t = firstLabelText(static_cast<CCNode*>(ch->objectAtIndex(i)), depth + 1);
        if (!t.empty()) return t;
    }
    return "";
}

struct Limits { size_t maxNodes = 2000; int maxChildren = 400; int maxDepth = 40; };

class Collector {
public:
    // ref: nodo cuyo espacio local se usa para la "caja" (la ventana). nullptr = coordenadas de mundo (Y hacia arriba).
    Collector(CCNode* ref, Limits lim) : m_ref(ref), m_lim(lim) {
        if (ref) { m_refH = ref->getContentSize().height; }
    }
    std::vector<ui::Node> run(CCNode* root) { walk(root, -1, 0, "0", true); return std::move(m_out); }
    bool truncated() const { return m_truncated; }

private:
    CCNode* m_ref; Limits m_lim; float m_refH = 0.f; std::vector<ui::Node> m_out; bool m_truncated = false;

    void box(CCNode* n, ui::Node& o) {
        const CCSize sz = n->getContentSize();
        const CCPoint c[4] = {n->convertToWorldSpace(ccp(0, 0)), n->convertToWorldSpace(ccp(sz.width, 0)), n->convertToWorldSpace(ccp(0, sz.height)), n->convertToWorldSpace(ccp(sz.width, sz.height))};
        const CCPoint u = n->convertToWorldSpace(ccp(1, 0)), v = n->convertToWorldSpace(ccp(0, 1));
        // escala acumulada hasta la pantalla (si la ventana esta escalada, p. ej. durante la animacion de apertura, se nota aqui)
        const float sx = std::hypot(u.x - c[0].x, u.y - c[0].y), sy = std::hypot(v.x - c[0].x, v.y - c[0].y);
        float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
        for (auto p : c) {
            if (m_ref) { p = m_ref->convertToNodeSpace(p); p.y = m_refH - p.y; }   // coordenadas de la ventana: Y desde ARRIBA
            x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y);
        }
        o.hasBox = true; o.bx0 = x0; o.by0 = y0; o.bx1 = x1; o.by1 = y1; o.wsx = sx; o.wsy = sy;
    }

    void walk(CCNode* n, int parent, int depth, std::string const& path, bool parentVisible) {
        if (!n) return;
        if (m_out.size() >= m_lim.maxNodes) { m_truncated = true; return; }
        ui::Node o;
        o.idx = int(m_out.size()); o.parent = parent; o.depth = depth; o.path = path;
        o.cls = className(n); o.kind = kindOf(o.cls); o.id = n->getID();
        o.tag = n->getTag(); o.z = n->getZOrder(); o.visible = n->isVisible(); o.effVisible = parentVisible && o.visible;
        const CCPoint p = n->getPosition(), a = n->getAnchorPoint(); const CCSize s = n->getContentSize();
        o.x = p.x; o.y = p.y; o.ax = a.x; o.ay = a.y; o.w = s.width; o.h = s.height;
        o.sx = n->getScaleX(); o.sy = n->getScaleY(); o.rot = n->getRotation(); o.skx = n->getSkewX(); o.sky = n->getSkewY();
        o.childCount = int(n->getChildrenCount());
        box(n, o);

        bool descend = true;
        if (auto* l = typeinfo_cast<CCLabelBMFont*>(n)) {
            const char* t = l->getString(); o.text = t ? t : ""; o.font = fntOf(l); rgbaOf(l, o);
            o.extra = ui::sfmt("letras=%d", o.childCount); descend = false;      // los hijos son las letras sueltas: no se listan
        } else if (auto* sp = typeinfo_cast<CCSprite*>(n)) { rgbaOf(sp, o); o.extra = spriteExtra(sp); }
        else if (auto* lc = typeinfo_cast<CCLayerColor*>(n)) { rgbaOf(lc, o); }
        if (auto* mi = typeinfo_cast<CCMenuItem*>(n)) {
            o.interactive = true; o.enabled = mi->isEnabled(); o.extra += itemExtra(mi);
            if (auto* tg = typeinfo_cast<CCMenuItemToggler*>(n)) o.extra += togglerExtra(tg);
            o.inner = firstLabelText(n);
        }
        if (auto* sl = typeinfo_cast<Slider*>(n)) o.extra += sliderExtra(sl);

        m_out.push_back(o);
        if (!descend) { m_out.back().skipped = o.childCount; return; }
        if (depth >= m_lim.maxDepth) { m_out.back().skipped = o.childCount; m_truncated = true; return; }
        auto* ch = n->getChildren();
        if (!ch) return;
        const unsigned total = ch->count(), lim = unsigned(std::min<int>(int(total), m_lim.maxChildren));
        const int me = m_out.back().idx;
        for (unsigned i = 0; i < lim; ++i) walk(static_cast<CCNode*>(ch->objectAtIndex(i)), me, depth + 1, path + "." + std::to_string(i), m_out[size_t(me)].effVisible);
        if (total > lim) { m_out[size_t(me)].skipped = int(total - lim); m_truncated = true; }
    }
};

// ---- entorno: pantalla, escala, resolucion ----
template <class D> std::string dirInfo(D* d) {
    std::string o;
    if constexpr (requires { d->getWinSize(); }) { auto s = d->getWinSize(); o += ui::sfmt("winSize (unidades de cocos) = %.2f x %.2f\n", s.width, s.height); }
    if constexpr (requires { d->getWinSizeInPixels(); }) { auto s = d->getWinSizeInPixels(); o += ui::sfmt("winSizeInPixels = %.2f x %.2f\n", s.width, s.height); }
    if constexpr (requires { d->getContentScaleFactor(); }) o += ui::sfmt("contentScaleFactor = %.4f\n", double(d->getContentScaleFactor()));
    if constexpr (requires { d->getOpenGLView()->getFrameSize(); }) { if (auto* v = d->getOpenGLView()) { auto s = v->getFrameSize(); o += ui::sfmt("framebuffer / ventana del sistema (frameSize) = %.2f x %.2f\n", s.width, s.height); } }
    if constexpr (requires { d->getOpenGLView()->getDesignResolutionSize(); }) { if (auto* v = d->getOpenGLView()) { auto s = v->getDesignResolutionSize(); o += ui::sfmt("resolucion de diseno = %.2f x %.2f\n", s.width, s.height); } }
    if constexpr (requires { d->getOpenGLView()->getScaleX(); d->getOpenGLView()->getScaleY(); }) { if (auto* v = d->getOpenGLView()) o += ui::sfmt("escala del viewport = %.4f x %.4f\n", double(v->getScaleX()), double(v->getScaleY())); }
    return o;
}

} // namespace gdx::uic
