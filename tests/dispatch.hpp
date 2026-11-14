// Despacho de metodos para los tests (mismo flujo que GifPopup): 0-16 planos, 17 capas, 18 arbol, 19+ metodos nuevos.
#pragma once
#include "chanframes.hpp"
#include "deltatree.hpp"
#include "gdsim.hpp"
namespace gdx {
struct BuiltAnim { std::string chain, err, note; size_t objects = 0; int triggers = 0, groups = 0; };
inline size_t countObjs(const std::string& chain) { size_t c = 0; for (char ch : chain) if (ch == ';') ++c; return c; }
inline BuiltAnim buildForTest(const GifData& g, const GifPlan& plan, int mi, int budget, const GifExportParams& p, int maxRaster) {
    BuiltAnim r;
    Options o; o.maxRaster = maxRaster; o.quality = 0; o.lightSpecialChannels = true; o.maxChannels = 0; o.colorChannels = false; o.mainChannels = false; o.budget = budget;
    if (mi >= 17) {
        LayeredOptions lo; lo.perFrame = budget; lo.tree = mi == 18; lo.maxTotal = 200000; lo.channels = 0; lo.firstChannel = 2; lo.method = mi >= 19 ? mi - 18 : 0;
        LayeredAnim A = buildAnim(g, plan, o, lo, nullptr, 4);
        r.note = A.note; r.chain = buildAnimString(A, plan, p, &r.err); r.triggers = animTriggerCount(A); r.groups = animGroupCount(A);
    } else if (mi < 17) {
        auto res = convertFrames(g, plan, Method(mi), o, nullptr, 4);
        r.chain = buildGifString(res, plan, p, &r.err); r.triggers = gifTriggerCount(plan.count()); r.groups = gifGroupCount(plan.count());
    } else { r.err = "metodo no implementado en dispatch"; return r; }
    r.objects = r.chain.empty() ? 0 : countObjs(r.chain);
    return r;
}
} // namespace gdx
