// Records the Direct3D 8 calls the original x86 plugin/zgp1807.hpi makes for a
// scripted sequence of ZGP commands.
//
// Build as i686 on Windows:
//   clang++ -target i686-w64-mingw32 -std=c++14 -O1 -static
//     direct/tests/zgp_windows_reference.cpp -o build/zgp-reference.exe
// Run from the original installation directory:
//   build/zgp-reference.exe plugin/zgp1807.hpi <script> <output>
//
// Script lines: <export> [args...], where each argument is
//   i:<int>            integer
//   s:<text>           string pointer; %XX is the byte with hex value XX
//   v:<a,b,...>        int array (its contents are logged after the call)
//   bmscr              the harness window's Win32 BMSCR (HWND at byte 0x34)
// Lines starting with '#' are comments. Directives: '@pos x y' sets the BMSCR's
// current position; '@chr n', '@spr n', '@mesh n' and '@obj n' dump
// character, sprite, mesh and object entries (CHR, SPR, MESH, OBJ lines);
// '@meshsurf n' dumps a mesh's own surfaces (MESHSURF); '@mem va count' dumps DWORDs of the
// DLL's globals (MEM line). ZGP's Direct3DCreate8 import is
// redirected to a proxy that logs every Direct3D call (direct/tests/d3d8_trace.hpp).
// After each command, every texture created during it is dumped (TEXTURE and
// LEVEL lines: each level's D3DSURFACE_DESC and a hash of its texels), then
// every vertex and index buffer (BUFFER lines: description and contents).
#include <windows.h>
#include <d3d8.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include "d3d8_trace.hpp"

static_assert(sizeof(D3DCAPS8) == 53 * 4, "D3DCAPS8 is logged as 53 DWORDs");
static_assert(sizeof(D3DSURFACE_DESC) == 8 * 4, "D3DSURFACE_DESC is logged as 8 DWORDs");
static_assert(sizeof(D3DDISPLAYMODE) == 4 * 4, "D3DDISPLAYMODE is logged as 4 DWORDs");

// Character entry n of ZGP's first unit table (0x100605dc), with pointers as
// indices: surface slot (0x100605d0, 0x21c bytes each), the character at +0x10,
// and the triangle array at +0x2c (count at +0x28, three float points each).
static void dump_character(FILE* out, const unsigned char* base, int n) {
    auto global = [&](std::uint32_t va) { return *reinterpret_cast<const std::uint32_t*>(base + (va - 0x10000000u)); };
    const std::uint32_t units = global(0x100605dc), surfaces = global(0x100605d0);
    const std::uint32_t* e = reinterpret_cast<const std::uint32_t*>(units + static_cast<std::uint32_t>(n) * 0x30u);
    const std::uint32_t surface = e[0] ? (e[0] - surfaces) / 0x21c : 0;
    const std::uint32_t frame = e[4] ? (e[4] - units) / 0x30 : 0;
    std::fprintf(out, "CHR %d surface %u w f%08x h f%08x diagonal %d frame %u f14 %u uv {f%08x f%08x f%08x f%08x} triangles %u {",
                 n, surface, e[1], e[2], static_cast<int>(e[3]), frame, e[5], e[6], e[7], e[8], e[9], e[10]);
    const std::uint32_t* t = reinterpret_cast<const std::uint32_t*>(e[11]);
    for (std::uint32_t i = 0; t && i < e[10] * 6; ++i) std::fprintf(out, "%sf%08x", i ? " " : "", t[i]);
    std::fprintf(out, "}\n");
}

// Sprite entry n (0x100605e0, 0x98 bytes = 38 DWORDs, hex). Pointer fields
// become indices: +0x28/+0x2c characters, +0x30/+0x40 sprites; +0x94 (a
// triangle buffer) becomes 1 when allocated.
static void dump_sprite(FILE* out, const unsigned char* base, int n) {
    auto global = [&](std::uint32_t va) { return *reinterpret_cast<const std::uint32_t*>(base + (va - 0x10000000u)); };
    const std::uint32_t units = global(0x100605dc), sprites = global(0x100605e0);
    const std::uint32_t* e = reinterpret_cast<const std::uint32_t*>(sprites + static_cast<std::uint32_t>(n) * 0x98u);
    std::fprintf(out, "SPR %d", n);
    for (int i = 0; i < 38; ++i) {
        std::uint32_t v = e[i];
        if ((i == 0x28 / 4 || i == 0x2c / 4) && v) v = (v - units) / 0x30;
        if ((i == 0x30 / 4 || i == 0x40 / 4) && v) v = (v - sprites) / 0x98;
        if (i == 0x94 / 4) v = v ? 1 : 0;
        std::fprintf(out, " %08x", v);
    }
    std::fprintf(out, "\n");
}

// Mesh entry n (0x100605d8, 0x40 bytes). Buffers become trace ids; the
// per-attribute arrays (+0x28 ranges, +0x2c materials, +0x3c surface
// pointers) are printed by value, surfaces as global slots (sN) or entries of
// the mesh's own surface array at +0x38 (eN).
static void dump_mesh(FILE* out, const unsigned char* base, int n) {
    auto global = [&](std::uint32_t va) { return *reinterpret_cast<const std::uint32_t*>(base + (va - 0x10000000u)); };
    const std::uint32_t surfaces = global(0x100605d0), meshes = global(0x100605d8);
    const std::uint32_t* e = reinterpret_cast<const std::uint32_t*>(meshes + static_cast<std::uint32_t>(n) * 0x40u);
    const std::uint32_t count = e[7];
    std::fprintf(out, "MESH %d type %u f04 %08x hit {f%08x f%08x f%08x} fvf %u size %u attributes %u vertices %u triangles %u ib #%u vb #%u ranges {",
                 n, e[0], e[1], e[2], e[3], e[4], e[5], e[6], count, e[8], e[9],
                 d3d8trace::object_id(reinterpret_cast<const void*>(e[12])),
                 d3d8trace::object_id(reinterpret_cast<const void*>(e[13])));
    const std::uint32_t* ranges = reinterpret_cast<const std::uint32_t*>(e[10]);
    for (std::uint32_t i = 0; ranges && i < count * 5; ++i) std::fprintf(out, "%s%u", i ? " " : "", ranges[i]);
    std::fprintf(out, "} materials {");
    const std::uint32_t* materials = reinterpret_cast<const std::uint32_t*>(e[11]);
    for (std::uint32_t i = 0; materials && i < count * 17; ++i) std::fprintf(out, "%sf%08x", i ? " " : "", materials[i]);
    std::fprintf(out, "} textures {");
    const std::uint32_t* textures = reinterpret_cast<const std::uint32_t*>(e[15]);
    for (std::uint32_t i = 0; textures && i < count; ++i) {
        const std::uint32_t t = textures[i];
        std::fprintf(out, "%s", i ? " " : "");
        if (!t) std::fprintf(out, "null");
        else if (e[14] && t >= e[14] && t < e[14] + count * 0x21c) std::fprintf(out, "e%u", (t - e[14]) / 0x21c);
        else std::fprintf(out, "s%u", (t - surfaces) / 0x21c);
    }
    std::fprintf(out, "}\n");
}

// Bytes of a string as text; bytes outside printable ASCII (and '%') as %XX.
static std::string escaped(const char* s) {
    std::string out;
    for (; *s; ++s) {
        const unsigned char c = static_cast<unsigned char>(*s);
        char hex[4];
        if (c > 0x20 && c < 0x7f && c != '%') out += static_cast<char>(c);
        else { std::snprintf(hex, sizeof(hex), "%%%02X", c); out += hex; }
    }
    return out;
}

// The mesh's own surfaces (+0x38, one 0x21c-byte surface per attribute; the
// count is +0x1c): texture trace id, key (+0x210), format (+0x214), mip levels
// (+0x218), name length (+0x20c), path (+0x4) and stripped name (+0x108).
// The constructor (0x10013910) only initializes the texture, the name length
// and the format; the other fields are printed once a load has set the name
// length, which every load does.
static void dump_mesh_surfaces(FILE* out, const unsigned char* base, int n) {
    auto global = [&](std::uint32_t va) { return *reinterpret_cast<const std::uint32_t*>(base + (va - 0x10000000u)); };
    const std::uint32_t meshes = global(0x100605d8);
    const std::uint32_t* e = reinterpret_cast<const std::uint32_t*>(meshes + static_cast<std::uint32_t>(n) * 0x40u);
    std::fprintf(out, "MESHSURF %d", n);
    for (std::uint32_t i = 0; e[14] && i < e[7]; ++i) {
        const unsigned char* s = reinterpret_cast<const unsigned char*>(e[14] + i * 0x21c);
        auto word = [&](int offset) { return *reinterpret_cast<const std::uint32_t*>(s + offset); };
        const unsigned texture = d3d8trace::object_id(reinterpret_cast<const void*>(word(0)));
        if (!word(0x20c)) {
            std::fprintf(out, " {#%u format %u unloaded}", texture, word(0x214));
            continue;
        }
        std::fprintf(out, " {#%u %08x %u %u %u %s %s}", texture, word(0x210), word(0x214), word(0x218), word(0x20c),
                     escaped(reinterpret_cast<const char*>(s + 4)).c_str(),
                     escaped(reinterpret_cast<const char*>(s + 0x108)).c_str());
    }
    std::fprintf(out, "\n");
}

// Object entry n (0x100605d4, 0x12c bytes = 75 DWORDs, hex). The mesh
// pointer at +0x7c becomes the mesh index.
static void dump_object(FILE* out, const unsigned char* base, int n) {
    auto global = [&](std::uint32_t va) { return *reinterpret_cast<const std::uint32_t*>(base + (va - 0x10000000u)); };
    const std::uint32_t objects = global(0x100605d4), meshes = global(0x100605d8);
    const std::uint32_t* e = reinterpret_cast<const std::uint32_t*>(objects + static_cast<std::uint32_t>(n) * 0x12cu);
    std::fprintf(out, "OBJ %d", n);
    for (int i = 0; i < 75; ++i) {
        std::uint32_t v = e[i];
        if (i == 0x7c / 4 && v) v = (v - meshes) / 0x40;
        // objclno's parent pointer, as an object index (signed: objclno does not
        // check the parent).
        if (i == 0x8c / 4 && v) v = static_cast<std::uint32_t>(static_cast<std::int32_t>(v - objects) / 0x12c);
        std::fprintf(out, " %08x", v);
    }
    std::fprintf(out, "\n");
}

// CSMesh (loaded by anilibload) builds D3DX meshes in heap scratch memory it
// never fully initializes: unused weight slots and vertices no face references
// are copied out of it. Its malloc and operator new imports are redirected to
// msvcrt's calloc, so those bytes are deterministic zeroes (as in the port's
// zero-initialized scratch) instead of earlier heap contents. msvcrt's free
// releases both.
using Calloc = void* (__cdecl*)(size_t, size_t);
static Calloc msvcrt_calloc() {
    static Calloc c = reinterpret_cast<Calloc>(GetProcAddress(LoadLibraryA("msvcrt.dll"), "calloc"));
    return c;
}
static void* __cdecl zeroed_malloc(size_t size) { return msvcrt_calloc()(size ? size : 1, 1); }
static void zero_csmesh_heap() {
    static bool done = false;
    HMODULE csmesh = GetModuleHandleA("CSMesh.dll");
    if (done || !csmesh) return;
    done = d3d8trace::patch_import(csmesh, "MSVCRT.dll", "malloc", reinterpret_cast<void*>(&zeroed_malloc)) &&
           d3d8trace::patch_import(csmesh, "MSVCRT.dll", "??2@YAPAXI@Z", reinterpret_cast<void*>(&zeroed_malloc));
    if (!done) std::fprintf(stderr, "CSMesh allocator imports not found\n");
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s zgp1807.hpi script output\n", argv[0]);
        return 2;
    }
    FILE* out = std::fopen(argv[3], "wb");
    FILE* script = std::fopen(argv[2], "rb");
    if (!out || !script) return 2;
    HWND window = CreateWindowA("STATIC", "zgp-reference", WS_OVERLAPPEDWINDOW, 0, 0, 960, 540, nullptr, nullptr,
                                GetModuleHandleA(nullptr), nullptr);
    HMODULE zgp = LoadLibraryA(argv[1]);
    if (!window || !zgp) { std::fprintf(stderr, "setup failed %lu\n", GetLastError()); return 2; }
    if (!d3d8trace::patch_import(zgp, "d3d8.dll", "Direct3DCreate8", reinterpret_cast<void*>(&d3d8trace::create))) {
        std::fprintf(stderr, "Direct3DCreate8 import not found\n");
        return 2;
    }
    d3d8trace::sink() = out;
    std::vector<unsigned char> bmscr(0x200);
    *reinterpret_cast<HWND*>(bmscr.data() + 0x34) = window;
    std::fprintf(out, "ZGP-REFERENCE 1\n");

    char line[4096];
    while (std::fgets(line, sizeof(line), script)) {
        std::string text(line);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        if (text.empty() || text[0] == '#') continue;
        std::istringstream fields(text);
        std::string name, token;
        fields >> name;
        if (name == "@pos") {  // the BMSCR's current position (cx, cy at +0x6c, +0x70)
            int x = 0, y = 0;
            fields >> x >> y;
            std::memcpy(bmscr.data() + 0x6c, &x, 4);
            std::memcpy(bmscr.data() + 0x70, &y, 4);
            continue;
        }
        if (name == "@mem") {  // dump DWORDs of the DLL's globals: @mem <hex va> <count>
            std::string va;
            int n = 0;
            fields >> va >> n;
            const std::uint32_t address = static_cast<std::uint32_t>(std::strtoul(va.c_str(), nullptr, 16));
            const unsigned char* base = reinterpret_cast<const unsigned char*>(zgp);
            std::fprintf(out, "MEM %08x", address);
            for (int i = 0; i < n; ++i) {
                std::uint32_t w;
                std::memcpy(&w, base + (address - 0x10000000u) + 4 * i, 4);
                std::fprintf(out, " %08x", w);
            }
            std::fprintf(out, "\n");
            continue;
        }
        if (name == "@chdir") {  // change the process working directory (the game's chdir)
            std::string path;
            std::getline(fields >> std::ws, path);  // the rest of the line: names may contain spaces
            if (!SetCurrentDirectoryA(path.c_str())) std::fprintf(out, "CHDIR %s failed\n", path.c_str());
            continue;
        }
        if (name == "@ani") {  // CSMesh object n: time, rate, hit size (+0x190..+0x1a0)
            int n = 0;
            fields >> n;
            HMODULE csmesh = GetModuleHandleA("CSMesh.dll");
            const unsigned char* objects =
                csmesh ? *reinterpret_cast<unsigned char* const*>(reinterpret_cast<const unsigned char*>(csmesh) + 0x3eb68)
                       : nullptr;
            std::fprintf(out, "ANI %d", n);
            for (int k = 0; objects && k < 5; ++k) {
                std::uint32_t w;
                std::memcpy(&w, objects + static_cast<std::size_t>(n) * 0x1a4 + 0x190 + 4 * k, 4);
                std::fprintf(out, " %08x", w);
            }
            std::fprintf(out, "\n");
            continue;
        }
        if (name == "@surf") {  // a surface's texture levels (+0x00 of 0x21c-byte entries at 0x100605d0)
            int n = 0;
            fields >> n;
            const unsigned char* base = reinterpret_cast<const unsigned char*>(zgp);
            std::uint32_t table;
            std::memcpy(&table, base + (0x100605d0u - 0x10000000u), 4);
            IDirect3DTexture8* texture = *reinterpret_cast<IDirect3DTexture8**>(table + static_cast<std::uint32_t>(n) * 0x21cu);
            if (texture) d3d8trace::dump_texture(texture);
            else std::fprintf(out, "SURFACE %d no texture\n", n);
            continue;
        }
        if (name == "@obj") {  // dump an object entry
            int n = 0;
            fields >> n;
            dump_object(out, reinterpret_cast<const unsigned char*>(zgp), n);
            continue;
        }
        if (name == "@mesh") {  // dump a mesh entry
            int n = 0;
            fields >> n;
            dump_mesh(out, reinterpret_cast<const unsigned char*>(zgp), n);
            continue;
        }
        if (name == "@meshvb") {  // a mesh's vertex buffer contents (+0x34 of the entry)
            int n = 0;
            fields >> n;
            const unsigned char* base = reinterpret_cast<const unsigned char*>(zgp);
            std::uint32_t meshes;
            std::memcpy(&meshes, base + (0x100605d8u - 0x10000000u), 4);
            IDirect3DVertexBuffer8* vb =
                *reinterpret_cast<IDirect3DVertexBuffer8**>(meshes + static_cast<std::uint32_t>(n) * 0x40u + 0x34u);
            BYTE* data = nullptr;
            D3DVERTEXBUFFER_DESC desc;
            std::memset(&desc, 0, sizeof(desc));
            if (!vb || FAILED(vb->GetDesc(&desc)) || FAILED(vb->Lock(0, 0, &data, D3DLOCK_READONLY))) {
                std::fprintf(out, "MESHVB %d none\n", n);
                continue;
            }
            std::vector<BYTE> copy(data, data + desc.Size);
            vb->Unlock();
            std::fprintf(out, "MESHVB %d #%u", n, d3d8trace::object_id(vb));
            d3d8trace::dump_buffer_bytes(copy.data(), copy.size());
            std::fprintf(out, "\n");
            continue;
        }
        if (name == "@meshsurf") {  // dump a mesh entry's own surfaces
            int n = 0;
            fields >> n;
            dump_mesh_surfaces(out, reinterpret_cast<const unsigned char*>(zgp), n);
            continue;
        }
        if (name == "@spr") {  // dump a sprite entry
            int n = 0;
            fields >> n;
            dump_sprite(out, reinterpret_cast<const unsigned char*>(zgp), n);
            continue;
        }
        if (name == "@chr") {  // dump a character entry (0x30 bytes at 0x100605dc + n * 0x30)
            int n = 0;
            fields >> n;
            dump_character(out, reinterpret_cast<const unsigned char*>(zgp), n);
            continue;
        }
        using Command = int (WINAPI*)(int, int, int, int);
        Command command = reinterpret_cast<Command>(GetProcAddress(zgp, name.c_str()));
        if (!command) { std::fprintf(out, "CALL %s missing\n", name.c_str()); continue; }
        int args[4] = {};
        std::vector<std::vector<int>> arrays;
        std::vector<std::string> strings;
        strings.reserve(4);
        arrays.reserve(4);
        int count = 0;
        std::vector<int> array_slots;
        while (count < 4 && fields >> token) {
            if (token.compare(0, 2, "i:") == 0) {
                args[count] = std::atoi(token.c_str() + 2);
            } else if (token.compare(0, 2, "s:") == 0) {
                std::string text;
                for (std::size_t i = 2; i < token.size(); ++i) {
                    if (token[i] == '%' && i + 2 < token.size()) {
                        text += static_cast<char>(std::strtol(token.substr(i + 1, 2).c_str(), nullptr, 16));
                        i += 2;
                    } else {
                        text += token[i];
                    }
                }
                strings.push_back(text);
                args[count] = reinterpret_cast<int>(strings.back().c_str());
            } else if (token.compare(0, 2, "v:") == 0) {
                std::vector<int> values;
                std::stringstream list(token.substr(2));
                std::string item;
                while (std::getline(list, item, ',')) values.push_back(std::atoi(item.c_str()));
                values.resize(values.size() + 16, 0);
                arrays.push_back(values);
                args[count] = reinterpret_cast<int>(arrays.back().data());
                array_slots.push_back(static_cast<int>(arrays.size() - 1));
            } else if (token == "bmscr") {
                args[count] = reinterpret_cast<int>(bmscr.data());
            }
            ++count;
        }
        std::fprintf(out, "CALL %s\n", text.c_str());
        std::fflush(out);
        const int result = command(args[0], args[1], args[2], args[3]);
        zero_csmesh_heap();
        d3d8trace::dump_created_textures();
        d3d8trace::dump_created_buffers();
        std::fprintf(out, "RET %d", result);
        for (int slot : array_slots) {
            std::fprintf(out, " [");
            for (std::size_t i = 0; i + 16 < arrays[slot].size(); ++i) std::fprintf(out, "%s%d", i ? "," : "", arrays[slot][i]);
            std::fprintf(out, "]");
        }
        std::fprintf(out, "\n");
        std::fflush(out);
    }
    std::fprintf(out, "END\n");
    std::fclose(out);
    return 0;
}
