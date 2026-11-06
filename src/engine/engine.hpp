// ============================================================================
//  gdx :: trazador BSP (unico trazador: lo usa el metodo de parches)
//  Convierte una imagen RGBA (con o sin transparencia) en rectangulos de color.
// ============================================================================
#pragma once
#include "core.hpp"
#include "partition.hpp"

namespace gdx {

// Garantiza shapes.size() <= opt.budget. Los pixeles con alfa < alphaThreshold no generan objetos.
inline Result traceBSP(const Image& srcIn, const Options& opt, Progress* prog = nullptr) {
    Result res;
    Options o = opt;
    o.budget = std::clamp(o.budget, 1, 80000);
    o.maxRaster = std::clamp(o.maxRaster, 32, 2048);
    res.aspect = srcIn.h > 0 ? float(srcIn.w) / float(srcIn.h) : 1.f;
    if (srcIn.empty()) return res;
    res.hadAlpha = hasTransparency(srcIn, o.alphaThreshold);
    report(prog, 0.f);

    Image grayStore;
    const Image* sp = &srcIn;
    if (o.grayscale) { grayStore = toGrayscale(srcIn); sp = &grayStore; }
    const Image& src = *sp;

    int w, h;
    fitSize(src.w, src.h, o.maxRaster, w, h);
    Image ras = resizeArea(src, w, h);
    res.shapes = treeShapes(ras, o.budget, o.alphaThreshold, /*bsp=*/true, prog);
    res.rasterW = ras.w;
    res.rasterH = ras.h;

    if (int(res.shapes.size()) > o.budget) res.shapes.resize(size_t(o.budget));   // garantia dura
    report(prog, 1.f);
    return res;
}

} // namespace gdx
