// Prueba del recolector de UI con un mock de cocos2d (tests/mock): reproduce la disposicion de GifPopup::init y comprueba que las
// cajas salen en coordenadas de la VENTANA (X desde la izquierda, Y desde arriba) iguales a las del codigo, y que el analisis detecta solapes.
//   g++ -std=c++23 -Itests/mock -Isrc/ui -Isrc/engine tests/uicollect_test.cpp -o uicollect_test && ./uicollect_test
#include "UiCollect.hpp"
#include <iostream>
using namespace geode::prelude;
template <class T> T* mkn(float x, float yFromTop, float w, float h, CCNode* parent, float H, float ax = 0.5f, float ay = 0.5f) {   // igual que put(): Anchor::TopLeft + ccp(x, -y)
    auto* n = new T(); n->setContentSize({w, h}); n->setAnchorPoint({ax, ay}); n->setPosition({x, H - yFromTop}); parent->addChild(n); return n; }
int main() {
    const float W = 430, H = 290;
    auto* scene = new CCNode(); scene->setContentSize({569, 320}); scene->setAnchorPoint({0, 0});
    auto* popup = new CCNode(); popup->setContentSize({569, 320}); popup->setAnchorPoint({0, 0}); scene->addChild(popup);
    auto* main = new CCNode(); main->setContentSize({W, H}); main->setAnchorPoint({0.5f, 0.5f}); main->setPosition({284.5f, 160}); popup->addChild(main);
    auto* menu = new CCMenu(); menu->setContentSize({W, H}); menu->setAnchorPoint({0, 0}); menu->setPosition({0, 0}); main->addChild(menu);
    // etiquetas como en init(): statA en (84,194) y statB en (84,207), ancla 0.5; "Objetos por frame" en (168,108) ancla izquierda
    auto* a = mkn<CCLabelBMFont>(84, 194, 150, 17, main, H); a->str = "Elige un GIF animado"; a->fnt = "chatFont.fnt"; a->setID("statA");
    auto* b = mkn<CCLabelBMFont>(84, 207, 150, 15, main, H); b->str = "(sin datos)"; b->fnt = "chatFont.fnt";
    auto* c = mkn<CCLabelBMFont>(168, 108, 90, 12, main, H, 0.f, 0.5f); c->str = "Objetos por frame"; c->fnt = "bigFont.fnt";
    auto* d = mkn<CCLabelBMFont>(418, 108, 200, 12, main, H, 1.f, 0.5f); d->str = "300 (tope: 120)  y mucho texto largo"; d->fnt = "bigFont.fnt";   // se pisa con c
    auto* e = mkn<CCLabelBMFont>(512, 50, 80, 12, main, H); e->str = "fuera";                                                                            // x 512 > 430
    auto* btn = mkn<CCMenuItemSpriteExtra>(332, 258, 100, 30, menu, H); btn->setID("place-btn");
    auto* bl = new CCLabelBMFont(); bl->str = "Colocar"; bl->setContentSize({60, 14}); bl->setPosition({50, 15}); btn->addChild(bl);
    auto* tg = mkn<CCMenuItemToggler>(174, 224, 20, 20, menu, H); tg->on = true;
    auto* sl = mkn<Slider>(292, 124, 150, 10, main, H); sl->th = new SliderThumb(); sl->th->val = 0.37f; sl->addChild(sl->th);

    gdx::uic::Collector col(main, {});
    auto ns = col.run(popup);
    int bad = 0;
    auto find = [&](const char* text) -> gdx::ui::Node const* { for (auto& n : ns) if (n.text == text) return &n; return nullptr; };
    auto near = [&](float a1, float b1) { return std::fabs(a1 - b1) < 0.01f; };
    auto A = find("Elige un GIF animado");
    if (!A || !near(A->bx0, 84 - 75) || !near(A->by0, 194 - 8.5f) || !near(A->bx1, 84 + 75) || !near(A->by1, 194 + 8.5f)) { std::cout << "FALLO coordenadas de statA\n"; ++bad; }
    auto C = find("Objetos por frame");
    if (!C || !near(C->bx0, 168) || !near(C->by0, 108 - 6)) { std::cout << "FALLO coordenadas de 'Objetos por frame' (ancla izquierda)\n"; ++bad; }
    for (auto& n : ns) if (n.id == "place-btn" && (!n.interactive || n.inner != "Colocar" || !near(n.bx0, 282) || !near(n.by0, 243))) { std::cout << "FALLO boton: inner='" << n.inner << "' caja " << n.bx0 << "," << n.by0 << "\n"; ++bad; }
    bool sawTog = false, sawSlider = false; for (auto& n : ns) { if (n.kind == "toggler" && n.extra.find("marcado=1") != std::string::npos) sawTog = true; if (n.kind == "slider" && n.extra.find("valor=0.3700") != std::string::npos) sawSlider = true; }
    if (!sawTog) { std::cout << "FALLO toggler\n"; ++bad; }
    if (!sawSlider) { std::cout << "FALLO slider\n"; ++bad; }
    auto issues = gdx::ui::analyze(ns, W, H);
    int over = 0, outside = 0; for (auto& s : issues) { if (s.find("textos SOLAPADOS") != std::string::npos) ++over; if (s.find("fuera de la ventana") != std::string::npos) ++outside; }
    for (auto& s : issues) std::cout << s << "\n";
    if (over != 1 || outside != 1) { std::cout << "FALLO analisis: solapes=" << over << " fuera=" << outside << " (esperado 1 y 1)\n"; ++bad; }
    // la ventana escalada (animacion de apertura): las cajas NO cambian (estan en unidades de la ventana) pero escPantalla si
    main->setScale(0.5f); gdx::uic::Collector c2(main, {}); auto n2 = c2.run(popup);
    for (auto& n : n2) if (n.text == "Elige un GIF animado" && (!near(n.bx0, 9) || !near(n.wsx, 0.5f))) { std::cout << "FALLO ventana escalada: bx0=" << n.bx0 << " ws=" << n.wsx << "\n"; ++bad; }
    // limites: pocos nodos -> se corta y lo avisa
    gdx::uic::Collector c3(main, {5, 400, 40}); auto n3 = c3.run(popup); if (n3.size() != 5 || !c3.truncated()) { std::cout << "FALLO limite de nodos\n"; ++bad; }
    { std::string e = gdx::uic::dirInfo(CCDirector::get()); if (e.find("winSize") == std::string::npos || e.find("frameSize") == std::string::npos || e.find("contentScaleFactor") == std::string::npos) { std::cout << "FALLO dirInfo:\n" << e; ++bad; } else std::cout << e; }
    std::string tree = gdx::ui::formatTree(ns); std::cout << "--- arbol ---\n" << tree.substr(0, 900) << "...\n";
    std::cout << (bad ? "HAY FALLOS\n" : "TODO OK\n"); return bad ? 1 : 0;
}
