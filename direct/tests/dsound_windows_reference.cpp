// Records reference behavior from the original x86 plugin/dsoundex.hpi.
//
// Build as i686 on Windows:
//   clang++ -target i686-w64-mingw32 -std=c++14 -O1 -static
//     direct/tests/dsound_windows_reference.cpp -o build/dsound-reference.exe -ldsound -ldxguid
// Run from the original installation directory (the game's working directory):
//   build/dsound-reference.exe <dsoundex.hpi> <output> <relative audio path>...
//
// Each file is loaded exactly as the game loads it: .pak through the game's
// ds_pak_load (skip 5 bytes, copy into an int array of (size-5)/4 elements,
// ds_memload) and other files through ds_load. The resulting DirectSound
// buffer is read back from the plugin's own buffer table (image offset
// 0x51d8, 8 bytes per slot) and its format, size and PCM bytes are recorded.
// The plugin's volume/pan conversions and status values are also recorded.
#include <windows.h>
#include <dsound.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using Cmd = int (WINAPI*)(int, int, int, int);

struct Plugin {
    HMODULE module;
    Cmd init, load, memload, play, loop, stop, getp, stat, vol, pan, dup, bye, setp;
    IDirectSoundBuffer* buffer(int id) const {
        const unsigned char* table = reinterpret_cast<const unsigned char*>(module) + 0x51d8;
        if (!table[id * 8 + 4]) return nullptr;
        return *reinterpret_cast<IDirectSoundBuffer* const*>(table + id * 8);
    }
};

template <typename T>
bool resolve(HMODULE module, const char* name, T& out) {
    out = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!out) std::fprintf(stderr, "missing export %s\n", name);
    return out != nullptr;
}

std::uint64_t fnv(const void* data, std::size_t size, std::uint64_t hash = 1469598103934665603ull) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) hash = (hash ^ p[i]) * 1099511628211ull;
    return hash;
}

void describe(FILE* out, IDirectSoundBuffer* buffer) {
    if (!buffer) { std::fprintf(out, " none\n"); return; }
    WAVEFORMATEX format{};
    DWORD written = 0;
    buffer->GetFormat(&format, sizeof(format), &written);
    DSBCAPS caps{};
    caps.dwSize = sizeof(caps);
    buffer->GetCaps(&caps);
    void *first = nullptr, *second = nullptr;
    DWORD first_size = 0, second_size = 0;
    std::uint64_t hash = 0;
    if (SUCCEEDED(buffer->Lock(0, 0, &first, &first_size, &second, &second_size, DSBLOCK_ENTIREBUFFER))) {
        hash = fnv(first, first_size);
        if (second) hash = fnv(second, second_size, hash);
        buffer->Unlock(first, first_size, second, second_size);
    }
    std::fprintf(out, " fmt %u %u %lu %lu %u %u flags %08lx bytes %lu hash %016llx\n", format.wFormatTag,
                 format.nChannels, format.nSamplesPerSec, format.nAvgBytesPerSec, format.nBlockAlign,
                 format.wBitsPerSample, caps.dwFlags, caps.dwBufferBytes, static_cast<unsigned long long>(hash));
}

bool read_file(const char* path, std::vector<unsigned char>& data) {
    FILE* in = std::fopen(path, "rb");
    if (!in) return false;
    std::fseek(in, 0, SEEK_END);
    const long size = std::ftell(in);
    std::fseek(in, 0, SEEK_SET);
    data.resize(size > 0 ? static_cast<std::size_t>(size) : 0);
    const bool ok = std::fread(data.data(), 1, data.size(), in) == data.size();
    std::fclose(in);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s dsoundex.hpi output [audio files]\n", argv[0]);
        return 2;
    }
    HWND window = CreateWindowA("STATIC", "dsound-reference", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr,
                                GetModuleHandleA(nullptr), nullptr);
    Plugin p{};
    p.module = LoadLibraryA(argv[1]);
    if (!window || !p.module) { std::fprintf(stderr, "setup failed %lu\n", GetLastError()); return 2; }
    if (!resolve(p.module, "_ds_init@16", p.init) || !resolve(p.module, "_ds_load@16", p.load) ||
        !resolve(p.module, "_ds_memload@16", p.memload) || !resolve(p.module, "_ds_play@16", p.play) ||
        !resolve(p.module, "_ds_loop@16", p.loop) || !resolve(p.module, "_ds_stop@16", p.stop) ||
        !resolve(p.module, "_ds_getp@16", p.getp) || !resolve(p.module, "_ds_stat@16", p.stat) ||
        !resolve(p.module, "_ds_vol@16", p.vol) || !resolve(p.module, "_ds_pan@16", p.pan) ||
        !resolve(p.module, "_ds_dup@16", p.dup) || !resolve(p.module, "_ds_bye@16", p.bye) ||
        !resolve(p.module, "_ds_setp@16", p.setp))
        return 2;
    FILE* out = std::fopen(argv[2], "wb");
    if (!out) return 2;

    // ds_init receives the active screen's BMSCR (HWND at byte 0x34) and the
    // runtime's DPM information string; the game passes no explicit values.
    std::vector<unsigned char> bmscr(0x200);
    *reinterpret_cast<HWND*>(bmscr.data() + 0x34) = window;
    static char dpm_info[] = "";
    std::fprintf(out, "DSOUND-REFERENCE 1\nINIT %d\n",
                 p.init(reinterpret_cast<int>(bmscr.data()), 0, 0, reinterpret_cast<int>(dpm_info)));

    // Unused slot results.
    std::fprintf(out, "EMPTY play %d stop %d stat %d getp %d vol %d\n", p.play(7, 0, 0, 0), p.stop(7, 0, 0, 0),
                 p.stat(7, 0, 0, 0), p.getp(7, 0, 0, 0), p.vol(7, 100, 0, 0));

    std::vector<std::string> paths;
    for (int i = 3; i < argc; ++i) {
        if (argv[i][0] == '@') {  // @list: one relative path per line
            FILE* list = std::fopen(argv[i] + 1, "rb");
            char line[1024];
            while (list && std::fgets(line, sizeof(line), list)) {
                std::string entry(line);
                while (!entry.empty() && (entry.back() == '\n' || entry.back() == '\r')) entry.pop_back();
                if (!entry.empty()) paths.push_back(entry);
            }
            if (list) std::fclose(list);
        } else {
            paths.push_back(argv[i]);
        }
    }
    for (const std::string& entry : paths) {
        const char* path = entry.c_str();
        const std::size_t length = std::strlen(path);
        const bool pak = length > 4 && _stricmp(path + length - 4, ".pak") == 0;
        int result;
        if (pak) {
            std::vector<unsigned char> file;
            if (!read_file(path, file) || file.size() < 5) { std::fprintf(out, "FILE %s unreadable\n", path); continue; }
            // dim temp_data, (size-5)/4 ; bload file, temp_data, size, 5 (clamped to the variable).
            const std::size_t bytes = (file.size() - 5) / 4 * 4;
            std::vector<unsigned char> variable(bytes + 4096, 0);
            std::memcpy(variable.data(), file.data() + 5, bytes);
            result = p.memload(reinterpret_cast<int>(variable.data()), 0, 0, 0);
            std::fprintf(out, "FILE %s memload %d", path, result);
        } else {
            result = p.load(0, reinterpret_cast<int>(path), 0, 0);
            std::fprintf(out, "FILE %s load %d", path, result);
        }
        describe(out, p.buffer(0));
    }

    // Status, cursor, volume and pan semantics on the last loaded buffer.
    if (IDirectSoundBuffer* buffer = p.buffer(0)) {
        // Status and cursor checks play the buffer; keep it silent (DSBVOLUME_MIN).
        buffer->SetVolume(DSBVOLUME_MIN);
        std::fprintf(out, "STAT loaded %d\n", p.stat(0, 0, 0, 0));
        std::fprintf(out, "STAT setp %d getp %d\n", p.setp(0, 4000, 0, 0), p.getp(0, 0, 0, 0));
        std::fprintf(out, "STAT play %d stat %d\n", p.play(0, 0, 0, 0), p.stat(0, 0, 0, 0));
        std::fprintf(out, "STAT stop %d stat %d\n", p.stop(0, 0, 0, 0), p.stat(0, 0, 0, 0));
        std::fprintf(out, "STAT loop %d stat %d\n", p.loop(0, 0, 0, 0), p.stat(0, 0, 0, 0));
        std::fprintf(out, "STAT dup %d dupstat %d\n", p.dup(5, 0, 0, 0), p.stat(5, 0, 0, 0));
        p.stop(0, 0, 0, 0);
        std::fprintf(out, "STAT play-at %d getp %d\n", p.play(0, 8000, 0, 0), p.getp(0, 0, 0, 0));
        p.stop(0, 0, 0, 0);
        // A non-looping buffer that plays to its end.
        WAVEFORMATEX format{};
        DWORD written = 0;
        buffer->GetFormat(&format, sizeof(format), &written);
        DSBCAPS caps{};
        caps.dwSize = sizeof(caps);
        buffer->GetCaps(&caps);
        const DWORD milliseconds = static_cast<DWORD>(1000.0 * caps.dwBufferBytes / format.nAvgBytesPerSec);
        p.play(0, 0, 0, 0);
        const DWORD started = GetTickCount();
        Sleep(milliseconds / 2);
        const int middle_stat = p.stat(0, 0, 0, 0), middle_position = p.getp(0, 0, 0, 0);
        DWORD stopped_after = 0;
        while (GetTickCount() - started < milliseconds * 4 + 2000) {
            if (p.stat(0, 0, 0, 0) == 0) { stopped_after = GetTickCount() - started; break; }
            Sleep(10);
        }
        std::fprintf(out, "STAT ended %lums middle %d %s end stat %d getp %d (stopped after about %lums)\n",
                     milliseconds, middle_stat, middle_position < 0 ? "advanced" : "not-advanced", p.stat(0, 0, 0, 0),
                     p.getp(0, 0, 0, 0), stopped_after);
        // Stop keeps the cursor; a looping buffer wraps.
        p.play(0, 0, 0, 0);
        Sleep(milliseconds / 3);
        p.stop(0, 0, 0, 0);
        std::fprintf(out, "STAT stopped-mid %s\n", p.getp(0, 0, 0, 0) < 0 ? "kept" : "reset");
        p.loop(0, 0, 0, 0);
        Sleep(milliseconds + 300);
        std::fprintf(out, "STAT looped stat %d\n", p.stat(0, 0, 0, 0));
        p.stop(0, 0, 0, 0);
        // The game calls ds_vol after ZGP's createdevm, which leaves the x87 unit in
        // 24-bit precision (see docs/csmesh-abi.md); record the conversions in that state.
        unsigned short control = 0;
        __asm__ volatile("fnstcw %0" : "=m"(control));
        const unsigned short single = static_cast<unsigned short>(control & ~0x0300);
        __asm__ volatile("fldcw %0" : : "m"(single));
        std::fprintf(out, "FPU %04x\n", single);
        for (int v = -2; v <= 260; ++v) {
            LONG volume = 1;
            const int r = p.vol(0, v, 0, 0);
            buffer->GetVolume(&volume);
            std::fprintf(out, "VOL %d %d %ld\n", v, r, volume);
        }
        for (int v = -130; v <= 130; ++v) {
            LONG pan = 1;
            const int r = p.pan(0, v, 0, 0);
            buffer->GetPan(&pan);
            std::fprintf(out, "PAN %d %d %ld\n", v, r, pan);
        }
        __asm__ volatile("fldcw %0" : : "m"(control));
        std::fprintf(out, "BYE %d stat-after %d\n", p.bye(0, 0, 0, 0), p.stat(0, 0, 0, 0));
    }
    std::fprintf(out, "END\n");
    std::fclose(out);
    return 0;
}
