// ============================================================================
//  gdx :: Decodificador de GIF propio (sin dependencias)
//  Por que existe: stb_image compone mal los GIF con "disposal 3" (restaurar al frame anterior) y los errores se ACUMULAN: los
//  ultimos frames salen con "estatica"/restos de otros frames. Aqui la composicion sigue la especificacion (y lo que hacen Pillow y
//  los navegadores) para los 4 metodos de disposal, transparencia, tablas de color locales/globales, entrelazado y subrectangulos.
//  Tambien decodifica en STREAMING: si w*h*4*frames pasa de maxBytes, cada frame se reduce (promedio k x k) al decodificarlo.
//  Un archivo truncado o corrupto devuelve los frames que se hayan podido leer (no revienta).
// ============================================================================
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace gdx {
namespace gifdec {

struct Out {
    int srcW = 0, srcH = 0, w = 0, h = 0;
    std::vector<uint8_t> rgba;          // frames guardados, uno tras otro (w*h*4 cada uno)
    std::vector<int> delaysMs;
    int frames = 0;
};

struct Cur {
    const uint8_t* d; size_t n; size_t i = 0;
    bool has(size_t k) const { return i + k <= n; }
    int u8() { return i < n ? d[i++] : -1; }
    int u16() { if (!has(2)) { i = n; return -1; } int v = d[i] | (d[i + 1] << 8); i += 2; return v; }
    bool skipSub() {                                   // salta una cadena de sub-bloques; false si se acaba el archivo
        for (;;) {
            int sz = u8();
            if (sz < 0) return false;
            if (sz == 0) return true;
            if (!has(size_t(sz))) { i = n; return false; }
            i += size_t(sz);
        }
    }
};

// LZW de GIF. data = bytes comprimidos ya concatenados. Rellena idx (npix) y devuelve cuantos pixeles se pudieron decodificar.
inline size_t lzw(const std::vector<uint8_t>& data, int minCode, uint8_t* idx, size_t npix) {
    if (minCode < 1 || minCode > 11) return 0;
    const int clear = 1 << minCode, eoi = clear + 1;
    uint16_t prefix[4096]; uint8_t suffix[4096], first[4096];
    uint8_t stack[4097];
    int next = eoi + 1, size = minCode + 1, prev = -1;
    uint32_t acc = 0; int bits = 0; size_t pos = 0, out = 0;
    for (int c = 0; c < clear; ++c) { prefix[c] = 0; suffix[c] = uint8_t(c); first[c] = uint8_t(c); }
    while (out < npix) {
        while (bits < size) { if (pos >= data.size()) return out; acc |= uint32_t(data[pos++]) << bits; bits += 8; }
        int code = int(acc & ((1u << size) - 1)); acc >>= size; bits -= size;
        if (code == clear) { next = eoi + 1; size = minCode + 1; prev = -1; continue; }
        if (code == eoi) return out;
        int cur = code; int sp = 0;
        if (prev < 0) {                                   // primer codigo tras un clear
            if (code >= clear) return out;                // corrupto
            idx[out++] = uint8_t(code); prev = code; continue;
        }
        if (code >= next) {                               // caso KwKwK: cadena(prev) + primer simbolo de prev
            if (code > next) return out;                  // corrupto
            stack[sp++] = first[prev]; cur = prev;
        }
        while (cur >= clear) {
            if (sp >= 4096) return out;
            stack[sp++] = suffix[cur]; cur = prefix[cur];
        }
        stack[sp++] = uint8_t(cur);
        const uint8_t f = uint8_t(cur);
        while (sp > 0 && out < npix) idx[out++] = stack[--sp];
        if (next < 4096) {                                // anadir entrada al diccionario
            prefix[next] = uint16_t(prev); suffix[next] = f; first[next] = first[prev]; ++next;
            if (next == (1 << size) && size < 12) ++size;
        }
        prev = code;
    }
    return out;
}

// Cuenta frames (sin decodificar los pixeles). Devuelve false si no parece un GIF.
inline bool scan(const uint8_t* d, size_t n, int& W, int& H, int& frames) {
    Cur c{d, n};
    if (n < 13 || std::memcmp(d, "GIF8", 4) != 0) return false;
    c.i = 6; W = c.u16(); H = c.u16(); int flags = c.u8(); c.u8(); c.u8();
    if (W <= 0 || H <= 0) return false;
    if (flags & 0x80) c.i += size_t(3) << ((flags & 7) + 1);
    frames = 0;
    while (c.i < n) {
        int b = c.u8();
        if (b == 0x3B || b < 0) break;
        if (b == 0x21) { c.u8(); if (!c.skipSub()) break; }
        else if (b == 0x2C) {
            if (!c.has(9)) break;
            c.u16(); c.u16(); c.u16(); c.u16(); int pk = c.u8();
            if (pk & 0x80) c.i += size_t(3) << ((pk & 7) + 1);
            c.u8();
            if (!c.skipSub()) { ++frames; break; }
            ++frames;
        } else break;
    }
    return frames > 0;
}

inline bool decode(const uint8_t* d, size_t n, size_t maxBytes, Out& o) {
    int W, H, count;
    if (!scan(d, n, W, H, count)) return false;
    if (int64_t(W) * H > 100000000LL) return false;                 // > 100 Mpx: absurdo
    int k = 1;
    while (size_t((W + k - 1) / k) * size_t((H + k - 1) / k) * 4u * size_t(count) > maxBytes && k < 64) ++k;
    const int ow = (W + k - 1) / k, oh = (H + k - 1) / k;
    o.srcW = W; o.srcH = H; o.w = ow; o.h = oh;
    o.rgba.reserve(size_t(ow) * oh * 4 * size_t(count)); o.delaysMs.reserve(size_t(count));

    Cur c{d, n}; c.i = 6; c.u16(); c.u16(); int flags = c.u8(); c.u8(); c.u8();
    std::vector<uint8_t> gct;
    if (flags & 0x80) { size_t sz = size_t(3) << ((flags & 7) + 1); if (!c.has(sz)) return false; gct.assign(d + c.i, d + c.i + sz); c.i += sz; }

    std::vector<uint8_t> canvas(size_t(W) * H * 4, 0), saved;       // saved: copia para disposal 3
    int pDispose = 0, pX = 0, pY = 0, pW = 0, pH = 0; bool havePrev = false;
    int gDispose = 0, gTransp = -1, gDelay = 10;
    std::vector<uint8_t> comp, idx;

    while (c.i < n && o.frames < count) {
        int b = c.u8();
        if (b == 0x3B || b < 0) break;
        if (b == 0x21) {
            int lab = c.u8();
            if (lab == 0xF9 && c.has(6) && d[c.i] == 4) {            // Graphic Control Extension
                int pk = d[c.i + 1]; gDispose = (pk >> 2) & 7;
                gDelay = d[c.i + 2] | (d[c.i + 3] << 8); gTransp = (pk & 1) ? d[c.i + 4] : -1;
                c.i += 5; if (c.u8() != 0) { /* terminador raro: se ignora */ }
            } else if (!c.skipSub()) break;
            continue;
        }
        if (b != 0x2C) break;
        if (!c.has(9)) break;
        int fx = c.u16(), fy = c.u16(), fw = c.u16(), fh = c.u16(), pk = c.u8();
        std::vector<uint8_t> lct; const std::vector<uint8_t>* pal = &gct;
        if (pk & 0x80) { size_t sz = size_t(3) << ((pk & 7) + 1); if (!c.has(sz)) break; lct.assign(d + c.i, d + c.i + sz); c.i += sz; pal = &lct; }
        const bool interlaced = (pk & 0x40) != 0;
        int mcs = c.u8();
        comp.clear();
        bool truncated = false;
        for (;;) {
            int sz = c.u8();
            if (sz < 0) { truncated = true; break; }
            if (sz == 0) break;
            size_t take = std::min(size_t(sz), n - c.i);
            comp.insert(comp.end(), d + c.i, d + c.i + take); c.i += take;
            if (take < size_t(sz)) { truncated = true; break; }
        }

        // 1) disposal del frame anterior
        if (havePrev) {
            if (pDispose == 2) {
                for (int y = std::max(0, pY); y < std::min(H, pY + pH); ++y)
                    for (int x = std::max(0, pX); x < std::min(W, pX + pW); ++x) std::memset(&canvas[(size_t(y) * W + x) * 4], 0, 4);
            } else if (pDispose == 3 && !saved.empty()) canvas = saved;
        }
        // 2) disposal 3: guardar el lienzo ANTES de dibujar este frame
        // (en el PRIMER frame, "restaurar al anterior" = lienzo vacio, como la especificacion y los navegadores)
        if (gDispose == 3) saved = canvas; else saved.clear();

        // 3) pintar
        if (fw > 0 && fh > 0 && fx < W && fy < H && !(fw > 65535)) {
            const size_t npix = size_t(fw) * size_t(fh);
            idx.assign(npix, 0xFF); std::vector<uint8_t> valid;
            size_t got = lzw(comp, mcs, idx.data(), npix);
            const size_t palN = pal->size() / 3;
            // orden de filas
            std::vector<int> rowY; rowY.resize(size_t(fh));
            if (!interlaced) for (int r = 0; r < fh; ++r) rowY[size_t(r)] = r;
            else {
                int r = 0; static const int start[4] = {0, 4, 2, 1}, step[4] = {8, 8, 4, 2};
                for (int p = 0; p < 4; ++p) for (int y = start[p]; y < fh; y += step[p]) rowY[size_t(r++)] = y;
            }
            for (int r = 0; r < fh; ++r) {
                const int y = fy + rowY[size_t(r)];
                if (y < 0 || y >= H) continue;
                for (int x = 0; x < fw; ++x) {
                    const size_t pi = size_t(r) * size_t(fw) + size_t(x);
                    if (pi >= got) break;                                   // pixeles que no se pudieron decodificar: se dejan como estaban
                    const int X = fx + x; if (X < 0 || X >= W) continue;
                    const int v = idx[pi];
                    if (v == gTransp || size_t(v) >= palN) continue;
                    uint8_t* q = &canvas[(size_t(y) * W + X) * 4];
                    q[0] = (*pal)[size_t(v) * 3]; q[1] = (*pal)[size_t(v) * 3 + 1]; q[2] = (*pal)[size_t(v) * 3 + 2]; q[3] = 255;
                }
            }
        }

        // 4) guardar el frame (reducido si hace falta)
        const size_t base = o.rgba.size(); o.rgba.resize(base + size_t(ow) * oh * 4);
        uint8_t* dst = &o.rgba[base];
        if (k == 1) std::memcpy(dst, canvas.data(), canvas.size());
        else {
            for (int y = 0; y < oh; ++y)
                for (int x = 0; x < ow; ++x) {
                    unsigned sum[3] = {0, 0, 0}, aw = 0, as = 0, cnt = 0;
                    for (int yy = y * k; yy < std::min(H, y * k + k); ++yy)
                        for (int xx = x * k; xx < std::min(W, x * k + k); ++xx) {
                            const uint8_t* q = &canvas[(size_t(yy) * W + xx) * 4];
                            sum[0] += q[0] * q[3]; sum[1] += q[1] * q[3]; sum[2] += q[2] * q[3]; aw += q[3]; as += q[3]; ++cnt;
                        }
                    uint8_t* e = dst + (size_t(y) * ow + x) * 4;
                    if (aw) { e[0] = uint8_t(sum[0] / aw); e[1] = uint8_t(sum[1] / aw); e[2] = uint8_t(sum[2] / aw); e[3] = uint8_t(as / cnt); }
                    else e[0] = e[1] = e[2] = e[3] = 0;
                }
        }
        o.delaysMs.push_back(gDelay * 10);
        ++o.frames;
        pDispose = gDispose; pX = fx; pY = fy; pW = fw; pH = fh; havePrev = true;
        gDispose = 0; gTransp = -1; gDelay = 10;                        // la GCE vale solo para el frame que le sigue
        if (truncated) break;
    }
    return o.frames > 0;
}

} // namespace gifdec
} // namespace gdx
