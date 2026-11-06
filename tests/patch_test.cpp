// Compara: trazar cada frame entero (metodo normal) vs fondo estatico + parches, con el mismo limite por frame.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "gifpatch.hpp"
#include <fstream>
#include <iostream>
using namespace gdx;

static double psnrFrame(const Image& target, const std::vector<Shape>& shapes, int rw, int rh) {
    Image r = renderShapes(shapes, rw, rh);
    return computePsnr(target, r, 128);
}

// lleva las formas de un Result (con su propio raster) al raster comun rw x rh
static std::vector<Shape> toCommon(const Result& r, int rw, int rh, int zoff) {
    std::vector<Shape> v = r.shapes;
    if (r.rasterW <= 0) return v;
    float sx = float(rw) / r.rasterW, sy = float(rh) / r.rasterH;
    for (auto& s : v) { s.cx *= sx; s.cy *= sy; s.w *= sx; s.h *= sy; s.z = int16_t(s.z + zoff); }
    return v;
}
int main(int argc, char** argv) {
    std::ifstream f(argc > 1 ? argv[1] : "tests/test.gif", std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    GifData g = decodeGif(bytes.data(), bytes.size());
    if (!g.ok()) { std::cout << "decode fail " << g.error << "\n"; return 1; }
    GifPlan plan = planFrames(g, 60, 1.0, 0.02);
    const int N = plan.count();
    std::cout << "GIF " << g.w << "x" << g.h << " frames=" << N << "\n";
    Method tracer = Method::BSP;
    if (argc > 2) tracer = Method(std::atoi(argv[2]));
    std::cout << "tracer=" << int(tracer) << "\n";
    std::cout << "limite | A: objs  PSNR | B(parches): objs  PSNR  (fondo/parche0/parche pixeles)\n";
    int TOL = argc > 3 ? std::atoi(argv[3]) : 28, MULT = argc > 4 ? std::atoi(argv[4]) : 6;
    std::cout << "tol=" << TOL << " mult=" << MULT << "\n";
    for (int B : {100}) {
        Options o; o.maxRaster = 384; o.quality = 0; o.lightSpecialChannels = true; o.maxChannels = 0; o.budget = B;
        // A: frames enteros
        auto resA = convertFrames(g, plan, tracer, o, nullptr, 4);
        size_t objA = 0; double psA = 0;
        // B: fondo + parches
        PatchSet ps = convertPatches(g, plan, tracer, o, TOL, B * MULT, nullptr, 4);
        size_t objB = ps.base.shapes.size(); double psB = 0;
        int rw = ps.rasterW, rh = ps.rasterH;
        int zo = patchMaxZ(ps.base) + 1;
        for (int i = 0; i < N; ++i) {
            const Image& orig = g.frames[size_t(plan.idx[size_t(i)])];
            Image tgt = (orig.w == rw && orig.h == rh) ? orig : resizeArea(orig, rw, rh);
            objA += resA[size_t(i)].shapes.size();
            psA += psnrFrame(tgt, toCommon(resA[size_t(i)], rw, rh, 0), rw, rh);
            std::vector<Shape> comp = toCommon(ps.base, rw, rh, 0);
            for (Shape s : toCommon(ps.patches[size_t(i)], rw, rh, zo)) comp.push_back(s);
            objB += ps.patches[size_t(i)].shapes.size();
            psB += psnrFrame(tgt, comp, rw, rh);
        }
        std::cout << B << " | " << objA << "  " << psA / N << " dB | " << objB << "  " << psB / N << " dB  ("
                  << ps.base.shapes.size() << " base, patch0 px=" << ps.patchPixels0 << "/" << ps.totalPixels
                  << ", still=" << ps.stillPixels << ")\n";
    }
    return 0;
}
