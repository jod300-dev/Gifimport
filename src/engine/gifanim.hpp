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
#include "gifdecode.hpp"

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
    // 1) decodificador propio (composicion correcta de disposal 0-3; ver gifdecode.hpp)
    {
        gifdec::Out o;
        if (gifdec::decode(data, len, maxBytes, o) && o.frames >= 1) {
            if (o.frames < 2) { g.error = "El GIF solo tiene 1 frame (no es animado)"; return g; }
            g.w = o.w; g.h = o.h; g.srcW = o.srcW; g.srcH = o.srcH;
            const size_t fs = size_t(o.w) * size_t(o.h) * 4;
            g.frames.resize(size_t(o.frames)); g.delaysMs.resize(size_t(o.frames));
            for (int i = 0; i < o.frames; ++i) {
                g.frames[size_t(i)].w = o.w; g.frames[size_t(i)].h = o.h;
                g.frames[size_t(i)].rgba.assign(o.rgba.begin() + std::ptrdiff_t(size_t(i) * fs), o.rgba.begin() + std::ptrdiff_t(size_t(i + 1) * fs));
                const int d = o.delaysMs[size_t(i)];
                g.delaysMs[size_t(i)] = d <= 10 ? 100 : d;       // igual que los navegadores: 0-10 ms => 100 ms
            }
            return g;
        }
    }
    // 2) respaldo: stb_image (por si el archivo tiene algo raro que el decodificador propio no entiende)
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
        g.delaysMs[size_t(i)] = d <= 10 ? 100 : d;
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
// Posicion de los Color triggers: UNA sola cuadricula compacta para TODOS los modos (paso 30 u en x e y, 20 columnas, sin huecos),
// a la izquierda del inicio y encima de las filas de Alpha/Spawn.
inline void colorTriggerPos(int i, double trigX, double sy, double& cx, double& cy) {
    cx = trigX - 30.0 * double(1 + i % 20);
    cy = sy + 90.0 + 30.0 * double(i / 20);
}
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


namespace detail {
// z-order de cada frame: BANDAS DISJUNTAS, frame 0 la mas alta (editor limpio) y el ultimo la mas baja. Asi dos frames nunca se mezclan
// aunque GD resuelva distinto los empates de z o haya dos frames visibles a la vez: el de la banda alta tapa por completo al de abajo.
// (Antes todos los frames menos el 0 compartian el rango 1..N y sus formas se intercalaban.)  Si no cabe (> 30000), se comparte.
inline std::vector<int> frameBands(const std::vector<Result>& results, int zBase) {
    const size_t n = results.size();
    std::vector<int> span(n, 1), base(n, zBase);
    long total = 0;
    for (size_t i = 0; i < n; ++i) { for (auto& sh : results[i].shapes) span[i] = std::max<int>(span[i], sh.z + 1); total += span[i]; }
    if (total > 30000) {                                           // demasiados niveles: esquema antiguo (frame 0 encima, el resto comparte)
        int mx = 1; for (int v : span) mx = std::max(mx, v);
        for (size_t i = 0; i < n; ++i) base[i] = zBase + (i == 0 ? mx : 0);
        return base;
    }
    long acc = 0;
    for (size_t k = n; k-- > 0;) { base[k] = zBase + int(acc); acc += span[k]; }
    return base;
}
} // namespace detail

// ---- Autocorreccion de los frames planos (metodos 1-17) ----------------------------------------------------------------------
// Antes cada F_i hacia solo "mostrar G_i + ocultar G_(i-1)". Si en GD UN solo trigger se pierde, llega fuera de orden (varios Spawn
// que vencen en el mismo tick tras un tiron/lag) o se pisa con otro, el resultado era un frame VACIO (se oculto el anterior pero no
// se mostro el nuevo) o un frame que se quedaba ENCIMA de los demas (no se oculto; los frames de indice menor tienen mas z).
// Ahora cada F_i fija el estado COMPLETO y es idempotente: muestra G_i y oculta TODOS los demas (N <= 12) o los 5 anteriores (N > 12), y
// cada F_i se lanza DOS veces (en t_i y unos ms despues): si la primera ejecucion falla, la segunda lo deja bien antes del siguiente frame.
constexpr int kFullStateMaxFrames = 12;     // hasta aqui: oculta todos los demas frames en cada paso
constexpr int kHideWindow = 5;              // mas frames: oculta los 5 anteriores
constexpr double kReassertDelay = 0.02;     // la segunda ejecucion de F_i llega 20 ms (o el 40 % del frame si es mas corto) despues
inline int hidesPerFrame(int frames) { return frames <= kFullStateMaxFrames ? std::max(1, frames - 1) : std::min(frames - 1, kHideWindow); }
inline double reassertDelay(double frameDelay) { return std::max(0.005, std::min(kReassertDelay, frameDelay * 0.4)); }
// Triggers: Spawn (F_i x2) + Spawn(L) + Spawn(I) + F_i (1 mostrar + H ocultar) + I (N-1) + 2 de arranque. Es una COTA (si hay frames vacios hay menos).
inline int gifTriggerCount(int frames) { return frames * (4 + hidesPerFrame(frames)) + 3; }
inline int gifGroupCount(int frames) { return 2 * frames + 2; }

namespace detail {
// Frame vacio (0 objetos: p. ej. una excepcion al convertirlo o un frame transparente): en vez de dejar un HUECO en la animacion
// muestra el ultimo frame que si tiene objetos (o el primero si es el principio).  src[i] = frame cuyo grupo se muestra en el paso i.
inline std::vector<int> frameSources(const std::vector<Result>& results) {
    const int n = int(results.size());
    std::vector<int> src(size_t(n), -1);
    int lastOk = -1;
    for (int i = 0; i < n; ++i) { if (!results[size_t(i)].shapes.empty()) lastOk = i; src[size_t(i)] = lastOk; }
    int firstOk = -1; for (int i = 0; i < n; ++i) if (!results[size_t(i)].shapes.empty()) { firstOk = i; break; }
    for (int i = 0; i < n && src[size_t(i)] < 0; ++i) src[size_t(i)] = firstOk;
    return src;
}
inline int emptyFrameCount(const std::vector<Result>& results) {
    int c = 0; for (auto& r : results) if (r.shapes.empty()) ++c; return c;
}
} // namespace detail

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

namespace detail {
// Triggers de los frames planos (compartido por buildGifString y buildGifStringCh).  Grupos: G0+i frame i, F0+i paso i, L bucle, I inicio.
//   L : Spawn(F_i, t_i) + Spawn(F_i, t_i + d) [segunda ejecucion] + Spawn(L, total) + Spawn(I, 0)
//   F_i: Alpha(G_src(i), 1) y luego Alpha(G_j, 0) para los demas frames (ver hidesPerFrame); nunca se oculta y muestra el mismo grupo.
//   I : Alpha(G_j, 0) para todos los frames distintos del grupo del frame 0.
inline void emitFlatTriggers(std::string& out, const GifExportParams& p, const GifPlan& plan, TrigBoard& board,
                             int G0, int F0, int L, int I, const std::vector<int>& src) {
    const int N = plan.count();
    const int H = hidesPerFrame(N);
    double x, y, t = 0;
    std::vector<double> start(size_t(N), 0.0);
    for (int i = 0; i < N; ++i) { board.atSpawn(x, y); addSpawn(out, p, x, y, F0 + i, t, L); start[size_t(i)] = t; t += plan.delay[size_t(i)]; }
    for (int i = 0; i < N; ++i) {                                              // segunda ejecucion (idempotente) de cada paso
        board.atSpawn(x, y);
        addSpawn(out, p, x, y, F0 + i, start[size_t(i)] + reassertDelay(plan.delay[size_t(i)]), L);
    }
    board.atSpawn(x, y); addSpawn(out, p, x, y, L, plan.total, L);
    board.atSpawn(x, y); addSpawn(out, p, x, y, I, 0, L);                      // autocorreccion en cada vuelta
    board.newBlock();

    for (int i = 0; i < N; ++i) {
        std::set<int> done; done.insert(src[size_t(i)]);
        board.atAlpha(x, y); addAlpha(out, p, x, y, G0 + src[size_t(i)], 1, F0 + i);       // primero MOSTRAR el frame nuevo
        for (int k = 1; k <= H; ++k) {
            // todos los demas (en orden de cercania al frame actual hacia atras) o los H anteriores
            const int j = ((i - k) % N + N) % N;
            const int s = src[size_t(j)];
            if (!done.insert(s).second) continue;
            board.atAlpha(x, y); addAlpha(out, p, x, y, G0 + s, 0, F0 + i);
        }
    }
    board.newBlock();
    {
        std::set<int> done; done.insert(src[0]);
        for (int i = 1; i < N; ++i) {
            if (!done.insert(src[size_t(i)]).second) continue;
            board.atAlpha(x, y); addAlpha(out, p, x, y, G0 + src[size_t(i)], 0, I);
        }
    }
    board.newBlock();
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
    const std::vector<int> zBands = detail::frameBands(results, p.eo.zBase);

    // --- objetos de cada frame, cada uno en su grupo G_i
    for (int i = 0; i < N; ++i) {
        const Result& r = results[size_t(i)];
        if (r.shapes.empty()) continue;
        GdExportOptions eo = p.eo;
        eo.groupStr = std::to_string(G0 + i);
        eo.zBase = zBands[size_t(i)];
        eo.monoFastPath = r.mono && !r.colored;
        std::string s = toGdString(r, eo);
        if (s.empty()) continue;
        out += s; out += ';';
    }

    // --- triggers (se mueven al principio de la cadena al final: ver triggersFirst)
    const size_t objEnd = out.size();
    const std::vector<int> src = detail::frameSources(results);          // frames vacios: se repite el anterior (sin hueco)
    const Result& r0 = results[size_t(src[0])];
    GdSize sz = gdImageSize(r0, p.eo);
    detail::TrigBoard board(detail::rowLeft(p.eo.originX - sz.w * 0.5, p.trigX), p.eo.originY + sz.h * 0.5 + 90.0);
    detail::emitFlatTriggers(out, p, plan, board, G0, F0, L, I, src);
    // arranque (triggers de posicion): lanzan L e I
    double sx = p.trigX, sy = board.nextY();
    detail::addSpawn(out, p, sx, board.rowY(), L, 0, 0);          // arranque: lanza L
    detail::addSpawn(out, p, sx, board.rowY() + 30.0, I, 0, 0);   // y lanza I, justo ENCIMA (los dos de arranque van apilados)

    detail::triggersFirst(out, objEnd);
    if (!out.empty() && out.back() == ';') out.pop_back();
    return out;
}

} // namespace gdx
