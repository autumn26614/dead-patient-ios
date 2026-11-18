// Records the original hspda.dll's sortnote/sortget (i686 Windows only).
//
//   clang++ -target i686-w64-mingw32 -std=c++14 -O1 -static direct/tests/hspda_windows_reference.cpp -o build/hspda-reference.exe
//   build/hspda-reference.exe "Corpse Party 2 Dead Patient/hspda.dll" "Corpse Party 2 Dead Patient/model/chara_model.lst" > reference.txt
//
// The plugin reads its arguments through HSPEXINFO callbacks (offsets in
// HSP 3's 32-bit layout): +0x84 prm_getva, +0x30 prm_getdi, +0x88
// prm_setva and +0x64 getproc, whose HspVarProc supplies GetPtr (+0x1c) and
// GetBlockSize (+0x40). The harness provides exactly those, with a string
// or int variable, and prints what the plugin stores (direct/tests/hspda_test.cpp
// prints the same lines natively).
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "hspda_cases.hpp"

namespace {

struct FakePVal {
    std::int16_t flag;       // 2 = str, 4 = int
    unsigned char pad[0x26];
    std::int32_t offset;     // +0x28 (APTR set by the plugin)
    unsigned char rest[0x20];
};

FakePVal pval;
std::vector<char> buffer;          // the string variable's storage (mutable: the plugin splits in place)
std::int32_t int_value = 0;
std::vector<int> int_args;         // getdi results, in order
std::string stored;                // last string set by the plugin
bool stored_string = false, stored_int = false;
unsigned char proc[0x80];
unsigned char exinfo[0x100];

int __cdecl getva(void** out) {
    *out = &pval;
    return 0;
}
int __cdecl getdi(int def) {
    if (int_args.empty()) return def;
    const int v = int_args.front();
    int_args.erase(int_args.begin());
    return v;
}
void __cdecl setva(void*, int, int type, const void* ptr) {
    if (type == 2) { stored = static_cast<const char*>(ptr); stored_string = true; }
    else if (type == 4) { std::memcpy(&int_value, ptr, 4); stored_int = true; }
    else std::printf("SETVA unexpected type %d\n", type);
}
void* __cdecl getproc(int) { return proc; }
void* __cdecl get_ptr(void* p) {
    FakePVal* v = static_cast<FakePVal*>(p);
    return v->flag == 2 ? static_cast<void*>(buffer.data()) : static_cast<void*>(&int_value);
}
void* __cdecl block_size(void*, void* data, int* size) {
    *size = static_cast<int>(buffer.size());
    return data;
}

std::string hex(const std::string& s) {
    std::string out;
    char b[4];
    for (unsigned char c : s) { std::snprintf(b, sizeof(b), "%02x", c); out += b; }
    return out.empty() ? "-" : out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s hspda.dll chara_model.lst\n", argv[0]); return 2; }
    HMODULE dll = LoadLibraryA(argv[1]);
    if (!dll) { std::fprintf(stderr, "cannot load %s\n", argv[1]); return 2; }
    using Export = int (WINAPI*)(void*, int, int, int);
    Export sortnote = reinterpret_cast<Export>(GetProcAddress(dll, "_sortnote@16"));
    Export sortget = reinterpret_cast<Export>(GetProcAddress(dll, "_sortget@16"));
    if (!sortnote || !sortget) { std::fprintf(stderr, "exports missing\n"); return 2; }
    auto put = [](int offset, void* f) { std::memcpy(exinfo + offset, &f, 4); };
    put(0x84, reinterpret_cast<void*>(&getva));
    put(0x30, reinterpret_cast<void*>(&getdi));
    put(0x88, reinterpret_cast<void*>(&setva));
    put(0x64, reinterpret_cast<void*>(&getproc));
    void* f = reinterpret_cast<void*>(&get_ptr);
    std::memcpy(proc + 0x1c, &f, 4);
    f = reinterpret_cast<void*>(&block_size);
    std::memcpy(proc + 0x40, &f, 4);

    auto get = [&](int n) {
        pval.flag = 4;
        int_value = 12345;
        stored_int = false;
        int_args = {n};
        const int r = sortget(exinfo, 0, 0, 0);
        std::printf("GET %d RET %d VALUE %d%s\n", n, r, int_value, stored_int ? "" : " (unset)");
    };
    get(0);  // before any sort

    std::vector<std::string> inputs = hspda_cases::cases();
    std::ifstream list(argv[2], std::ios::binary);
    inputs.push_back(std::string((std::istreambuf_iterator<char>(list)), std::istreambuf_iterator<char>()));
    for (std::size_t k = 0; k < inputs.size(); ++k) {
        for (int order = 0; order < 2; ++order) {
            pval.flag = 2;
            buffer.assign(inputs[k].begin(), inputs[k].end());
            buffer.resize(buffer.size() + 64, 0);
            stored_string = false;
            int_args = {order};
            const int r = sortnote(exinfo, 0, 0, 0);
            std::printf("CASE %u ORDER %d RET %d TEXT %s\n", static_cast<unsigned>(k), order, r,
                        stored_string ? hex(stored).c_str() : "(unset)");
            int lines = 0;
            for (std::size_t i = 0; i + 1 < stored.size(); ++i)
                if (stored[i] == '\r' && stored[i + 1] == '\n') ++lines;
            for (int n = -1; n <= lines; ++n) get(n);
        }
    }
    return 0;
}
