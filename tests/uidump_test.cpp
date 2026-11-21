// Prueba del modulo de volcado de UI (sin Geode): detecta solapes, desbordes, botones ocultos, etc. y NO marca lo normal.
//   g++ -O2 -std=c++23 -Isrc/engine tests/uidump_test.cpp -o uidump_test && ./uidump_test
#include "uidump.hpp"
#include <iostream>
using namespace gdx::ui;
static Node mk(std::string path, std::string kind, float x0, float y0, float x1, float y1, std::string text = "") {
    Node n; n.path = path; n.kind = kind; n.cls = kind == "texto" ? "cocos2d::CCLabelBMFont" : "cocos2d::CCMenuItemSpriteExtra"; n.text = text;
    n.bx0 = x0; n.by0 = y0; n.bx1 = x1; n.by1 = y1; n.hasBox = true; n.x = x0; n.y = y0; n.w = x1 - x0; n.h = y1 - y0;
    n.interactive = kind == "boton"; n.depth = int(std::count(path.begin(), path.end(), '.')); return n;
}
static int count(std::vector<std::string> const& v, const char* needle) { int c = 0; for (auto& s : v) if (s.find(needle) != std::string::npos) ++c; return c; }
int main() {
    std::vector<Node> ns;
    ns.push_back(mk("0.1", "texto", 10, 10, 110, 30, "Objetos por frame"));
    ns.push_back(mk("0.2", "texto", 60, 12, 160, 32, "300 (tope: 120)"));          // pisa al anterior: PROBLEMA
    ns.push_back(mk("0.3", "texto", 10, 100, 110, 116, "Linea A"));
    ns.push_back(mk("0.4", "texto", 10, 114, 110, 130, "Linea B"));                // roce de 2 px: solo AVISO
    ns.push_back(mk("0.5", "texto", 400, 50, 470, 70, "Se sale"));                 // ventana 430: PROBLEMA
    ns.push_back(mk("0.6", "boton", 200, 200, 280, 230)); ns.back().inner = "Colocar";
    ns.push_back(mk("0.6.0", "texto", 210, 205, 270, 225, "Colocar"));              // dentro de su boton: NO es problema
    ns.push_back(mk("0.7", "boton", 270, 200, 350, 230)); ns.back().inner = "GIF";  // pisa al boton 0.6 (10 px de 80)
    ns.push_back(mk("0.8", "texto", 205, 205, 275, 225, "Tapado por boton"));       // texto sobre boton: PROBLEMA
    ns.push_back(mk("0.9", "boton", 5, 5, 15, 15)); ns.back().effVisible = false;   // activo pero oculto: AVISO
    ns.push_back(mk("0.10", "texto", 20, 150, 20, 150, "cero"));                    // tamano cero: PROBLEMA
    ns.push_back(mk("0.11", "texto", 300, 100, 340, 103, "diminuto"));              // alto 3: AVISO
    ns.push_back(mk("0.12", "texto", 20, 40, 60, 50, "NaN")); ns.back().x = NAN;    // PROBLEMA
    ns.push_back(mk("0.13", "texto", 300, 10, 340, 30, "oculto")); ns.back().effVisible = false;
    ns.push_back(mk("0.14", "texto", 300, 10, 340, 30, "no se ve por op=0")); ns.back().op = 0;
    ns.push_back(mk("0.15", "toggler", 156, 206, 191, 241)); ns.back().interactive = true;
    ns.back().idx = int(ns.size()) - 1;
    ns.push_back(mk("0.15.0", "boton", 156, 206, 191, 241)); ns.back().effVisible = false; ns.back().parent = int(ns.size()) - 2;   // hijo oculto de un check: NO se avisa
    ns.push_back(mk("0.16", "boton", -15, -16, 21, 22));                                                                            // boton de cerrar: sobresale 16 px -> INFO, no AVISO
    ns.push_back(mk("0.17", "boton", 400, 100, 470, 130));                                                                         // sobresale 40 px -> AVISO
    auto r = analyze(ns, 430, 290);
    for (auto& l : r) std::cout << l << "\n";
    int bad = 0;
    auto expect = [&](const char* needle, int n) { int c = count(r, needle); if (c != n) { std::cout << "FALLO: '" << needle << "' esperado " << n << " y salio " << c << "\n"; ++bad; } };
    expect("textos SOLAPADOS", 2);          // 0.1 <-> 0.2  y  0.8 <-> etiqueta interior del boton 0.6
    expect("se rozan", 2);                  // Linea A / Linea B  y  el boton de cerrar de prueba con la 1.a etiqueta
    expect("PROBLEMA fuera de la ventana", 1);
    expect("INFO boton sobresale", 1);
    expect("AVISO boton fuera de la ventana", 1);
    expect("texto y boton SOLAPADOS", 1);   // 0.8 vs 0.6 (0.8 vs 0.7 solo 5 px: roce); 0.8 no es hijo de ningun boton
    expect("botones SOLAPADOS", 0);         // 10 px de 80 = 12 %: por debajo del 25 %, no se marca
    expect("ACTIVO pero oculto", 1);
    expect("tamano cero", 1);
    expect("muy pequeno", 1);
    expect("no finito", 1);
    std::string t = formatTree(ns); std::string x = formatTexts(ns), b = formatButtons(ns);
    if (t.find("texto=\"Objetos por frame\"") == std::string::npos || x.find("Linea B") == std::string::npos || b.find("Colocar") == std::string::npos) { std::cout << "FALLO formato\n"; ++bad; }
    std::cout << (bad ? "HAY FALLOS\n" : "TODO OK\n"); return bad ? 1 : 0;
}
