#pragma once
// Deterministic Direct3D 8 call trace used by the Windows ZGP reference
// harness. Include after <d3d8.h>. The generated proxies forward every call to
// the real Direct3D object and log it with:
//   - created objects as sequence ids (#n), never addresses,
//   - structures and matrices by value (floats as IEEE bit patterns),
//   - user vertex/index data by byte size and FNV-1a hash.
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

namespace d3d8trace {

inline FILE*& sink() {
    static FILE* out = nullptr;
    return out;
}

inline std::map<const void*, unsigned>& ids() {
    static std::map<const void*, unsigned> map;
    return map;
}

inline unsigned& next_id() {
    static unsigned id = 1;
    return id;
}

inline void begin(const char* name) {
    if (sink()) std::fprintf(sink(), "%s", name);
}
inline void end() {
    if (sink()) std::fprintf(sink(), "\n");
}
inline void log_uint(unsigned long value) {
    if (sink()) std::fprintf(sink(), " %lu", value);
}
inline void log_float(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    if (sink()) std::fprintf(sink(), " f%08x", bits);
}
inline void log_text(const char* text) {
    if (sink()) std::fprintf(sink(), " %s", text);
}
inline void log_result(unsigned long value) {
    if (sink()) std::fprintf(sink(), " -> %08lx", value);
}

// Layout codes: "<count><kind>" with kind f (float bits), u (unsigned), i (signed), x (hex dword).
inline void log_struct(const void* data, const char* layout) {
    if (!sink()) return;
    if (!data) { std::fprintf(sink(), " null"); return; }
    const int count = std::atoi(layout);
    const char kind = layout[std::strlen(layout) - 1];
    const std::uint32_t* words = static_cast<const std::uint32_t*>(data);
    std::fprintf(sink(), " {");
    for (int i = 0; i < count; ++i) {
        if (kind == 'f') std::fprintf(sink(), "%sf%08x", i ? " " : "", words[i]);
        else if (kind == 'i') std::fprintf(sink(), "%s%d", i ? " " : "", static_cast<std::int32_t>(words[i]));
        else if (kind == 'x') std::fprintf(sink(), "%s%08x", i ? " " : "", words[i]);
        else std::fprintf(sink(), "%s%u", i ? " " : "", words[i]);
    }
    std::fprintf(sink(), "}");
}
inline void log_out_struct(const void* data, const char* layout) {
    if (sink()) std::fprintf(sink(), " out");
    log_struct(data, layout);
}
inline void log_out_uint(const void* data) {
    if (!sink()) return;
    if (!data) { std::fprintf(sink(), " out null"); return; }
    std::fprintf(sink(), " out %u", *static_cast<const std::uint32_t*>(data));
}
inline void log_rects(const void* rects, unsigned long count) {
    if (!sink()) return;
    if (!rects) { std::fprintf(sink(), " null"); return; }
    log_struct(rects, (std::to_string(count * 4) + "i").c_str());
}

inline unsigned object_id(const void* object) {
    if (!object) return 0;
    auto it = ids().find(object);
    if (it != ids().end()) return it->second;
    const unsigned id = next_id()++;
    ids()[object] = id;
    return id;
}
inline void log_object(const void* object) {
    if (sink()) std::fprintf(sink(), " #%u", object_id(object));
}
template <typename T>
inline void log_created(T** out) {
    if (sink()) std::fprintf(sink(), " out #%u", out && *out ? object_id(*out) : 0u);
}

// Textures created since the last dump. Each holds a reference (invisible to
// the trace) so the harness can read the final contents after a command, even
// if D3DX released a temporary texture during it.
inline std::vector<IDirect3DTexture8*>& created_textures() {
    static std::vector<IDirect3DTexture8*> list;
    return list;
}
inline void log_created(IDirect3DTexture8** out) {
    if (out && *out) {
        // A new texture may reuse a freed one's address: give it a fresh id,
        // so ids do not depend on the allocator.
        ids().erase(*out);
        (*out)->AddRef();
        created_textures().push_back(*out);
    }
    if (sink()) std::fprintf(sink(), " out #%u", out && *out ? object_id(*out) : 0u);
}

// Bytes in one row of texels (or one row of 4x4 blocks for DXTn).
inline std::size_t row_bytes(D3DFORMAT format, unsigned width, unsigned& rows_per_block) {
    rows_per_block = 1;
    switch (static_cast<unsigned>(format)) {
    case D3DFMT_R8G8B8: return width * 3u;
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: return width * 4u;
    case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4:
    case D3DFMT_A8R3G3B2: case D3DFMT_X4R4G4B4: case D3DFMT_A8L8: return width * 2u;
    case D3DFMT_A8: case D3DFMT_R3G3B2: case D3DFMT_L8: case D3DFMT_P8: return width;
    case D3DFMT_DXT1: rows_per_block = 4; return ((width + 3u) / 4u) * 8u;
    case D3DFMT_DXT2: case D3DFMT_DXT3: case D3DFMT_DXT4: case D3DFMT_DXT5:
        rows_per_block = 4; return ((width + 3u) / 4u) * 16u;
    default: return 0;
    }
}

// Logs each level's D3DSURFACE_DESC and a hash of its texel rows (pitch
// padding excluded), read back with a read-only lock on the real texture.
inline void dump_texture(IDirect3DTexture8* texture) {
    if (!sink()) return;
    const DWORD levels = texture->GetLevelCount();
    std::fprintf(sink(), "TEXTURE #%u levels %lu\n", object_id(texture), static_cast<unsigned long>(levels));
    for (DWORD level = 0; level < levels; ++level) {
        D3DSURFACE_DESC desc;
        std::memset(&desc, 0, sizeof(desc));
        texture->GetLevelDesc(level, &desc);
        std::fprintf(sink(), "LEVEL %lu", static_cast<unsigned long>(level));
        log_struct(&desc, "8u");
        unsigned block_rows = 1;
        const std::size_t row = row_bytes(desc.Format, desc.Width, block_rows);
        D3DLOCKED_RECT locked;
        const HRESULT r = texture->LockRect(level, &locked, nullptr, D3DLOCK_READONLY);
        if (FAILED(r) || !row) {
            std::fprintf(sink(), " unreadable %08lx\n", static_cast<unsigned long>(r));
            if (SUCCEEDED(r)) texture->UnlockRect(level);
            continue;
        }
        std::uint64_t hash = 1469598103934665603ull;
        const unsigned rows = (desc.Height + block_rows - 1) / block_rows;
        for (unsigned y = 0; y < rows; ++y) {
            const unsigned char* p = static_cast<const unsigned char*>(locked.pBits) + y * locked.Pitch;
            for (std::size_t i = 0; i < row; ++i) hash = (hash ^ p[i]) * 1099511628211ull;
        }
        texture->UnlockRect(level);
        std::fprintf(sink(), " data %zu:%016llx\n", row * rows, static_cast<unsigned long long>(hash));
    }
}
inline void dump_created_textures() {
    for (IDirect3DTexture8* texture : created_textures()) {
        dump_texture(texture);
        texture->Release();
    }
    created_textures().clear();
}

inline std::uint64_t fnv(const void* data, std::size_t size);

// Vertex and index buffers created since the last dump, held like textures.
// After each command their description and final contents are logged
// (BUFFER lines; contents as DWORDs when small, always as a hash).
struct CreatedBuffer {
    IDirect3DVertexBuffer8* vertices = nullptr;
    IDirect3DIndexBuffer8* indices = nullptr;
};
inline std::vector<CreatedBuffer>& created_buffers() {
    static std::vector<CreatedBuffer> list;
    return list;
}
// New buffers are zero-filled before the caller sees them, as the port's
// buffers start. Bytes the original never writes (D3DX leaves unused weight
// slots and unreferenced vertices of a converted mesh untouched) are then
// deterministic instead of whatever the allocator left behind.
template <typename B>
inline void zero_fill(B* buffer) {
    BYTE* data = nullptr;
    if (SUCCEEDED(buffer->Lock(0, 0, &data, 0))) {
        UINT size = 0;
        if (std::is_same<B, IDirect3DVertexBuffer8>::value) {
            D3DVERTEXBUFFER_DESC d;
            reinterpret_cast<IDirect3DVertexBuffer8*>(buffer)->GetDesc(&d);
            size = d.Size;
        } else {
            D3DINDEXBUFFER_DESC d;
            reinterpret_cast<IDirect3DIndexBuffer8*>(buffer)->GetDesc(&d);
            size = d.Size;
        }
        std::memset(data, 0, size);
        buffer->Unlock();
    }
}

inline void log_created(IDirect3DVertexBuffer8** out) {
    if (out && *out) {
        zero_fill(*out);
        ids().erase(*out);
        (*out)->AddRef();
        created_buffers().push_back({*out, nullptr});
    }
    if (sink()) std::fprintf(sink(), " out #%u", out && *out ? object_id(*out) : 0u);
}
inline void log_created(IDirect3DIndexBuffer8** out) {
    if (out && *out) {
        zero_fill(*out);
        ids().erase(*out);
        (*out)->AddRef();
        created_buffers().push_back({nullptr, *out});
    }
    if (sink()) std::fprintf(sink(), " out #%u", out && *out ? object_id(*out) : 0u);
}
inline void dump_buffer_bytes(const BYTE* data, std::size_t size) {
    std::fprintf(sink(), " data %zu:%016llx", size, static_cast<unsigned long long>(fnv(data, size)));
    // D3D8TRACE_FULL_BUFFERS=1 prints every buffer's contents (local
    // investigation of large meshes); the checked-in references use the limit.
    static const bool full = std::getenv("D3D8TRACE_FULL_BUFFERS") != nullptr;
    if (size > 4096 && !full) return;
    std::fprintf(sink(), " {");
    for (std::size_t i = 0; i + 4 <= size; i += 4) {
        std::uint32_t w;
        std::memcpy(&w, data + i, 4);
        std::fprintf(sink(), "%s%08x", i ? " " : "", w);
    }
    std::fprintf(sink(), "}");
}
// D3DXGetFVFVertexSize.
inline UINT fvf_stride(DWORD fvf) {
    static const UINT position[8] = {0, 12, 16, 16, 20, 24, 28, 32};
    UINT size = position[(fvf & 0xe) >> 1];
    if (fvf & 0x10) size += 12;
    if (fvf & 0x20) size += 4;
    if (fvf & 0x40) size += 4;
    if (fvf & 0x80) size += 4;
    static const UINT coordinates[4] = {8, 12, 16, 4};
    for (DWORD i = 0; i < ((fvf >> 8) & 0xf); ++i) size += coordinates[(fvf >> (16 + 2 * i)) & 3];
    return size;
}

// A D3DX mesh creates its index buffer, then its vertex buffer. In a vertex
// buffer created directly after a 16-bit index buffer, the vertices that index
// buffer does not reference are dumped as zeroes: D3DX's converted meshes copy
// them from out of range (undefined memory), and no face draws them. The line
// reports how many vertices were masked. The native recorder applies the same
// rule (direct/runtime/d3d8.hpp).
inline UINT mask_unreferenced(std::vector<BYTE>& vertices, UINT stride, const std::vector<BYTE>& indices) {
    if (!stride) return 0;
    const UINT count = static_cast<UINT>(vertices.size() / stride);
    std::vector<bool> used(count, false);
    for (std::size_t i = 0; i + 2 <= indices.size(); i += 2) {
        const UINT v = static_cast<UINT>(indices[i] | indices[i + 1] << 8);
        if (v < count) used[v] = true;
    }
    UINT masked = 0;
    for (UINT v = 0; v < count; ++v) {
        if (used[v]) continue;
        std::memset(vertices.data() + static_cast<std::size_t>(v) * stride, 0, stride);
        ++masked;
    }
    return masked;
}

inline void dump_created_buffers() {
    std::vector<BYTE> previous_indices;
    bool previous_was_index16 = false;
    for (const CreatedBuffer& b : created_buffers()) {
        if (sink()) {
            BYTE* data = nullptr;
            if (b.vertices) {
                D3DVERTEXBUFFER_DESC desc;
                std::memset(&desc, 0, sizeof(desc));
                b.vertices->GetDesc(&desc);
                std::fprintf(sink(), "BUFFER #%u vertex", object_id(b.vertices));
                log_struct(&desc, "6u");
                if (SUCCEEDED(b.vertices->Lock(0, 0, &data, D3DLOCK_READONLY))) {
                    std::vector<BYTE> copy(data, data + desc.Size);
                    b.vertices->Unlock();
                    if (previous_was_index16) {
                        const UINT masked = mask_unreferenced(copy, fvf_stride(desc.FVF), previous_indices);
                        std::fprintf(sink(), " masked %u", masked);
                    }
                    dump_buffer_bytes(copy.data(), copy.size());
                }
                previous_was_index16 = false;
            } else {
                D3DINDEXBUFFER_DESC desc;
                std::memset(&desc, 0, sizeof(desc));
                b.indices->GetDesc(&desc);
                std::fprintf(sink(), "BUFFER #%u index", object_id(b.indices));
                log_struct(&desc, "5u");
                if (SUCCEEDED(b.indices->Lock(0, 0, &data, D3DLOCK_READONLY))) {
                    dump_buffer_bytes(data, desc.Size);
                    previous_indices.assign(data, data + desc.Size);
                    previous_was_index16 = desc.Format == D3DFMT_INDEX16;
                    b.indices->Unlock();
                }
            }
            std::fprintf(sink(), "\n");
        }
        if (b.vertices) b.vertices->Release();
        if (b.indices) b.indices->Release();
    }
    created_buffers().clear();
}

inline std::uint64_t fnv(const void* data, std::size_t size) {
    std::uint64_t hash = 1469598103934665603ull;
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) hash = (hash ^ p[i]) * 1099511628211ull;
    return hash;
}
inline void log_data(const void* data, std::size_t size) {
    if (!sink()) return;
    if (!data) { std::fprintf(sink(), " data null"); return; }
    std::fprintf(sink(), " data %zu:%016llx", size, static_cast<unsigned long long>(fnv(data, size)));
}

inline std::size_t vertex_count_up(D3DPRIMITIVETYPE type, unsigned count) {
    switch (type) {
    case D3DPT_POINTLIST: return count;
    case D3DPT_LINELIST: return count * 2;
    case D3DPT_LINESTRIP: return count + 1;
    case D3DPT_TRIANGLELIST: return count * 3;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN: return count + 2;
    default: return 0;
    }
}

IDirect3DDevice8* wrap_device(IDirect3DDevice8* device);

// Real object -> its tracing proxy (IDirect3D8 and the device).
inline std::map<const void*, void*>& proxies() {
    static std::map<const void*, void*> map;
    return map;
}
template <typename T>
inline T* proxy_for(T* real) {
    auto it = proxies().find(real);
    return it == proxies().end() ? real : static_cast<T*>(it->second);
}

#include "d3d8_trace_generated.hpp"

inline IDirect3DDevice8* wrap_device(IDirect3DDevice8* device) {
    TraceDevice* proxy = new TraceDevice(device);
    ids()[proxy] = object_id(device);
    proxies()[device] = static_cast<IDirect3DDevice8*>(proxy);
    return proxy;
}

// Replacement for Direct3DCreate8 installed in the traced module's import table.
inline IDirect3D8* WINAPI create(UINT version) {
    static HMODULE module = LoadLibraryA("d3d8.dll");
    using Create = IDirect3D8* (WINAPI*)(UINT);
    Create real = reinterpret_cast<Create>(GetProcAddress(module, "Direct3DCreate8"));
    begin("Direct3DCreate8");
    log_uint(version);
    IDirect3D8* d3d = real ? real(version) : nullptr;
    IDirect3D8* proxy = d3d ? new TraceDirect3D(d3d) : nullptr;
    if (proxy) {
        ids()[proxy] = object_id(d3d);
        proxies()[d3d] = proxy;
    }
    log_object(proxy);
    end();
    return proxy;
}

// Points the module's import of name at replacement. Returns false if absent.
inline bool patch_import(HMODULE module, const char* dll, const char* name, void* replacement) {
    auto base = reinterpret_cast<unsigned char*>(module);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (_stricmp(reinterpret_cast<char*>(base + imp->Name), dll)) continue;
        auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
        auto slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (names->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            auto byname = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<char*>(byname->Name), name)) continue;
            DWORD old;
            VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
            slots->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
            VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
            return true;
        }
    }
    return false;
}

}  // namespace d3d8trace
