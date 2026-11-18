// Records reference behavior from the original x86 plugin/CSMesh.dll.
//
// Build as i686 on Windows:
//   clang++ -target i686-w64-mingw32 -std=c++14 -O1 -static
//     direct/tests/csmesh_windows_reference.cpp -o build/csmesh-reference.exe
// Run:
//   build/csmesh-reference.exe <CSMesh.dll> <installation/model> <output> [model-dir ...]
//
// The original plugin is driven exactly like ZGP drives it: Init receives the
// address of the Direct3D 8 device pointer, the device is created with ZGP's
// default behavior flags (software vertex processing, no FPU_PRESERVE, so the
// x87 unit runs in single precision), the working directory is the model
// directory and Load receives "model.x". Floats are recorded as raw bit
// patterns so the arm64 implementation can be compared exactly.
#include <windows.h>
#include <d3d8.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {

using InitFn = int (*)(void*, int);
using LoadFn = int (*)(int, const char*, int);
using IntOutFn = int (*)(int, int*);
using FloatOutFn = int (*)(int, float*);
using SetFloatFn = int (*)(int, float);
using SetIntFn = int (*)(int, int);
using RenderFn = int (*)(int, const float*, const void*);
using NamesFn = int (*)(int, char*);
using KeyTimeFn = int (*)(int, float*, int);
using MatrixFn = int (*)(int, float*, const char*, int);
using DeleteFn = int (*)();

struct Plugin {
    InitFn init;
    DeleteFn destroy;
    LoadFn load;
    RenderFn render;
    SetFloatFn set_time;
    SetFloatFn set_rate;
    SetIntFn set_list;
    NamesFn mesh_names;
    NamesFn list_names;
    IntOutFn num_mesh;
    IntOutFn num_list;
    IntOutFn num_key;
    FloatOutFn get_time;
    KeyTimeFn key_time;
    FloatOutFn max_time;
    FloatOutFn get_rate;
    MatrixFn mesh_matrix;
    MatrixFn mesh_minmax;
};

template <typename T>
bool resolve(HMODULE module, const char* name, T& out) {
    out = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!out) std::fprintf(stderr, "missing export %s\n", name);
    return out != nullptr;
}

std::uint32_t bits(float value) {
    std::uint32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

std::string hex_bytes(const char* text) {
    static const char digits[] = "0123456789abcdef";
    std::string result;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        result.push_back(digits[*p >> 4]);
        result.push_back(digits[*p & 15]);
    }
    return result.empty() ? "-" : result;
}

unsigned short control_word() {
    unsigned short value = 0;
    __asm__ volatile("fnstcw %0" : "=m"(value));
    return value;
}

void write_floats(FILE* out, const float* values, int count) {
    for (int i = 0; i < count; ++i) std::fprintf(out, " %08x", bits(values[i]));
}

// Original object layout (see docs/csmesh-abi.md): the slot array pointer is
// at image offset 0x3eb68, each slot is 0x1a4 bytes with the drawing element
// at +0x08, whose first field is the root SFrame. SFrame: +0x04 animated
// local matrix, +0x84 combined matrix written by Render's UpdateFrames,
// +0xec sibling, +0xf0 first child, +0xf4 animation flag.
struct FrameDigest {
    unsigned count = 0;
    std::uint64_t hash = 1469598103934665603ull;
    std::vector<float> values;
};

void add_frame(const unsigned char* frame, FrameDigest& digest, bool keep) {
    for (int offset : {0x04, 0x84}) {
        const unsigned char* matrix = frame + offset;
        for (int i = 0; i < 64; ++i) digest.hash = (digest.hash ^ matrix[i]) * 1099511628211ull;
        if (keep) {
            float values[16];
            std::memcpy(values, matrix, sizeof(values));
            digest.values.insert(digest.values.end(), values, values + 16);
        }
    }
    ++digest.count;
}

void digest_frames(const unsigned char* frame, FrameDigest& digest, bool keep) {
    for (; frame; frame = *reinterpret_cast<const unsigned char* const*>(frame + 0xec)) {
        if (!frame[0xf4]) {
            add_frame(frame, digest, keep);
        }
        digest_frames(*reinterpret_cast<const unsigned char* const*>(frame + 0xf0), digest, keep);
    }
}

FrameDigest frame_digest(HMODULE module, int slot, bool keep) {
    FrameDigest digest;
    const unsigned char* slots = *reinterpret_cast<const unsigned char* const*>(reinterpret_cast<const unsigned char*>(module) + 0x3eb68);
    const unsigned char* element = *reinterpret_cast<const unsigned char* const*>(slots + slot * 0x1a4 + 8);
    if (!element) return digest;
    const unsigned char* root = *reinterpret_cast<const unsigned char* const*>(element);
    // The root has no siblings; walk it alone.
    if (root && !root[0xf4]) add_frame(root, digest, keep);
    if (root) digest_frames(*reinterpret_cast<const unsigned char* const*>(root + 0xf0), digest, keep);
    return digest;
}

std::vector<float> sample_times(const std::vector<float>& keys, float maximum) {
    std::vector<float> result = {0.0f, 0.5f, 1.0f, 2.25f, -2.5f, 12345.625f};
    const std::size_t limit = std::min<std::size_t>(keys.size(), 16);
    for (std::size_t i = 0; i < limit; ++i)
        for (float delta : {0.0f, 0.3f, 0.5f, 0.7f}) result.push_back(keys[i] + delta);
    if (!keys.empty()) result.push_back(keys.back());
    for (float delta : {-0.5f, 0.0f, 0.5f, 1.0f, 1.5f}) result.push_back(maximum + delta);
    result.push_back(maximum * 2.0f + 3.3f);
    std::vector<float> unique;
    std::set<std::uint32_t> seen;
    for (float value : result)
        if (seen.insert(bits(value)).second) unique.push_back(value);
    return unique;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s CSMesh.dll model-root output [model-dir ...]\n", argv[0]);
        return 2;
    }
    char dll_path[MAX_PATH], model_root[MAX_PATH];
    if (!GetFullPathNameA(argv[1], MAX_PATH, dll_path, nullptr) ||
        !GetFullPathNameA(argv[2], MAX_PATH, model_root, nullptr)) return 2;

    HWND window = CreateWindowA("STATIC", "csmesh-reference", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
                                nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
    HMODULE d3d8 = LoadLibraryA("d3d8.dll");
    using CreateFn = IDirect3D8* (WINAPI*)(UINT);
    CreateFn create = d3d8 ? reinterpret_cast<CreateFn>(GetProcAddress(d3d8, "Direct3DCreate8")) : nullptr;
    IDirect3D8* direct3d = create ? create(D3D_SDK_VERSION) : nullptr;
    if (!window || !direct3d) { std::fprintf(stderr, "Direct3D 8 is unavailable\n"); return 2; }
    D3DDISPLAYMODE mode{};
    direct3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode);
    D3DPRESENT_PARAMETERS parameters{};
    parameters.Windowed = TRUE;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.BackBufferFormat = mode.Format;
    parameters.BackBufferWidth = 64;
    parameters.BackBufferHeight = 64;
    parameters.EnableAutoDepthStencil = TRUE;
    parameters.AutoDepthStencilFormat = D3DFMT_D16;
    IDirect3DDevice8* device = nullptr;
    const unsigned short before = control_word();
    // ZGP's default: D3DCREATE_SOFTWARE_VERTEXPROCESSING without FPU_PRESERVE.
    HRESULT created = direct3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, 0x20, &parameters, &device);
    if (FAILED(created) || !device) { std::fprintf(stderr, "CreateDevice failed %08lx\n", created); return 2; }
    const unsigned short after = control_word();

    HMODULE module = LoadLibraryA(dll_path);
    if (!module) { std::fprintf(stderr, "LoadLibrary failed %lu\n", GetLastError()); return 2; }
    Plugin p{};
    if (!resolve(module, "Init", p.init) || !resolve(module, "Delete", p.destroy) ||
        !resolve(module, "Load", p.load) || !resolve(module, "Render", p.render) ||
        !resolve(module, "SetTime", p.set_time) || !resolve(module, "SetRate", p.set_rate) ||
        !resolve(module, "SetList", p.set_list) || !resolve(module, "GetMeshName", p.mesh_names) ||
        !resolve(module, "GetListName", p.list_names) || !resolve(module, "GetNumMesh", p.num_mesh) ||
        !resolve(module, "GetNumList", p.num_list) || !resolve(module, "GetNumKey", p.num_key) ||
        !resolve(module, "GetTime", p.get_time) || !resolve(module, "GetKeyTime", p.key_time) ||
        !resolve(module, "GetMaxTime", p.max_time) || !resolve(module, "GetRate", p.get_rate) ||
        !resolve(module, "GetMeshMatrix", p.mesh_matrix) || !resolve(module, "GetMeshMinMax", p.mesh_minmax))
        return 2;

    FILE* out = std::fopen(argv[3], "wb");
    if (!out) return 2;
    std::fprintf(out, "CSMESH-REFERENCE 1\nFPU %04x %04x\n", before, after);
    IDirect3DDevice8* device_slot = device;
    std::fprintf(out, "INIT %d\n", p.init(&device_slot, 0));

    std::vector<std::string> models;
    for (int i = 4; i < argc; ++i) models.push_back(argv[i]);
    if (models.empty()) {
        WIN32_FIND_DATAA entry;
        HANDLE find = FindFirstFileA((std::string(model_root) + "\\*").c_str(), &entry);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || entry.cFileName[0] == '.') continue;
                const std::string dir = std::string(model_root) + "\\" + entry.cFileName;
                if (GetFileAttributesA((dir + "\\model.x").c_str()) != INVALID_FILE_ATTRIBUTES &&
                    GetFileAttributesA((dir + "\\model.mqx").c_str()) != INVALID_FILE_ATTRIBUTES)
                    models.push_back(entry.cFileName);
            } while (FindNextFileA(find, &entry));
            FindClose(find);
        }
        std::sort(models.begin(), models.end());
    }

    const int slot = 1;
    for (const std::string& model : models) {
        const std::string dir = std::string(model_root) + "\\" + model;
        if (!SetCurrentDirectoryA(dir.c_str())) { std::fprintf(out, "MODEL %s missing\n", model.c_str()); continue; }
        const int loaded = p.load(slot, "model.x", 0);
        std::fprintf(out, "MODEL %s %d\n", model.c_str(), loaded);
        if (!loaded) continue;
        p.set_rate(slot, 1.0f);
        int lists = 0, meshes = 0;
        p.num_list(slot, &lists);
        p.num_mesh(slot, &meshes);
        std::fprintf(out, "COUNTS %d %d\n", lists, meshes);
        std::vector<char> names(static_cast<std::size_t>(std::max(meshes, 1)) * 256);
        p.mesh_names(slot, names.data());
        std::vector<std::string> mesh_names;
        for (int i = 0; i < meshes; ++i) {
            const char* name = names.data() + i * 256;
            mesh_names.push_back(name);
            std::fprintf(out, "MESH %d %s\n", i, hex_bytes(name).c_str());
        }
        // A null name matches every mesh container, including unnamed ones.
        {
            float matrix[16] = {}, box[6] = {};
            p.mesh_matrix(slot, matrix, nullptr, 0);
            p.mesh_minmax(slot, box, nullptr, 0);
            std::fprintf(out, "ALL");
            write_floats(out, matrix, 16);
            write_floats(out, box, 6);
            std::fprintf(out, "\n");
        }
        // Static pose: independent of animation state and of the last world matrix.
        for (int i = 0; i < meshes; ++i) {
            if (mesh_names[i].empty()) continue;
            float matrix[16] = {}, box[6] = {};
            p.mesh_matrix(slot, matrix, mesh_names[i].c_str(), 0);
            std::fprintf(out, "STATIC %d", i);
            write_floats(out, matrix, 16);
            p.mesh_minmax(slot, box, mesh_names[i].c_str(), 0);
            write_floats(out, box, 6);
            std::fprintf(out, "\n");
        }
        for (int list = 0; list < lists; ++list) {
            // CSMesh clears only the first 16 of its 256 list heads before loading.
            // Later heads start from uninitialized heap memory, so the original
            // behavior there is undefined and cannot be recorded reliably.
            if (list >= 16) { std::fprintf(out, "UNINITIALIZED-LIST %d\n", list); continue; }
            p.set_list(slot, list);
            int keys = 0;
            float maximum = 0.0f;
            p.num_key(slot, &keys);
            p.max_time(slot, &maximum);
            std::vector<float> key_times;
            for (int k = 0; k < keys; ++k) {
                float time = 0.0f;
                p.key_time(slot, &time, k);
                key_times.push_back(time);
            }
            float clamped_low = 0.0f, clamped_high = 0.0f;
            p.key_time(slot, &clamped_low, -5);
            p.key_time(slot, &clamped_high, keys + 7);
            std::fprintf(out, "LIST %d %d %08x %08x %08x", list, keys, bits(maximum), bits(clamped_low), bits(clamped_high));
            write_floats(out, key_times.data(), static_cast<int>(key_times.size()));
            std::fprintf(out, "\n");
            int sample_index = 0;
            for (float time : sample_times(key_times, maximum)) {
                p.set_time(slot, time);
                float stored = 0.0f;
                p.get_time(slot, &stored);
                device->BeginScene();
                p.render(slot, nullptr, nullptr);
                device->EndScene();
                std::fprintf(out, "TIME %08x %08x\n", bits(time), bits(stored));
                const FrameDigest digest = frame_digest(module, slot, sample_index < 2);
                std::fprintf(out, "FRAMES %u %016llx", digest.count, static_cast<unsigned long long>(digest.hash));
                write_floats(out, digest.values.data(), static_cast<int>(digest.values.size()));
                std::fprintf(out, "\n");
                for (int i = 0; i < meshes; ++i) {
                    if (mesh_names[i].empty()) continue;
                    float matrix[16] = {};
                    p.mesh_matrix(slot, matrix, mesh_names[i].c_str(), 1);
                    std::fprintf(out, "POSE %d", i);
                    write_floats(out, matrix, 16);
                    if (sample_index < 3) {
                        float box[6] = {};
                        p.mesh_minmax(slot, box, mesh_names[i].c_str(), 1);
                        write_floats(out, box, 6);
                    }
                    std::fprintf(out, "\n");
                }
                ++sample_index;
            }
        }
        std::fflush(out);
    }
    std::fprintf(out, "END\n");
    std::fclose(out);
    return 0;
}
