// ============================================================================
//  gdx :: núcleo del motor de conversión imagen -> formas para Geometry Dash
//  Sin dependencias de Geode: se puede compilar y probar en cualquier PC.
// ============================================================================
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace gdx {

// ---------------------------------------------------------------- tipos ----

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;           // RGBA8 "straight" (sin premultiplicar)
    bool empty() const { return w <= 0 || h <= 0; }
    uint8_t* at(int x, int y) { return &rgba[(size_t(y) * w + x) * 4]; }
    const uint8_t* at(int x, int y) const { return &rgba[(size_t(y) * w + x) * 4]; }
};

// Rectángulo (posiblemente rotado) en coordenadas del "raster" de trabajo.
// Origen arriba-izquierda, y hacia abajo, unidades = píxeles del raster.
struct Shape {
    float cx = 0, cy = 0;                // centro
    float w = 1, h = 1;                  // tamaño en los ejes locales
    float rot = 0;                       // grados, sentido horario en pantalla
    uint8_t r = 0, g = 0, b = 0;
    int16_t z = 0;                       // capa: mayor = se dibuja encima
};

enum class Method : int {
    UniformGrid = 0,  // 1. Cuadrícula uniforme
    Quadtree,         // 2. Quadtree adaptativo (mejor-primero)
    BSP,              // 3. Particiones binarias adaptativas (k-d)
    Pyramid,          // 4. Pirámide de capas (residuos dispersos)
    Regions,          // 5. Regiones por paleta + rectángulos voraces
    Runs,             // 6. Franjas horizontales con tolerancia
    Geometrize,       // 7. Rectángulos rotados por búsqueda aleatoria
    Hybrid,           // 8. Híbrido: BSP + refinado con rectángulos rotados
    // ---- estilos monocromos (sin HSV, pocos objetos) ----
    Halftone,         // 9.  Trama de diamantes (halftone)
    Engrave,          // 10. Grabado de líneas de grosor variable
    Hatch,            // 11. Sombreado cruzado (cross-hatch)
    Stipple,          // 12. Estarcido / puntillismo
    Dither,           // 13. Tramado Bayer + rectángulos fusionados
    Ink,              // 14. Tinta (umbral adaptativo)
    Strokes,          // 15. Boceto de trazos sobre los bordes
    Spiral,           // 16. Espiral de grosor variable
    Truchet,          // 17. Diagonales tipo "10 PRINT"
    Ascii,            // 18. Arte ASCII con objetos de texto (1 objeto por fila)
    COUNT
};

struct Options {
    int budget = 10000;                  // máximo de objetos (duro)
    int maxRaster = 768;                 // lado máximo del raster interno
    int alphaThreshold = 128;            // alfa >= umbral => píxel opaco
    int quality = 2;                     // 0 rápido, 1 normal, 2 alto, 3 máximo
    float hybridFraction = -1.f;         // fracción del presupuesto para la base BSP del híbrido (<0 = automática)
    uint32_t seed = 1337;
    bool invertInk = false;              // estilos monocromos: tinta blanca sobre fondo negro
    float asciiAspect = 1.0f;            // ASCII: alto/ancho de una celda de carácter (según la fuente)
    // ---- estilos ligeros (9-18) ----
    bool colorChannels = false;          // true: colores de la imagen (base de color + tinta del mismo tono) en canales
    int paletteSize = 48;                // canales de color del estilo ligero (se reparten en pares base/tinta)
    bool lightSpecialChannels = false;   // blanco y negro: usar los canales especiales 1010/1011 en vez de canales libres
    int asciiChunk = 60;                 // caracteres máximos por objeto de texto (las filas largas se parten)
    // ---- métodos principales (1-8) ----
    bool mainChannels = false;           // colores en canales (sin HSV)
    bool grayscale = false;              // escala de grises (con HSV o con canales según mainChannels)
    int mainPaletteSize = 64;            // canales de la paleta de los métodos principales
    // ---- canales disponibles ----
    int maxChannels = 997;               // canales libres que puede usar este resultado (el 1 nunca; lo fija la ventana)
};

// Una fila de arte ASCII: se exporta como UN objeto de texto de GD.
struct TextRow {
    float cx = 0, cy = 0;                // centro del texto (unidades del raster = celdas)
    std::string text;
    bool colored = false;                // true: usa el color (r,g,b) en un canal de la paleta
    uint8_t r = 0, g = 0, b = 0;
};

struct Progress {
    std::atomic<float> fraction{0.f};
    std::atomic<bool> cancel{false};
};

struct Result {
    std::vector<Shape> shapes;
    int rasterW = 0, rasterH = 0;        // tamaño del raster al que se refieren las formas
    float aspect = 1.f;                  // ancho/alto de la imagen original
    double psnr = 0;                     // calidad aproximada (dB, RGB)
    double ms = 0;                       // tiempo de cálculo
    bool cancelled = false;
    bool hadAlpha = false;
    bool mono = false;                   // solo tinta/papel (negro y blanco), sin HSV
    uint8_t inkValue = 0;                // 0 = tinta negra, 255 = tinta blanca
    bool colored = false;                // el resultado usa canales de color (cada color distinto = un canal, sin HSV)
    std::vector<TextRow> textRows;       // arte ASCII (objetos de texto)
    std::vector<std::array<uint8_t, 3>> palette;   // colores de los canales (índice = Shape::pal)
    Image previewImg;                    // vista previa propia (ASCII); vacía = renderizar `shapes`
    std::string note;                    // info extra para mostrar al usuario
};

inline bool isCancelled(Progress* p) { return p && p->cancel.load(std::memory_order_relaxed); }
inline void report(Progress* p, float f) { if (p) p->fraction.store(f, std::memory_order_relaxed); }

// ------------------------------------------------------------------- rng ---

struct Rng {
    uint64_t state, inc;
    explicit Rng(uint64_t seed = 1) : state(0), inc((seed << 1u) | 1u) { next(); state += seed * 0x9E3779B97F4A7C15ULL; next(); }
    uint32_t next() {
        uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        uint32_t xs = uint32_t(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = uint32_t(old >> 59u);
        return (xs >> rot) | (xs << ((~rot + 1u) & 31u));
    }
    float f() { return float(next() >> 8) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * f(); }
    int below(int n) { return n <= 1 ? 0 : int(next() % uint32_t(n)); }
    float gauss() {                       // aprox. normal(0,1)
        float s = 0; for (int i = 0; i < 4; ++i) s += f();
        return (s - 2.f) * 1.7320508f;
    }
};

// -------------------------------------------------------------- redimensión

// Reducción/ampliación por promedio de área exacto (ponderado por alfa).
inline Image resizeArea(const Image& s, int nw, int nh) {
    Image o; o.w = nw; o.h = nh;
    o.rgba.assign(size_t(std::max(nw, 0)) * std::max(nh, 0) * 4, 0);
    if (s.empty() || nw <= 0 || nh <= 0) return o;
    if (nw == s.w && nh == s.h) { o.rgba = s.rgba; return o; }

    // paso horizontal: (nw x s.h), valores premultiplicados por alfa
    std::vector<float> tmp(size_t(nw) * s.h * 4);
    const double sx = double(s.w) / nw;
    for (int dx = 0; dx < nw; ++dx) {
        double a0 = dx * sx, a1 = (dx + 1) * sx;
        int i0 = int(std::floor(a0)), i1 = int(std::ceil(a1)) - 1;
        if (i1 >= s.w) i1 = s.w - 1;
        if (i0 > i1) i0 = i1;
        for (int y = 0; y < s.h; ++y) {
            double acc[4] = {0, 0, 0, 0}, ws = 0;
            for (int i = i0; i <= i1; ++i) {
                double wl = std::min<double>(i + 1, a1) - std::max<double>(i, a0);
                if (wl <= 0) continue;
                const uint8_t* p = s.at(i, y);
                double a = p[3] / 255.0;
                acc[0] += wl * p[0] * a; acc[1] += wl * p[1] * a; acc[2] += wl * p[2] * a; acc[3] += wl * a;
                ws += wl;
            }
            float* t = &tmp[(size_t(y) * nw + dx) * 4];
            if (ws > 0) for (int k = 0; k < 4; ++k) t[k] = float(acc[k] / ws);
            else t[0] = t[1] = t[2] = t[3] = 0;
        }
    }
    // paso vertical
    const double sy = double(s.h) / nh;
    for (int dy = 0; dy < nh; ++dy) {
        double a0 = dy * sy, a1 = (dy + 1) * sy;
        int j0 = int(std::floor(a0)), j1 = int(std::ceil(a1)) - 1;
        if (j1 >= s.h) j1 = s.h - 1;
        if (j0 > j1) j0 = j1;
        for (int dx = 0; dx < nw; ++dx) {
            double acc[4] = {0, 0, 0, 0}, ws = 0;
            for (int j = j0; j <= j1; ++j) {
                double wl = std::min<double>(j + 1, a1) - std::max<double>(j, a0);
                if (wl <= 0) continue;
                const float* t = &tmp[(size_t(j) * nw + dx) * 4];
                for (int k = 0; k < 4; ++k) acc[k] += wl * t[k];
                ws += wl;
            }
            uint8_t* p = o.at(dx, dy);
            if (ws > 0) {
                double A = acc[3] / ws;                    // alfa media 0..1
                if (A > 1e-6) {
                    for (int k = 0; k < 3; ++k)
                        p[k] = uint8_t(std::clamp(acc[k] / ws / A + 0.5, 0.0, 255.0));
                    p[3] = uint8_t(std::clamp(A * 255.0 + 0.5, 0.0, 255.0));
                }
            }
        }
    }
    return o;
}

// Tamaño de raster que respeta el aspecto y un lado máximo (sin ampliar).
inline void fitSize(int sw, int sh, int maxSide, int& nw, int& nh) {
    double sc = std::min(1.0, double(maxSide) / std::max(sw, sh));
    nw = std::max(1, int(std::lround(sw * sc)));
    nh = std::max(1, int(std::lround(sh * sc)));
}

inline bool hasTransparency(const Image& im, int thr) {
    for (size_t i = 3; i < im.rgba.size(); i += 4) if (im.rgba[i] < thr) return true;
    return false;
}

// --------------------------------------------- sumas acumuladas (SAT) -------
// Estadísticas O(1) por rectángulo, contando sólo píxeles opacos.
// Error de un rectángulo = suma de cuadrados de las diferencias de color
// respecto a la media + penalización por píxeles opacos/transparentes mezclados.

constexpr double kAlphaPenalty = 60000.0;

struct Stats {
    int w = 0, h = 0;
    std::vector<int32_t> sr, sg, sb, n;
    std::vector<uint64_t> sq;
    size_t idx(int x, int y) const { return size_t(y) * (w + 1) + x; }

    void build(const Image& im, int alphaThr) {
        w = im.w; h = im.h;
        size_t sz = size_t(w + 1) * (h + 1);
        sr.assign(sz, 0); sg.assign(sz, 0); sb.assign(sz, 0); n.assign(sz, 0); sq.assign(sz, 0);
        for (int y = 0; y < h; ++y) {
            int32_t rr = 0, gg = 0, bb = 0, nn = 0; uint64_t qq = 0;
            for (int x = 0; x < w; ++x) {
                const uint8_t* p = im.at(x, y);
                if (p[3] >= alphaThr) {
                    rr += p[0]; gg += p[1]; bb += p[2]; ++nn;
                    qq += uint64_t(p[0]) * p[0] + uint64_t(p[1]) * p[1] + uint64_t(p[2]) * p[2];
                }
                size_t i = idx(x + 1, y + 1), up = idx(x + 1, y);
                sr[i] = sr[up] + rr; sg[i] = sg[up] + gg; sb[i] = sb[up] + bb;
                n[i] = n[up] + nn;   sq[i] = sq[up] + qq;
            }
        }
    }
    struct Q { int area, n; double sr, sg, sb, sq; };
    Q query(int x0, int y0, int x1, int y1) const {
        size_t a = idx(x0, y0), b = idx(x1, y0), c = idx(x0, y1), d = idx(x1, y1);
        Q q;
        q.area = (x1 - x0) * (y1 - y0);
        q.n  = n[d] - n[b] - n[c] + n[a];
        q.sr = double(sr[d]) - sr[b] - sr[c] + sr[a];
        q.sg = double(sg[d]) - sg[b] - sg[c] + sg[a];
        q.sb = double(sb[d]) - sb[b] - sb[c] + sb[a];
        q.sq = double(sq[d]) - double(sq[b]) - double(sq[c]) + double(sq[a]);
        return q;
    }
    // error total del rectángulo si se pinta de un solo color
    double sse(int x0, int y0, int x1, int y1) const {
        Q q = query(x0, y0, x1, y1);
        return sseOf(q);
    }
    static double sseOf(const Q& q) {
        double e = 0;
        if (q.n > 0) e = q.sq - (q.sr * q.sr + q.sg * q.sg + q.sb * q.sb) / q.n;
        if (e < 0) e = 0;
        e += kAlphaPenalty * std::min(q.n, q.area - q.n);
        return e;
    }
    static bool mostlyOpaque(const Q& q) { return q.n * 2 >= q.area && q.n > 0; }
    static void mean(const Q& q, uint8_t& r, uint8_t& g, uint8_t& b) {
        if (q.n <= 0) { r = g = b = 0; return; }
        r = uint8_t(std::clamp(q.sr / q.n + 0.5, 0.0, 255.0));
        g = uint8_t(std::clamp(q.sg / q.n + 0.5, 0.0, 255.0));
        b = uint8_t(std::clamp(q.sb / q.n + 0.5, 0.0, 255.0));
    }
};

// -------------------------------------------------------- render + PSNR -----

// Rasteriza las formas (en orden de z, estable) sobre un lienzo RGBA transparente.
inline Image renderShapes(const std::vector<Shape>& shapes, int W, int H) {
    Image c; c.w = W; c.h = H; c.rgba.assign(size_t(W) * H * 4, 0);
    std::vector<size_t> order(shapes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return shapes[a].z < shapes[b].z; });
    for (size_t oi : order) {
        const Shape& s = shapes[oi];
        double rad = s.rot * 3.14159265358979323846 / 180.0;
        double cs = std::cos(rad), sn = std::sin(rad);
        double hx = s.w * 0.5, hy = s.h * 0.5;
        double ex = std::abs(hx * cs) + std::abs(hy * sn), ey = std::abs(hx * sn) + std::abs(hy * cs);
        int x0 = std::max(0, int(std::floor(s.cx - ex - 0.5))), x1 = std::min(W - 1, int(std::ceil(s.cx + ex + 0.5)));
        int y0 = std::max(0, int(std::floor(s.cy - ey - 0.5))), y1 = std::min(H - 1, int(std::ceil(s.cy + ey + 0.5)));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                double dx = x + 0.5 - s.cx, dy = y + 0.5 - s.cy;
                double lx = dx * cs + dy * sn, ly = -dx * sn + dy * cs;
                if (lx >= -hx - 1e-4 && lx < hx + 1e-4 && ly >= -hy - 1e-4 && ly < hy + 1e-4) {
                    uint8_t* p = c.at(x, y);
                    p[0] = s.r; p[1] = s.g; p[2] = s.b; p[3] = 255;
                }
            }
    }
    return c;
}

// Igual que renderShapes pero a k veces la resolución (para ver líneas finas).
inline Image renderShapesScaled(const std::vector<Shape>& shapes, int W, int H, int k) {
    if (k <= 1) return renderShapes(shapes, W, H);
    std::vector<Shape> sc = shapes;
    for (auto& s : sc) { s.cx *= k; s.cy *= k; s.w *= k; s.h *= k; }
    return renderShapes(sc, W * k, H * k);
}

// PSNR (dB) entre el objetivo y el render. Píxeles presentes en sólo uno de los
// dos cuentan como error grande fijo.
inline double computePsnr(const Image& target, const Image& render, int alphaThr) {
    if (target.w != render.w || target.h != render.h) return 0;
    double err = 0; size_t cnt = 0;
    const double miss = 3.0 * 255.0 * 255.0 * 0.25;
    for (int y = 0; y < target.h; ++y)
        for (int x = 0; x < target.w; ++x) {
            const uint8_t* t = target.at(x, y); const uint8_t* r = render.at(x, y);
            bool to = t[3] >= alphaThr, ro = r[3] > 0;
            if (!to && !ro) continue;
            ++cnt;
            if (to && ro) {
                double d0 = double(t[0]) - r[0], d1 = double(t[1]) - r[1], d2 = double(t[2]) - r[2];
                err += d0 * d0 + d1 * d1 + d2 * d2;
            } else err += miss;
        }
    if (cnt == 0) return 99;
    double mse = err / (3.0 * cnt);
    if (mse < 1e-9) return 99;
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

inline double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

} // namespace gdx
