// ============================================================================
//  gdx :: GIF animado -> objetos de GD + Alpha triggers + Spawn triggers (bucle infinito)
//  Sin dependencias de Geode (se puede probar en cualquier PC).
//
//  Esquema (N frames):
//    G_i  (i = 0..N-1)  grupo con los objetos del frame i
//    F_i                grupo con 2 Alpha: mostrar G_i (1) y ocultar G_{i-1} (0)
//    L                  grupo del bucle: Spawn(F_i, retardo t_i) para cada i + Spawn(L, retardo = duracion total)
//    I                  grupo de inicio: Alpha(G_i, 0) para i >= 1
//    2 Spawn "de posicion" en x = trigX: uno lanza L (arranca la animacion) y otro lanza I
//  Total de triggers = 4N + 2.   Grupos necesarios = 2N + 2.
// ============================================================================
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>
#include "engine.hpp"
#include "gdexport.hpp"
#ifndef STBI_NO_STDIO
#define STBI_NO_STDIO
#endif
#include "stb_image.h"     // solo declaraciones; la implementacion esta en stb_impl.cpp

namespace gdx {

// ------------------------------------------------------------ decodificar ---

struct GifData {
    int w = 0, h = 0;                 // tamano de los frames guardados (puede ser menor que el del archivo si se redujo por memoria)
    int srcW = 0, srcH = 0;           // tamano original del GIF
    std::vector<Image> frames;        // cada frame ya compuesto (lienzo completo)
    std::vector<int> delaysMs;        // retardo original de cada frame
    std::string error;
    bool ok() const { return error.empty() && frames.size() >= 2; }
};

// Tope de memoria de los frames decodificados (RGBA). Si el GIF lo pasa, los frames se reducen (promedio k x k) al decodificar:
// la geometria final no depende de la resolucion (el raster de trabajo ya se limita a "max-raster", <= 1024 px).
constexpr size_t kGifMaxBytes = 400u * 1024u * 1024u;

inline GifData decodeGif(const uint8_t* data, size_t len, size_t maxBytes = kGifMaxBytes) {
    GifData g;
    if (!data || len < 10) { g.error = "El archivo esta vacio o no es un GIF"; return g; }
    int* delays = nullptr;
    int w = 0, h = 0, z = 0, sw = 0, sh = 0;
    unsigned char* px = stbi_load_gif_scaled_from_memory(data, int(len), &delays, &z, &w, &h, &sw, &sh, maxBytes);
    if (!px) { g.error = "No se pudo leer el GIF (archivo danado o formato no soportado)"; return g; }
    if (z < 2 || !delays) {
        stbi_image_free(px); if (delays) stbi_image_free(delays);
        g.error = "El GIF solo tiene 1 frame (no es animado)";
        return g;
    }
    g.w = w; g.h = h; g.srcW = sw; g.srcH = sh;
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

namespace detail {
// Colocacion de los triggers de frames en DOS filas, cada una avanzando hacia la derecha UNO TRAS OTRO (paso 30 u, sin huecos):
//   fila de arriba : todos los ALPHA (mostrar/ocultar frames), en orden de frames
//   fila de abajo  : todos los SPAWN (bucle L, autocorreccion), en orden de frames
// Dentro de una fila ningun trigger queda encima o debajo de otro (cada uno tiene su propia x), asi no se pisan ni se interponen.
// Los 2 Spawn de arranque (posicion) van uno encima del otro a la izquierda, como en la v0.4.2. Los Color triggers pueden apilarse.
struct TrigBoard {
    double left, alphaY, spawnY, dx; int ka = 0, ks = 0;
    TrigBoard(double l, double top, double ddx = 30.0) : left(l), alphaY(top + 60.0), spawnY(top), dx(ddx) {}
    void atAlpha(double& x, double& y) { x = left + ka * dx; y = alphaY; ++ka; }
    void atSpawn(double& x, double& y) { x = left + ks * dx; y = spawnY; ++ks; }
    void newBlock() {}                                               // sin huecos: las filas son continuas
    double rowY() const { return spawnY; }                           // y del primer Spawn de arranque (el segundo va 30 u mas arriba)
    double nextY() const { return alphaY + 60.0; }                   // base para los Color triggers (encima de las dos filas)
};
// x inicial de las filas: a la derecha de los triggers de arranque (trigX) y no antes del borde izquierdo de la imagen
inline double rowLeft(double imageLeft, double trigX) { return std::max(imageLeft, trigX + 60.0); }
} // namespace detail

// Limite de objetos del editor de GD 2.2 (si se pasa, createObjectsFromString corta la cadena: se pierden los ULTIMOS objetos).
// Las cadenas se generan con los TRIGGERS PRIMERO (ver triggersFirst) para que un recorte nunca deje la animacion sin triggers.
constexpr int kEditorObjectLimit = 200000;
constexpr int kEditorObjectMargin = 3000;      // colchon para lo que el nivel ya tenga o lo que GD anada

namespace detail {
// Mueve la parte de triggers (out[objEnd..]) delante de los objetos (out[0..objEnd)). Ambas partes terminan en ';'.
inline void triggersFirst(std::string& out, size_t objEnd) {
    if (objEnd == 0 || objEnd >= out.size()) return;
    if (out[objEnd - 1] != ';') return;
    std::string t = out.substr(objEnd);
    if (t.empty() || t.back() != ';') t += ';';
    out.erase(objEnd);
    out.insert(0, t);
}
} // namespace detail

constexpr int kGifTriggersPerFrame = 4;
inline int gifTriggerCount(int frames) { return kGifTriggersPerFrame * frames + 3; }   // +1: Spawn(I) en cada vuelta
inline int gifGroupCount(int frames) { return 2 * frames + 2; }

// Objetos por frame que caben respetando el tope total (triggers incluidos).
inline int gifPerFrameBudget(int requested, int frames, int maxTotal, int extraObjects = 0) {
    int room = (maxTotal - gifTriggerCount(frames) - std::max(0, extraObjects)) / std::max(1, frames);
    return std::clamp(std::min(requested, room), 1, 80000);
}

// ------------------------------------------------------------ convertir -----

// Convierte los frames del plan en paralelo. Devuelve un Result por frame del plan.
inline std::vector<Result> convertFrames(const GifData& g, const GifPlan& plan, Method method, const Options& opt,
                                         Progress* prog, int threads) {
    const int n = plan.count();
    std::vector<Result> out;
    out.resize(size_t(n));
    std::atomic<int> next{0}, done{0};
    threads = std::clamp(threads, 1, std::max(1, n));
    auto worker = [&]() {
        for (;;) {
            if (isCancelled(prog)) return;
            int i = next.fetch_add(1);
            if (i >= n) return;
            try {
                out[size_t(i)] = convert(g.frames[size_t(plan.idx[size_t(i)])], method, opt, nullptr);
            } catch (...) {
                out[size_t(i)] = Result{};
            }
            report(prog, float(done.fetch_add(1) + 1) / float(n));
        }
    };
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; ++t) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();
    return out;
}

// -------------------------------------------------------- cadena de GD ------

struct GifExportParams {
    GdExportOptions eo;               // origen, lado largo, canal de color, bleed...
    int baseGroup = 1;                // primer grupo libre (se usan baseGroup .. baseGroup + 2N + 1)
    double trigX = -30;               // x de los 2 triggers de arranque
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

// results = un Result por frame del plan. Devuelve "" si algo no cuadra (errmsg explica).
inline std::string buildGifString(const std::vector<Result>& results, const GifPlan& plan,
                                  const GifExportParams& p, std::string* errmsg = nullptr) {
    auto fail = [&](const char* m) { if (errmsg) *errmsg = m; return std::string(); };
    const int N = plan.count();
    if (N < 2 || int(results.size()) != N) return fail("Hacen falta al menos 2 frames");
    if (p.baseGroup < 1 || p.baseGroup + gifGroupCount(N) - 1 > 9999)
        return fail("No quedan grupos libres suficientes (max. 9999)");
    const int G0 = p.baseGroup, F0 = G0 + N, L = G0 + 2 * N, I = G0 + 2 * N + 1;

    std::string out;
    size_t total = 0;
    for (auto& r : results) total += r.shapes.size();
    if (total == 0) return fail("Ningun frame genero objetos");
    out.reserve(total * 100 + size_t(gifTriggerCount(N)) * 90);

    // El editor no ejecuta triggers: todos los frames se ven apilados. El frame 0 va ENCIMA (z-order mayor) para que en el
    // editor se vea limpio; en el juego solo hay un frame visible a la vez, asi que el orden entre frames no importa.
    int zSpan = 1;
    for (auto& r : results) for (auto& sh : r.shapes) zSpan = std::max<int>(zSpan, sh.z + 1);

    // --- objetos de cada frame, cada uno en su grupo G_i
    for (int i = 0; i < N; ++i) {
        const Result& r = results[size_t(i)];
        if (r.shapes.empty()) continue;
        GdExportOptions eo = p.eo;
        eo.groupStr = std::to_string(G0 + i);
        eo.zBase = p.eo.zBase + (i == 0 ? zSpan : 0);
        eo.monoFastPath = r.mono && !r.colored;
        std::string s = toGdString(r, eo);
        if (s.empty()) continue;
        out += s; out += ';';
    }

    // --- triggers (se mueven al principio de la cadena al final: ver triggersFirst)
    const size_t objEnd = out.size();
    const Result& r0 = results[0];
    GdSize sz = gdImageSize(r0, p.eo);
    detail::TrigBoard board(detail::rowLeft(p.eo.originX - sz.w * 0.5, p.trigX), p.eo.originY + sz.h * 0.5 + 90.0);
    auto posA = [&](double& x, double& y) { board.atAlpha(x, y); };
    auto posS = [&](double& x, double& y) { board.atSpawn(x, y); };
    double x, y;

    // bucle L: Spawn(F_i, t_i) + Spawn(L, total)
    double t = 0;
    for (int i = 0; i < N; ++i) {
        posS(x, y);
        detail::addSpawn(out, p, x, y, F0 + i, t, L);
        t += plan.delay[size_t(i)];
    }
    posS(x, y);
    detail::addSpawn(out, p, x, y, L, plan.total, L);
    // Autocorreccion: en cada vuelta se vuelve a ocultar todo salvo el frame 0, asi un trigger fallido no deja frames superpuestos.
    posS(x, y);
    detail::addSpawn(out, p, x, y, I, 0, L);
    board.newBlock();

    // F_i: mostrar G_i, ocultar el frame anterior
    for (int i = 0; i < N; ++i) {
        posA(x, y); detail::addAlpha(out, p, x, y, G0 + i, 1, F0 + i);
        posA(x, y); detail::addAlpha(out, p, x, y, G0 + (i + N - 1) % N, 0, F0 + i);
    }
    board.newBlock();
    // I: ocultar los frames 1..N-1 al empezar
    for (int i = 1; i < N; ++i) {
        posA(x, y); detail::addAlpha(out, p, x, y, G0 + i, 0, I);
    }
    board.newBlock();
    // arranque (triggers de posicion): lanzan L e I
    double sx = p.trigX, sy = board.nextY();
    detail::addSpawn(out, p, sx, board.rowY(), L, 0, 0);          // arranque: lanza L
    detail::addSpawn(out, p, sx, board.rowY() + 30.0, I, 0, 0);   // y lanza I, justo ENCIMA (los dos de arranque van apilados)

    detail::triggersFirst(out, objEnd);
    if (!out.empty() && out.back() == ';') out.pop_back();
    return out;
}

} // namespace gdx
