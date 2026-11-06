// ============================================================================
//  gdx :: GIF animado -> Alpha triggers + Spawn triggers en bucle infinito
//  Sin dependencias de Geode (se puede probar en cualquier PC).
//
//  Esquema (N frames):
//    G_i  (i = 0..N-1)  grupo con los objetos (parche) del frame i
//    F_i                grupo con Alpha: mostrar G_i (1) y ocultar el frame anterior (0);
//                       F_0 ademas oculta TODOS los demas frames (resincroniza cada vuelta)
//    L                  grupo del bucle: Spawn(F_i, retardo t_i) para cada i + Spawn(L, retardo = duracion total)
//    1 Spawn "de posicion" (x = trigX) que lanza L: arranca la animacion
//  Triggers = 4N.   Grupos = 2N + 1.
// ============================================================================
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include "core.hpp"
#include "gdexport.hpp"
#ifndef STBI_NO_STDIO
#define STBI_NO_STDIO
#endif
#include "stb_image.h"     // solo declaraciones; la implementacion esta en stb_impl.cpp

namespace gdx {

// ------------------------------------------------------------ decodificar ---

struct GifData {
    int w = 0, h = 0;
    std::vector<Image> frames;        // cada frame ya compuesto (lienzo completo)
    std::vector<int> delaysMs;        // retardo original de cada frame
    std::string error;
    bool ok() const { return error.empty() && frames.size() >= 2; }
};

inline GifData decodeGif(const uint8_t* data, size_t len) {
    GifData g;
    int* delays = nullptr;
    int w = 0, h = 0, z = 0, c = 0;
    unsigned char* px = stbi_load_gif_from_memory(data, int(len), &delays, &w, &h, &z, &c, 4);
    if (!px) { g.error = "No se pudo leer el GIF"; return g; }
    if (z < 2 || !delays) {
        stbi_image_free(px); if (delays) stbi_image_free(delays);
        g.error = "El GIF solo tiene 1 frame (no es animado)";
        return g;
    }
    g.w = w; g.h = h;
    const size_t fs = size_t(w) * size_t(h) * 4;
    g.frames.resize(size_t(z));
    g.delaysMs.resize(size_t(z));
    for (int i = 0; i < z; ++i) {
        g.frames[size_t(i)].w = w; g.frames[size_t(i)].h = h;
        g.frames[size_t(i)].rgba.assign(px + size_t(i) * fs, px + size_t(i + 1) * fs);
        int d = delays[i];
        g.delaysMs[size_t(i)] = d <= 10 ? 100 : d;       // igual que los navegadores: 0-10 ms => 100 ms
    }
    stbi_image_free(px);
    stbi_image_free(delays);
    return g;
}

// --------------------------------------------------------------- plan -------

struct GifPlan {
    std::vector<int> idx;             // frames elegidos (indices del GIF)
    std::vector<double> delay;        // segundos que dura cada frame elegido
    double total = 0;                 // duracion del bucle
    int count() const { return int(idx.size()); }
};

// Si el GIF tiene mas frames que maxFrames se reparten de forma uniforme y el retardo de los
// saltados se suma al elegido, asi la duracion total se conserva.
inline GifPlan planFrames(const GifData& g, int maxFrames, double speed, double minDelaySec) {
    GifPlan p;
    const int n = int(g.frames.size());
    maxFrames = std::clamp(maxFrames, 2, std::max(2, n));
    const int m = std::min(n, maxFrames);
    speed = std::clamp(speed, 0.1, 10.0);
    for (int k = 0; k < m; ++k) {
        int a = int(int64_t(k) * n / m), b = int(int64_t(k + 1) * n / m);
        if (b <= a) b = a + 1;
        double ms = 0;
        for (int i = a; i < b && i < n; ++i) ms += g.delaysMs[size_t(i)];
        double sec = std::max(minDelaySec, ms / 1000.0 / speed);
        p.idx.push_back(a);
        p.delay.push_back(sec);
        p.total += sec;
    }
    return p;
}

inline int gifTriggerCount(int frames) { return 4 * frames; }
inline int gifGroupCount(int frames) { return 2 * frames + 1; }

// Objetos por frame que caben respetando el tope total (triggers incluidos).
inline int gifPerFrameBudget(int requested, int frames, int maxTotal) {
    int room = (maxTotal - gifTriggerCount(frames)) / std::max(1, frames);
    return std::clamp(std::min(requested, room), 1, 80000);
}

// -------------------------------------------------------- cadena de GD ------

struct GifExportParams {
    GdExportOptions eo;               // origen, lado largo, solape...
    int baseGroup = 1;                // primer grupo libre (se usan baseGroup .. baseGroup + 2N)
    double trigX = 0;                 // x del trigger de arranque
    int objectIdAlpha = 1007;
    int objectIdSpawn = 1268;
};

namespace detail {
inline void addTriggerHead(std::string& s, int id, double x, double y) {
    s += "1,"; s += std::to_string(id);
    s += ",2,"; appendNum(s, x, 2);
    s += ",3,"; appendNum(s, y, 2);
    s += ",36,1";
}
// Alpha "spawn triggered" dentro del grupo ownGroup
inline void addAlpha(std::string& s, const GifExportParams& p, double x, double y, int target, int opacity, int ownGroup) {
    addTriggerHead(s, p.objectIdAlpha, x, y);
    s += ",51,"; s += std::to_string(target);
    s += ",35,"; s += std::to_string(opacity);
    s += ",10,0,62,1,87,1,57,"; s += std::to_string(ownGroup);
    s += ';';
}
// ownGroup <= 0: trigger de posicion (se activa al llegar a x); si no, "spawn triggered" dentro de ese grupo
inline void addSpawn(std::string& s, const GifExportParams& p, double x, double y, int target, double delay, int ownGroup) {
    addTriggerHead(s, p.objectIdSpawn, x, y);
    s += ",51,"; s += std::to_string(target);
    if (delay > 0) { s += ",63,"; appendNum(s, delay, 4); }
    if (ownGroup > 0) { s += ",62,1,87,1,57,"; s += std::to_string(ownGroup); }
    s += ';';
}
} // namespace detail

// Triggers de la animacion (sin objetos). N = frames del plan; plan.delay[i] = duracion del frame i.
// x0,y0 = esquina inferior izquierda de la cuadricula donde se colocan los triggers.
inline std::string buildGifTriggers(const GifPlan& plan, const GifExportParams& p, double x0, double y0,
                                    double startY, std::string* errmsg = nullptr) {
    auto fail = [&](const char* m) { if (errmsg) *errmsg = m; return std::string(); };
    const int N = plan.count();
    if (N < 2) return fail("Hacen falta al menos 2 frames");
    if (p.baseGroup < 1 || p.baseGroup + gifGroupCount(N) - 1 > 9999)
        return fail("No quedan grupos libres suficientes (max. 9999)");
    const int G0 = p.baseGroup, F0 = G0 + N, L = G0 + 2 * N;

    std::string out;
    out.reserve(size_t(gifTriggerCount(N)) * 90);
    const int cols = 30;
    int k = 0;
    auto pos = [&](double& x, double& y) { x = x0 + (k % cols) * 30.0; y = y0 + (k / cols) * 30.0; ++k; };
    double x, y;

    // bucle L: Spawn(F_i, t_i) + Spawn(L, total)
    double t = 0;
    for (int i = 0; i < N; ++i) {
        pos(x, y);
        detail::addSpawn(out, p, x, y, F0 + i, t, L);
        t += plan.delay[size_t(i)];
    }
    pos(x, y);
    detail::addSpawn(out, p, x, y, L, plan.total, L);

    // F_i: mostrar G_i y ocultar el anterior; F_0 oculta ademas todos los demas (resincroniza cada vuelta)
    for (int i = 0; i < N; ++i) {
        pos(x, y); detail::addAlpha(out, p, x, y, G0 + i, 1, F0 + i);
        pos(x, y); detail::addAlpha(out, p, x, y, G0 + (i + N - 1) % N, 0, F0 + i);
    }
    for (int i = 1; i < N - 1; ++i) {
        pos(x, y); detail::addAlpha(out, p, x, y, G0 + i, 0, F0);
    }
    // arranque (trigger de posicion): lanza L
    detail::addSpawn(out, p, p.trigX, startY, L, 0, 0);

    if (!out.empty() && out.back() == ';') out.pop_back();
    return out;
}

// Une dos cadenas de objetos con ';'
inline std::string joinGd(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return a + ";" + b;
}

} // namespace gdx
