// ============================================================================
//  gdx :: METODO DE PARCHES para GIFs ("Estatico + parches")
//
//  >>> Este archivo es el unico que contiene el algoritmo del metodo: para actualizar el metodo
//  >>> basta con reemplazarlo (la ventana solo usa convertPatches / buildGifPatchString /
//  >>> combineForPreview / patchBaseBudget / patchPerFrameBudget y las estructuras PatchParams / PatchSet).
//
//  Idea:
//    1. FONDO ESTATICO: cada pixel toma su color mediano a lo largo del tiempo (solo donde es opaco en
//       todos los frames). Se traza UNA vez, con mas presupuesto, y se queda siempre visible (sin grupo).
//    2. PARCHES: para cada frame se trazan solo los pixeles que se alejan del fondo (diferencia > tolerancia,
//       o que no son opacos siempre). Cada parche va en el grupo de su frame y se anima con Alpha + Spawn.
//  Trazador: BSP adaptativo (engine.hpp).
// ============================================================================
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <thread>
#include <vector>
#include "engine.hpp"
#include "gifanim.hpp"

namespace gdx {

struct PatchParams {
    int tolerance = 28;                  // diferencia de color (0-255) para que un pixel cuente como "cambiado"
    int baseBudget = 1800;               // objetos del fondo estatico
    int threads = 2;                     // hilos de calculo
    int maxPatches = 1 << 30;            // < N: solo calcula los primeros (vista previa rapida)
};

struct PatchSet {
    Result base;                         // fondo estatico (siempre visible)
    std::vector<Result> patches;         // un parche por frame del plan (puede estar vacio)
    int rasterW = 0, rasterH = 0;
    size_t stillPixels = 0, totalPixels = 0;
    size_t patchPixels0 = 0;             // pixeles que cambian en el parche 0 (informativo)
};

// Presupuesto: el fondo usa baseMult veces el limite de un parche y no pasa de un tercio del tope total.
inline int patchBaseBudget(int perFrame, int baseMult, int maxTotal) {
    return std::clamp(perFrame * std::max(1, baseMult), 1, std::max(1, maxTotal / 3));
}
// Objetos por parche que caben en el tope total una vez descontado el fondo.
inline int patchPerFrameBudget(int requested, int frames, int maxTotal, int baseBudget) {
    return gifPerFrameBudget(requested, frames, maxTotal - baseBudget);
}

namespace detail {
inline Result emptyResult(int rw, int rh, float aspect) {
    Result r; r.rasterW = rw; r.rasterH = rh; r.aspect = aspect; return r;
}

// Traza una imagen con transparencia (mascara de parche). El arbol descarta las celdas con menos de la mitad
// de pixeles opacos, y un parche pequeno de color uniforme nunca se divide: desapareceria. Por eso se recorta
// a la caja de pixeles opacos, se traza, y las formas se llevan de vuelta al raster comun rw x rh.
// Si aun asi sale vacio, cuadricula exacta de respaldo.
inline Result traceMasked(const Image& img, const Options& opt, int rw, int rh, float aspect) {
    const int thr = std::clamp(opt.alphaThreshold, 1, 255);
    int x0 = img.w, y0 = img.h, x1 = -1, y1 = -1;
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x)
            if (img.at(x, y)[3] >= thr) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    if (x1 < x0) return emptyResult(rw, rh, aspect);
    const int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
    Image crop; crop.w = cw; crop.h = ch; crop.rgba.resize(size_t(cw) * size_t(ch) * 4);
    for (int y = 0; y < ch; ++y)
        std::copy(img.at(x0, y0 + y), img.at(x0, y0 + y) + size_t(cw) * 4, crop.rgba.begin() + size_t(y) * size_t(cw) * 4);

    Options o = opt;
    o.maxRaster = std::max({o.maxRaster, cw, ch});                   // sin reescalar el recorte
    Result r = traceBSP(crop, o, nullptr);
    if (r.shapes.empty() || r.rasterW <= 0) {                         // respaldo: cuadricula exacta
        int gw = 0, gh = 0;
        r = Result{};
        r.shapes = gridShapes(crop, std::max(1, o.budget), thr, gw, gh);
        r.rasterW = gw; r.rasterH = gh;
    }
    if (r.shapes.empty() || r.rasterW <= 0 || r.rasterH <= 0) return emptyResult(rw, rh, aspect);
    const float sx = float(cw) / float(r.rasterW), sy = float(ch) / float(r.rasterH);
    for (Shape& sh : r.shapes) {
        sh.cx = float(x0) + sh.cx * sx; sh.cy = float(y0) + sh.cy * sy;
        sh.w *= sx; sh.h *= sy;
    }
    r.rasterW = rw; r.rasterH = rh; r.aspect = aspect;
    return r;
}
} // namespace detail

inline PatchSet convertPatches(const GifData& g, const GifPlan& plan, const Options& opt,
                               const PatchParams& pp, Progress* prog) {
    PatchSet ps;
    const int N = plan.count();
    if (N < 2 || g.frames.empty()) return ps;
    int rw, rh;
    fitSize(g.w, g.h, std::clamp(opt.maxRaster, 32, 2048), rw, rh);
    ps.rasterW = rw; ps.rasterH = rh;
    const float aspect = rh > 0 ? float(g.w) / float(g.h) : 1.f;

    // --- frames a la resolucion de trabajo (sin copiar si ya coinciden); escala de grises si se pide
    std::vector<Image> store;
    std::vector<const Image*> fr(size_t(N), nullptr);
    const bool same = (rw == g.w && rh == g.h);
    store.resize(size_t(N));
    for (int i = 0; i < N; ++i) {
        const Image& src = g.frames[size_t(plan.idx[size_t(i)])];
        if (same && !opt.grayscale) { fr[size_t(i)] = &src; continue; }
        Image im = same ? src : resizeArea(src, rw, rh);
        if (opt.grayscale) im = toGrayscale(im);
        store[size_t(i)] = std::move(im);
        fr[size_t(i)] = &store[size_t(i)];
    }
    if (isCancelled(prog)) return ps;

    // --- fondo: mediana temporal por pixel, solo donde el pixel es opaco en TODOS los frames
    const int thr = std::clamp(opt.alphaThreshold, 1, 255);
    const size_t np = size_t(rw) * size_t(rh);
    Image baseImg; baseImg.w = rw; baseImg.h = rh; baseImg.rgba.assign(np * 4, 0);
    std::vector<uint8_t> opaqueAlways;
    opaqueAlways.assign(np, 0);
    std::vector<uint8_t> tmp;
    tmp.resize(size_t(N));
    for (size_t p = 0; p < np; ++p) {
        bool all = true;
        for (int i = 0; i < N; ++i) if (fr[size_t(i)]->rgba[p * 4 + 3] < thr) { all = false; break; }
        if (!all) continue;
        opaqueAlways[p] = 1;
        for (int c = 0; c < 3; ++c) {
            for (int i = 0; i < N; ++i) tmp[size_t(i)] = fr[size_t(i)]->rgba[p * 4 + size_t(c)];
            auto mid = tmp.begin() + N / 2;
            std::nth_element(tmp.begin(), mid, tmp.end());
            baseImg.rgba[p * 4 + size_t(c)] = *mid;
        }
        baseImg.rgba[p * 4 + 3] = 255;
        ++ps.stillPixels;
    }
    ps.totalPixels = np;

    // --- mascara de parche de cada frame
    // kRing > 0: anillo (en pixeles) alrededor de lo que cambia, con el color real del frame. Asi el trazador ve
    // el borde del cambio y lo divide; sin el anillo, un sprite plano se pintaria como un rectangulo entero.
    // Solo se usa si la traza normal (solo mascara) se queda muy corta de objetos; en imagenes con detalle
    // el anillo gasta presupuesto de mas y empeora el resultado.
    const int tol = std::clamp(pp.tolerance, 1, 255);
    auto makePatch = [&](int i, size_t* count, int kRing) {
        Image pi; pi.w = rw; pi.h = rh; pi.rgba.assign(np * 4, 0);
        const uint8_t* f = fr[size_t(i)]->rgba.data();
        std::vector<uint8_t> changed;
        changed.assign(np, 0);
        size_t n = 0;
        for (size_t p = 0; p < np; ++p) {
            if (f[p * 4 + 3] < thr) continue;                                  // transparente en este frame
            bool diff = !opaqueAlways[p];
            if (!diff) {
                const uint8_t* b = &baseImg.rgba[p * 4];
                int d = std::max({std::abs(int(f[p * 4]) - int(b[0])),
                                  std::abs(int(f[p * 4 + 1]) - int(b[1])),
                                  std::abs(int(f[p * 4 + 2]) - int(b[2]))});
                diff = d > tol;
            }
            if (diff) { changed[p] = 1; ++n; }
        }
        if (count) *count = n;
        if (n == 0) return pi;
        std::vector<uint8_t> hz;                                               // dilatacion separable de radio kRing
        hz.assign(np, 0);
        for (int y = 0; y < rh; ++y)
            for (int x = 0; x < rw; ++x) {
                uint8_t v = 0;
                for (int dx = -kRing; dx <= kRing && !v; ++dx) {
                    int xx = x + dx;
                    if (xx >= 0 && xx < rw && changed[size_t(y) * size_t(rw) + size_t(xx)]) v = 1;
                }
                hz[size_t(y) * size_t(rw) + size_t(x)] = v;
            }
        for (int y = 0; y < rh; ++y)
            for (int x = 0; x < rw; ++x) {
                const size_t p = size_t(y) * size_t(rw) + size_t(x);
                if (f[p * 4 + 3] < thr) continue;
                bool inRing = changed[p] != 0;
                for (int dy = -kRing; dy <= kRing && !inRing; ++dy) {
                    int yy = y + dy;
                    if (yy >= 0 && yy < rh && hz[size_t(yy) * size_t(rw) + size_t(x)]) inRing = true;
                }
                if (!inRing) continue;
                pi.rgba[p * 4] = f[p * 4]; pi.rgba[p * 4 + 1] = f[p * 4 + 1];
                pi.rgba[p * 4 + 2] = f[p * 4 + 2]; pi.rgba[p * 4 + 3] = 255;
            }
        return pi;
    };

    // --- trazado del fondo (una vez) y de los parches (en paralelo)
    const int M = std::min(N, std::max(1, pp.maxPatches));
    ps.patches.resize(size_t(N));
    Options ob = opt;
    ob.budget = std::max(1, pp.baseBudget);
    if (ps.stillPixels > 0) {
        try { ps.base = detail::traceMasked(baseImg, ob, rw, rh, aspect); } catch (...) { ps.base = Result{}; }
    }
    if (ps.base.rasterW <= 0) ps.base = detail::emptyResult(rw, rh, aspect);
    report(prog, 1.f / float(M + 1));

    std::atomic<int> next{0}, done{1};
    const int threads = std::clamp(pp.threads, 1, std::max(1, M));
    auto worker = [&]() {
        for (;;) {
            if (isCancelled(prog)) return;
            int i = next.fetch_add(1);
            if (i >= M) return;
            size_t cnt = 0;
            Image pi = makePatch(i, &cnt, 0);
            if (i == 0) ps.patchPixels0 = cnt;
            Result r;
            if (cnt > 0) {
                try {
                    r = detail::traceMasked(pi, opt, rw, rh, aspect);
                    if (int(r.shapes.size()) * 4 < opt.budget) {             // sprite plano: el arbol no se dividio
                        Image p2 = makePatch(i, nullptr, 2);
                        Result r2 = detail::traceMasked(p2, opt, rw, rh, aspect);
                        if (r2.shapes.size() > r.shapes.size()) r = std::move(r2);
                    }
                } catch (...) { r = Result{}; }
            }
            if (r.rasterW <= 0) r = detail::emptyResult(rw, rh, aspect);
            ps.patches[size_t(i)] = std::move(r);
            report(prog, float(done.fetch_add(1) + 1) / float(M + 1));
        }
    };
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; ++t) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();
    for (auto& r : ps.patches) if (r.rasterW <= 0) r = detail::emptyResult(rw, rh, aspect);
    return ps;
}

inline int patchMaxZ(const Result& r) {
    int z = 0;
    for (const Shape& s : r.shapes) z = std::max(z, int(s.z));
    return z;
}

// Cadena completa de GD: fondo (sin grupo) + parches (un grupo por frame) + triggers.
inline std::string buildGifPatchString(const PatchSet& ps, const GifPlan& plan, GifExportParams p,
                                       std::string* errmsg = nullptr) {
    auto fail = [&](const char* m) { if (errmsg) *errmsg = m; return std::string(); };
    const int N = plan.count();
    if (N < 2 || int(ps.patches.size()) != N) return fail("Hacen falta al menos 2 frames");

    std::string base;
    if (!ps.base.shapes.empty()) {
        GdExportOptions eo = p.eo;
        eo.groupStr.clear();
        base = toGdString(ps.base, eo);
    }
    const int G0 = p.baseGroup;
    GdExportOptions pe = p.eo;
    pe.zBase += patchMaxZ(ps.base) + 1;                   // los parches siempre por encima del fondo
    std::string patches;
    size_t total = ps.base.shapes.size();
    for (int i = 0; i < N; ++i) {
        const Result& r = ps.patches[size_t(i)];
        total += r.shapes.size();
        if (r.shapes.empty()) continue;
        pe.groupStr = std::to_string(G0 + i);
        patches = joinGd(patches, toGdString(r, pe));
    }
    if (total == 0) return fail("Ningun frame genero objetos");

    // triggers: cuadricula encima de la imagen, siempre con x >= 0
    GdSize sz = gdImageSize(ps.base, p.eo);
    const double left = std::max(p.eo.originX - sz.w * 0.5, 0.0);
    const double top = p.eo.originY + sz.h * 0.5 + 45.0;
    const int rows = (gifTriggerCount(N) + 29) / 30;
    std::string err;
    std::string trig = buildGifTriggers(plan, p, left, top, top + (rows + 1) * 30.0, &err);
    if (trig.empty()) return fail(err.empty() ? "No se pudieron generar los triggers" : err.c_str());
    return joinGd(joinGd(base, patches), trig);
}

// Fondo + parche 0 en un solo Result (vista previa del frame 0).
inline Result combineForPreview(const PatchSet& ps) {
    Result r = ps.base;
    r.rasterW = ps.rasterW; r.rasterH = ps.rasterH;
    r.shapes.clear();
    auto add = [&](const Result& src, int zoff) {
        if (src.rasterW <= 0 || src.rasterH <= 0) return;
        float sx = float(ps.rasterW) / float(src.rasterW), sy = float(ps.rasterH) / float(src.rasterH);
        for (Shape s : src.shapes) {
            s.cx *= sx; s.cy *= sy; s.w *= sx; s.h *= sy; s.z = int16_t(s.z + zoff);
            r.shapes.push_back(s);
        }
    };
    add(ps.base, 0);
    if (!ps.patches.empty()) {
        add(ps.patches[0], patchMaxZ(ps.base) + 1);
        if (!ps.patches[0].note.empty()) r.note = ps.patches[0].note;
    }
    return r;
}

} // namespace gdx
