// CPU pixels for HSP `buffer` screens, with Windows HSP's semantics.
//
// On Windows (OpenHSP win32gui/hspwnd_win.cpp), `buffer id, sx, sy` creates a
// memory DC over a 24-bit bottom-up DIB whose rows are padded to a DWORD, and
// Init's Cls(0) paints it with WHITE_BRUSH. `boxf x1, y1, x2, y2` is
// FillRect(x1, y1, x2 + 1, y2 + 1) with a brush of the current color; it does
// not use gmode. HSP3Dish draws through OpenGL ES only to the main screen and
// throws HSPERR_UNSUPPORTED_FUNCTION for a buffer, so the iOS runtime keeps
// these pixels itself. The game's map-chip loader clears buffer 25 this way.
#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace cp2 {

class BufferSurface {
public:
    // Allocates and paints white, as `buffer` does. Non-positive sizes hold
    // no pixels (CreateDIBSection fails for them on Windows).
    void create(int width, int height) {
        width_ = width > 0 ? width : 0;
        height_ = height > 0 ? height : 0;
        stride_ = ((width_ * 3) + 3) & ~3;
        pixels_.assign(static_cast<std::size_t>(stride_) * static_cast<std::size_t>(height_), 0xff);
        ++revision;
    }

    int width() const { return width_; }
    int height() const { return height_; }
    int stride() const { return stride_; }
    // The DIB bytes: rows bottom-up, BGR, each row padded to a DWORD.
    const std::vector<std::uint8_t>& bytes() const { return pixels_; }
    // The DIB bytes for drawing code (fcpoly); counts as a change.
    std::uint8_t* mutable_bytes() {
        ++revision;
        return pixels_.data();
    }

    // FillRect(left, top, right, bottom): an inverted rectangle is normalized
    // (Windows HSP's boxf 8, 5, 6, 2 fills column 7, rows 3 and 4), the larger
    // edges are exclusive, an empty one fills nothing, and it is clipped to
    // the bitmap. `rgb` is 0x00RRGGBB (HSP3Dish's color without its alpha).
    void fill(int left, int top, int right, int bottom, std::uint32_t rgb) {
        if (right < left) std::swap(left, right);
        if (bottom < top) std::swap(top, bottom);
        left = std::max(left, 0);
        top = std::max(top, 0);
        right = std::min(right, width_);
        bottom = std::min(bottom, height_);
        if (right <= left || bottom <= top) return;
        ++revision;
        const std::uint8_t b = rgb & 0xff, g = (rgb >> 8) & 0xff, r = (rgb >> 16) & 0xff;
        for (int y = top; y < bottom; ++y) {
            std::uint8_t* row = row_data(y);
            for (int x = left; x < right; ++x) {
                row[x * 3] = b;
                row[x * 3 + 1] = g;
                row[x * 3 + 2] = r;
            }
        }
    }

    // cls 0..4: Windows paints the bitmap with stock brush `mode` (WHITE,
    // LTGRAY, GRAY, DKGRAY, BLACK). Other values (not HSP cls modes) select
    // other stock objects on Windows and are not modeled: nothing changes.
    void cls(int mode) {
        static const std::uint32_t stock_brush[5] = {0xffffff, 0xc0c0c0, 0x808080, 0x404040, 0x000000};
        if (mode >= 0 && mode < 5) fill(0, 0, width_, height_, stock_brush[mode]);
    }

    // GetPixel: 0x00RRGGBB, or -1 (CLR_INVALID) outside the bitmap.
    std::int64_t pixel(int x, int y) const {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) return -1;
        const std::uint8_t* p = pixels_.data() + static_cast<std::size_t>(height_ - 1 - y) * stride_ + x * 3;
        return (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) | p[0];
    }

    // picload's drawing at (x, y) of an RGBA picture (top row first), as
    // HspWnd::Picload draws it on Windows. Pictures stb_image decodes (PNG,
    // PSD, TGA) go through RenderAlphaBitmap: a position beyond the bitmap
    // (x > width or y > height) fails (picload's error), the picture is
    // clipped, and with four components each pixel blends with a = alpha +
    // (alpha >> 7): dst = (dst * (256 - a) >> 8) + (src * a >> 8), a full
    // alpha copying. Other pictures (BMP, JPEG through IPicture) are copied
    // opaque and clipped.
    bool draw_picture(int x, int y, const std::uint8_t* rgba, int width, int height, bool stb, int components) {
        int sx = 0, sy = 0, w = width, h = height;
        if (stb && (x > width_ || y > height_)) return false;
        if (x < 0) { sx = -x; x = 0; w -= sx; }
        if (y < 0) { sy = -y; y = 0; h -= sy; }
        if (x + w > width_) w = width_ - x;
        if (y + h > height_) h = height_ - y;
        if (w <= 0 || h <= 0) return true;
        ++revision;
        const bool blend = stb && components >= 4;
        for (int row = 0; row < h; ++row) {
            std::uint8_t* d = row_data(y + row) + x * 3;
            const std::uint8_t* s = rgba + (static_cast<std::size_t>(sy + row) * width + sx) * 4;
            for (int col = 0; col < w; ++col, d += 3, s += 4) {
                const int a = s[3] + (s[3] >> 7), rest = 256 - a;
                if (!blend || !rest) {
                    d[0] = s[2];
                    d[1] = s[1];
                    d[2] = s[0];
                    continue;
                }
                d[0] = static_cast<std::uint8_t>(((d[0] * rest) >> 8) + ((s[2] * a) >> 8));
                d[1] = static_cast<std::uint8_t>(((d[1] * rest) >> 8) + ((s[1] * a) >> 8));
                d[2] = static_cast<std::uint8_t>(((d[2] * rest) >> 8) + ((s[0] * a) >> 8));
            }
        }
        return true;
    }

    // gcopy between memory bitmaps: Windows HSP's Bmscr::Blt with its clipping
    // (the source extent is cut at `>=` its edge, as written), for gmode 1
    // copy, 2 black transparent, 3 blend by `rate`, 4 blend except the current
    // color, 5 add, 6 subtract, 7 per-pixel alpha from the bytes one source
    // width later in the row. gmode 0 is BitBlt on Windows; inside both
    // bitmaps it copies as gmode 1 does, which is what the port does.
    void blt(int mode, const BufferSurface& src, int cx, int cy, int xx, int yy, int asx, int asy, int rate,
             std::uint32_t color_rgb) {
        int sx = asx;
        const int bsx = width_;
        if (xx < 0) { sx += xx; cx -= xx; xx = 0; }
        if (xx + sx >= src.width_) sx -= (xx + sx) - src.width_;
        if (cx > bsx) return;
        if (cx < 0) {
            sx = cx + sx;
            if (sx <= 0) return;
            xx -= cx;
            cx = 0;
        }
        if (cx > bsx - sx) sx -= cx - (bsx - sx);
        int sy = asy;
        const int bsy = height_;
        if (yy < 0) { sy += yy; cy -= yy; yy = 0; }
        if (yy + sy >= src.height_) sy -= (yy + sy) - src.height_;
        if (cy > bsy) return;
        if (cy < 0) {
            sy = cy + sy;
            if (sy <= 0) return;
            yy -= cy;
            cy = 0;
        }
        if (cy > bsy - sy) sy -= cy - (bsy - sy);
        if (sx <= 0 || sy <= 0) return;
        if (mode == 0) mode = 1;
        if (mode >= 3 && mode <= 6) {
            if (rate > 256) rate = 256;
            if (rate <= 0) return;
        }
        if (mode == 3 && rate == 256) mode = 1;  // no destination share: the normal copy
        ++revision;
        const int keep = 256 - rate;
        const int kb = static_cast<int>(color_rgb & 0xff), kg = static_cast<int>((color_rgb >> 8) & 0xff),
                  kr = static_cast<int>((color_rgb >> 16) & 0xff);
        for (int row = 0; row < sy; ++row) {
            std::uint8_t* d = row_data(cy + row) + cx * 3;
            const std::uint8_t* srow = src.row_data(yy + row);
            const std::uint8_t* s = srow + xx * 3;
            for (int col = 0; col < sx; ++col, d += 3, s += 3) {
                switch (mode) {
                case 2:
                    if (s[0] | s[1] | s[2]) {
                        d[0] = s[0];
                        d[1] = s[1];
                        d[2] = s[2];
                    }
                    break;
                case 3:
                    for (int k = 0; k < 3; ++k) d[k] = clamp255(((d[k] * keep) >> 8) + ((s[k] * rate) >> 8));
                    break;
                case 4:
                    if (s[0] != kb || s[1] != kg || s[2] != kr)
                        for (int k = 0; k < 3; ++k) d[k] = clamp255(((d[k] * keep) >> 8) + ((s[k] * rate) >> 8));
                    break;
                case 5:
                    for (int k = 0; k < 3; ++k) d[k] = clamp255(d[k] + ((s[k] * rate) >> 8));
                    break;
                case 6:
                    for (int k = 0; k < 3; ++k) {
                        const int v = d[k] - ((s[k] * rate) >> 8);
                        d[k] = static_cast<std::uint8_t>(v < 0 ? 0 : v);
                    }
                    break;
                case 7:
                    // Each byte's alpha is the byte asx * 3 later in the DIB
                    // (an image with its alpha on the right). Past the end of
                    // a row it reads on into the next row in memory, the row
                    // above in the bottom-up DIB, as Windows does; past the
                    // end of the bitmap (undefined memory there) it reads 0.
                    for (int k = 0; k < 3; ++k) {
                        const std::size_t at = static_cast<std::size_t>(srow - src.pixels_.data()) +
                                               static_cast<std::size_t>(xx + col) * 3 + k + static_cast<std::size_t>(asx) * 3;
                        const int a = at < src.pixels_.size() ? src.pixels_[at] : 0;
                        if (!a) continue;
                        d[k] = a == 255 ? s[k] : static_cast<std::uint8_t>(((d[k] * (255 - a)) >> 8) + ((s[k] * a) >> 8));
                    }
                    break;
                default:
                    d[0] = s[0];
                    d[1] = s[1];
                    d[2] = s[2];
                    break;
                }
            }
        }
    }

    // Text coverage (0..255 per pixel, rows top first) drawn at (x, y) in
    // `rgb` (0x00RRGGBB), clipped: dst + (color - dst) * coverage / 255.
    void draw_coverage(int x, int y, const std::uint8_t* coverage, int width, int height, std::uint32_t rgb) {
        const int c[3] = {static_cast<int>(rgb & 0xff), static_cast<int>((rgb >> 8) & 0xff),
                          static_cast<int>((rgb >> 16) & 0xff)};
        bool changed = false;
        for (int row = 0; row < height; ++row) {
            if (y + row < 0 || y + row >= height_) continue;
            std::uint8_t* d = row_data(y + row);
            for (int col = 0; col < width; ++col) {
                if (x + col < 0 || x + col >= width_) continue;
                const int a = coverage[static_cast<std::size_t>(row) * width + col];
                if (!a) continue;
                changed = true;
                for (int k = 0; k < 3; ++k) {
                    std::uint8_t& v = d[(x + col) * 3 + k];
                    v = static_cast<std::uint8_t>(v + ((c[k] - v) * a) / 255);
                }
            }
        }
        if (changed) ++revision;
    }

    // RGBA texels (top row first) of a `tw` x `th` texture holding the bitmap
    // at its top-left; with `black_transparent`, black texels get alpha 0.
    void rgba_texture(std::vector<std::uint8_t>& out, int tw, int th, bool black_transparent) const {
        out.assign(static_cast<std::size_t>(tw) * th * 4, 0);
        for (int y = 0; y < height_ && y < th; ++y) {
            const std::uint8_t* s = row_data(y);
            std::uint8_t* d = out.data() + static_cast<std::size_t>(y) * tw * 4;
            for (int x = 0; x < width_ && x < tw; ++x) {
                d[x * 4] = s[x * 3 + 2];
                d[x * 4 + 1] = s[x * 3 + 1];
                d[x * 4 + 2] = s[x * 3];
                d[x * 4 + 3] = (black_transparent && !(s[x * 3] | s[x * 3 + 1] | s[x * 3 + 2])) ? 0 : 255;
            }
        }
    }

    std::uint32_t revision = 0;      // bumped by every change of the pixels
    int texture = -1;                // the HSP3Dish texture gcopy draws from
    std::uint32_t uploaded = ~0u;    // the revision in that texture
    bool uploaded_transparent = false;

private:
    static std::uint8_t clamp255(int v) { return static_cast<std::uint8_t>(v > 255 ? 255 : v); }
    const std::uint8_t* row_data(int y) const {
        return pixels_.data() + static_cast<std::size_t>(height_ - 1 - y) * stride_;
    }
    std::uint8_t* row_data(int y) { return pixels_.data() + static_cast<std::size_t>(height_ - 1 - y) * stride_; }

    int width_ = 0, height_ = 0, stride_ = 0;
    std::vector<std::uint8_t> pixels_;
};

// Whether Windows HSP's picload decodes the file with stb_image (".png",
// ".psd", ".tga", compared in lower case) rather than OLE's IPicture (BMP,
// JPEG, GIF). Both are decoded with stb_image v2.23 here, as HSP3Dish links
// it; the difference is how the pixels are drawn (draw_picture's `stb`).
inline bool picload_uses_stb(const std::string& name) {
    const std::size_t dot = name.find_last_of('.');
    const std::size_t slash = name.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return false;
    std::string ext = name.substr(dot);
    for (char& c : ext) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return ext == ".png" || ext == ".psd" || ext == ".tga";
}

// picload "file", mode on a memory bitmap whose picture is decoded (RGBA,
// `components` as stb_image reports them). Modes 0 and 2 re-create the
// bitmap at the picture's size (white, cursor at 0, 0), 2 then clears it
// black; the picture is drawn at the cursor. Returns false for
// RenderAlphaBitmap's failure (picload's error 4 on Windows). `x` and `y`
// are the cursor (left unchanged by the call, which re-creates it).
inline bool picload_surface(BufferSurface& s, int mode, int& x, int& y, const std::uint8_t* rgba, int width, int height,
                            int components, bool stb) {
    if (mode == 0 || mode == 2) {
        s.create(width, height);
        x = y = 0;
        if (mode == 2) s.cls(4);
    }
    return s.draw_picture(x, y, rgba, width, height, stb, components);
}

// Surfaces by screen id. A screen that is not a `buffer` has none.
inline std::map<int, BufferSurface>& buffer_surfaces() {
    static std::map<int, BufferSurface> surfaces;
    return surfaces;
}

inline BufferSurface* buffer_surface(int id) {
    auto it = buffer_surfaces().find(id);
    return it == buffer_surfaces().end() ? nullptr : &it->second;
}

}  // namespace cp2
