// gsquare onto HSP memory bitmaps: Windows HSP's software polygon filler
// (OpenHSP src/hsp3/win32gui/fcpoly.cpp, ScanLine, DrawPolygonTex and
// DrawPolygonF4, with hsp3gr_wingui.cpp's gsquare argument handling and
// Bmscr::GetAlphaOperation), operating on the same 24-bit bottom-up DIB
// memory (buffer_surface.hpp). OpenHSP's license is in HSP-LICENSE.txt.
//
// Precision: ScanLine steps edges in `double`, but in the game it runs after
// ZGP's createdevm, which (Direct3D 8 without FPU_PRESERVE) puts the x87
// unit in single precision for the thread; every step then rounds to 24
// bits, which float arithmetic reproduces. x87_single_precision() selects it.
#pragma once

#include "buffer_surface.hpp"
#include "x87_state.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace cp2 {

namespace fcpoly {

struct Vertex {
    std::int16_t x, y, tx, ty;  // IAXIS2: shorts
};

struct Scan {
    int min_x, max_x, min_tx, min_ty, max_tx, max_ty;
};

// ScanLine: each edge's x and texture coordinates per row, the leftmost and
// rightmost kept; rows start at the first edge row and end at the last.
template <typename Real>
inline void scan_line(const Vertex v[4], int width, int height, std::vector<Scan>& scan, int& start, int& end) {
    start = height;
    end = -1;
    scan.assign(static_cast<std::size_t>(std::max(height, 0)), Scan{width, -1, 0, 0, 0, 0});
    for (int i = 0; i < 4; ++i) {
        const Vertex* a = &v[i];
        const Vertex* b = &v[(i + 1) % 4];
        if (a->y == b->y) {
            const int y = a->y;
            if (y < height && y >= 0) {
                start = std::min(y, start);
                end = std::max(y, end);
                if (a->x > b->x) std::swap(a, b);
                Scan& s = scan[static_cast<std::size_t>(y)];
                if (a->x < s.min_x) { s.min_x = a->x; s.min_tx = a->tx; s.min_ty = a->ty; }
                if (b->x > s.max_x) { s.max_x = b->x; s.max_tx = b->tx; s.max_ty = b->ty; }
            }
            continue;
        }
        if (a->y > b->y) std::swap(a, b);
        const int y1 = a->y, y2 = b->y;
        if (y1 < height || y2 >= 0) {  // as written in fcpoly.cpp
            start = std::min(y1, start);
            end = std::max(y2, end);
            const Real step = static_cast<Real>(1.0f) / static_cast<Real>(y2 - y1);
            const Real dx = static_cast<Real>(b->x - a->x) * step;
            const Real tdx = static_cast<Real>(b->tx - a->tx) * step;
            const Real tdy = static_cast<Real>(b->ty - a->ty) * step;
            Real x = static_cast<Real>(a->x), tx = static_cast<Real>(a->tx), ty = static_cast<Real>(a->ty);
            const int last = std::min(height - 1, y2);
            for (int y = y1; y <= last; ++y) {
                if (y >= 0) {
                    Scan& s = scan[static_cast<std::size_t>(y)];
                    if (static_cast<double>(x) < static_cast<double>(s.min_x)) {
                        s.min_x = static_cast<int>(x);
                        s.min_tx = static_cast<int>(tx);
                        s.min_ty = static_cast<int>(ty);
                    }
                    if (static_cast<double>(x) > static_cast<double>(s.max_x)) {
                        s.max_x = static_cast<int>(x);
                        s.max_tx = static_cast<int>(tx);
                        s.max_ty = static_cast<int>(ty);
                    }
                }
                x = x + dx;
                tx = tx + tdx;
                ty = ty + tdy;
            }
        }
    }
    start = std::max(0, start);
    end = std::min(height, end);
}

inline void scan(const Vertex v[4], int width, int height, std::vector<Scan>& out, int& start, int& end) {
    if (x87_single_precision()) scan_line<float>(v, width, height, out, start, end);
    else scan_line<double>(v, width, height, out, start, end);
}

// Bmscr::GetAlphaOperation: the rate clamped (above 255: 256, or 255 from
// gmode 4), with the operation in the high byte (3, 4: blend; 5: add; 6:
// subtract); other modes are 0x100, a plain copy.
inline int alpha_operation(int gmode, int rate) {
    int alpha = rate;
    if (alpha < 0) alpha = 0;
    if (alpha > 255) {
        alpha = 256;
        if (gmode >= 4) alpha = 255;
    }
    switch (gmode) {
    case 3: case 4: return alpha;
    case 5: return alpha | 0x200;
    case 6: return alpha | 0x300;
    default: return 0x100;
    }
}

// DrawPolygonTex: texture coordinates in 16.16 fixed point stepped per pixel
// in unsigned 32-bit arithmetic; a texel offset beyond the texture is
// skipped without advancing the destination, as in the original.
inline void draw_textured(std::uint8_t* dest, int dest_width, int dest_height, const std::uint8_t* tex, int tex_width,
                          int tex_height, const Vertex v[4], bool color_key, int alpha_word, std::uint8_t ck1,
                          std::uint8_t ck2, std::uint8_t ck3) {
    const int dest_bytes = ((dest_width * 3) + 3) & ~3;
    const int tex_bytes = ((tex_width * 3) + 3) & ~3;
    const std::uint32_t tex_size = static_cast<std::uint32_t>(tex_height * tex_bytes);
    std::vector<Scan> scans;
    int start, end;
    scan(v, dest_width, dest_height, scans, start, end);
    if (start >= dest_height || end < 0) return;
    int op = 1, alpha = 0;
    if (alpha_word != 0x100) {
        op = alpha_word >> 8;
        alpha = (alpha_word & 255) + 1;
        if (op != 0 && op != 2 && op != 3) op = 1;
    }
    const int ialpha = 0x100 - alpha;
    static bool reported = false;
    for (int y = start; y < end; ++y) {
        const Scan& s = scans[static_cast<std::size_t>(y)];
        int min_x = s.min_x, max_x = s.max_x;
        if (max_x < 0) continue;
        std::uint32_t tx = static_cast<std::uint32_t>(s.min_tx) << 16, ty = static_cast<std::uint32_t>(s.min_ty) << 16;
        std::uint32_t tdx = 0, tdy = 0;
        if (max_x != min_x) {
            const int step = 0x10000 / (max_x - min_x);
            tdx = static_cast<std::uint32_t>((s.max_tx - s.min_tx) * step);
            tdy = static_cast<std::uint32_t>((s.max_ty - s.min_ty) * step);
        }
        while (min_x < 0) {
            ++min_x;
            tx += tdx;
            ty += tdy;
        }
        max_x = std::min(dest_width, max_x);
        std::uint8_t* p = dest + static_cast<std::size_t>(dest_height - 1 - y) * dest_bytes + min_x * 3;
        for (int x = min_x; x < max_x; ++x) {
            const int offset = static_cast<int>((ty >> 16) * static_cast<std::uint32_t>(tex_bytes) + (tx >> 16) * 3u);
            if (offset < static_cast<int>(tex_size)) {
                if (offset < 0) {
                    // The original reads memory before the texture here.
                    if (!reported) std::fprintf(stderr, "[CP2 screen] gsquare: texel before the source bitmap skipped\n");
                    reported = true;
                    p += 3;
                } else {
                    const std::uint8_t* t = tex + offset;
                    if (op == 1) {
                        if (!color_key || (t[0] | t[1] | t[2])) {
                            p[0] = t[0];
                            p[1] = t[1];
                            p[2] = t[2];
                        }
                    } else if (op == 0) {
                        if (!(color_key && t[0] == ck1 && t[1] == ck2 && t[2] == ck3))
                            for (int k = 0; k < 3; ++k)
                                p[k] = static_cast<std::uint8_t>(static_cast<std::int16_t>((t[k] * alpha + p[k] * ialpha) >> 8));
                    } else if (op == 2) {
                        for (int k = 0; k < 3; ++k) {
                            const int a = ((t[k] * alpha) >> 8) + p[k];
                            p[k] = static_cast<std::uint8_t>(a > 255 ? 255 : a);
                        }
                    } else {
                        for (int k = 0; k < 3; ++k) {
                            const int a = p[k] - ((t[k] * alpha) >> 8);
                            p[k] = static_cast<std::uint8_t>(a < 0 ? 0 : a);
                        }
                    }
                    p += 3;
                }
            }
            tx += tdx;
            ty += tdy;
        }
    }
}

// DrawPolygonF4: a flat color (DIB byte order 0x00RRGGBB as CnvRGB gives).
inline void draw_flat(std::uint8_t* dest, int dest_width, int dest_height, const Vertex v[4], int alpha_word,
                      std::uint32_t rgb) {
    const int dest_bytes = ((dest_width * 3) + 3) & ~3;
    std::vector<Scan> scans;
    int start, end;
    scan(v, dest_width, dest_height, scans, start, end);
    if (start >= dest_height || end < 0) return;
    const int c[3] = {static_cast<int>(rgb & 0xff), static_cast<int>((rgb >> 8) & 0xff), static_cast<int>((rgb >> 16) & 0xff)};
    int op = 1, alpha = 0, cc[3] = {0, 0, 0};
    if (alpha_word != 0x100) {
        op = alpha_word >> 8;
        alpha = (alpha_word & 255) + 1;
        for (int k = 0; k < 3; ++k) cc[k] = (c[k] * alpha) >> 8;
        if (op != 0 && op != 2 && op != 3) op = 1;
    }
    const int ialpha = 0x100 - alpha;
    for (int y = start; y < end; ++y) {
        const Scan& s = scans[static_cast<std::size_t>(y)];
        int min_x = std::max(s.min_x, 0), max_x = s.max_x;
        if (max_x < 0) continue;
        max_x = std::min(dest_width, max_x);
        std::uint8_t* p = dest + static_cast<std::size_t>(dest_height - 1 - y) * dest_bytes + min_x * 3;
        for (int x = min_x; x < max_x; ++x, p += 3)
            for (int k = 0; k < 3; ++k) {
                int a;
                if (op == 1) a = c[k];
                else if (op == 0) a = ((p[k] * ialpha) >> 8) + cc[k];
                else if (op == 2) a = std::min(255, cc[k] + p[k]);
                else a = std::max(0, p[k] - cc[k]);
                p[k] = static_cast<std::uint8_t>(a);
            }
    }
}

}  // namespace fcpoly

// gsquare src, x, y, tx, ty onto a memory bitmap (hsp3gr_wingui.cpp, case
// 0x37): vertices truncated to shorts, texture coordinates clamped to the
// source (0..size) with v flipped for the bottom-up DIB; gmode 2 keys out
// black, gmode 4 the current color with blending; a null source fills with
// the current color (`gsquare -1, ...`). `rgb` is 0x00RRGGBB.
inline void gsquare_surface(BufferSurface& dest, const BufferSurface* src, const int* x, const int* y, const int* tx,
                            const int* ty, int gmode, int rate, std::uint32_t rgb) {
    fcpoly::Vertex v[4];
    auto limit = [](int n, int lo, int hi) { return n < lo ? lo : n > hi ? hi : n; };
    for (int i = 0; i < 4; ++i) {
        v[i].x = static_cast<std::int16_t>(x[i]);
        v[i].y = static_cast<std::int16_t>(y[i]);
        v[i].tx = static_cast<std::int16_t>(src ? limit(tx[i], 0, src->width()) : 0);
        v[i].ty = static_cast<std::int16_t>(src ? src->height() - 1 - limit(ty[i], 0, src->height()) : 0);
    }
    const int alpha = fcpoly::alpha_operation(gmode, rate);
    std::uint8_t* pixels = dest.mutable_bytes();
    if (!src) {
        fcpoly::draw_flat(pixels, dest.width(), dest.height(), v, alpha, rgb);
        return;
    }
    const bool key = gmode == 2 || gmode == 4;
    fcpoly::draw_textured(pixels, dest.width(), dest.height(), src->bytes().data(), src->width(), src->height(), v, key,
                          alpha, static_cast<std::uint8_t>(rgb & 0xff), static_cast<std::uint8_t>((rgb >> 8) & 0xff),
                          static_cast<std::uint8_t>((rgb >> 16) & 0xff));
}

}  // namespace cp2
