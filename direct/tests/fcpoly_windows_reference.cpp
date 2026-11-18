// Reference for direct/runtime/hsp_polygon.hpp from the game's own HSP
// runtime: maps CorpseParty2.exe's sections at its image base (0x400000)
// and calls its fcpoly routines (SetPolyDest 0x40b650, SetPolySource
// 0x40cae0, SetPolyColorKey 0x40b6a0, DrawPolygonF4 0x40b980,
// DrawPolygonTex 0x40bd20) on generated quads, with the x87 unit in single
// precision (as after ZGP's createdevm) or in its default double precision.
// gsquare's argument handling (hsp3gr_wingui.cpp) is applied here as in
// Windows HSP. Writes one line per case: the case and an FNV-1a hash of the
// destination bitmap.
//
// Build (i686, an image base away from 0x400000):
//   clang++ -target i686-w64-mingw32 -std=c++14 -O1 -static -Wl,--image-base,0x10000000
//           direct/tests/fcpoly_windows_reference.cpp -o build/fcpoly-reference.exe
//   build/fcpoly-reference.exe "Corpse Party 2 Dead Patient/CorpseParty2.exe" OUTPUT
#include <windows.h>
#include <float.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "fcpoly_cases.hpp"

namespace {

#pragma pack(push, 1)
struct Poly4 {
    short code, attr, tex, npoly;
    short v[4][4];  // x, y, tx, ty
    int color;
    short alpha, specular;
};
#pragma pack(pop)

typedef void(__cdecl* SetDest)(void*, int, int);
typedef void(__cdecl* SetKey)(int);
typedef void(__cdecl* Draw)(Poly4*);

bool map_image(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::vector<unsigned char> file;
    unsigned char buffer[65536];
    std::size_t n;
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) file.insert(file.end(), buffer, buffer + n);
    std::fclose(f);
    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    const IMAGE_NT_HEADERS32* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data() + dos->e_lfanew);
    if (nt->OptionalHeader.ImageBase != 0x400000) {
        std::fprintf(stderr, "image base %08lx\n", static_cast<unsigned long>(nt->OptionalHeader.ImageBase));
        return false;
    }
    unsigned char* base = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(0x400000),
                                                                   nt->OptionalHeader.SizeOfImage,
                                                                   MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (base != reinterpret_cast<unsigned char*>(0x400000)) {
        std::fprintf(stderr, "VirtualAlloc at 0x400000 (%lu bytes) failed: error %lu\n",
                     static_cast<unsigned long>(nt->OptionalHeader.SizeOfImage), GetLastError());
        return false;
    }
    std::memcpy(base, file.data(), nt->OptionalHeader.SizeOfHeaders);
    const IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s) {
        const std::size_t size = std::min<std::size_t>(s->SizeOfRawData, s->Misc.VirtualSize ? s->Misc.VirtualSize : s->SizeOfRawData);
        if (s->PointerToRawData && size) std::memcpy(base + s->VirtualAddress, file.data() + s->PointerToRawData, size);
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: fcpoly-reference CorpseParty2.exe OUTPUT\n");
        return 2;
    }
    if (!map_image(argv[1])) {
        std::fprintf(stderr, "cannot map %s at 0x400000\n", argv[1]);
        return 2;
    }
    // ScanLine's buffer, large enough that it never calls the CRT's malloc.
    static unsigned char scan[24 * 4096];
    *reinterpret_cast<int*>(0x435848) = 4096;
    *reinterpret_cast<void**>(0x43585c) = scan;
    SetDest set_dest = reinterpret_cast<SetDest>(0x40b650);
    SetDest set_source = reinterpret_cast<SetDest>(0x40cae0);
    SetKey set_key = reinterpret_cast<SetKey>(0x40b6a0);
    Draw draw_flat = reinterpret_cast<Draw>(0x40b980);
    Draw draw_textured = reinterpret_cast<Draw>(0x40bd20);
    FILE* out = std::fopen(argv[2], "w");
    if (!out) return 2;
    std::vector<unsigned char> source, dest;
    fcpoly_cases::source_pixels(source);
    for (int single = 0; single < 2; ++single) {
        unsigned int control = 0;
        _controlfp_s(&control, single ? _PC_24 : _PC_53, _MCW_PC);
        for (int index = 0; index < fcpoly_cases::kCount; ++index) {
            fcpoly_cases::Case c;
            fcpoly_cases::make(index, c);
            fcpoly_cases::dest_pixels(dest);
            set_dest(dest.data(), fcpoly_cases::kDestWidth, fcpoly_cases::kDestHeight);
            set_source(source.data(), fcpoly_cases::kSourceWidth, fcpoly_cases::kSourceHeight);
            // hsp3gr_wingui.cpp gsquare and Bmscr::GetAlphaOperation.
            Poly4 poly = {};
            for (int i = 0; i < 4; ++i) {
                auto limit = [](int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; };
                poly.v[i][0] = static_cast<short>(c.x[i]);
                poly.v[i][1] = static_cast<short>(c.y[i]);
                poly.v[i][2] = static_cast<short>(c.flat ? 0 : limit(c.u[i], 0, fcpoly_cases::kSourceWidth));
                poly.v[i][3] = static_cast<short>(c.flat ? 0 : fcpoly_cases::kSourceHeight - 1 - limit(c.v[i], 0, fcpoly_cases::kSourceHeight));
            }
            int alpha = c.rate < 0 ? 0 : c.rate;
            if (alpha > 255) alpha = c.gmode >= 4 ? 255 : 256;
            if (c.gmode == 5) alpha |= 0x200;
            else if (c.gmode == 6) alpha |= 0x300;
            else if (c.gmode != 3 && c.gmode != 4) alpha = 0x100;
            poly.alpha = static_cast<short>(alpha);
            poly.npoly = 1;
            poly.attr = static_cast<short>((c.gmode == 2 || c.gmode == 4) ? 0x8000 : 0);
            // COLORREF 0x00bbggrr for the key; CnvRGB's 0x00rrggbb for fills.
            const unsigned int colorref = ((c.rgb & 0xff) << 16) | (c.rgb & 0xff00) | ((c.rgb >> 16) & 0xff);
            if (c.gmode == 4) set_key(static_cast<int>(colorref));
            if (c.flat) {
                poly.color = static_cast<int>(c.rgb);
                draw_flat(&poly);
            } else {
                poly.color = 0xffffff;
                draw_textured(&poly);
            }
            std::fprintf(out, "%s %d %016llx\n", single ? "single" : "double", index,
                         static_cast<unsigned long long>(fcpoly_cases::hash(dest)));
        }
    }
    std::fclose(out);
    return 0;
}
