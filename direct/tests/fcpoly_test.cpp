// gsquare onto memory bitmaps (direct/runtime/hsp_polygon.hpp) against the
// game's own HSP runtime: fcpoly_windows_reference.cpp runs the same
// generated cases (fcpoly_cases.hpp) through CorpseParty2.exe's fcpoly with
// the x87 unit in double and in single precision.
//   fcpoly_test REFERENCE
#include "hsp_polygon.hpp"
#include "fcpoly_cases.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: fcpoly_test REFERENCE\n");
        return 2;
    }
    std::ifstream in(argv[1]);
    std::map<std::string, std::string> expected;
    for (std::string line; std::getline(in, line);) {
        std::istringstream fields(line);
        std::string mode, index, hash;
        if (fields >> mode >> index >> hash) expected[mode + " " + index] = hash;
    }
    if (expected.empty()) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    std::vector<unsigned char> source_bytes, dest_bytes;
    fcpoly_cases::source_pixels(source_bytes);
    cp2::BufferSurface source;
    source.create(fcpoly_cases::kSourceWidth, fcpoly_cases::kSourceHeight);
    std::memcpy(source.mutable_bytes(), source_bytes.data(), source_bytes.size());
    int compared = 0, mismatches = 0;
    for (int single = 0; single < 2; ++single) {
        cp2::x87_single_precision() = single != 0;
        for (int index = 0; index < fcpoly_cases::kCount; ++index) {
            fcpoly_cases::Case c;
            fcpoly_cases::make(index, c);
            fcpoly_cases::dest_pixels(dest_bytes);
            cp2::BufferSurface dest;
            dest.create(fcpoly_cases::kDestWidth, fcpoly_cases::kDestHeight);
            std::memcpy(dest.mutable_bytes(), dest_bytes.data(), dest_bytes.size());
            cp2::gsquare_surface(dest, c.flat ? nullptr : &source, c.x, c.y, c.u, c.v, c.gmode, c.rate, c.rgb);
            char hash[32];
            std::snprintf(hash, sizeof(hash), "%016llx", static_cast<unsigned long long>(fcpoly_cases::hash(dest.bytes())));
            const std::string key = std::string(single ? "single" : "double") + " " + std::to_string(index);
            ++compared;
            if (expected[key] != hash) {
                if (++mismatches <= 8)
                    std::fprintf(stderr, "%s: gmode %d rate %d %s: port %s, game %s\n", key.c_str(), c.gmode, c.rate,
                                 c.flat ? "flat" : "textured", hash, expected[key].c_str());
            }
        }
    }
    std::printf("gsquare: %d cases compared with the game's fcpoly, %d mismatches\n", compared, mismatches);
    return mismatches ? 1 : 0;
}
