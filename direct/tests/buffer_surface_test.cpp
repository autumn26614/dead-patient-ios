// Replays direct/tests/buffer_surface_reference.hsp on cp2::BufferSurface and
// compares every pixel with what the official Windows HSP 3.7 runtime drew
// (direct/tests/fixtures/buffer_surface.reference, from pget). Pictures are
// decoded with the stb_image v2.23 that HSP3Dish links (and Windows HSP uses
// for PNG, PSD and TGA).
//   buffer_surface_test REFERENCE FIXTURES_DIRECTORY
#include "buffer_surface.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "../ios/hspproj/hsp3dish/emscripten/stb_image.h"

#include <cstdlib>
#include <iterator>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

// HSP's boxf x1, y1, x2, y2 on a buffer: FillRect(x1, y1, x2 + 1, y2 + 1).
void boxf(cp2::BufferSurface& s, int x1, int y1, int x2, int y2, std::uint32_t rgb) {
    s.fill(x1, y1, x2 + 1, y2 + 1, rgb);
}

std::string dump(const cp2::BufferSurface& s) {
    std::string row;
    char cell[16];
    for (int y = 0; y < s.height(); ++y)
        for (int x = 0; x < s.width(); ++x) {
            std::snprintf(cell, sizeof(cell), "%06x ", static_cast<unsigned>(s.pixel(x, y)));
            row += cell;
        }
    return row;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: buffer_surface_test REFERENCE FIXTURES_DIRECTORY\n";
        return 2;
    }
    const std::string fixtures = argv[2];
    std::vector<std::string> out;
    auto add = [&](const cp2::BufferSurface& s, const char* label) {
        out.push_back(dump(s));
        out.push_back(label);
    };
    const int sx = 13, sy = 7;
    cp2::BufferSurface s;
    s.create(sx, sy);
    add(s, "buffer");
    boxf(s, 2, 1, 5, 3, 0xff0000);
    add(s, "boxf 2,1,5,3");
    boxf(s, 8, 5, 6, 2, 0x00ff00);
    add(s, "boxf 8,5,6,2 inverted");
    boxf(s, -3, -2, 20, 1, 0x0000ff);
    add(s, "boxf -3,-2,20,1 clipped");
    boxf(s, 4, 4, 4, 4, 0x0a141e);
    add(s, "boxf 4,4,4,4");
    boxf(s, 11, 5, sx, sy, 0xc86432);  // omitted x2, y2 default to the size
    add(s, "boxf 11,5 defaults");
    boxf(s, 0, 0, 2, 2, 0x010203);  // gmode does not apply
    add(s, "boxf with gmode 3, 128");
    boxf(s, 0, 0, sx, sy, 0x070809);
    add(s, "boxf all");
    for (int mode = 0; mode < 5; ++mode) {
        s.cls(mode);
        add(s, ("cls " + std::to_string(mode)).c_str());
    }
    boxf(s, 1, 1, 3, 3, 0xffff00);
    s.create(sx, sy);
    add(s, "buffer again");
    cp2::BufferSurface small;
    small.create(5, 3);
    boxf(small, 0, 0, 1024, 1024, 0x000000);
    add(small, "buffer 2, boxf 0,0,1024,1024 black");

    // picload into buffers, decoded with the stb_image HSP3Dish links.
    struct Picture {
        std::vector<std::uint8_t> rgba;
        int width = 0, height = 0, components = 0;
        bool stb = false;
    };
    auto load = [&](const std::string& name, Picture& p) {
        const std::string path = fixtures + "/" + name;
        std::ifstream file(path, std::ios::binary);
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        unsigned char* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &p.width, &p.height,
                                                      &p.components, 4);
        if (!pixels) {
            std::cerr << "cannot decode " << path << "\n";
            std::exit(2);
        }
        p.rgba.assign(pixels, pixels + static_cast<std::size_t>(p.width) * p.height * 4);
        stbi_image_free(pixels);
        p.stb = cp2::picload_uses_stb(name);
    };
    Picture bmp, png;
    load("buffer_test.bmp", bmp);
    load("buffer_test.png", png);
    int cx = 0, cy = 0;
    auto picload = [&](cp2::BufferSurface& target, const Picture& p, int mode) {
        if (!cp2::picload_surface(target, mode, cx, cy, p.rgba.data(), p.width, p.height, p.components, p.stb)) {
            std::cerr << "picload failed\n";
            std::exit(2);
        }
    };
    cp2::BufferSurface b3;
    b3.create(13, 7);
    cx = 2; cy = 1; picload(b3, bmp, 1);
    add(b3, "picload bmp 1 at 2,1");
    cx = 9; cy = 4; picload(b3, png, 1);
    add(b3, "picload png 1 at 9,4");
    cx = -3; cy = -2; picload(b3, png, 1);
    add(b3, "picload png 1 at -3,-2");
    cx = -2; cy = 3; picload(b3, bmp, 1);
    add(b3, "picload bmp 1 at -2,3");
    cx = 5; cy = 5; picload(b3, bmp, 0);
    add(b3, ("picload bmp 0 size " + std::to_string(b3.width()) + "," + std::to_string(b3.height())).c_str());
    cx = 1; cy = 1; picload(b3, png, 2);
    add(b3, ("picload png 2 size " + std::to_string(b3.width()) + "," + std::to_string(b3.height())).c_str());
    picload(b3, png, 0);
    add(b3, ("picload png 0 size " + std::to_string(b3.width()) + "," + std::to_string(b3.height())).c_str());
    // pget: the pixel becomes the current color (white outside the bitmap).
    auto pget = [&](const cp2::BufferSurface& s, int x, int y) {
        const std::int64_t p = s.pixel(x, y);
        const std::uint32_t c = p < 0 ? 0xffffffu : static_cast<std::uint32_t>(p);
        out.push_back("pget " + std::to_string(x) + "," + std::to_string(y) + " " + std::to_string((c >> 16) & 0xff) +
                      "," + std::to_string((c >> 8) & 0xff) + "," + std::to_string(c & 0xff));
    };
    pget(b3, 2, 1);
    pget(b3, 5, 3);
    // gcopy between buffers in every gmode.
    cp2::BufferSurface b4;
    b4.create(16, 8);
    cx = 0; cy = 0; picload(b4, bmp, 1);
    cx = 7; cy = 0; picload(b4, bmp, 1);
    boxf(b4, 0, 5, 3, 7, 0x000000);
    boxf(b4, 4, 5, 6, 7, 0x0a141e);
    cp2::BufferSurface b5;
    for (int m = 0; m < 8; ++m) {
        b5.create(12, 9);
        boxf(b5, 0, 0, 12, 9, 0x5aa0dc);
        b5.blt(m, b4, 1, 1, 2, 1, 9, 7, 100, 0x0a141e);
        add(b5, ("gcopy gmode " + std::to_string(m)).c_str());
    }
    b5.create(12, 9);
    boxf(b5, 0, 0, 12, 9, 0x5aa0dc);
    b5.blt(7, b4, 2, 2, 0, 0, 5, 4, 100, 0x0a141e);
    add(b5, "gcopy gmode 7 alpha in the row");
    b5.blt(3, b4, 2, 2, 2, 1, 9, 7, 256, 0x0a141e);
    add(b5, "gcopy gmode 3 rate 256");
    b5.blt(5, b4, 2, 2, 2, 1, 9, 7, 300, 0x0a141e);
    add(b5, "gcopy gmode 5 rate 300");
    b5.blt(6, b4, 2, 2, 2, 1, 9, 7, 0, 0x0a141e);
    add(b5, "gcopy gmode 6 rate 0");
    b5.create(12, 9);
    b5.blt(1, b4, -2, -1, 0, 0, 16, 8, 0, 0);
    add(b5, "gcopy clipped at -2,-1");
    b5.blt(1, b4, 8, 5, 0, 0, 16, 8, 0, 0);
    add(b5, "gcopy clipped at 8,5");
    b5.blt(1, b4, 0, 0, 12, 3, 8, 8, 0, 0);
    add(b5, "gcopy source clipped at 12,3");
    b5.blt(1, b4, 3, 3, -2, -1, 6, 4, 0, 0);
    add(b5, "gcopy negative source -2,-1");

    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::cerr << "cannot read " << argv[1] << "\n";
        return 2;
    }
    std::vector<std::string> expected;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        expected.push_back(line);
    }
    while (!expected.empty() && expected.back().empty()) expected.pop_back();
    int mismatches = 0;
    for (std::size_t i = 0; i < std::max(out.size(), expected.size()); ++i) {
        const std::string a = i < out.size() ? out[i] : "<missing>";
        const std::string b = i < expected.size() ? expected[i] : "<missing>";
        if (a != b) {
            if (++mismatches <= 5) std::cerr << "line " << i + 1 << "\n  port:    " << a << "\n  windows: " << b << "\n";
        }
    }
    // The surface keeps a 24-bit bottom-up DIB with DWORD-padded rows.
    if (s.stride() != 40 || s.bytes().size() != 40u * 7u) {
        std::cerr << "unexpected DIB layout: stride " << s.stride() << "\n";
        ++mismatches;
    }
    std::printf("buffer surface: %zu lines compared with Windows HSP, %d mismatches\n", out.size(), mismatches);
    return mismatches ? 1 : 0;
}
