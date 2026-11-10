// ============================================================================
//  gdx :: API pública del motor
// ============================================================================
#pragma once
#include "core.hpp"
#include "partition.hpp"
#include "regions.hpp"
#include "geometrize.hpp"
#include "sanitize.hpp"
#include "mono.hpp"

namespace gdx {

constexpr int kNormalMethodCount = 8;     // métodos "normales"; el resto son estilos monocromos
inline bool isMonoMethod(Method m) { return int(m) >= kNormalMethodCount; }

struct MethodInfo { const char* name; const char* desc; bool slow; };

inline const MethodInfo& methodInfo(Method m) {
    static const MethodInfo kInfo[int(Method::COUNT)] = {
        {"1. Cuadricula uniforme",
         "Una celda por objeto con el color medio. Rapido; ideal para pixel art.", false},
        {"2. Quadtree adaptativo",
         "Divide en 4 solo donde hay detalle. Usa justo el limite de objetos.", false},
        {"3. BSP adaptativo (k-d)",
         "Cortes optimos en X o Y: rectangulos de cualquier forma. Muy bueno en fotos.", false},
        {"4. Piramide de capas",
         "Bloque grande y capas de detalle solo donde el color cambia. Ahorra objetos.", false},
        {"5. Regiones por paleta",
         "Reduce la paleta y fusiona zonas iguales en rectangulos. Para ilustraciones.", false},
        {"6. Franjas horizontales",
         "Tiras de color por filas o columnas con tolerancia. Estilo escaneo, muy rapido.", false},
        {"7. Rectangulos rotados (puro)",
         "Fondo plano + SOLO rectangulos girados por busqueda aleatoria (estilo Primitive). Sin cuadricula: aspecto pictorico.", true},
        {"8. Hibrido BSP + rotados",
         "Base de bloques BSP (30 % de los objetos) + rectangulos girados solo donde queda error. Mas limpio que el 7.", true},
        {"9. Halftone (diamantes)",
         "Trama de periodico: diamantes cuyo tamano sigue la oscuridad. Sin HSV.", false},
        {"10. Grabado de lineas",
         "Lineas paralelas de grosor variable, como un grabado. Pocos objetos.", false},
        {"11. Sombreado cruzado",
         "Capas de rayas a 45, 135, 0 y 90 grados segun la oscuridad. Sin HSV.", false},
        {"12. Estarcido (puntos)",
         "Nube de puntos mas densa donde la imagen es oscura (puntillismo).", false},
        {"13. Tramado Bayer",
         "Blanco y negro con tramado ordenado, fusionado en rectangulos. Sin HSV.", false},
        {"14. Tinta (comic)",
         "Umbral adaptativo: manchas de tinta solidas, estilo comic. Muy pocos objetos.", false},
        {"15. Boceto de trazos",
         "Trazos cortos orientados sobre los bordes, como un dibujo a lapiz.", false},
        {"16. Espiral",
         "Una espiral cuyo grosor sigue la oscuridad de la imagen.", false},
        {"17. Diagonales 10 PRINT",
         "Una diagonal / o \\ por celda segun la estructura local y el tono.", false},
        {"18. Arte ASCII (texto)",
         "Texto en objetos de texto: la imagen en pocos objetos. Pide fuente monoespaciada.", false},
    };
    return kInfo[int(m)];
}

// Convierte la imagen a formas con el método indicado. Garantiza shapes.size() + textRows.size() <= opt.budget.
inline Result convertImpl(const Image& srcIn, Method method, const Options& opt, Progress* prog) {
    Result res;
    double t0 = nowMs();
    Options o = opt;
    o.budget = std::clamp(o.budget, 1, 80000);
    o.maxRaster = std::clamp(o.maxRaster, 32, 2048);
    o.maxChannels = std::clamp(o.maxChannels, 0, 997);
    res.aspect = srcIn.h > 0 ? float(srcIn.w) / float(srcIn.h) : 1.f;
    if (srcIn.empty()) return res;
    res.hadAlpha = hasTransparency(srcIn, o.alphaThreshold);
    report(prog, 0.f);

    const bool light = isMonoMethod(method);
    // Métodos principales en escala de grises: se trabaja con la imagen en grises desde el principio.
    Image grayStore;
    const Image* sp = &srcIn;
    if (!light && o.grayscale) { grayStore = toGrayscale(srcIn); sp = &grayStore; }
    const Image& src = *sp;

    auto treeRaster = [&](Image& ras) {
        int w, h; fitSize(src.w, src.h, o.maxRaster, w, h);
        ras = resizeArea(src, w, h);
    };

    // Las formas libres que se solapan no pueden dejar huecos transparentes:
    // con transparencia se usa BSP (que sí respeta los bordes) y se avisa.
    if (res.hadAlpha && (method == Method::Geometrize || method == Method::Hybrid)) {
        method = Method::BSP;
        res.note = "Imagen con transparencia: se uso BSP (los rectangulos rotados no pueden dejar huecos).";
    }

    Image reference;            // raster contra el que se mide el PSNR
    bool haveRef = false;
    switch (method) {
        case Method::UniformGrid: {
            int w, h; res.shapes = gridShapes(src, o.budget, o.alphaThreshold, w, h);
            res.rasterW = w; res.rasterH = h;
            reference = resizeArea(src, w, h); haveRef = true;
            break;
        }
        case Method::Quadtree:
        case Method::BSP: {
            Image ras; treeRaster(ras);
            res.shapes = treeShapes(ras, o.budget, o.alphaThreshold, method == Method::BSP, prog);
            res.rasterW = ras.w; res.rasterH = ras.h; reference = std::move(ras); haveRef = true;
            break;
        }
        case Method::Pyramid: {
            Image ras; treeRaster(ras);
            res.shapes = pyramidShapes(ras, o.budget, o.alphaThreshold, prog);
            res.rasterW = ras.w; res.rasterH = ras.h; reference = std::move(ras); haveRef = true;
            break;
        }
        case Method::Regions: {
            Candidate c = regionsBest(src, o, prog);
            res.shapes = std::move(c.shapes); res.rasterW = c.w; res.rasterH = c.h; res.psnr = c.psnr;
            break;
        }
        case Method::Runs: {
            Candidate c = runsBest(src, o, prog);
            res.shapes = std::move(c.shapes); res.rasterW = c.w; res.rasterH = c.h; res.psnr = c.psnr;
            break;
        }
        case Method::Geometrize:
        case Method::Hybrid: {
            int w, h; Image target;
            res.shapes = geometrizeShapes(src, o, method == Method::Hybrid ? hybridFractionFor(o) : 0.0, prog, w, h, &target);
            res.rasterW = w; res.rasterH = h; reference = std::move(target); haveRef = true;
            if (method == Method::Hybrid && !res.shapes.empty() && std::all_of(res.shapes.begin(), res.shapes.end(), [](const Shape& s) { return s.rot == 0.f; }))
                res.note = "El BSP solo ya es casi perfecto (>= 45 dB): no hicieron falta rectangulos girados.";
            break;
        }
        case Method::Halftone: case Method::Engrave: case Method::Hatch: case Method::Stipple:
        case Method::Dither: case Method::Ink: case Method::Strokes: case Method::Spiral: case Method::Truchet:
        case Method::Ascii: {
            res.mono = true;
            const bool shapesStyle = method != Method::Ascii;
            Options os = o;
            if (o.colorChannels && shapesStyle) os.budget = std::max(50, int(o.budget * 0.65));   // 35 % queda para la base de color
            res.shapes = monoShapes(src, method, os, res, prog);
            if (shapesStyle) {
                if (o.colorChannels && !isCancelled(prog)) {
                    int baseBudget = std::min(o.budget - int(res.shapes.size()), int(o.budget * 0.4));
                    colorizeLightShapes(res, src, o, std::max(20, baseBudget));
                } else {
                    res.colored = !o.lightSpecialChannels;       // blanco y negro: 2 canales libres (o los especiales 1010/1011)
                }
            }
            if (res.colored && !o.colorChannels && o.maxChannels < 2) {       // sin canales libres: especiales 1010/1011
                res.colored = false;
                for (auto& t : res.textRows) t.colored = false;
                res.note = "No hay canales libres: se usan los especiales 1010/1011.";
            }
            res.psnr = -1;                       // la fidelidad tonal no se mide en dB para tramas
            break;
        }
        default: break;
    }
    if (isCancelled(prog)) res.cancelled = true;

    // Métodos principales con canales de color (sin HSV): paleta de K colores, cada uno = un canal.
    if (!light && o.mainChannels && o.maxChannels >= 2 && !res.shapes.empty()) {
        int K = o.grayscale ? std::min(o.mainPaletteSize, 32) : o.mainPaletteSize;
        K = std::clamp(K, 2, std::max(2, o.maxChannels));
        quantizeShapesToPalette(res.shapes, K);
        res.colored = true;
        if (method == Method::Regions || method == Method::Runs) {         // su PSNR se calculó antes de la paleta
            int rw, rh; fitSize(src.w, src.h, 512, rw, rh);
            res.psnr = psnrScaled(resizeArea(src, rw, rh), res.shapes, res.rasterW, res.rasterH, o.alphaThreshold);
        }
    }

    // Garantía dura del presupuesto (no debería activarse).
    if (int(res.shapes.size() + res.textRows.size()) > o.budget) {
        size_t keep = size_t(std::max(0, o.budget));
        if (res.textRows.size() > keep) res.textRows.resize(keep);
        res.shapes.resize(std::min(res.shapes.size(), keep - res.textRows.size()));
    }

    if (haveRef && !res.mono) {
        Image r = renderShapes(res.shapes, res.rasterW, res.rasterH);
        res.psnr = computePsnr(reference, r, o.alphaThreshold);
    }
    res.ms = nowMs() - t0;
    report(prog, 1.f);
    return res;
}

// Punto de entrada UNICO: convierte y aplica el saneado comun (formas dentro del lienzo, z-order compacto).
inline Result convert(const Image& srcIn, Method method, const Options& opt, Progress* prog = nullptr) {
    Result r = convertImpl(srcIn, method, opt, prog);
    sanitizeResult(r);
    return r;
}

} // namespace gdx
