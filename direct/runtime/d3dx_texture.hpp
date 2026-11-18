#pragma once
// Native transcription of the D3DX8 texture loader statically linked into
// ZGP 1.807 (plugin/zgp1807.hpi). See docs/zgp-renderer.md, "Texture loading".
//
//   D3DXCreateTextureFromFileInMemoryEx  0x10031272
//   D3DXCheckTextureRequirements         0x10030f96 -> 0x1003097e
//   D3DXCreateTexture                    0x10030fbe
//   D3DXLoadSurfaceFromMemory            0x10030d60 -> blitter 0x100379b5
//   image file decoders                  0x100393ad (BMP, PPM, DDS, JPEG, PNG, TGA, DIB)
//
// The device calls (GetDirect3D, GetDeviceCaps, GetDisplayMode,
// CheckDeviceFormat, Release, CreateTexture) are made in the original order,
// so a recorded trace matches the original's line for line.
//
// Pixel conversion follows D3DX's float pipeline exactly: rows are decoded to
// RGBA floats, color-keyed, resampled and encoded. While encoding and while
// building filter weights, D3DX sets the x87 rounding control to truncate,
// with the 24-bit precision Direct3D 8 selects; those operations use the
// chop_* helpers below, which compute the same results portably. Everything
// else is ordinary round-to-nearest float arithmetic with FMA contraction off.
//
// Only the paths ZGP reaches with the game's files are implemented: TGA, BMP
// and PNG decoding, the formats they produce, and the copy, convert and
// LINEAR filters. Anything else is reported on stderr and fails with
// E_NOTIMPL instead of producing different pixels.
#include "d3d8.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#if defined(__clang__)
#define CP2_D3DX_STRICT_FP _Pragma("clang fp contract(off)")
#else
#define CP2_D3DX_STRICT_FP
#endif

namespace cp2 {
namespace d3dx {

using d3d8::HResult;
constexpr HResult kOk = d3d8::kOk;
constexpr HResult kFail = d3d8::kFail;
constexpr HResult kNotImplemented = d3d8::kNotImplemented;
constexpr HResult kOutOfMemory = d3d8::kOutOfMemory;
constexpr HResult kInvalidCall = d3d8::kInvalidCall;
constexpr HResult kNotAvailable = d3d8::kNotAvailable;
constexpr HResult kInvalidData = 0x88760b59;  // D3DXERR_INVALIDDATA

enum Format : std::uint32_t {
    kUnknown = 0, kR8G8B8 = 20, kA8R8G8B8 = 21, kX8R8G8B8 = 22, kR5G6B5 = 23, kX1R5G5B5 = 24, kA1R5G5B5 = 25,
    kA4R4G4B4 = 26, kR3G3B2 = 27, kA8 = 28, kA8R3G3B2 = 29, kX4R4G4B4 = 30, kA8P8 = 40, kP8 = 41, kL8 = 50,
    kA8L8 = 51, kA4L4 = 52,
};

inline HResult not_implemented(const char* what, std::uint32_t value) {
    std::fprintf(stderr, "[CP2 D3DX] %s %u is not implemented\n", what, static_cast<unsigned>(value));
    return kNotImplemented;
}

// ---------------------------------------------------------------------------
// x87 truncating arithmetic (24-bit precision, round toward zero).

// The float nearest to zero among those not exceeding hi + lo in magnitude,
// where hi + lo is exact and hi = round-to-nearest(hi + lo).
inline float chop_exact(double hi, double lo) {
    float f = static_cast<float>(hi);
    const double df = static_cast<double>(f);
    if (hi > 0.0) {
        if (df > hi || (df == hi && lo < 0.0)) f = std::nextafter(f, 0.0f);
    } else if (hi < 0.0) {
        if (df < hi || (df == hi && lo > 0.0)) f = std::nextafter(f, 0.0f);
    }
    return f;
}
inline float chop_mul(float a, float b) {
    return chop_exact(static_cast<double>(a) * static_cast<double>(b), 0.0);  // exact in double
}
inline float chop_add(float a, float b) {
    CP2_D3DX_STRICT_FP
    const double x = a, y = b;
    const double s = x + y;
    const double bb = s - x;
    const double e = (x - (s - bb)) + (y - bb);  // two-sum: x + y == s + e exactly
    return chop_exact(s, e);
}
inline float chop_sub(float a, float b) { return chop_add(a, -b); }
// fistp: truncation, or the integer indefinite value when out of range.
inline std::int32_t fistp_chop(float f) {
    if (!(f > -2147483648.0f && f < 2147483648.0f)) return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(f);
}

// ---------------------------------------------------------------------------
// Format table (0x1005ca78, 0x28-byte entries; default entry 0x1005cf00).

struct FormatInfo {
    std::uint32_t format, category, bits, channels[5], scorable, texture;
};

inline const FormatInfo* format_table(std::size_t& count) {
    static const FormatInfo table[] = {
        {20, 0, 24, {0, 0, 8, 8, 8}, 0, 1},  {21, 0, 32, {0, 8, 8, 8, 8}, 1, 1},
        {22, 0, 32, {0, 0, 8, 8, 8}, 1, 1},  {23, 0, 16, {0, 0, 5, 6, 5}, 1, 1},
        {24, 0, 16, {0, 0, 5, 5, 5}, 1, 1},  {40, 1, 16, {0, 8, 8, 8, 8}, 1, 1},
        {41, 1, 8, {0, 8, 8, 8, 8}, 1, 1},   {25, 0, 16, {0, 1, 5, 5, 5}, 1, 1},
        {26, 0, 16, {0, 4, 4, 4, 4}, 1, 1},  {27, 0, 8, {0, 0, 3, 3, 2}, 1, 1},
        {28, 0, 8, {0, 8, 0, 0, 0}, 1, 1},   {29, 0, 16, {0, 8, 3, 3, 2}, 1, 1},
        {30, 0, 16, {0, 0, 4, 4, 4}, 1, 1},  {50, 2, 8, {0, 0, 8, 8, 8}, 1, 1},
        {51, 2, 16, {0, 8, 8, 8, 8}, 1, 1},  {52, 2, 8, {0, 4, 4, 4, 4}, 1, 1},
        {60, 3, 16, {0, 0, 0, 8, 8}, 1, 1},  {61, 3, 16, {6, 0, 0, 5, 5}, 1, 1},
        {62, 3, 32, {8, 0, 0, 8, 8}, 1, 1},  {63, 3, 32, {0, 8, 8, 8, 8}, 1, 1},
        {64, 3, 32, {0, 0, 0, 16, 16}, 1, 1}, {65, 3, 32, {0, 0, 11, 11, 10}, 1, 1},
        {0x59565955u, 0, 16, {0, 0, 8, 8, 8}, 0, 1},  // UYVY
        {0x32595559u, 0, 16, {0, 0, 8, 8, 8}, 0, 1},  // YUY2
        {0x31545844u, 0, 16, {0, 1, 5, 6, 5}, 0, 1},  // DXT1
        {0x32545844u, 0, 16, {0, 4, 5, 6, 5}, 0, 1},  // DXT2
        {0x33545844u, 0, 16, {0, 4, 5, 6, 5}, 0, 1},  // DXT3
        {0x34545844u, 0, 16, {0, 3, 5, 6, 5}, 0, 1},  // DXT4
        {0x35545844u, 0, 16, {0, 3, 5, 6, 5}, 0, 1},  // DXT5
    };
    count = sizeof(table) / sizeof(table[0]);
    return table;
}

// 0x100308da.
inline const FormatInfo& format_info(std::uint32_t format) {
    static const FormatInfo fallback = {0, 0, 32, {0, 8, 8, 8, 8}, 0, 0};
    std::size_t count;
    const FormatInfo* table = format_table(count);
    for (std::size_t i = 0; i < count; ++i)
        if (table[i].format == format) return table[i];
    return fallback;
}

inline bool is_dxt(std::uint32_t f) {
    return f == 0x31545844u || f == 0x32545844u || f == 0x33545844u || f == 0x34545844u || f == 0x35545844u;
}

// 0x100308fe: channel mismatch score of candidate against the requested format.
inline std::uint32_t format_score(const FormatInfo& candidate, const FormatInfo& requested) {
    if (!candidate.scorable) return 0xffffffffu;
    const std::uint32_t a = candidate.category, d = requested.category;
    if (a != d) {
        if (a == 0 && d == 3) return 0xffffffffu;
        if (a == 3 || a == 2 || a == 1) return 0xffffffffu;
    }
    std::uint32_t score = 0;
    for (int i = 0; i < 5; ++i) {
        const std::uint32_t r = requested.channels[i], c = candidate.channels[i];
        if (r < c) score += r == 0 ? 0x100u : c - r;
        else if (r > c) score += c == 0 ? 0x1000000u : (r - c) << 16;
    }
    return score;
}

// ---------------------------------------------------------------------------
// D3DXCheckTextureRequirements (0x1003097e with resource type 0).

inline HResult check_texture_requirements(d3d8::Device* device, std::uint32_t* width, std::uint32_t* height,
                                          std::uint32_t* levels, std::uint32_t usage, std::uint32_t* format,
                                          std::uint32_t pool) {
    if (!device) return kInvalidCall;
    if (usage == 0xffffffffu) usage = 0;
    if ((usage & 3) != usage) return kInvalidCall;
    if (pool != 0 && pool != 1 && pool != 2) return kInvalidCall;
    d3d8::Direct3D* d3d = nullptr;
    d3d8::Caps caps;
    d3d8::DisplayMode mode;
    device->get_direct3d(d3d);
    device->get_device_caps(caps);
    device->get_display_mode(mode);
    std::uint32_t w = width ? *width : 0xffffffffu;
    std::uint32_t h = height ? *height : 0xffffffffu;
    std::uint32_t depth = 0xffffffffu;  // no depth for 2D textures
    if (w == 0xffffffffu) {
        if (h == 0xffffffffu) w = h = 256;
        else w = h;
    } else if (h == 0xffffffffu) {
        h = w;
    }
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    if (depth == 0xffffffffu || depth == 0) depth = 1;
    if (w > caps.words[d3d8::Caps::kMaxTextureWidth]) w = caps.words[d3d8::Caps::kMaxTextureWidth];
    if (h > caps.words[d3d8::Caps::kMaxTextureHeight]) h = caps.words[d3d8::Caps::kMaxTextureHeight];
    const std::uint32_t texture_caps = caps.words[d3d8::Caps::kTextureCaps];
    std::uint32_t aspect = caps.words[d3d8::Caps::kMaxTextureAspectRatio];
    if (texture_caps & 0x20) aspect = 1;  // D3DPTEXTURECAPS_SQUAREONLY
    if (aspect) {
        if (w > h * aspect) w = h * aspect;
        if (h > aspect * w) h = aspect * w;
    }
    if (texture_caps & 0x2) {  // D3DPTEXTURECAPS_POW2
        auto round_up = [](std::uint32_t v) {
            std::uint32_t p = 1;
            while (p < v) p <<= 1;
            return p;
        };
        w = round_up(w);
        h = round_up(h);
        depth = round_up(depth);
    }
    if (format && is_dxt(*format)) {
        w = (w + 3) & ~3u;
        h = (h + 3) & ~3u;
    }
    std::uint32_t mips = levels ? *levels : 0xffffffffu;
    auto pow2 = [](std::uint32_t v) { return (v & (v - 1)) == 0; };
    if ((texture_caps & 0x4000) && pow2(w) && pow2(h) && pow2(depth)) {  // D3DPTEXTURECAPS_MIPMAP
        auto count = [](std::uint32_t v) {
            std::uint32_t n = 1;
            while (v != 0 && !(v & 1)) { v >>= 1; ++n; }
            return n;
        };
        std::uint32_t full = count(w);
        const std::uint32_t hn = count(h);
        if (hn > full) full = hn;
        if (mips > full || mips == 0) mips = full;
    } else {
        mips = 1;
    }
    const std::uint32_t requested = format ? *format : 0;
    const FormatInfo& wanted = format_info(requested);
    std::size_t count;
    const FormatInfo* table = format_table(count);
    std::uint32_t chosen = 0, best = 0xffffffffu, chosen_bits = 0;
    bool have_copy = false;  // the original compares against an uninitialized copy until one is chosen
    for (std::size_t i = 0; i < count; ++i) {
        const FormatInfo& entry = table[i];
        if (!entry.texture) continue;
        if (d3d8::failed(d3d->check_device_format(caps.words[d3d8::Caps::kAdapterOrdinal],
                                                  caps.words[d3d8::Caps::kDeviceType], mode.format, usage,
                                                  d3d8::kResourceTexture, entry.format)))
            continue;
        if (entry.format == requested) { chosen = requested; break; }
        if (!entry.scorable) continue;
        const std::uint32_t score = format_score(entry, wanted);
        if (score < best || (score == best && have_copy && entry.bits < chosen_bits)) {
            chosen = entry.format;
            chosen_bits = entry.bits;
            best = score;
            have_copy = true;
        }
    }
    d3d->release();
    if (!chosen) return kNotAvailable;
    if (width) *width = w;
    if (height) *height = h;
    if (levels) *levels = mips;
    if (format) *format = chosen;
    return kOk;
}

// D3DXCreateTexture (0x10030fbe).
inline HResult create_texture(d3d8::Device* device, std::uint32_t width, std::uint32_t height,
                              std::uint32_t levels, std::uint32_t usage, std::uint32_t format, std::uint32_t pool,
                              d3d8::Texture*& texture) {
    texture = nullptr;
    if (!device) return kInvalidCall;
    if (usage == 0xffffffffu) usage = 0;
    HResult r = check_texture_requirements(device, &width, &height, &levels, usage, &format, pool);
    if (d3d8::failed(r)) return r;
    r = device->create_texture(width, height, levels, usage, format, pool, texture);
    return d3d8::failed(r) ? r : kOk;
}

// ---------------------------------------------------------------------------
// Decoded image (the loader object of 0x10037ac7: format, data, pitch, rect,
// palette and the next image of a chain).

struct Image {
    std::uint32_t format = 0;
    const unsigned char* data = nullptr;
    std::int32_t pitch = 0;
    std::uint32_t width = 0, height = 0;           // rect (0, 0, width, height)
    std::vector<unsigned char> owned;              // pixel storage when not in place
    std::vector<unsigned char> palette;            // 256 PALETTEENTRY (R, G, B, flags) or empty
};

inline std::uint16_t le16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | p[1] << 8); }
inline std::uint32_t le32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

// DIB decoder (0x10037b39): BITMAPINFOHEADER and its pixels.
inline HResult decode_dib(const unsigned char* data, std::size_t size, Image& image) {
    if (size < 0x28 || le32(data) < 0x28) return kFail;
    const std::uint32_t header = le32(data);
    const std::uint32_t width = le32(data + 4);
    const std::int32_t height_raw = static_cast<std::int32_t>(le32(data + 8));
    const std::uint32_t height = height_raw > 0 ? static_cast<std::uint32_t>(height_raw)
                                                : static_cast<std::uint32_t>(-static_cast<std::int64_t>(height_raw));
    const std::uint16_t bit_count = le16(data + 0xe);
    std::uint32_t colors = le32(data + 0x20);
    if (bit_count <= 8 && colors == 0) colors = 1u << bit_count;
    const std::uint64_t offset = static_cast<std::uint64_t>(header) + static_cast<std::uint64_t>(colors) * 4;
    if (offset > size) return kFail;
    if (le16(data + 0xc) != 1) return kFail;
    const std::uint32_t compression = le32(data + 0x10);
    if (height_raw < 0 && compression != 0 && compression != 3) return kFail;
    std::uint32_t format = 0;
    if (compression > 2) {
        // The original returns E_NOTIMPL here, which only makes the loader
        // try its next decoder; kNotImplemented is reserved for port gaps.
        if (compression != 3) return kFail;
        if (header < 0x6c) return kFail;
        const std::uint32_t r = le32(data + 0x28), g = le32(data + 0x2c), b = le32(data + 0x30),
                            a = le32(data + 0x34);
        if (bit_count == 32) {
            if (b != 0xff || g != 0xff00 || r != 0xff0000) return kFail;
            if (a == 0) format = kX8R8G8B8;
            else if (a == 0xff000000u) format = kA8R8G8B8;
            else return kFail;
        } else if (bit_count == 24) {
            if (b != 0xff || g != 0xff00 || r != 0xff0000 || a != 0) return kFail;
            format = kR8G8B8;
        } else if (bit_count == 16) {
            if (b == 0xff && g == 0xff && r == 0xff && a == 0xff00) format = kA8L8;
            else if (b == 0x1f && g == 0x7e0 && r == 0xf800 && a == 0) format = kR5G6B5;
            else if (b == 0x1f && g == 0x3e0 && r == 0x7c00 && a == 0) format = kX1R5G5B5;
            else if (b == 0x1f && g == 0x3e0 && r == 0x7c00 && a == 0x8000) format = kA1R5G5B5;
            else if (b == 0xf && g == 0xf0 && r == 0xf00 && a == 0xf000) format = kA4R4G4B4;
            else if (b == 3 && g == 0x1c && r == 0xe0 && a == 0xff00) format = kA8R3G3B2;
            else return kFail;
        } else {
            return kFail;
        }
    } else if (bit_count == 1 || bit_count == 4 || bit_count == 8) {
        format = kP8;
        if (colors > 256) return not_implemented("DIB palette size", colors);  // overflows the original
        image.palette.assign(1024, 0xff);
        const unsigned char* q = data + header;
        for (std::uint32_t i = 0; i < colors; ++i, q += 4) {
            image.palette[i * 4 + 0] = q[2];
            image.palette[i * 4 + 1] = q[1];
            image.palette[i * 4 + 2] = q[0];
            image.palette[i * 4 + 3] = 0xff;
        }
    } else if (bit_count == 16) {
        format = kX1R5G5B5;
    } else if (bit_count == 24) {
        format = kR8G8B8;
    } else if (bit_count == 32) {
        format = kX8R8G8B8;
    } else {
        return kFail;
    }
    std::uint32_t source_stride, row;
    if (bit_count == 1) { source_stride = ((width >> 3) + 3) & ~3u; row = width; }
    else if (bit_count == 4) { source_stride = ((width >> 1) + 3) & ~3u; row = width; }
    else { row = (bit_count >> 3) * width; source_stride = (row + 3) & ~3u; }
    const std::uint32_t pitch = (row + 3) & ~3u;
    image.format = format;
    image.width = width;
    image.height = height;
    image.pitch = static_cast<std::int32_t>(pitch);
    const unsigned char* pixels = data + offset;
    const std::size_t available = size - static_cast<std::size_t>(offset);
    if (height_raw < 0 && bit_count >= 8) {
        if (static_cast<std::uint64_t>(pitch) * height > available) return kFail;
        image.data = pixels;
        return kOk;
    }
    image.owned.assign(static_cast<std::size_t>(pitch) * height, 0);
    unsigned char* buffer = image.owned.data();
    const std::ptrdiff_t total = static_cast<std::ptrdiff_t>(pitch) * height;
    const std::ptrdiff_t step = height_raw < 0 ? static_cast<std::ptrdiff_t>(pitch) : -static_cast<std::ptrdiff_t>(pitch);
    std::ptrdiff_t at = height_raw < 0 ? 0 : static_cast<std::ptrdiff_t>(height ? height - 1 : 0) * pitch;
    std::size_t src = 0;
    auto put = [&](std::ptrdiff_t index, unsigned char value) {
        if (index < 0 || index >= total) return false;
        buffer[index] = value;
        return true;
    };
    if (compression == 1 || compression == 2) {
        // RLE8 (1) / RLE4 (2): bottom-up only. Writes outside the image fail
        // here; the original would write past its buffer.
        const bool nibbles = compression == 2;
        std::ptrdiff_t line = at;
        while (at >= 0) {
            if (src + 2 > available) return kFail;
            const unsigned char count = pixels[src], value = pixels[src + 1];
            if (count) {
                for (unsigned i = 0; i < count; ++i) {
                    const unsigned char v = nibbles ? ((i & 1) ? (value & 0xf) : (value >> 4)) : value;
                    if (!put(at + i, v)) return kFail;
                }
                at += count;
                src += 2;
                continue;
            }
            if (value == 0) { line -= pitch; at = line; src += 2; continue; }  // end of line
            if (value == 1) { at = -static_cast<std::ptrdiff_t>(pitch); src += 2; continue; }  // end of bitmap
            if (value == 2) {  // delta (the line start is not updated)
                if (src + 4 > available) return kFail;
                at += pixels[src + 2] - static_cast<std::ptrdiff_t>(pixels[src + 3]) * pitch;
                src += 4;
                continue;
            }
            const std::size_t bytes = nibbles ? (((value >> 1) + 1) & ~1u) : ((value + 1u) & ~1u);
            if (src + 2 + bytes > available) return kFail;
            for (unsigned i = 0; i < value; ++i) {
                unsigned char v = pixels[src + 2 + (nibbles ? i / 2 : i)];
                if (nibbles) v = (i & 1) ? (v & 0xf) : (v >> 4);
                if (!put(at + i, v)) return kFail;
            }
            at += value;
            src += bytes + 2;
        }
        return kOk;
    }
    for (std::uint32_t y = 0; y < height; ++y, at += step, src += source_stride) {
        if (bit_count == 1) {
            if (src + ((width + 7) >> 3) > available) return kFail;
            for (std::uint32_t x = 0; x < width; ++x) buffer[at + x] = (pixels[src + (x >> 3)] >> (7 - (x & 7))) & 1;
        } else if (bit_count == 4) {
            if (src + ((width + 1) >> 1) > available) return kFail;
            for (std::uint32_t x = 0; x < width; ++x) {
                const unsigned char v = pixels[src + (x >> 1)];
                buffer[at + x] = (x & 1) ? (v & 0xf) : (v >> 4);
            }
        } else {
            if (src + row > available) return kFail;
            std::memcpy(buffer + at, pixels + src, row);
        }
    }
    return kOk;
}

// BMP decoder (0x1003937d): BITMAPFILEHEADER, then the DIB that follows.
// bfOffBits is not used; the DIB decoder finds the pixels after the palette.
inline HResult decode_bmp(const unsigned char* data, std::size_t size, Image& image) {
    if (size < 0xe || data[0] != 'B' || data[1] != 'M' || le32(data + 2) > size) return kFail;
    return decode_dib(data + 0xe, size - 0xe, image);
}

// TGA decoder (0x1003840c).
inline HResult decode_tga(const unsigned char* data, std::size_t size, Image& image) {
    if (size < 18) return kFail;
    const unsigned char colormap_type = data[1], type = data[2];
    if (colormap_type & 0xfe) return kFail;
    if (type & 0xf4) return kFail;
    const std::uint32_t width = le16(data + 12), height = le16(data + 14);
    if (!width || !height) return kFail;
    const std::uint32_t entry_bytes = (data[7] + 7u) >> 3;
    std::uint32_t colormap_format = 0;
    if (colormap_type) {
        switch (data[7]) {
        case 15: colormap_format = kX1R5G5B5; break;
        case 16: colormap_format = kA1R5G5B5; break;
        case 24: colormap_format = kR8G8B8; break;
        case 32: colormap_format = kA8R8G8B8; break;
        default: return kFail;
        }
    }
    const std::uint32_t pixel_bytes = (data[16] + 7u) >> 3;
    std::uint32_t format = 0;
    switch (type & 3) {
    case 1:
        if (!colormap_type || data[16] != 8) return kFail;
        format = kP8;
        break;
    case 2:
        switch (data[16]) {
        case 15: format = kX1R5G5B5; break;
        case 16: format = kA1R5G5B5; break;
        case 24: format = kR8G8B8; break;
        case 32: format = kA8R8G8B8; break;
        default: return kFail;
        }
        break;
    case 3:
        if (data[16] != 8) return kFail;
        format = kL8;
        break;
    default:
        return kFail;
    }
    const bool top_down = (data[17] & 0x20) != 0;
    const bool left_to_right = (data[17] & 0x10) == 0;
    const bool rle = (type & 8) != 0;
    std::size_t left = size - 18;
    if (left < data[0]) return kFail;
    left -= data[0];
    const unsigned char* p = data + 18 + data[0];
    const std::uint32_t colormap_length = le16(data + 5);
    const std::size_t colormap_bytes = static_cast<std::size_t>(colormap_length) * entry_bytes;
    if (left < colormap_bytes) return kFail;
    if (format == kP8) {
        const std::uint32_t first = le16(data + 3);
        if (first + colormap_length > 256) return kFail;
        image.palette.assign(1024, 0xff);
        const unsigned char* q = p;
        for (std::uint32_t i = first; i < first + colormap_length; ++i) {
            unsigned r, g, b, a = 0xff;
            if (colormap_format == kA1R5G5B5 || colormap_format == kX1R5G5B5) {
                const unsigned v = le16(q);
                q += 2;
                if (colormap_format == kA1R5G5B5) a = (v >> 15) * 0xff;
                const unsigned r5 = (v >> 10) & 0x1f, g5 = (v >> 5) & 0x1f, b5 = v & 0x1f;
                r = (r5 << 3) | (r5 >> 2);
                g = (g5 << 3) | (g5 >> 2);
                b = (b5 << 3) | (b5 >> 2);
            } else if (colormap_format == kA8R8G8B8) {
                const std::uint32_t v = le32(q);
                q += 4;
                a = v >> 24;
                r = (v >> 16) & 0xff;
                g = (v >> 8) & 0xff;
                b = v & 0xff;
            } else {
                r = q[2];
                g = q[1];
                b = q[0];
                q += 3;
            }
            image.palette[i * 4 + 0] = static_cast<unsigned char>(r);
            image.palette[i * 4 + 1] = static_cast<unsigned char>(g);
            image.palette[i * 4 + 2] = static_cast<unsigned char>(b);
            image.palette[i * 4 + 3] = static_cast<unsigned char>(a);
        }
    }
    p += colormap_bytes;
    left -= colormap_bytes;
    const std::uint32_t pitch = width * pixel_bytes;
    const std::size_t image_bytes = static_cast<std::size_t>(width) * height * pixel_bytes;
    image.format = format;
    image.pitch = static_cast<std::int32_t>(pitch);
    image.width = width;
    image.height = height;
    if (!rle && top_down && left_to_right) {
        if (left < image_bytes) return kFail;
        image.data = p;
        return kOk;
    }
    image.owned.assign(image_bytes, 0);
    unsigned char* buffer = image.owned.data();
    const std::ptrdiff_t total = static_cast<std::ptrdiff_t>(image_bytes);
    std::ptrdiff_t row_start = top_down ? 0 : static_cast<std::ptrdiff_t>(height - 1) * pitch;
    for (std::uint32_t y = 0; y < height; ++y) {
        std::ptrdiff_t at = left_to_right ? row_start : row_start + pitch - pixel_bytes;
        std::uint32_t done = 0;
        while (done < width) {
            std::uint32_t count = width;
            bool repeat = false;
            if (rle) {
                if (left < 1) return kFail;
                repeat = (*p & 0x80) != 0;
                count = (*p & 0x7fu) + 1;
                ++p;
                --left;
            }
            done += count;
            for (std::uint32_t i = 0; i < count; ++i) {
                if (left < pixel_bytes) return kFail;
                // A packet that runs past the row continues in memory, as in
                // the original; writes outside the image fail here.
                if (at < 0 || at + static_cast<std::ptrdiff_t>(pixel_bytes) > total) return kFail;
                std::memcpy(buffer + at, p, pixel_bytes);
                if (!repeat) { p += pixel_bytes; left -= pixel_bytes; }
                at += left_to_right ? static_cast<std::ptrdiff_t>(pixel_bytes) : -static_cast<std::ptrdiff_t>(pixel_bytes);
            }
            if (repeat) { p += pixel_bytes; left -= pixel_bytes; }
        }
        row_start += top_down ? static_cast<std::ptrdiff_t>(pitch) : -static_cast<std::ptrdiff_t>(pitch);
    }
    return kOk;
}

// --- PNG through the libpng 1.0.5 copy (decoder 0x10038b54) ---------------

// zlib inflate (RFC 1950/1951), checking the Adler-32 trailer as zlib does.
class Inflater {
public:
    Inflater(const unsigned char* data, std::size_t size) : data_(data), size_(size) {}

    bool run(std::vector<unsigned char>& out) {
        if (size_ < 2) return false;
        const unsigned cmf = data_[0], flg = data_[1];
        if ((cmf & 0xf) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 != 0 || (flg & 0x20)) return false;
        pos_ = 2;
        bool last = false;
        while (!last) {
            int v;
            if ((v = bits(1)) < 0) return false;
            last = v != 0;
            const int type = bits(2);
            if (type == 0) {
                bit_count_ = 0;
                if (pos_ + 4 > size_) return false;
                const unsigned len = data_[pos_] | data_[pos_ + 1] << 8, nlen = data_[pos_ + 2] | data_[pos_ + 3] << 8;
                pos_ += 4;
                if ((len ^ 0xffffu) != nlen || pos_ + len > size_) return false;
                out.insert(out.end(), data_ + pos_, data_ + pos_ + len);
                pos_ += len;
            } else if (type == 1) {
                Huffman lit, dist;
                std::vector<unsigned char> lengths(288 + 32);
                for (int i = 0; i < 144; ++i) lengths[i] = 8;
                for (int i = 144; i < 256; ++i) lengths[i] = 9;
                for (int i = 256; i < 280; ++i) lengths[i] = 7;
                for (int i = 280; i < 288; ++i) lengths[i] = 8;
                for (int i = 0; i < 30; ++i) lengths[288 + i] = 5;
                if (!lit.build(lengths.data(), 288) || !dist.build(lengths.data() + 288, 30)) return false;
                if (!block(out, lit, dist)) return false;
            } else if (type == 2) {
                Huffman lit, dist;
                if (!dynamic_tables(lit, dist) || !block(out, lit, dist)) return false;
            } else {
                return false;
            }
        }
        bit_count_ = 0;
        if (pos_ + 4 > size_) return false;
        std::uint32_t a = 1, b = 0;
        for (unsigned char c : out) { a = (a + c) % 65521u; b = (b + a) % 65521u; }
        const std::uint32_t expected = static_cast<std::uint32_t>(data_[pos_]) << 24 | data_[pos_ + 1] << 16 |
                                       data_[pos_ + 2] << 8 | data_[pos_ + 3];
        return ((b << 16) | a) == expected;
    }

private:
    struct Huffman {
        std::uint16_t counts[16];
        std::uint16_t symbols[320];
        bool build(const unsigned char* lengths, int n) {
            std::memset(counts, 0, sizeof(counts));
            for (int i = 0; i < n; ++i) counts[lengths[i]]++;
            counts[0] = 0;
            int left = 1;
            for (int len = 1; len < 16; ++len) {
                left <<= 1;
                left -= counts[len];
                if (left < 0) return false;
            }
            std::uint16_t offs[16];
            offs[1] = 0;
            for (int len = 1; len < 15; ++len) offs[len + 1] = static_cast<std::uint16_t>(offs[len] + counts[len]);
            for (int i = 0; i < n; ++i)
                if (lengths[i]) symbols[offs[lengths[i]]++] = static_cast<std::uint16_t>(i);
            return true;
        }
    };

    int bits(int need) {
        while (bit_count_ < need) {
            if (pos_ >= size_) return -1;
            bit_buffer_ |= static_cast<std::uint32_t>(data_[pos_++]) << bit_count_;
            bit_count_ += 8;
        }
        const int v = static_cast<int>(bit_buffer_ & ((1u << need) - 1));
        bit_buffer_ >>= need;
        bit_count_ -= need;
        return v;
    }
    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            const int b = bits(1);
            if (b < 0) return -1;
            code |= b;
            const int count = h.counts[len];
            if (code - count < first) return h.symbols[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
    bool dynamic_tables(Huffman& lit, Huffman& dist) {
        const int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
        if (nlen > 286 || ndist > 30 || ncode < 4) return false;
        static const unsigned char order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        unsigned char lengths[320] = {};
        for (int i = 0; i < ncode; ++i) {
            const int v = bits(3);
            if (v < 0) return false;
            lengths[order[i]] = static_cast<unsigned char>(v);
        }
        Huffman code_table;
        if (!code_table.build(lengths, 19)) return false;
        std::memset(lengths, 0, sizeof(lengths));
        int index = 0;
        while (index < nlen + ndist) {
            int symbol = decode(code_table);
            if (symbol < 0) return false;
            if (symbol < 16) { lengths[index++] = static_cast<unsigned char>(symbol); continue; }
            int value = 0, repeat;
            if (symbol == 16) {
                if (index == 0) return false;
                value = lengths[index - 1];
                repeat = 3 + bits(2);
            } else if (symbol == 17) {
                repeat = 3 + bits(3);
            } else {
                repeat = 11 + bits(7);
            }
            if (index + repeat > nlen + ndist) return false;
            while (repeat--) lengths[index++] = static_cast<unsigned char>(value);
        }
        if (lengths[256] == 0) return false;
        return lit.build(lengths, nlen) && dist.build(lengths + nlen, ndist);
    }
    bool block(std::vector<unsigned char>& out, const Huffman& lit, const Huffman& dist) {
        static const std::uint16_t len_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const unsigned char len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const std::uint16_t dist_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                                    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                                    6145, 8193, 12289, 16385, 24577};
        static const unsigned char dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                                     6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int symbol = decode(lit);
            if (symbol < 0) return false;
            if (symbol < 256) { out.push_back(static_cast<unsigned char>(symbol)); continue; }
            if (symbol == 256) return true;
            symbol -= 257;
            if (symbol >= 29) return false;
            const int le = bits(len_extra[symbol]);
            if (le < 0) return false;
            const int length = len_base[symbol] + le;
            const int ds = decode(dist);
            if (ds < 0 || ds >= 30) return false;
            const int de = bits(dist_extra[ds]);
            if (de < 0) return false;
            const std::size_t distance = static_cast<std::size_t>(dist_base[ds] + de);
            if (distance > out.size()) return false;
            const std::size_t from = out.size() - distance;
            for (int i = 0; i < length; ++i) out.push_back(out[from + static_cast<std::size_t>(i)]);
        }
    }

    const unsigned char* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    std::uint32_t bit_buffer_ = 0;
    int bit_count_ = 0;
};

inline std::uint32_t be32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) << 24 | static_cast<std::uint32_t>(p[1]) << 16 |
           static_cast<std::uint32_t>(p[2]) << 8 | p[3];
}

inline std::uint32_t png_crc(const unsigned char* p, std::size_t n) {
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    std::uint32_t c = 0xffffffffu;
    for (std::size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

// D3DX asks libpng for: 16-bit stripping, packing, gray expansion, gamma only
// for gAMA without sRGB (screen gamma 2.2, threshold 0.05), tRNS expansion,
// BGR order, and an 0xff filler after RGB. Output formats: gray -> L8,
// gray+alpha -> A8L8, RGB -> X8R8G8B8, RGBA -> A8R8G8B8, palette -> P8.
inline HResult decode_png(const unsigned char* data, std::size_t size, Image& image) {
    static const unsigned char signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    const std::size_t check = size < 8 ? size : 8;  // png_sig_cmp(data, 0, size)
    if (check == 0 || std::memcmp(data, signature, check) != 0) return kFail;
    if (size < 8) return kFail;
    std::size_t pos = 8;
    std::uint32_t width = 0, height = 0;
    unsigned depth = 0, color = 0, interlace = 0;
    bool have_header = false, have_srgb = false, have_gamma = false, have_trns = false, have_palette = false;
    std::uint32_t gamma = 0;
    std::vector<unsigned char> compressed;
    while (pos + 12 <= size) {
        const std::uint32_t length = be32(data + pos);
        if (length > size - pos - 12) return kFail;
        const unsigned char* type = data + pos + 4;
        const unsigned char* body = data + pos + 8;
        const bool critical = !(type[0] & 0x20);
        const bool crc_ok = png_crc(type, length + 4) == be32(body + length);
        pos += 12 + length;
        if (!crc_ok) {
            if (critical) return kFail;  // libpng: CRC error on a critical chunk
            continue;                    // ancillary chunk with a bad CRC is discarded
        }
        if (!std::memcmp(type, "IHDR", 4)) {
            if (length != 13) return kFail;
            width = be32(body);
            height = be32(body + 4);
            depth = body[8];
            color = body[9];
            interlace = body[12];
            have_header = true;
        } else if (!std::memcmp(type, "PLTE", 4)) {
            have_palette = true;
        } else if (!std::memcmp(type, "tRNS", 4)) {
            have_trns = true;
        } else if (!std::memcmp(type, "gAMA", 4) && length == 4) {
            have_gamma = true;
            gamma = be32(body);
        } else if (!std::memcmp(type, "sRGB", 4)) {
            have_srgb = true;
        } else if (!std::memcmp(type, "IDAT", 4)) {
            compressed.insert(compressed.end(), body, body + length);
        } else if (!std::memcmp(type, "IEND", 4)) {
            break;
        }
    }
    if (!have_header || !width || !height) return kFail;
    std::uint32_t format;
    unsigned channels;
    switch (color) {
    case 0: format = kL8; channels = 1; break;
    case 2: format = kR8G8B8; channels = 3; break;
    case 3: format = kP8; channels = 1; break;
    case 4: format = kA8L8; channels = 2; break;
    case 6: format = kA8R8G8B8; channels = 4; break;
    default: return kFail;
    }
    (void)have_palette;
    if (format == kP8) return not_implemented("PNG color type", color);
    if (depth != 8) return not_implemented("PNG bit depth", depth);
    if (interlace) return not_implemented("PNG interlace method", interlace);
    if (!have_srgb && have_gamma) {
        const double file_gamma = static_cast<double>(gamma) / 100000.0;
        const float g = static_cast<float>(file_gamma);  // png_ptr->gamma is a float in 1.0.5
        if (std::fabs(2.2 * g - 1.0) > 0.05) return not_implemented("PNG gamma transform for gAMA", gamma);
    }
    if (have_trns) return not_implemented("PNG tRNS expansion for color type", color);
    std::vector<unsigned char> raw;
    Inflater inflater(compressed.data(), compressed.size());
    if (!inflater.run(raw)) return kFail;
    const std::size_t stride = static_cast<std::size_t>(width) * channels;
    if (raw.size() < (stride + 1) * height) return kFail;
    const std::uint32_t out_channels = format == kR8G8B8 ? 4 : channels;
    if (format == kR8G8B8) format = kX8R8G8B8;
    image.format = format;
    image.width = width;
    image.height = height;
    image.pitch = static_cast<std::int32_t>(width * out_channels);
    image.owned.assign(static_cast<std::size_t>(image.pitch) * height, 0);
    std::vector<unsigned char> previous(stride, 0), current(stride);
    for (std::uint32_t y = 0; y < height; ++y) {
        const unsigned char* line = raw.data() + y * (stride + 1);
        const unsigned filter = line[0];
        for (std::size_t i = 0; i < stride; ++i) {
            const unsigned a = i >= channels ? current[i - channels] : 0;
            const unsigned b = previous[i];
            const unsigned c = i >= channels ? previous[i - channels] : 0;
            unsigned predictor;
            switch (filter) {
            case 0: predictor = 0; break;
            case 1: predictor = a; break;
            case 2: predictor = b; break;
            case 3: predictor = (a + b) / 2; break;
            case 4: {
                const int p = static_cast<int>(a + b) - static_cast<int>(c);
                const int pa = std::abs(p - static_cast<int>(a)), pb = std::abs(p - static_cast<int>(b)),
                          pc = std::abs(p - static_cast<int>(c));
                predictor = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                break;
            }
            default: return kFail;
            }
            current[i] = static_cast<unsigned char>(line[1 + i] + predictor);
        }
        unsigned char* out = image.owned.data() + static_cast<std::size_t>(y) * image.pitch;
        for (std::uint32_t x = 0; x < width; ++x) {
            const unsigned char* s = current.data() + static_cast<std::size_t>(x) * channels;
            if (channels == 3) {  // BGR + filler
                out[x * 4 + 0] = s[2]; out[x * 4 + 1] = s[1]; out[x * 4 + 2] = s[0]; out[x * 4 + 3] = 0xff;
            } else if (channels == 4) {  // BGRA
                out[x * 4 + 0] = s[2]; out[x * 4 + 1] = s[1]; out[x * 4 + 2] = s[0]; out[x * 4 + 3] = s[3];
            } else if (channels == 2) {
                out[x * 2 + 0] = s[0]; out[x * 2 + 1] = s[1];
            } else {
                out[x] = s[0];
            }
        }
        previous.swap(current);
    }
    return kOk;
}

// Image loader (0x100393ad): the first decoder that accepts the data wins.
// Decoders return kFail for "not this format" (the next one is tried) and
// kNotImplemented for data the original would decode but this port cannot;
// the latter stops the search. PPM, DDS and JPEG are not ported: data with
// their signatures stops here instead of reaching a later decoder.
inline HResult load_image(const unsigned char* data, std::size_t size, Image& image) {
    HResult r = decode_bmp(data, size, image);
    if (!d3d8::failed(r) || r == kNotImplemented) return r;
    image = Image();
    if (size >= 2 && data[0] == 'P' && (data[1] == '3' || data[1] == '6'))
        return not_implemented("PPM image type", data[1]);
    if (size >= 4 && le32(data) == 0x20534444u) return not_implemented("DDS image", 0);
    if (size >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff) return not_implemented("JPEG image", 0);
    r = decode_png(data, size, image);
    if (!d3d8::failed(r) || r == kNotImplemented) return r;
    image = Image();
    r = decode_tga(data, size, image);
    if (!d3d8::failed(r)) return r;
    image = Image();
    r = decode_dib(data, size, image);
    if (!d3d8::failed(r) || r == kNotImplemented) return r;
    image = Image();
    return kInvalidData;
}

// ---------------------------------------------------------------------------
// Pixel codecs (0x1003f796 factory, 0x1003ca6b base constructor).

struct Color {
    float r, g, b, a;
};

inline const float* dither_table(bool dither) {
    static float none[32], ordered[32];
    static bool ready = false;
    if (!ready) {
        static const std::uint32_t ordered_bits[32] = {
            0x3f780000u, 0x3ef00000u, 0x3f580000u, 0x3eb00000u, 0x3f780000u, 0x3ef00000u, 0x3f580000u, 0x3eb00000u,
            0x3e600000u, 0x3f380000u, 0x3dc00000u, 0x3f180000u, 0x3e600000u, 0x3f380000u, 0x3dc00000u, 0x3f180000u,
            0x3f480000u, 0x3e900000u, 0x3f680000u, 0x3ed00000u, 0x3f480000u, 0x3e900000u, 0x3f680000u, 0x3ed00000u,
            0x3d000000u, 0x3f080000u, 0x3e200000u, 0x3f280000u, 0x3d000000u, 0x3f080000u, 0x3e200000u, 0x3f280000u};
        for (int i = 0; i < 32; ++i) {
            none[i] = 0.5f;
            std::memcpy(&ordered[i], &ordered_bits[i], 4);
        }
        ready = true;
    }
    return dither ? ordered : none;  // 0x1005d748 / 0x1005d6c8
}

// 1/255 as stored at 0x100552a8 (0x3b808081).
inline float unit() {
    const std::uint32_t b = 0x3b808081u;
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

// Blit descriptor built by D3DXLoadSurfaceFromMemory.
struct Surface {
    unsigned char* data = nullptr;
    std::uint32_t format = 0;
    std::int32_t pitch = 0, slice_pitch = 0;
    std::uint32_t box[6] = {0, 0, 0, 0, 0, 1};  // left, top, right, bottom, front, back
    bool dither = false;
    std::uint32_t color_key = 0;
    const unsigned char* palette = nullptr;  // 256 PALETTEENTRY
};

class Codec {
public:
    // The original factory knows every uncompressed format of the table; only
    // the conversions ZGP's files need are ported (can_decode/can_encode).
    // Returns false (with a diagnostic) for compressed and YUV formats.
    bool create(const Surface& s) {
        CP2_D3DX_STRICT_FP
        format_ = s.format;
        const FormatInfo& info = format_info(s.format);
        if (info.format != s.format || is_dxt(s.format) || s.format == 0x59565955u || s.format == 0x32595559u) {
            not_implemented("D3DX pixel format", s.format);
            return false;
        }
        const unsigned bits = info.bits;
        decodes_ = format_ == kR8G8B8 || format_ == kA8R8G8B8 || format_ == kX8R8G8B8 || format_ == kP8 ||
                   format_ == kL8;
        encodes_ = format_ == kA8R8G8B8 || format_ == kX8R8G8B8 || format_ == kA8L8 || format_ == kL8;
        data_ = s.data;
        pitch_ = s.pitch;
        slice_pitch_ = s.slice_pitch;
        const float c = unit();
        const std::uint32_t k = s.color_key;  // 0x1002526e
        key_.r = static_cast<float>((k >> 16) & 0xff) * c;
        key_.g = static_cast<float>((k >> 8) & 0xff) * c;
        key_.b = static_cast<float>(k & 0xff) * c;
        key_.a = static_cast<float>(k >> 24) * c;
        keyed_ = k != 0;
        dither_ = dither_table(s.dither);
        bytes_ = bits >> 3;
        palette_kind_ = format_ == kP8 || format_ == kA8P8;
        if (palette_kind_) {
            for (int i = 0; i < 256; ++i) {
                if (s.palette) {
                    palette_[i].r = static_cast<float>(s.palette[i * 4 + 0]) * c;
                    palette_[i].g = static_cast<float>(s.palette[i * 4 + 1]) * c;
                    palette_[i].b = static_cast<float>(s.palette[i * 4 + 2]) * c;
                    palette_[i].a = static_cast<float>(s.palette[i * 4 + 3]) * c;
                } else {
                    palette_[i] = Color{1.0f, 1.0f, 1.0f, 1.0f};
                }
            }
        }
        width_ = s.box[2] - s.box[0];
        height_ = s.box[3] - s.box[1];
        depth_ = s.box[5] - s.box[4];
        row_bytes_ = bytes_ * width_;
        data_ += static_cast<std::ptrdiff_t>(bytes_) * s.box[0] + static_cast<std::ptrdiff_t>(pitch_) * s.box[1] +
                 static_cast<std::ptrdiff_t>(slice_pitch_) * s.box[4];
        return true;
    }

    std::uint32_t format() const { return format_; }
    bool can_decode() const { return decodes_; }
    bool can_encode() const { return encodes_; }
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    std::uint32_t depth() const { return depth_; }
    bool keyed() const { return keyed_; }
    bool has_palette() const { return palette_kind_; }
    const Color* palette() const { return palette_; }
    unsigned char* row(std::uint32_t y, std::uint32_t z) const {
        return data_ + static_cast<std::ptrdiff_t>(pitch_) * y + static_cast<std::ptrdiff_t>(slice_pitch_) * z;
    }
    std::uint32_t row_bytes() const { return row_bytes_; }

    // vtable slot 1: one row to RGBA floats, then the color key.
    void decode(std::uint32_t y, std::uint32_t z, Color* out) const {
        CP2_D3DX_STRICT_FP
        const unsigned char* p = row(y, z);
        const float c = unit();
        for (std::uint32_t x = 0; x < width_; ++x) {
            Color& o = out[x];
            switch (format_) {
            case kR8G8B8:  // 0x1003df3f
                o.r = static_cast<float>(p[2]) * c; o.g = static_cast<float>(p[1]) * c;
                o.b = static_cast<float>(p[0]) * c; o.a = 1.0f;
                p += 3;
                break;
            case kA8R8G8B8:  // 0x1003dfcb
                o.r = static_cast<float>(p[2]) * c; o.g = static_cast<float>(p[1]) * c;
                o.b = static_cast<float>(p[0]) * c; o.a = static_cast<float>(p[3]) * c;
                p += 4;
                break;
            case kX8R8G8B8:  // 0x1003e06d
                o.r = static_cast<float>(p[2]) * c; o.g = static_cast<float>(p[1]) * c;
                o.b = static_cast<float>(p[0]) * c; o.a = 1.0f;
                p += 4;
                break;
            case kP8:  // 0x1003e697
                o = palette_[*p++];
                break;
            case kL8: {  // 0x1003e6fb
                const float v = static_cast<float>(*p++) * c;
                o.r = v; o.g = v; o.b = v; o.a = 1.0f;
                break;
            }
            default: break;
            }
        }
        if (keyed_) {  // 0x1003cc70
            for (std::uint32_t x = 0; x < width_; ++x) {
                Color& o = out[x];
                if (o.r == key_.r && o.g == key_.g && o.b == key_.b && o.a == key_.a) o = Color{0.0f, 0.0f, 0.0f, 0.0f};
            }
        }
    }

    // Rec. 709 luminance in the original's order: (r*Kr + b*Kb) + g*Kg, with
    // the constants at 0x10055214, 0x10055210 and 0x1005520c (truncating).
    static float luminance(const Color& c) {
        const float l = chop_add(chop_mul(c.r, 0.212500006f), chop_mul(c.b, 0.0720999986f));
        return chop_add(l, chop_mul(c.g, 0.715399981f));
    }

    // vtable slot 2: one row from RGBA floats, truncating (0x1003ca38).
    void encode(std::uint32_t y, std::uint32_t z, const Color* in) const {
        unsigned char* p = row(y, z);
        const float* d = dither_ + ((z & 3) + 8 * (y & 3));
        const float k255 = 255.0f;
        for (std::uint32_t x = 0; x < width_; ++x) {
            const Color& c = in[x];
            const float dx = d[x & 3];
            auto channel = [&](float v) { return fistp_chop(chop_add(chop_mul(v, k255), dx)); };
            switch (format_) {
            case kA8R8G8B8: {  // 0x1003cda1
                const std::uint32_t r = static_cast<std::uint32_t>(channel(c.r));
                const std::uint32_t g = static_cast<std::uint32_t>(channel(c.g));
                const std::uint32_t b = static_cast<std::uint32_t>(channel(c.b));
                const std::uint32_t a = static_cast<std::uint32_t>(channel(c.a));
                const std::uint32_t v = ((((a << 8) | r) << 8 | g) << 8) | b;
                std::memcpy(p, &v, 4);  // little-endian, as on x86
                p += 4;
                break;
            }
            case kX8R8G8B8: {  // 0x1003ce9a
                const std::uint32_t r = static_cast<std::uint32_t>(channel(c.r));
                const std::uint32_t g = static_cast<std::uint32_t>(channel(c.g));
                const std::uint32_t b = static_cast<std::uint32_t>(channel(c.b));
                const std::uint32_t v = ((r << 8 | g) << 8) | b;
                std::memcpy(p, &v, 4);
                p += 4;
                break;
            }
            case kA8L8: {  // 0x1003d883
                const std::uint32_t lum = static_cast<std::uint32_t>(fistp_chop(chop_add(chop_mul(luminance(c), k255), dx)));
                const std::uint32_t a = static_cast<std::uint32_t>(channel(c.a));
                const std::uint16_t v = static_cast<std::uint16_t>(lum | ((a & 0xff) << 8));
                std::memcpy(p, &v, 2);
                p += 2;
                break;
            }
            case kL8:  // 0x1003d7e5
                *p++ = static_cast<unsigned char>(fistp_chop(chop_add(chop_mul(luminance(c), k255), dx)));
                break;
            default: break;
            }
        }
    }

private:
    unsigned char* data_ = nullptr;
    std::uint32_t format_ = 0;
    std::int32_t pitch_ = 0, slice_pitch_ = 0;
    Color key_ = {0, 0, 0, 0};
    bool keyed_ = false;
    const float* dither_ = nullptr;
    std::uint32_t bytes_ = 0;
    bool decodes_ = false, encodes_ = false;
    bool palette_kind_ = false;
    Color palette_[256];
    std::uint32_t width_ = 0, height_ = 0, depth_ = 0, row_bytes_ = 0;
};

// 0x10036f56: LINEAR filter taps for one axis.
struct Tap {
    std::int32_t i0;
    float w0;
    std::int32_t i1;
    float w1;
};

inline std::vector<Tap> linear_taps(std::uint32_t source, std::uint32_t target, bool wrap) {
    CP2_D3DX_STRICT_FP
    std::vector<Tap> taps(target);
    const float scale = static_cast<float>(source) / static_cast<float>(target);  // before 0x10036306
    for (std::uint32_t i = 0; i < target; ++i) {
        const float x = chop_sub(chop_mul(static_cast<float>(i), scale), 0.5f);
        const float floor_x = static_cast<float>(std::floor(static_cast<double>(x)));  // CRT floor, stored as float
        std::int32_t i0 = fistp_chop(floor_x);
        std::int32_t i1 = i0 + 1;
        if (i0 < 0) i0 = wrap ? static_cast<std::int32_t>(source) - 1 : 0;
        if (static_cast<std::uint32_t>(i1) >= source) i1 = wrap ? 0 : static_cast<std::int32_t>(source) - 1;
        const float fraction = chop_sub(x, floor_x);
        const float w0 = chop_sub(1.0f, fraction);
        const float w1 = chop_sub(1.0f, w0);
        taps[i] = Tap{i0, w0, i1, w1};
    }
    return taps;
}

// 2D BOX (strategy 5, 0x10036818). Without DITHER, with the same format on
// both sides and a source of at least 2x2, the original uses an integer path
// per format (0x100355fc X8R8G8B8, 0x1003549e A8R8G8B8, ...); formats whose
// entry is the E_NOTIMPL stub 0x10036075 (R8G8B8, A8P8, P8) and all other
// cases use the float path: each output texel is
// ((row0[2x+1] + row0[2x]) + row1[2x]) + row1[2x+1], times 0.25, in single
// precision, where a source of width or height 1 repeats its texel or row.
inline HResult box_2d(Codec& d, Codec& s, bool dither) {
    CP2_D3DX_STRICT_FP
    const std::uint32_t format = s.format();
    if (!dither && d.format() == format && s.width() >= 2 && s.height() >= 2) {
        if (format == kX8R8G8B8 || format == kA8R8G8B8) {
            for (std::uint32_t y = 0; y < d.height(); ++y) {
                const unsigned char* r0 = s.row(2 * y, 0);
                const unsigned char* r1 = s.row(2 * y + 1, 0);
                unsigned char* out = d.row(y, 0);
                for (std::uint32_t x = 0; x < d.width(); ++x) {
                    const std::uint32_t p00 = le32(r0 + 8 * x), p01 = le32(r0 + 8 * x + 4);
                    const std::uint32_t p10 = le32(r1 + 8 * x), p11 = le32(r1 + 8 * x + 4);
                    std::uint32_t v;
                    if (format == kX8R8G8B8) {
                        // 0x100355fc: red/blue and green lanes, alpha 0.
                        std::uint32_t rb = (p00 & 0xff00ffu) + (p10 & 0xff00ffu);
                        rb += p01 & 0xff00ffu;
                        rb = (rb + (p11 & 0xff00ffu) + 0x20002u) & 0x3fc03fcu;
                        std::uint32_t g = (p00 & 0xff00u) + (p10 & 0xff00u);
                        g += p01 & 0xff00u;
                        g = (g + (p11 & 0xff00u) + 0x200u) & 0x3fc00u;
                        v = (rb | g) >> 2;
                    } else {
                        // 0x1003549e: alpha/green from the quarter values,
                        // red/blue from the sums, combined with xor masks.
                        const std::uint32_t m = 0xffc03fc0u, n = 0x3fc03fc0u;
                        std::uint32_t ag = ((p00 >> 2) & m) + ((p10 >> 2) & m);
                        ag += (p01 >> 2) & m;
                        ag = ag + ((p11 >> 2) & m) - 0x7fff80u;
                        std::uint32_t rb = (p00 & 0xff00ffu) + (p10 & 0xff00ffu);
                        rb += p01 & 0xff00ffu;
                        rb = (rb + (p11 & 0xff00ffu) + 0x20002u) >> 2;
                        std::uint32_t q = ((p00 >> 2) & n) + ((p10 >> 2) & n);
                        q += (p01 >> 2) & n;
                        q = q + ((p11 >> 2) & n) + 0x800080u;
                        v = ((ag ^ rb) & 0xff00ffu) ^ q;
                    }
                    std::memcpy(out + 4 * x, &v, 4);
                }
            }
            return kOk;
        }
        static const std::uint32_t integer_paths[] = {kA8R8G8B8, kX8R8G8B8, kR5G6B5, kX1R5G5B5, kA1R5G5B5, kA4R4G4B4,
                                                      kR3G3B2, kA8, kL8, kA8R3G3B2, kX4R4G4B4, kA8L8, kA4L4};
        for (std::uint32_t f : integer_paths)
            if (f == format) return not_implemented("D3DX integer BOX filter for format", format);
    }
    if (!s.can_decode()) return not_implemented("D3DX decoding of format", s.format());
    if (!d.can_encode()) return not_implemented("D3DX encoding of format", d.format());
    std::vector<Color> out(d.width()), r0(s.width()), r1(s.width());
    const bool one_row = s.height() == 1;
    const std::uint32_t second = s.width() == 1 ? 0 : 1;
    for (std::uint32_t y = 0; y < d.height(); ++y) {
        s.decode(2 * y, 0, r0.data());
        if (!one_row) s.decode(2 * y + 1, 0, r1.data());
        const std::vector<Color>& q = one_row ? r0 : r1;
        for (std::uint32_t x = 0; x < d.width(); ++x) {
            const Color& a = r0[2 * x];
            const Color& b = r0[2 * x + second];
            const Color& c = q[2 * x];
            const Color& e = q[2 * x + second];
            Color sum{b.r + a.r, b.g + a.g, b.b + a.b, b.a + a.a};
            sum = Color{sum.r + c.r, sum.g + c.g, sum.b + c.b, sum.a + c.a};
            sum = Color{sum.r + e.r, sum.g + e.g, sum.b + e.b, sum.a + e.a};
            out[x] = Color{sum.r * 0.25f, sum.g * 0.25f, sum.b * 0.25f, sum.a * 0.25f};
        }
        d.encode(y, 0, out.data());
    }
    return kOk;
}

// Blitter (0x100379b5) with the strategies ZGP's calls reach: raw copy
// (0x10036339), same-size conversion (0x1003646b) and 2D LINEAR (0x10037027).
inline HResult blit(const Surface& destination, const Surface& source, std::uint32_t filter) {
    CP2_D3DX_STRICT_FP
    if ((filter & 0xffff) == 0 || (filter & 0xffff) > 5 || (filter & 0xfff00000u)) return kInvalidCall;
    Surface dst = destination;
    dst.dither = (filter & 0x80000) != 0;
    Codec d, s;
    if (!d.create(dst) || !s.create(source)) return kNotImplemented;
    auto convertible = [&]() {
        if (!s.can_decode()) { not_implemented("D3DX decoding of format", s.format()); return false; }
        if (!d.can_encode()) { not_implemented("D3DX encoding of format", d.format()); return false; }
        return true;
    };
    // Strategy 1: identical format and size without a color key.
    if (s.format() == d.format() && !s.keyed() && s.width() == d.width() && s.height() == d.height() &&
        s.depth() == d.depth()) {
        bool same = true;
        if (d.has_palette()) same = std::memcmp(d.palette(), s.palette(), 0x400) == 0;  // only 64 entries compared
        if (same) {
            for (std::uint32_t z = 0; z < d.depth(); ++z)
                for (std::uint32_t y = 0; y < d.height(); ++y) std::memcpy(d.row(y, z), s.row(y, z), d.row_bytes());
            return kOk;
        }
    }
    // Strategy 2: same size, any formats.
    if (s.width() == d.width() && s.height() == d.height() && s.depth() == d.depth()) {
        if (!convertible()) return kNotImplemented;
        std::vector<Color> buffer(d.width());
        for (std::uint32_t z = 0; z < d.depth(); ++z)
            for (std::uint32_t y = 0; y < d.height(); ++y) {
                s.decode(y, z, buffer.data());
                d.encode(y, z, buffer.data());
            }
        return kOk;
    }
    // Strategy 5 (0x10036818): 2D BOX, each axis halved (or already 1).
    if ((filter & 0xff) == 5 && s.depth() == 1 && d.depth() == 1 &&
        (d.width() == s.width() >> 1 || (d.width() == 1 && s.width() == 1)) &&
        (d.height() == s.height() >> 1 || (d.height() == 1 && s.height() == 1))) {
        return box_2d(d, s, (filter & 0x80000) != 0) ;
    }
    if ((filter & 0xff) != 3) return not_implemented("D3DX resampling filter", filter & 0xff);
    if (!convertible()) return kNotImplemented;
    // Strategy 7: 2D LINEAR. Two decoded source rows are blended per output row.
    const std::vector<Tap> xt = linear_taps(s.width(), d.width(), !(filter & 0x10000));
    const std::vector<Tap> yt = linear_taps(s.height(), d.height(), !(filter & 0x20000));
    std::vector<Color> out(d.width()), p(s.width()), q(s.width());
    for (std::uint32_t y = 0; y < d.height(); ++y) {
        const Tap& ty = yt[y];
        s.decode(static_cast<std::uint32_t>(ty.i0), 0, p.data());
        s.decode(static_cast<std::uint32_t>(ty.i1), 0, q.data());
        for (std::uint32_t x = 0; x < d.width(); ++x) {
            const Tap& tx = xt[x];
            const Color& q1 = q[static_cast<std::uint32_t>(tx.i1)];
            const Color& q0 = q[static_cast<std::uint32_t>(tx.i0)];
            const Color& p1 = p[static_cast<std::uint32_t>(tx.i1)];
            const Color& p0 = p[static_cast<std::uint32_t>(tx.i0)];
            const Color xq{tx.w0 * q0.r + tx.w1 * q1.r, tx.w0 * q0.g + tx.w1 * q1.g, tx.w0 * q0.b + tx.w1 * q1.b,
                           tx.w0 * q0.a + tx.w1 * q1.a};
            const Color xp{tx.w0 * p0.r + tx.w1 * p1.r, tx.w0 * p0.g + tx.w1 * p1.g, tx.w0 * p0.b + tx.w1 * p1.b,
                           tx.w0 * p0.a + tx.w1 * p1.a};
            out[x] = Color{xp.r * ty.w0 + xq.r * ty.w1, xp.g * ty.w0 + xq.g * ty.w1, xp.b * ty.w0 + xq.b * ty.w1,
                           xp.a * ty.w0 + xq.a * ty.w1};
        }
        d.encode(y, 0, out.data());
    }
    return kOk;
}

// D3DXLoadSurfaceFromMemory (0x10030d60) into a whole texture level.
inline HResult load_level_from_memory(d3d8::Texture& texture, std::uint32_t level, const Image& image,
                                      const unsigned char* palette, std::uint32_t filter, std::uint32_t color_key) {
    if (!image.data) return kInvalidCall;
    if (filter == 0xffffffffu) filter = format_info(image.format).category != 3 ? 0x80004u : 0x80002u;
    d3d8::SurfaceDesc desc;
    HResult r = texture.level_desc(level, desc);
    if (d3d8::failed(r)) return r;
    if (is_dxt(desc.format)) return not_implemented("D3DX DXT destination format", desc.format);
    d3d8::LockedRect locked;
    r = texture.lock_rect(level, locked, 0);
    if (d3d8::failed(r)) return r;
    Surface dst;
    dst.data = locked.bits;
    dst.format = desc.format;
    dst.pitch = locked.pitch;
    dst.box[2] = desc.width;
    dst.box[3] = desc.height;
    Surface src;
    src.data = const_cast<unsigned char*>(image.data);
    src.format = image.format;
    src.pitch = image.pitch;
    src.box[2] = image.width;
    src.box[3] = image.height;
    src.color_key = color_key;
    src.palette = palette;
    r = blit(dst, src, filter);
    texture.unlock_rect(level);
    return d3d8::failed(r) ? r : kOk;
}

// One texture level as a blitter surface (D3DXLoadSurfaceFromSurface's lock).
inline HResult lock_level(d3d8::Texture& texture, std::uint32_t level, Surface& surface) {
    d3d8::SurfaceDesc desc;
    HResult r = texture.level_desc(level, desc);
    if (d3d8::failed(r)) return r;
    d3d8::LockedRect locked;
    r = texture.lock_rect(level, locked, 0);
    if (d3d8::failed(r)) return r;
    surface.data = locked.bits;
    surface.format = desc.format;
    surface.pitch = locked.pitch;
    surface.box[2] = desc.width;
    surface.box[3] = desc.height;
    return kOk;
}

// D3DXFilterTexture (0x10031141): fills the levels after `source` with
// D3DXLoadSurfaceFromSurface (0x10031024, no color key). POINT and BOX read
// the previous level; other filters always read the source level.
inline std::uint32_t texture_format(d3d8::Texture& texture) {
    d3d8::SurfaceDesc desc;
    texture.level_desc(0, desc);
    return desc.format;
}

inline HResult filter_texture(d3d8::Texture& texture, const unsigned char* palette, std::uint32_t source,
                              std::uint32_t filter) {
    if (filter == 0xffffffffu) {
        d3d8::SurfaceDesc desc;
        texture.level_desc(0, desc);
        filter = format_info(desc.format).category != 3 ? 5u : 2u;
    }
    const std::uint32_t levels = texture.level_count();
    if (source == 0xffffffffu) source = 0;
    if (source >= levels) return kInvalidCall;
    const bool cascade = (filter & 0xff) == 2 || (filter & 0xff) == 5;
    std::uint32_t from = source;
    for (std::uint32_t level = source + 1; level < levels; ++level) {
        if (is_dxt(texture_format(texture))) return not_implemented("D3DX DXT filtering", texture_format(texture));
        Surface src, dst;
        HResult r = lock_level(texture, from, src);
        if (d3d8::failed(r)) return r;
        r = lock_level(texture, level, dst);
        if (d3d8::failed(r)) { texture.unlock_rect(from); return r; }
        src.palette = palette;
        dst.palette = palette;
        r = blit(dst, src, filter);
        texture.unlock_rect(level);
        texture.unlock_rect(from);
        if (d3d8::failed(r)) return r;
        if (cascade) from = level;
    }
    return kOk;
}

// D3DXCreateTextureFromFileInMemoryEx (0x10031272). Source info and palette
// outputs are not used by ZGP and are not supported here.
inline HResult create_texture_from_memory(d3d8::Device* device, const unsigned char* data, std::size_t size,
                                          std::uint32_t width, std::uint32_t height, std::uint32_t levels,
                                          std::uint32_t usage, std::uint32_t format, std::uint32_t pool,
                                          std::uint32_t filter, std::uint32_t mip_filter, std::uint32_t color_key,
                                          d3d8::Texture*& texture) {
    texture = nullptr;
    if (!device || !data || !size) return kInvalidCall;
    Image image;
    HResult r = load_image(data, size, image);
    if (d3d8::failed(r)) return r;
    if (!image.data) image.data = image.owned.data();
    const std::uint32_t images = 1;  // only DDS files carry image chains
    if (width == 0 || width == 0xffffffffu) width = image.width;
    if (height == 0 || height == 0xffffffffu) height = image.height;
    const std::uint32_t source = image.format;
    if (format == 0) {
        if (color_key) {
            if (source == kR8G8B8) format = kA8R8G8B8;
            else if (source <= kA8R8G8B8) format = source;
            else if (source <= kR5G6B5) format = kA8R8G8B8;
            else if (source == kX1R5G5B5 || source == kR3G3B2 || source == kX4R4G4B4) format = kA1R5G5B5;
            else if (source == kL8) format = kA8L8;
            else format = source;
        } else {
            format = source == kR8G8B8 ? kX8R8G8B8 : source;
        }
    }
    if (filter == 0xffffffffu) filter = format_info(source).category != 3 ? 0x80004u : 0x80002u;
    if (mip_filter == 0xffffffffu) mip_filter = format_info(source).category != 3 ? 5u : 2u;
    const bool use_filter_texture = (filter & 0xff) == 1 || (mip_filter & 0xff) == 2 || (mip_filter & 0xff) == 5;
    // Palette with the color key applied to matching entries (then no key).
    unsigned char palette[1024];
    if (!image.palette.empty()) {
        const std::uint32_t key = ((color_key >> 16) & 0xff) | ((color_key & 0xff) << 16) | (color_key & 0xff00ff00u);
        for (int i = 0; i < 256; ++i) {
            const std::uint32_t entry = le32(image.palette.data() + i * 4);
            const std::uint32_t value = entry == key ? 0 : entry;
            std::memcpy(palette + i * 4, &value, 4);
        }
        color_key = 0;
    } else {
        std::memset(palette, 0xff, sizeof(palette));
    }
    if (format == kA8P8) {
        format = kA8R8G8B8;
    } else if (format == kP8) {
        format = kX8R8G8B8;
        if (image.palette.empty()) return not_implemented("D3DX P8 request without an image palette", 0);
        for (int i = 0; i < 256; ++i)
            if (image.palette[i * 4 + 3] != 0xff) { format = kA8R8G8B8; break; }
    }
    r = check_texture_requirements(device, &width, &height, &levels, usage, &format, pool);
    if (d3d8::failed(r)) return r;
    if (pool == d3d8::kPoolDefault) return not_implemented("D3DX texture pool", pool);
    d3d8::Texture* created = nullptr;
    r = device->create_texture(width, height, levels, usage, format, pool, created);
    if (d3d8::failed(r)) return r;
    std::uint32_t level = 0;
    for (; level < images && level < levels; ++level) {
        r = load_level_from_memory(*created, level, image, palette, filter, color_key);
        if (d3d8::failed(r)) { created->release(); return r; }
    }
    if (use_filter_texture) {
        if (images < levels) {
            r = filter_texture(*created, palette, images - 1, mip_filter);
            if (d3d8::failed(r)) { created->release(); return r; }
        }
    } else {
        for (; level < levels; ++level) {
            r = load_level_from_memory(*created, level, image, palette, mip_filter, color_key);
            if (d3d8::failed(r)) { created->release(); return r; }
        }
    }
    texture = created;
    return kOk;
}

}  // namespace d3dx
}  // namespace cp2
