// Generated gsquare cases shared by the game-runtime reference
// (fcpoly_windows_reference.cpp) and the port's test (fcpoly_test.cpp):
// 24-bit bottom-up DIBs with DWORD-padded rows, a destination of 200x150
// and a 48x32 source, quads from small to larger than the destination, UVs
// inside and outside the source, every gmode 0-6 with rates around the
// clamps, and flat fills.
#pragma once

#include <cstdint>
#include <vector>

namespace fcpoly_cases {

const int kDestWidth = 200, kDestHeight = 150, kSourceWidth = 48, kSourceHeight = 32;
const int kCount = 600;

struct Case {
    int x[4], y[4], u[4], v[4];
    int gmode, rate;
    unsigned int rgb;  // 0x00RRGGBB: the current color (key or fill)
    bool flat;
};

inline std::uint32_t next(std::uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s >> 8;
}

inline int stride(int width) { return ((width * 3) + 3) & ~3; }

inline void source_pixels(std::vector<unsigned char>& p) {
    p.assign(static_cast<std::size_t>(stride(kSourceWidth)) * kSourceHeight, 0);
    std::uint32_t s = 12345;
    for (int y = 0; y < kSourceHeight; ++y)
        for (int x = 0; x < kSourceWidth; ++x) {
            unsigned char* q = &p[static_cast<std::size_t>(y) * stride(kSourceWidth) + x * 3];
            const std::uint32_t r = next(s);
            // Some black texels (gmode 2) and some key-colored ones (gmode 4).
            if (r % 11 == 0) { q[0] = q[1] = q[2] = 0; continue; }
            if (r % 13 == 0) { q[0] = 0x1e; q[1] = 0x14; q[2] = 0x0a; continue; }
            q[0] = static_cast<unsigned char>(r);
            q[1] = static_cast<unsigned char>(r >> 8);
            q[2] = static_cast<unsigned char>(r >> 16);
        }
}

inline void dest_pixels(std::vector<unsigned char>& p) {
    p.assign(static_cast<std::size_t>(stride(kDestWidth)) * kDestHeight, 0);
    for (int y = 0; y < kDestHeight; ++y)
        for (int x = 0; x < kDestWidth; ++x) {
            unsigned char* q = &p[static_cast<std::size_t>(y) * stride(kDestWidth) + x * 3];
            q[0] = static_cast<unsigned char>(x * 7 + y);
            q[1] = static_cast<unsigned char>(x + y * 5);
            q[2] = static_cast<unsigned char>(180 - x + y);
        }
}

inline void make(int index, Case& c) {
    std::uint32_t s = 0x9e3779b9u ^ static_cast<std::uint32_t>(index * 7919);
    next(s);
    const int spread = index % 3 == 0 ? 60 : index % 3 == 1 ? 160 : 320;
    const int cx = static_cast<int>(next(s) % 260) - 30, cy = static_cast<int>(next(s) % 210) - 30;
    for (int i = 0; i < 4; ++i) {
        c.x[i] = cx + static_cast<int>(next(s) % spread) - spread / 2;
        c.y[i] = cy + static_cast<int>(next(s) % spread) - spread / 2;
        c.u[i] = static_cast<int>(next(s) % (kSourceWidth + 20)) - 10;
        c.v[i] = static_cast<int>(next(s) % (kSourceHeight + 20)) - 10;
    }
    if (index % 10 == 0) {  // axis-aligned with the full source
        const int w = 20 + static_cast<int>(next(s) % 120), h = 10 + static_cast<int>(next(s) % 90);
        c.x[0] = cx; c.y[0] = cy; c.x[1] = cx + w; c.y[1] = cy; c.x[2] = cx + w; c.y[2] = cy + h; c.x[3] = cx; c.y[3] = cy + h;
        c.u[0] = 0; c.v[0] = 0; c.u[1] = kSourceWidth; c.v[1] = 0; c.u[2] = kSourceWidth; c.v[2] = kSourceHeight; c.u[3] = 0; c.v[3] = kSourceHeight;
    }
    c.gmode = static_cast<int>(next(s) % 7);
    static const int rates[] = {0, 1, 77, 128, 200, 255, 256, 300, -5};
    c.rate = rates[next(s) % 9];
    c.rgb = index % 4 == 0 ? 0x0a141eu : (next(s) & 0xffffffu);
    c.flat = index % 9 == 4;
}

inline std::uint64_t hash(const std::vector<unsigned char>& p) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char b : p) h = (h ^ b) * 1099511628211ull;
    return h;
}

}  // namespace fcpoly_cases
