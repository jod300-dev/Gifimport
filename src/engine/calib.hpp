// ============================================================================
//  gdx :: Patron de calibracion (diagnostico, SIN GIF y SIN triggers de animacion)
//  Coloca ~100 objetos con el MISMO exportador que usa el mod y que prueban, por separado, como dibuja GD:
//    Fila 1  orden de dibujo por z-order (rojo vs azul solapados): z distintos, creacion en orden inverso, z muy altos y EMPATES
//    Fila 2  rotacion con escala no uniforme (franja 40x5 girada 0,30,60,90,120,150) sobre un cuadrado gris de referencia
//    Fila 3  (derecha: z=0 frente a z=1)  base + franja girada encima (lo que hacen los metodos 7/8/18/19), creada antes / despues, y franjas finas
//    Fila 4  tamanos diminutos (alto 6, 3, 1.5, 0.75, 0.4, 0.2 unidades)
//    Fila 5  teselado sub-unidad: 60 tiras de 0.5 u forman un degradado continuo (si hay huecos/bandas, GD redondea posiciones/escalas)
//    Fila 6  canales de color 2,3,4,5 (rojo, verde, azul, amarillo) con Color triggers, junto a los mismos colores en HSV
//    PRUEBA DE BUCLE (debajo): 12 frames sinteticos con la animacion real (triggers, grupos, bandas de z): ver buildLoopTest
//  Sin dependencias de Geode.
// ============================================================================
#pragma once
#include "chanframes.hpp"

namespace gdx {

struct CalibResult { std::string str; Result hsv, chan; std::vector<std::array<uint8_t, 3>> palette; double hsvCenterY = 0, chanCenterY = 0; };

// PRUEBA DE BUCLE (si baseGroup > 0): 12 frames sinteticos CONOCIDOS pasados por el mismo exportador de animacion que usan los metodos 1-17
// (grupos, Alpha/Spawn, bandas de z por frame, bucle con autocorreccion). Cada frame = fondo de un color distinto (rueda de color), una franja
// negra girada (15 grados por frame) y un cuadrado blanco que avanza de izquierda a derecha. Al JUGAR el nivel se debe ver:
//   - los 12 colores en orden, uno cada 0,1 s, SIN mezclas ni parpadeos de otros colores
//   - la franja girando y el cuadrado blanco avanzando sin saltos
//   - el bucle limpio: del ultimo frame (rosa) pasa directo al primero (rojo), sin que el ultimo desaparezca ni se quede un frame a medias
// En el editor (sin jugar) se ve apilado: debe verse el frame 0 (rojo) completo encima.
inline std::string buildLoopTest(GdExportOptions eo, double trigX, int baseGroup, double centerY) {
    const int N = 12;
    std::vector<Result> res; res.resize(size_t(N));
    GifPlan plan;
    for (int i = 0; i < N; ++i) {
        Result& r = res[size_t(i)]; r.rasterW = 120; r.rasterH = 80; r.aspect = 1.5f;
        const float h = 360.f * float(i) / float(N);                        // hue -> rgb
        const float c = 0.8f, x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f)), m = 0.2f;
        float rr = 0, gg = 0, bb = 0;
        if (h < 60) { rr = c; gg = x; } else if (h < 120) { rr = x; gg = c; } else if (h < 180) { gg = c; bb = x; }
        else if (h < 240) { gg = x; bb = c; } else if (h < 300) { rr = x; bb = c; } else { rr = c; bb = x; }
        auto add = [&](float cx, float cy, float w, float hh, float rot, uint8_t R, uint8_t G, uint8_t B, int z) {
            Shape sh; sh.cx = cx; sh.cy = cy; sh.w = w; sh.h = hh; sh.rot = rot; sh.r = R; sh.g = G; sh.b = B; sh.z = int16_t(z); r.shapes.push_back(sh);
        };
        add(60, 40, 120, 80, 0, uint8_t((rr + m) * 255), uint8_t((gg + m) * 255), uint8_t((bb + m) * 255), 0);
        add(60, 40, 90, 6, 15.f * float(i), 0, 0, 0, 1);
        add(8.f + 9.f * float(i), 68, 10, 10, 0, 255, 255, 255, 2);
        plan.idx.push_back(i); plan.delay.push_back(0.1);
    }
    plan.total = 0.1 * N;
    GifExportParams p; p.eo = eo; p.eo.longSide = 120; p.eo.originY = centerY; p.eo.groupStr.clear(); p.eo.channels.clear(); p.baseGroup = baseGroup; p.trigX = trigX;
    return buildGifString(res, plan, p, nullptr);
}

inline CalibResult buildCalibration(GdExportOptions eo, double trigX, int baseGroup = 0) {
    CalibResult cr;
    const uint8_t RED[3] = {230, 30, 30}, BLUE[3] = {30, 60, 230}, GRAY[3] = {150, 150, 150}, GREEN[3] = {30, 180, 60}, BLACK[3] = {0, 0, 0};
    Result A; A.rasterW = 320; A.rasterH = 150; A.aspect = 320.f / 150.f;
    auto add = [&](float cx, float cy, float w, float h, float rot, const uint8_t* c, int z) {
        Shape s; s.cx = cx; s.cy = cy; s.w = w; s.h = h; s.rot = rot; s.r = c[0]; s.g = c[1]; s.b = c[2]; s.z = int16_t(z);
        A.shapes.push_back(s);
    };
    // Fila 1: z-order (cada par = rojo y azul solapados; el orden de aqui es el orden de creacion)
    struct Pair { bool redFirst; int zr, zb; };
    const Pair pairs[6] = {{true, 1, 5}, {false, 1, 5}, {true, 1, 30}, {true, 1, 300}, {true, 3, 3}, {false, 3, 3}};
    for (int k = 0; k < 6; ++k) {
        float x0 = 25.f + 50.f * k;
        if (pairs[k].redFirst) { add(x0 - 6, 15, 26, 26, 0, RED, pairs[k].zr); add(x0 + 6, 15, 26, 26, 0, BLUE, pairs[k].zb); }
        else                   { add(x0 + 6, 15, 26, 26, 0, BLUE, pairs[k].zb); add(x0 - 6, 15, 26, 26, 0, RED, pairs[k].zr); }
    }
    // Fila 2: rotacion (gris de referencia z=1, franja negra 40x5 girada z=2)
    const float angs[6] = {0, 30, 60, 90, 120, 150};
    for (int k = 0; k < 6; ++k) { float x0 = 25.f + 50.f * k; add(x0, 55, 40, 40, 0, GRAY, 1); add(x0, 55, 40, 5, angs[k], BLACK, 2); }
    // Fila 3: base + franja girada (como los metodos de rectangulos girados): creada despues / antes / franjas finas
    add(25, 92, 36, 36, 0, GRAY, 1);    add(25, 92, 50, 4, 45, GREEN, 2);          // base primero
    add(75, 92, 50, 4, 45, GREEN, 2);   add(75, 92, 36, 36, 0, GRAY, 1);           // franja primero
    add(125, 92, 36, 36, 0, GRAY, 1);   add(125, 92, 50, 1, 45, GREEN, 2);         // franja de 1 u
    add(175, 92, 36, 36, 0, GRAY, 1);   add(175, 92, 50, 0.5f, 45, GREEN, 2);      // franja de 0.5 u
    add(225, 92, 36, 36, 0, GRAY, 1);   for (int j = 0; j < 8; ++j) add(225.f, 92.f - 14 + 4 * j, 34, 1.5f, 20.f * (j % 3), j % 2 ? GREEN : BLACK, 2 + j);   // 8 franjas apiladas
    // Fila 3 (derecha): z-order 0 frente a 1. Esperado: el AZUL (z=1) encima del rojo (z=0) en los dos casos.
    // Si el ROJO queda encima, GD trata z=0 como \"por defecto\" y lo dibuja sobre los z>=1 (error que tenian los metodos 7/8).
    add(278, 92, 20, 20, 0, RED, 0);  add(286, 92, 20, 20, 0, BLUE, 1);        // rojo(0) creado primero
    add(310, 92, 20, 20, 0, BLUE, 1); add(302, 92, 20, 20, 0, RED, 0);         // azul(1) creado primero
    // Fila 4: tamanos diminutos
    const float hs[6] = {6, 3, 1.5f, 0.75f, 0.4f, 0.2f};
    for (int k = 0; k < 6; ++k) add(25.f + 50.f * k, 118, 8, hs[k], 0, BLACK, 1);
    // Fila 5: teselado sub-unidad (60 tiras de 0.5 u: degradado negro -> blanco)
    for (int i = 0; i < 60; ++i) { uint8_t v = uint8_t(i * 255 / 59); uint8_t c[3] = {v, v, v}; add(40.f + 0.5f * i, 138, 0.5f, 10, 0, c, 1); }
    // Canales: 4 cuadrados con canales 2..5
    Result B; B.rasterW = 320; B.rasterH = 20; B.aspect = 16.f; B.colored = true;
    cr.palette = {{230, 30, 30}, {30, 180, 60}, {30, 60, 230}, {240, 220, 40}};
    for (int k = 0; k < 4; ++k) { Shape s; s.cx = 25.f + 50.f * k; s.cy = 10; s.w = 24; s.h = 14; s.r = cr.palette[size_t(k)][0]; s.g = cr.palette[size_t(k)][1]; s.b = cr.palette[size_t(k)][2]; B.shapes.push_back(s); }
    // Los mismos 4 colores en HSV, a la derecha
    for (int k = 0; k < 4; ++k) { Shape s; s.cx = 225.f + 24.f * k; s.cy = 143; s.w = 20; s.h = 14; s.r = cr.palette[size_t(k)][0]; s.g = cr.palette[size_t(k)][1]; s.b = cr.palette[size_t(k)][2]; s.z = 1; A.shapes.push_back(s); }

    eo.longSide = 320; eo.zBase = 0; eo.zShift = 0; eo.groupStr.clear(); eo.monoFastPath = false; eo.bleed = 0.15;   // zShift 0: z CRUDO (para probar el 0)
    GdExportOptions eoA = eo;                                    // HSV (misma ruta que los metodos normales)
    std::string sA = toGdString(A, eoA);
    GdExportOptions eoB = eo; eoB.originY = eo.originY - (150 * 0.5 + 12 + 20 * 0.5);
    ChannelMap cm = buildChannelMap(B);
    eoB.channels.assign(cm.colors.size(), 0);
    for (size_t i = 0; i < cm.colors.size(); ++i) for (size_t j = 0; j < cr.palette.size(); ++j)
        if (cm.colors[i][0] == cr.palette[j][0] && cm.colors[i][1] == cr.palette[j][1] && cm.colors[i][2] == cr.palette[j][2]) eoB.channels[i] = kFirstColorChannel + int(j);
    std::string sB = toGdString(B, eoB);
    cr.str = sA + ";" + sB + ";";                               // ';' final: appendColorTriggers concatena a continuacion
    appendColorTriggers(cr.str, cr.palette, kFirstColorChannel, trigX, eo.originY + 150 * 0.5 + 60);
    if (baseGroup > 0) {                                           // prueba de bucle debajo del patron (+ sus triggers y los 2 de arranque)
        cr.str += buildLoopTest(eo, trigX - 150, baseGroup, eo.originY - 400);
        cr.str += ";";
    }
    if (!cr.str.empty() && cr.str.back() == ';') cr.str.pop_back();
    cr.hsv = std::move(A); cr.chan = std::move(B);
    cr.hsvCenterY = eo.originY; cr.chanCenterY = eoB.originY;
    return cr;
}

} // namespace gdx
