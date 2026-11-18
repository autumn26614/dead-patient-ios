#pragma once
// A Direct3D 8 style interface for the native ZGP renderer.
//
// ZGP's original code is expressed as calls on IDirect3D8/IDirect3DDevice8.
// The native transcription (zgp_device.hpp, d3dx_texture.hpp) makes the same
// calls, in the same order, against these interfaces. Implementations:
//   - Recorder: decorates any implementation and logs every call in the trace
//     format of direct/tests/d3d8_trace.hpp, so the transcription can be
//     compared line by line with the original DLL's trace;
//   - the platform device (OpenGL ES on iOS).
// Only the methods ZGP reaches are declared; the set grows with the port.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace cp2 {
namespace d3d8 {

using HResult = std::uint32_t;
constexpr HResult kOk = 0;
constexpr HResult kNotAvailable = 0x8876086a;  // D3DERR_NOTAVAILABLE
constexpr HResult kInvalidCall = 0x8876086c;   // D3DERR_INVALIDCALL
constexpr HResult kFail = 0x80004005;          // E_FAIL
constexpr HResult kNotImplemented = 0x80004001;  // E_NOTIMPL
constexpr HResult kOutOfMemory = 0x8007000e;   // E_OUTOFMEMORY
inline bool failed(HResult r) { return (r & 0x80000000u) != 0; }

// D3DFORMAT values used by ZGP.
constexpr std::uint32_t kFormatR8G8B8 = 20;
constexpr std::uint32_t kFormatA8R8G8B8 = 21;
constexpr std::uint32_t kFormatX8R8G8B8 = 22;
constexpr std::uint32_t kFormatL8 = 50;
constexpr std::uint32_t kFormatA8L8 = 51;
constexpr std::uint32_t kFormatD24S8 = 75;
constexpr std::uint32_t kFormatD24X8 = 77;
constexpr std::uint32_t kFormatD16 = 80;
constexpr std::uint32_t kUsageDepthStencil = 2;
constexpr std::uint32_t kResourceSurface = 1;
constexpr std::uint32_t kResourceTexture = 3;
constexpr std::uint32_t kPoolDefault = 0;
constexpr std::uint32_t kPoolManaged = 1;
constexpr std::uint32_t kPoolSystemMem = 2;
constexpr std::uint32_t kFormatVertexData = 100;
constexpr std::uint32_t kFormatIndex16 = 101;
constexpr std::uint32_t kFormatIndex32 = 102;
constexpr std::uint32_t kResourceVertexBuffer = 6;
constexpr std::uint32_t kResourceIndexBuffer = 7;

inline std::uint32_t fourcc(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24;
}

struct DisplayMode {
    std::uint32_t width = 0, height = 0, refresh = 0, format = 0;
};

// D3DPRESENT_PARAMETERS (13 DWORDs).
struct PresentParameters {
    std::uint32_t width = 0, height = 0, format = 0, back_buffer_count = 0, multisample = 0, swap_effect = 0;
    std::uint32_t window = 0, windowed = 0, auto_depth_stencil = 0, depth_format = 0, flags = 0, refresh = 0,
                  interval = 0;
};

// D3DCAPS8 as 53 raw DWORDs (several fields are floats).
struct Caps {
    enum Index {
        kDeviceType = 0, kAdapterOrdinal = 1, kTextureCaps = 15, kMaxTextureWidth = 22, kMaxTextureHeight = 23,
        kMaxVolumeExtent = 24, kMaxTextureAspectRatio = 26,
    };
    std::uint32_t words[53];
    Caps() { std::memset(words, 0, sizeof(words)); }
};

// D3DSURFACE_DESC.
struct SurfaceDesc {
    std::uint32_t format = 0, type = 0, usage = 0, pool = 0, size = 0, multisample = 0, width = 0, height = 0;
};

struct LockedRect {
    std::int32_t pitch = 0;
    unsigned char* bits = nullptr;
};

struct Matrix {
    float m[16];
};

// RECT / D3DRECT.
struct Rect {
    std::int32_t left = 0, top = 0, right = 0, bottom = 0;
};

constexpr HResult kDeviceLost = 0x88760868;  // D3DERR_DEVICELOST
constexpr std::uint32_t kClearTarget = 1, kClearZBuffer = 2, kClearStencil = 4;
enum PrimitiveType : std::uint32_t {
    kPointList = 1, kLineList = 2, kLineStrip = 3, kTriangleList = 4, kTriangleStrip = 5, kTriangleFan = 6,
};

inline std::uint32_t vertex_count(std::uint32_t type, std::uint32_t primitives) {
    switch (type) {
    case kPointList: return primitives;
    case kLineList: return primitives * 2;
    case kLineStrip: return primitives + 1;
    case kTriangleList: return primitives * 3;
    case kTriangleStrip: case kTriangleFan: return primitives + 2;
    default: return 0;
    }
}

// The pre-transformed vertex ZGP's 2D drawing uses (FVF 0x144:
// XYZRHW | DIFFUSE | TEX1, 28 bytes).
constexpr std::uint32_t kFvfScreen = 0x144;
struct ScreenVertex {
    float x, y, z, rhw;
    std::uint32_t color;
    std::uint32_t u, v;  // float bits, stored as the original pushes them
};
static_assert(sizeof(ScreenVertex) == 28, "D3DFVF_XYZRHW | DIFFUSE | TEX1");

// D3DVIEWPORT8.
struct Viewport {
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
    float min_z = 0.0f, max_z = 1.0f;
};

// D3DLIGHT8 as 26 raw DWORDs (type, diffuse, specular, ambient, position,
// direction, range, falloff, attenuation x3, theta, phi).
struct Light {
    std::uint32_t words[26];
    Light() { std::memset(words, 0, sizeof(words)); }
    float get(int i) const { float f; std::memcpy(&f, &words[i], 4); return f; }
    void set(int i, float f) { std::memcpy(&words[i], &f, 4); }
};

// LOGFONTA, as ZGP fills it for D3DXCreateFontIndirect (face in CP932).
struct LogFont {
    std::int32_t height = 0, width = 0, escapement = 0, orientation = 0, weight = 0;
    std::uint8_t italic = 0, underline = 0, strike_out = 0, charset = 0, out_precision = 0, clip_precision = 0,
                 quality = 0, pitch_and_family = 0;
    std::string face;
};

// Bytes in one row of a level (one row of 4x4 blocks for DXTn); 0 if unknown.
inline std::uint32_t row_bytes(std::uint32_t format, std::uint32_t width, std::uint32_t& rows_per_block) {
    rows_per_block = 1;
    switch (format) {
    case 20: return width * 3u;
    case 21: case 22: return width * 4u;
    case 23: case 24: case 25: case 26: case 29: case 30: case 40: case 51: return width * 2u;
    case 27: case 28: case 41: case 50: case 52: return width;
    default: break;
    }
    if (format == fourcc('D', 'X', 'T', '1')) { rows_per_block = 4; return ((width + 3u) / 4u) * 8u; }
    if (format == fourcc('D', 'X', 'T', '2') || format == fourcc('D', 'X', 'T', '3') ||
        format == fourcc('D', 'X', 'T', '4') || format == fourcc('D', 'X', 'T', '5')) {
        rows_per_block = 4;
        return ((width + 3u) / 4u) * 16u;
    }
    return 0;
}

// IDirect3DTexture8. Reference counted like COM: the creator holds one
// reference; release() returns the new count and destroys the texture at 0.
class Texture {
public:
    virtual ~Texture() {}
    virtual std::uint32_t add_ref() = 0;
    virtual std::uint32_t release() = 0;
    virtual std::uint32_t level_count() = 0;
    virtual HResult level_desc(std::uint32_t level, SurfaceDesc& desc) = 0;
    // Locks a whole level (no sub-rectangle is used by ZGP or D3DX here).
    virtual HResult lock_rect(std::uint32_t level, LockedRect& locked, std::uint32_t flags) = 0;
    virtual HResult unlock_rect(std::uint32_t level) = 0;
    unsigned trace_id = 0;  // object id in recorded traces
};

// IDirect3DVertexBuffer8 / IDirect3DIndexBuffer8, reference counted like
// textures. desc() is D3DVERTEXBUFFER_DESC (format, type, usage, pool, size,
// FVF) or D3DINDEXBUFFER_DESC (the first five).
class Buffer {
public:
    virtual ~Buffer() {}
    virtual std::uint32_t add_ref() = 0;
    virtual std::uint32_t release() = 0;
    virtual bool is_index() const = 0;
    virtual void desc(std::uint32_t words[6]) const = 0;
    // Locks the whole buffer (offset 0, size 0 as ZGP locks it).
    virtual HResult lock(unsigned char*& data, std::uint32_t flags) = 0;
    virtual HResult unlock() = 0;
    unsigned trace_id = 0;  // object id in recorded traces
};

class Direct3D;

class Device {
public:
    virtual ~Device() {}
    // Called by the Direct3D implementation that created this device.
    virtual void attach_creator(Direct3D*) {}
    virtual HResult get_direct3d(Direct3D*& d3d) = 0;  // adds a reference
    // IUnknown::AddRef on the device (D3DX meshes hold a device reference).
    // The count reflects the runtime's internal objects; traces mask it.
    virtual std::uint32_t add_ref() = 0;
    // IUnknown::Release of such a reference. The device itself stays owned by
    // its creator; the count is masked in traces like AddRef's.
    virtual std::uint32_t release() = 0;
    // GetCreationParameters (D3DX's skin mesh queries it; the result is not used here).
    virtual HResult get_creation_parameters() = 0;
    virtual HResult get_device_caps(Caps& caps) = 0;
    virtual HResult get_display_mode(DisplayMode& mode) = 0;
    virtual HResult create_texture(std::uint32_t width, std::uint32_t height, std::uint32_t levels,
                                   std::uint32_t usage, std::uint32_t format, std::uint32_t pool,
                                   Texture*& texture) = 0;
    virtual HResult create_vertex_buffer(std::uint32_t length, std::uint32_t usage, std::uint32_t fvf,
                                         std::uint32_t pool, Buffer*& buffer) = 0;
    virtual HResult create_index_buffer(std::uint32_t length, std::uint32_t usage, std::uint32_t format,
                                        std::uint32_t pool, Buffer*& buffer) = 0;
    virtual HResult set_texture_stage_state(std::uint32_t stage, std::uint32_t type, std::uint32_t value) = 0;
    virtual HResult set_texture(std::uint32_t stage, Texture* texture) = 0;  // null unbinds
    virtual HResult set_render_state(std::uint32_t state, std::uint32_t value) = 0;
    virtual HResult light_enable(std::uint32_t index, std::uint32_t enable) = 0;
    virtual HResult set_light(std::uint32_t index, const Light& light) = 0;
    virtual HResult set_transform(std::uint32_t type, const Matrix& matrix) = 0;
    virtual HResult get_transform(std::uint32_t type, Matrix& matrix) = 0;
    virtual HResult set_viewport(const Viewport& viewport) = 0;
    virtual HResult get_viewport(Viewport& viewport) = 0;
    virtual HResult clear(std::uint32_t count, const Rect* rects, std::uint32_t flags, std::uint32_t color,
                          float z, std::uint32_t stencil) = 0;
    virtual HResult begin_scene() = 0;
    virtual HResult end_scene() = 0;
    // Present with no window override and no dirty region (as ZGP calls it).
    virtual HResult present(const Rect* source, const Rect* destination) = 0;
    virtual HResult reset(PresentParameters& parameters) = 0;
    virtual HResult set_vertex_shader(std::uint32_t handle) = 0;
    virtual HResult draw_primitive_up(std::uint32_t type, std::uint32_t primitives, const void* data,
                                      std::uint32_t stride) = 0;
    // 3D drawing (objdraw, CSMesh): D3DMATERIAL8 as 17 floats (diffuse,
    // ambient, specular, emissive RGBA, power).
    virtual HResult set_material(const float material[17]) = 0;
    // MultiplyTransform: transform = matrix * transform.
    virtual HResult multiply_transform(std::uint32_t type, const Matrix& matrix) = 0;
    virtual HResult set_stream_source(std::uint32_t stream, Buffer* buffer, std::uint32_t stride) = 0;
    virtual HResult set_indices(Buffer* buffer, std::uint32_t base_vertex) = 0;
    virtual HResult draw_indexed_primitive(std::uint32_t type, std::uint32_t min_index, std::uint32_t vertices,
                                           std::uint32_t start_index, std::uint32_t primitives) = 0;
    // D3DXCreateFontIndirect from the D3DX8 copy linked into ZGP, replacing
    // any previous font. Its internal GDI glyph cache and sprite state blocks
    // are library details; the platform provides its own text rasterizer (see
    // docs/zgp-renderer.md).
    virtual HResult create_text_font(const LogFont& font) = 0;
};

class Direct3D {
public:
    virtual ~Direct3D() {}
    virtual std::uint32_t add_ref() = 0;
    virtual std::uint32_t release() = 0;
    virtual HResult adapter_display_mode(std::uint32_t adapter, DisplayMode& mode) = 0;
    virtual HResult adapter_identifier(std::uint32_t adapter, std::uint32_t flags) = 0;
    virtual HResult device_caps(std::uint32_t adapter, std::uint32_t type, Caps& caps) = 0;
    virtual HResult check_device_type(std::uint32_t adapter, std::uint32_t type, std::uint32_t display_format,
                                      std::uint32_t back_format, std::uint32_t windowed) = 0;
    virtual HResult check_device_format(std::uint32_t adapter, std::uint32_t type, std::uint32_t adapter_format,
                                        std::uint32_t usage, std::uint32_t resource, std::uint32_t format) = 0;
    virtual HResult create_device(std::uint32_t adapter, std::uint32_t type, std::uint32_t window,
                                  std::uint32_t behavior, PresentParameters& parameters, Device*& device) = 0;
};

// ---------------------------------------------------------------------------
// Recorder: logs calls exactly like the Windows proxy, then forwards them.

class TraceSink {
public:
    explicit TraceSink(FILE* out) : out_(out) {}
    void line(const std::string& text) { if (out_) std::fprintf(out_, "%s\n", text.c_str()); }
    unsigned next_id() { return next_++; }

private:
    FILE* out_;
    unsigned next_ = 1;
};

inline std::string u(std::uint32_t v) { return std::to_string(v); }
inline std::string hex8(std::uint32_t v) {
    char text[9];
    std::snprintf(text, sizeof(text), "%08x", v);
    return text;
}
inline std::string result(HResult r) { return " -> " + hex8(r); }
inline std::uint32_t bits(float f) { std::uint32_t b; std::memcpy(&b, &f, 4); return b; }
inline std::string mode_text(const DisplayMode& m) {
    return "{" + u(m.width) + " " + u(m.height) + " " + u(m.refresh) + " " + u(m.format) + "}";
}

// FNV-1a over a level's texel rows (pitch padding excluded), as the Windows
// harness hashes texture contents.
inline std::string level_hash(Texture& texture, std::uint32_t level) {
    SurfaceDesc desc;
    texture.level_desc(level, desc);
    std::string text = "LEVEL " + u(level) + " {" + u(desc.format) + " " + u(desc.type) + " " + u(desc.usage) + " " +
                       u(desc.pool) + " " + u(desc.size) + " " + u(desc.multisample) + " " + u(desc.width) + " " +
                       u(desc.height) + "}";
    std::uint32_t block_rows = 1;
    const std::uint32_t row = row_bytes(desc.format, desc.width, block_rows);
    LockedRect locked;
    const HResult r = texture.lock_rect(level, locked, 0x10);  // D3DLOCK_READONLY
    if (failed(r) || !row) {
        if (!failed(r)) texture.unlock_rect(level);
        return text + " unreadable " + hex8(r);
    }
    std::uint64_t hash = 1469598103934665603ull;
    const std::uint32_t rows = (desc.height + block_rows - 1) / block_rows;
    for (std::uint32_t y = 0; y < rows; ++y) {
        const unsigned char* p = locked.bits + static_cast<std::ptrdiff_t>(y) * locked.pitch;
        for (std::uint32_t i = 0; i < row; ++i) hash = (hash ^ p[i]) * 1099511628211ull;
    }
    texture.unlock_rect(level);
    char tail[64];
    std::snprintf(tail, sizeof(tail), " data %u:%016llx", static_cast<unsigned>(row * rows),
                  static_cast<unsigned long long>(hash));
    return text + tail;
}

// D3DXGetFVFVertexSize.
inline std::uint32_t fvf_stride(std::uint32_t fvf) {
    static const std::uint32_t position[8] = {0, 12, 16, 16, 20, 24, 28, 32};
    std::uint32_t size = position[(fvf & 0xe) >> 1];
    if (fvf & 0x10) size += 12;
    if (fvf & 0x20) size += 4;
    if (fvf & 0x40) size += 4;
    if (fvf & 0x80) size += 4;
    static const std::uint32_t coordinates[4] = {8, 12, 16, 4};
    for (std::uint32_t i = 0; i < ((fvf >> 8) & 0xf); ++i) size += coordinates[(fvf >> (16 + 2 * i)) & 3];
    return size;
}

// Zeroes the vertices a 16-bit index buffer does not reference; returns how many.
inline std::uint32_t mask_unreferenced(std::vector<unsigned char>& vertices, std::uint32_t stride,
                                       const std::vector<unsigned char>& indices) {
    if (!stride) return 0;
    const std::uint32_t count = static_cast<std::uint32_t>(vertices.size() / stride);
    std::vector<bool> used(count, false);
    for (std::size_t i = 0; i + 2 <= indices.size(); i += 2) {
        const std::uint32_t v = static_cast<std::uint32_t>(indices[i] | indices[i + 1] << 8);
        if (v < count) used[v] = true;
    }
    std::uint32_t masked = 0;
    for (std::uint32_t v = 0; v < count; ++v) {
        if (used[v]) continue;
        std::memset(vertices.data() + static_cast<std::size_t>(v) * stride, 0, stride);
        ++masked;
    }
    return masked;
}

class RecordingDirect3D;

inline std::string present_text(const PresentParameters& p);

// RECT arrays as the Windows proxy logs them ("4i" per rectangle).
inline std::string rects_text(const Rect* rects, std::uint32_t count) {
    if (!rects) return "null";
    std::string text = "{";
    for (std::uint32_t i = 0; i < count; ++i) {
        const Rect& r = rects[i];
        text += (i ? " " : "") + std::to_string(r.left) + " " + std::to_string(r.top) + " " +
                std::to_string(r.right) + " " + std::to_string(r.bottom);
    }
    return text + "}";
}

class RecordingDevice : public Device {
public:
    RecordingDevice(Device* inner, TraceSink& sink, Direct3D* owner) : inner_(inner), sink_(sink), owner_(owner) {}
    ~RecordingDevice() override {
        for (Texture* t : created_) t->release();
        for (Buffer* b : created_buffers_) b->release();
    }
    Device* inner() const { return inner_; }

    HResult get_direct3d(Direct3D*& d3d) override;
    std::uint32_t add_ref() override {
        const std::uint32_t count = inner_->add_ref();
        sink_.line("AddRef" + result(count));
        return count;
    }
    std::uint32_t release() override {
        const std::uint32_t count = inner_->release();
        sink_.line("Release" + result(count));
        return count;
    }
    HResult get_creation_parameters() override {
        HResult r = inner_->get_creation_parameters();
        sink_.line("GetCreationParameters ptr" + result(r));
        return r;
    }
    HResult get_device_caps(Caps& caps) override {
        HResult r = inner_->get_device_caps(caps);
        std::string text = "GetDeviceCaps ptr out {";
        for (int k = 0; k < 53; ++k) text += (k ? " " : "") + hex8(caps.words[k]);
        sink_.line(text + "}" + result(r));
        return r;
    }
    HResult get_display_mode(DisplayMode& mode) override {
        HResult r = inner_->get_display_mode(mode);
        sink_.line("GetDisplayMode ptr out " + mode_text(mode) + result(r));
        return r;
    }
    HResult create_texture(std::uint32_t w, std::uint32_t h, std::uint32_t levels, std::uint32_t usage,
                           std::uint32_t format, std::uint32_t pool, Texture*& texture) override {
        HResult r = inner_->create_texture(w, h, levels, usage, format, pool, texture);
        unsigned id = 0;
        if (r == kOk && texture) {
            id = sink_.next_id();
            texture->trace_id = id;
            texture->add_ref();
            created_.push_back(texture);
        }
        sink_.line("CreateTexture " + u(w) + " " + u(h) + " " + u(levels) + " " + u(usage) + " " + u(format) + " " +
                   u(pool) + " ptr out #" + u(id) + result(r));
        return r;
    }
    HResult create_vertex_buffer(std::uint32_t length, std::uint32_t usage, std::uint32_t fvf, std::uint32_t pool,
                                 Buffer*& buffer) override {
        HResult r = inner_->create_vertex_buffer(length, usage, fvf, pool, buffer);
        sink_.line("CreateVertexBuffer " + u(length) + " " + u(usage) + " " + u(fvf) + " " + u(pool) + " ptr out #" +
                   u(track(r, buffer)) + result(r));
        return r;
    }
    HResult create_index_buffer(std::uint32_t length, std::uint32_t usage, std::uint32_t format, std::uint32_t pool,
                                Buffer*& buffer) override {
        HResult r = inner_->create_index_buffer(length, usage, format, pool, buffer);
        sink_.line("CreateIndexBuffer " + u(length) + " " + u(usage) + " " + u(format) + " " + u(pool) + " ptr out #" +
                   u(track(r, buffer)) + result(r));
        return r;
    }
    HResult set_texture_stage_state(std::uint32_t s, std::uint32_t t, std::uint32_t v) override {
        HResult r = inner_->set_texture_stage_state(s, t, v);
        sink_.line("SetTextureStageState " + u(s) + " " + u(t) + " " + u(v) + result(r));
        return r;
    }
    HResult set_texture(std::uint32_t s, Texture* texture) override {
        HResult r = inner_->set_texture(s, texture);
        sink_.line("SetTexture " + u(s) + " #" + u(texture ? texture->trace_id : 0) + result(r));
        return r;
    }
    HResult set_render_state(std::uint32_t s, std::uint32_t v) override {
        HResult r = inner_->set_render_state(s, v);
        sink_.line("SetRenderState " + u(s) + " " + u(v) + result(r));
        return r;
    }
    HResult light_enable(std::uint32_t i, std::uint32_t e) override {
        HResult r = inner_->light_enable(i, e);
        sink_.line("LightEnable " + u(i) + " " + u(e) + result(r));
        return r;
    }
    HResult set_light(std::uint32_t i, const Light& light) override {
        HResult r = inner_->set_light(i, light);
        std::string text = "SetLight " + u(i) + " {";
        for (int k = 0; k < 26; ++k) text += (k ? " " : "") + hex8(light.words[k]);
        sink_.line(text + "}" + result(r));
        return r;
    }
    HResult set_transform(std::uint32_t t, const Matrix& m) override {
        HResult r = inner_->set_transform(t, m);
        std::string text = "SetTransform " + u(t) + " {";
        for (int k = 0; k < 16; ++k) text += std::string(k ? " " : "") + "f" + hex8(bits(m.m[k]));
        sink_.line(text + "}" + result(r));
        return r;
    }
    HResult get_transform(std::uint32_t t, Matrix& m) override {
        HResult r = inner_->get_transform(t, m);
        std::string text = "GetTransform " + u(t) + " ptr out {";
        for (int k = 0; k < 16; ++k) text += std::string(k ? " " : "") + "f" + hex8(bits(m.m[k]));
        sink_.line(text + "}" + result(r));
        return r;
    }
    HResult set_viewport(const Viewport& v) override {
        HResult r = inner_->set_viewport(v);
        sink_.line("SetViewport {" + u(v.x) + " " + u(v.y) + " " + u(v.width) + " " + u(v.height) + " " +
                   u(bits(v.min_z)) + " " + u(bits(v.max_z)) + "}" + result(r));
        return r;
    }
    HResult get_viewport(Viewport& v) override {
        HResult r = inner_->get_viewport(v);
        sink_.line("GetViewport ptr out {" + u(v.x) + " " + u(v.y) + " " + u(v.width) + " " + u(v.height) + " " +
                   u(bits(v.min_z)) + " " + u(bits(v.max_z)) + "}" + result(r));
        return r;
    }
    HResult clear(std::uint32_t count, const Rect* rects, std::uint32_t flags, std::uint32_t color, float z,
                  std::uint32_t stencil) override {
        HResult r = inner_->clear(count, rects, flags, color, z, stencil);
        sink_.line("Clear " + u(count) + " " + rects_text(rects, count) + " " + u(flags) + " " + u(color) + " f" +
                   hex8(bits(z)) + " " + u(stencil) + result(r));
        return r;
    }
    HResult begin_scene() override {
        HResult r = inner_->begin_scene();
        sink_.line("BeginScene" + result(r));
        return r;
    }
    HResult end_scene() override {
        HResult r = inner_->end_scene();
        sink_.line("EndScene" + result(r));
        return r;
    }
    HResult present(const Rect* source, const Rect* destination) override {
        HResult r = inner_->present(source, destination);
        sink_.line("Present " + rects_text(source, 1) + " " + rects_text(destination, 1) + " null null" + result(r));
        return r;
    }
    HResult reset(PresentParameters& p) override {
        const std::string before = present_text(p);
        HResult r = inner_->reset(p);
        sink_.line("Reset " + before + " out " + present_text(p) + result(r));
        return r;
    }
    HResult set_vertex_shader(std::uint32_t handle) override {
        HResult r = inner_->set_vertex_shader(handle);
        sink_.line("SetVertexShader " + u(handle) + result(r));
        return r;
    }
    HResult draw_primitive_up(std::uint32_t type, std::uint32_t primitives, const void* data,
                              std::uint32_t stride) override {
        HResult r = inner_->draw_primitive_up(type, primitives, data, stride);
        const std::size_t size = static_cast<std::size_t>(vertex_count(type, primitives)) * stride;
        std::uint64_t hash = 1469598103934665603ull;
        const unsigned char* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; data && i < size; ++i) hash = (hash ^ p[i]) * 1099511628211ull;
        char tail[64];
        std::snprintf(tail, sizeof(tail), " data %u:%016llx", static_cast<unsigned>(size),
                      static_cast<unsigned long long>(hash));
        sink_.line("DrawPrimitiveUP " + u(type) + " " + u(primitives) + (data ? " ptr " : " null ") + u(stride) +
                   (data ? std::string(tail) : std::string(" data null")) + result(r));
        return r;
    }
    HResult set_material(const float m[17]) override {
        HResult r = inner_->set_material(m);
        std::string text = "SetMaterial {";
        for (int k = 0; k < 17; ++k) text += std::string(k ? " " : "") + "f" + hex8(bits(m[k]));
        sink_.line(text + "}" + result(r));
        return r;
    }
    HResult multiply_transform(std::uint32_t t, const Matrix& m) override {
        HResult r = inner_->multiply_transform(t, m);
        std::string text = "MultiplyTransform " + u(t) + " {";
        for (int k = 0; k < 16; ++k) text += std::string(k ? " " : "") + "f" + hex8(bits(m.m[k]));
        sink_.line(text + "}" + result(r));
        return r;
    }
    HResult set_stream_source(std::uint32_t stream, Buffer* buffer, std::uint32_t stride) override {
        HResult r = inner_->set_stream_source(stream, buffer, stride);
        sink_.line("SetStreamSource " + u(stream) + " #" + u(buffer ? buffer->trace_id : 0) + " " + u(stride) +
                   result(r));
        return r;
    }
    HResult set_indices(Buffer* buffer, std::uint32_t base_vertex) override {
        HResult r = inner_->set_indices(buffer, base_vertex);
        sink_.line("SetIndices #" + u(buffer ? buffer->trace_id : 0) + " " + u(base_vertex) + result(r));
        return r;
    }
    HResult draw_indexed_primitive(std::uint32_t type, std::uint32_t min_index, std::uint32_t vertices,
                                   std::uint32_t start_index, std::uint32_t primitives) override {
        HResult r = inner_->draw_indexed_primitive(type, min_index, vertices, start_index, primitives);
        sink_.line("DrawIndexedPrimitive " + u(type) + " " + u(min_index) + " " + u(vertices) + " " +
                   u(start_index) + " " + u(primitives) + result(r));
        return r;
    }
    HResult create_text_font(const LogFont& font) override {
        HResult r = inner_->create_text_font(font);
        sink_.line("D3DXCreateFontIndirect");
        return r;
    }

    // Logs one texture's levels (the harness's @surf).
    void dump_texture(Texture& t) {
        const std::uint32_t levels = t.level_count();
        sink_.line("TEXTURE #" + u(t.trace_id) + " levels " + u(levels));
        for (std::uint32_t l = 0; l < levels; ++l) sink_.line(level_hash(t, l));
    }

    // Logs every texture created since the last call, as the Windows harness
    // does after each command (TEXTURE and LEVEL lines).
    void dump_created_textures() {
        for (Texture* t : created_) {
            const std::uint32_t levels = t->level_count();
            sink_.line("TEXTURE #" + u(t->trace_id) + " levels " + u(levels));
            for (std::uint32_t l = 0; l < levels; ++l) sink_.line(level_hash(*t, l));
            t->release();
        }
        created_.clear();
    }

    // Logs every vertex and index buffer created since the last call with its
    // description and final contents (BUFFER lines), as the Windows harness
    // does: a hash, and the DWORDs when the buffer is at most 4096 bytes.
    void dump_created_buffers() {
        std::vector<unsigned char> previous_indices;
        bool previous_was_index16 = false;
        for (Buffer* b : created_buffers_) {
            std::uint32_t d[6] = {};
            b->desc(d);
            const int words = b->is_index() ? 5 : 6;
            std::string text = "BUFFER #" + u(b->trace_id) + (b->is_index() ? " index {" : " vertex {");
            for (int k = 0; k < words; ++k) text += (k ? " " : "") + u(d[k]);
            text += "}";
            unsigned char* locked = nullptr;
            if (!failed(b->lock(locked, 0x10))) {  // D3DLOCK_READONLY
                const std::uint32_t size = d[4];
                std::vector<unsigned char> copy(locked, locked + size);
                b->unlock();
                unsigned char* data = copy.data();
                // The Windows harness's rule (direct/tests/d3d8_trace.hpp): in a
                // vertex buffer created directly after a 16-bit index buffer,
                // vertices that index buffer does not reference are dumped as zeroes.
                if (b->is_index()) {
                    previous_indices = copy;
                    previous_was_index16 = d[0] == kFormatIndex16;
                } else {
                    if (previous_was_index16) text += " masked " + u(mask_unreferenced(copy, fvf_stride(d[5]), previous_indices));
                    previous_was_index16 = false;
                }
                std::uint64_t hash = 1469598103934665603ull;
                for (std::uint32_t i = 0; i < size; ++i) hash = (hash ^ data[i]) * 1099511628211ull;
                char tail[64];
                std::snprintf(tail, sizeof(tail), " data %u:%016llx", static_cast<unsigned>(size),
                              static_cast<unsigned long long>(hash));
                text += tail;
                if (size <= 4096) {
                    text += " {";
                    for (std::uint32_t i = 0; i + 4 <= size; i += 4) {
                        std::uint32_t w;
                        std::memcpy(&w, data + i, 4);
                        text += (i ? " " : "") + hex8(w);
                    }
                    text += "}";
                }
            }
            sink_.line(text);
            b->release();
        }
        created_buffers_.clear();
    }

private:
    unsigned track(HResult r, Buffer* buffer) {
        if (r != kOk || !buffer) return 0;
        buffer->trace_id = sink_.next_id();
        buffer->add_ref();
        created_buffers_.push_back(buffer);
        return buffer->trace_id;
    }

    Device* inner_;
    TraceSink& sink_;
    Direct3D* owner_;
    std::vector<Texture*> created_;
    std::vector<Buffer*> created_buffers_;
};

inline std::string present_text(const PresentParameters& p) {
    return "{" + u(p.width) + " " + u(p.height) + " " + u(p.format) + " " + u(p.back_buffer_count) + " " +
           u(p.multisample) + " " + u(p.swap_effect) + " " + u(p.window) + " " + u(p.windowed) + " " +
           u(p.auto_depth_stencil) + " " + u(p.depth_format) + " " + u(p.flags) + " " + u(p.refresh) + " " +
           u(p.interval) + "}";
}

class RecordingDirect3D : public Direct3D {
public:
    RecordingDirect3D(Direct3D* inner, TraceSink& sink) : inner_(inner), sink_(sink) {
        id_ = sink_.next_id();
        sink_.line("Direct3DCreate8 120 #" + u(id_));
    }
    unsigned id() const { return id_; }
    RecordingDevice* device() const { return device_.get(); }

    std::uint32_t add_ref() override { return inner_->add_ref(); }
    std::uint32_t release() override {
        const std::uint32_t count = inner_->release();
        sink_.line("Release" + result(count));
        return count;
    }
    HResult adapter_display_mode(std::uint32_t a, DisplayMode& mode) override {
        HResult r = inner_->adapter_display_mode(a, mode);
        sink_.line("GetAdapterDisplayMode " + u(a) + " ptr out " + mode_text(mode) + result(r));
        return r;
    }
    HResult adapter_identifier(std::uint32_t a, std::uint32_t f) override {
        HResult r = inner_->adapter_identifier(a, f);
        sink_.line("GetAdapterIdentifier " + u(a) + " " + u(f) + " ptr" + result(r));
        return r;
    }
    HResult device_caps(std::uint32_t a, std::uint32_t t, Caps& caps) override {
        HResult r = inner_->device_caps(a, t, caps);
        std::string text = "GetDeviceCaps " + u(a) + " " + u(t) + " ptr out {";
        for (int k = 0; k < 53; ++k) text += (k ? " " : "") + hex8(caps.words[k]);
        sink_.line(text + "}" + result(r));
        return r;
    }
    HResult check_device_type(std::uint32_t a, std::uint32_t t, std::uint32_t d, std::uint32_t b, std::uint32_t w) override {
        HResult r = inner_->check_device_type(a, t, d, b, w);
        sink_.line("CheckDeviceType " + u(a) + " " + u(t) + " " + u(d) + " " + u(b) + " " + u(w) + result(r));
        return r;
    }
    HResult check_device_format(std::uint32_t a, std::uint32_t t, std::uint32_t af, std::uint32_t usage,
                                std::uint32_t res, std::uint32_t f) override {
        HResult r = inner_->check_device_format(a, t, af, usage, res, f);
        sink_.line("CheckDeviceFormat " + u(a) + " " + u(t) + " " + u(af) + " " + u(usage) + " " + u(res) + " " +
                   u(f) + result(r));
        return r;
    }
    HResult create_device(std::uint32_t a, std::uint32_t t, std::uint32_t window, std::uint32_t behavior,
                          PresentParameters& p, Device*& device) override {
        const std::string before = present_text(p);
        Device* real = nullptr;
        HResult r = inner_->create_device(a, t, window, behavior, p, real);
        unsigned id = 0;
        device = nullptr;
        if (r == kOk && real) {
            id = sink_.next_id();
            device_.reset(new RecordingDevice(real, sink_, this));
            device = device_.get();
        }
        sink_.line("CreateDevice " + u(a) + " " + u(t) + " " + (window ? "hwnd" : "null") + " " + u(behavior) +
                   " " + before + " ptr out " + present_text(p) + " out #" + u(id) + result(r));
        return r;
    }

private:
    Direct3D* inner_;
    TraceSink& sink_;
    unsigned id_ = 0;
    std::unique_ptr<RecordingDevice> device_;
};

// GetDirect3D hands back the recording Direct3D (as the Windows proxy hands
// D3DX the traced IDirect3D8) so that the caller's Release is logged too.
inline HResult RecordingDevice::get_direct3d(Direct3D*& d3d) {
    Direct3D* real = nullptr;
    HResult r = inner_->get_direct3d(real);  // adds the reference on the real object
    d3d = r == kOk ? owner_ : nullptr;
    const unsigned id = r == kOk ? static_cast<RecordingDirect3D*>(owner_)->id() : 0;
    sink_.line("GetDirect3D ptr out #" + u(id) + result(r));
    return r;
}

}  // namespace d3d8
}  // namespace cp2
