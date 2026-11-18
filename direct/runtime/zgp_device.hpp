#pragma once
// Native transcription of ZGP 1.807's device creation and 3D setup
// (createdx, createdevm, set3d and their helpers), issuing the same Direct3D 8
// calls in the same order as the original. See docs/zgp-renderer.md for the
// recovered addresses. Floating-point math after CreateDevice runs in the
// x87 single-precision mode Direct3D 8 selects, so it uses float operations
// in the original order; FMA contraction is disabled for that reason.
#include "d3d8.hpp"
#include "csmesh.hpp"
#include "csmesh_mesh.hpp"
#include "d3dx_texture.hpp"
#include "zgp_state.hpp"
#include "zgp_math.hpp"
#include "x87_state.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#if defined(__clang__)
#define CP2_ZGP_STRICT_FP _Pragma("clang fp contract(off)")
#else
#define CP2_ZGP_STRICT_FP
#endif

namespace cp2 {
namespace zgp {

// ---------------------------------------------------------------------------
// D3DX8 math copies statically linked into ZGP.

struct Vec3 {
    float x, y, z;
};

// D3DXVec3Normalize (0x10024c25).
inline Vec3 normalize(const Vec3& v) {
    CP2_ZGP_STRICT_FP
    float length2 = v.x * v.x + v.y * v.y;
    length2 = length2 + v.z * v.z;
    const float delta = length2 - 1.0f;
    if (!(delta < -9.99999974737875e-06f) && !(delta > 9.99999974737875e-06f)) return v;
    if (!(length2 > 9.99999943962493e-11f)) return Vec3{0.0f, 0.0f, 0.0f};
    const float inverse = 1.0f / std::sqrt(length2);
    return Vec3{inverse * v.x, inverse * v.y, inverse * v.z};
}

// D3DXVec3Cross (0x10024a01).
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    CP2_ZGP_STRICT_FP
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - b.z * a.x, a.x * b.y - b.x * a.y};
}

// D3DXMatrixInverse (0x100243ca), generated from the original's x87 code by
// tools/x87_to_cpp.py: cofactors in the original's operation order (single
// precision; every x87 result is rounded to 24 bits in Direct3D's
// precision mode), the determinant, its reciprocal and the adjugate scaled
// by it. A non-finite reciprocal leaves `out` untouched and returns false
// (the original returns NULL).
inline bool matrix_inverse(const float m[16], float out[16], float* determinant) {
    CP2_ZGP_STRICT_FP
    const float t0 = ((m[5] * m[0]) - (m[4] * m[1]));
    const float t1 = ((m[9] * m[0]) - (m[8] * m[1]));
    const float t2 = ((m[13] * m[0]) - (m[12] * m[1]));
    const float t3 = ((m[9] * m[4]) - (m[8] * m[5]));
    const float t4 = ((m[13] * m[4]) - (m[12] * m[5]));
    const float t5 = ((m[13] * m[8]) - (m[12] * m[9]));
    const float t6 = m[6];
    const float t7 = m[7];
    const float t8 = m[10];
    const float t9 = m[11];
    const float t10 = (((m[2] * t3) - (m[6] * t1)) + (m[10] * t0));
    const float t11 = (((m[6] * t2) - (m[14] * t0)) - (m[2] * t4));
    const float t12 = (((m[2] * t5) - (m[10] * t2)) + (m[14] * t1));
    const float t13 = (((m[10] * t4) - (m[14] * t3)) - (m[6] * t5));
    const float t14 = (((t1 * m[7]) - (t0 * m[11])) - (t3 * m[3]));
    const float t15 = (((t4 * m[3]) - (t2 * m[7])) + (t0 * m[15]));
    const float t16 = (((t2 * m[11]) - (t1 * m[15])) - (t5 * m[3]));
    const float t17 = (((t5 * m[7]) - (t4 * m[11])) + (t3 * m[15]));
    const float t18 = ((m[2] * m[7]) - (m[6] * m[3]));
    const float t19 = ((m[2] * t9) - (t8 * m[3]));
    const float t20 = (((t19 * m[5]) - (t18 * m[9])) - (((t6 * t9) - (t8 * t7)) * m[1]));
    const float t21 = (((((t6 * m[15]) - (m[14] * t7)) * m[1]) - (((m[2] * m[15]) - (m[14] * m[3])) * m[5])) + (t18 * m[13]));
    const float t22 = (((((m[2] * m[15]) - (m[14] * m[3])) * m[9]) - (t19 * m[13])) - (((t8 * m[15]) - (m[14] * t9)) * m[1]));
    const float t23 = (((((t8 * m[15]) - (m[14] * t9)) * m[5]) - (((t6 * m[15]) - (m[14] * t7)) * m[9])) + (((t6 * t9) - (t8 * t7)) * m[13]));
    const float t24 = (((((t6 * t9) - (t8 * t7)) * m[0]) - (t19 * m[4])) + (t18 * m[8]));
    const float t25 = (((((m[2] * m[15]) - (m[14] * m[3])) * m[4]) - (t18 * m[12])) - (((t6 * m[15]) - (m[14] * t7)) * m[0]));
    const float t26 = (((m[0] * ((t8 * m[15]) - (m[14] * t9))) - (((m[2] * m[15]) - (m[14] * m[3])) * m[8])) + (t19 * m[12]));
    const float t27 = (((((t6 * m[15]) - (m[14] * t7)) * m[8]) - (((t6 * t9) - (t8 * t7)) * m[12])) - (((t8 * m[15]) - (m[14] * t9)) * m[4]));
    const float d = ((((t20 * m[12]) + (t21 * m[8])) + (t22 * m[4])) + (t23 * m[0]));
    if (determinant) *determinant = d;
    const float t28 = 1.0f / d;
    // _finite on the reciprocal (widened to double): no output otherwise.
    if (!std::isfinite(static_cast<double>(t28))) return false;
    out[0] = (t23 * t28);
    out[1] = (t22 * t28);
    out[2] = (t21 * t28);
    out[3] = (t20 * t28);
    out[4] = (t27 * t28);
    out[5] = (t26 * t28);
    out[6] = (t25 * t28);
    out[7] = (t24 * t28);
    out[8] = (t17 * t28);
    out[9] = (t16 * t28);
    out[10] = (t15 * t28);
    out[11] = (t14 * t28);
    out[12] = (t13 * t28);
    out[13] = (t12 * t28);
    out[14] = (t11 * t28);
    out[15] = (t10 * t28);
    return true;
}

// D3DXMatrixLookAtLH (0x100250f1).
inline d3d8::Matrix look_at(const Vec3& eye, const Vec3& at, const Vec3& up) {
    CP2_ZGP_STRICT_FP
    const Vec3 z = normalize(Vec3{at.x - eye.x, at.y - eye.y, at.z - eye.z});
    const Vec3 x = normalize(cross(up, z));
    const Vec3 y = cross(z, x);
    auto dot = [&](const Vec3& r) {
        float s = r.y * eye.y + r.x * eye.x;
        s = s + r.z * eye.z;
        return -s;
    };
    d3d8::Matrix m;
    m.m[0] = x.x; m.m[1] = y.x; m.m[2] = z.x; m.m[3] = 0.0f;
    m.m[4] = x.y; m.m[5] = y.y; m.m[6] = z.y; m.m[7] = 0.0f;
    m.m[8] = x.z; m.m[9] = y.z; m.m[10] = z.z; m.m[11] = 0.0f;
    m.m[12] = dot(x); m.m[13] = dot(y); m.m[14] = dot(z); m.m[15] = 1.0f;
    return m;
}

// D3DXMatrixPerspectiveFovLH (0x1002496d).
inline d3d8::Matrix perspective(float fovy, float aspect, float zn, float zf) {
    CP2_ZGP_STRICT_FP
    const float half = fovy * 0.5f;
    const float c = static_cast<float>(std::cos(static_cast<double>(half)));
    const float s = static_cast<float>(std::sin(static_cast<double>(half)));
    const float y_scale = c / s;
    const float q = zf / (zf - zn);
    d3d8::Matrix m;
    std::memset(m.m, 0, sizeof(m.m));
    m.m[0] = y_scale / aspect;
    m.m[5] = y_scale;
    m.m[10] = q;
    m.m[11] = 1.0f;
    m.m[14] = -(q * zn);
    return m;
}

// ---------------------------------------------------------------------------
// Platform adapter profile.

// D3DCAPS8 of the reference machine's Direct3D 8 device, as D3DX read it
// (direct/tests/zgp/texture.reference). D3DX's texture decisions depend on
// it: no POW2 or NONPOW2CONDITIONAL restriction, MIPMAP support, 16384 texel
// limits. The port presents this device profile.
inline const d3d8::Caps& reference_caps() {
    static d3d8::Caps caps;
    static bool ready = false;
    if (!ready) {
        static const std::uint32_t words[53] = {
            0x00000001, 0x00000000, 0x00020000, 0x200a0000, 0x00000420, 0x8000000f, 0x00000000, 0x001bbef0,
            0x00000ef2, 0x00f37191, 0x000000ff, 0x00001fff, 0x00001fff, 0x000000ff, 0x00084208, 0x0001ecc5,
            0x03030700, 0x03030300, 0x03030300, 0x0000003f, 0x0000003f, 0x0000001f, 0x00004000, 0x00004000,
            0x00000800, 0x00002000, 0x00004000, 0x00000010, 0x501502f9, 0xccbebc20, 0xccbebc20, 0x4cbebc20,
            0x4cbebc20, 0x00000000, 0x000000ff, 0x00180008, 0x03feffff, 0x00000008, 0x00000008, 0x0000007b,
            0xffffffff, 0x00000006, 0x00000004, 0x000000ff, 0x46000000, 0x00ffffff, 0x00ffffff, 0x00000010,
            0x000000ff, 0xfffe0101, 0x00000060, 0xffff0104, 0x477fe000};
        std::memcpy(caps.words, words, sizeof(words));
        ready = true;
    }
    return caps;
}

// Texture formats the reference device accepts (CheckDeviceFormat with no
// usage on an X8R8G8B8 display): A8R8G8B8, X8R8G8B8, R5G6B5, X1R5G5B5,
// A1R5G5B5, A4R4G4B4, A8, L8, A8L8, V8U8, X8L8V8U8, Q8W8V8U8, V16U16, DXT1-5.
inline bool reference_texture_format(std::uint32_t f) {
    switch (f) {
    case 21: case 22: case 23: case 24: case 25: case 26: case 28: case 50: case 51: case 60: case 62: case 63:
    case 64:
        return true;
    default:
        return f == 0x31545844u || f == 0x32545844u || f == 0x33545844u || f == 0x34545844u || f == 0x35545844u;
    }
}

// Answers Direct3D 8 capability queries for a device that renders with an
// X8R8G8B8 display, D16/D24X8/D24S8 depth buffers, and windowed operation,
// with the reference device's capabilities and texture formats. Unsupported
// depth formats report NOTAVAILABLE; asking for DEPTHSTENCIL use of a
// non-depth format is an INVALIDCALL, as in Direct3D 8.
class PortableAdapter : public d3d8::Direct3D {
public:
    using Factory = std::function<d3d8::Device*(const d3d8::PresentParameters&)>;
    PortableAdapter(std::uint32_t width, std::uint32_t height, Factory factory)
        : width_(width), height_(height), factory_(std::move(factory)) {}

    std::uint32_t add_ref() override { return ++references_; }
    std::uint32_t release() override { return references_ ? --references_ : 0; }  // owned by the platform
    d3d8::HResult adapter_display_mode(std::uint32_t adapter, d3d8::DisplayMode& mode) override {
        if (adapter != 0) return d3d8::kInvalidCall;
        mode.width = width_;
        mode.height = height_;
        mode.refresh = 60;
        mode.format = d3d8::kFormatX8R8G8B8;
        return d3d8::kOk;
    }
    d3d8::HResult adapter_identifier(std::uint32_t adapter, std::uint32_t) override {
        return adapter == 0 ? d3d8::kOk : d3d8::kInvalidCall;
    }
    d3d8::HResult device_caps(std::uint32_t adapter, std::uint32_t type, d3d8::Caps& caps) override {
        if (adapter != 0 || type != 1) return d3d8::kInvalidCall;
        caps = reference_caps();
        return d3d8::kOk;
    }
    d3d8::HResult check_device_type(std::uint32_t adapter, std::uint32_t type, std::uint32_t display,
                                    std::uint32_t back, std::uint32_t windowed) override {
        if (adapter != 0 || type != 1) return d3d8::kInvalidCall;
        return display == d3d8::kFormatX8R8G8B8 && back == d3d8::kFormatX8R8G8B8 && windowed ? d3d8::kOk
                                                                                              : d3d8::kNotAvailable;
    }
    d3d8::HResult check_device_format(std::uint32_t adapter, std::uint32_t type, std::uint32_t adapter_format,
                                      std::uint32_t usage, std::uint32_t resource, std::uint32_t format) override {
        if (adapter != 0 || type != 1 || adapter_format != d3d8::kFormatX8R8G8B8) return d3d8::kInvalidCall;
        if (usage == d3d8::kUsageDepthStencil && resource == d3d8::kResourceSurface) {
            if (format < 70) return d3d8::kInvalidCall;
            return format == d3d8::kFormatD16 || format == d3d8::kFormatD24X8 || format == d3d8::kFormatD24S8
                       ? d3d8::kOk
                       : d3d8::kNotAvailable;
        }
        if (usage == 0 && resource == d3d8::kResourceTexture)
            return reference_texture_format(format) ? d3d8::kOk : d3d8::kNotAvailable;
        return d3d8::kNotAvailable;
    }
    d3d8::HResult create_device(std::uint32_t adapter, std::uint32_t type, std::uint32_t window, std::uint32_t,
                                d3d8::PresentParameters& p, d3d8::Device*& device) override;

private:
    std::uint32_t width_, height_;
    Factory factory_;
    std::uint32_t references_ = 1;
};

// A Direct3D 8 texture whose levels are kept in memory. Rows are padded to a
// DWORD, as on the reference device: a 1x1 A8L8 level reports Size 4.
class StateTexture : public d3d8::Texture {
public:
    struct Level {
        std::uint32_t width, height, pitch, rows;
        std::vector<unsigned char> bytes;  // zero-filled at creation
    };
    StateTexture(std::uint32_t format, std::uint32_t usage, std::uint32_t pool) : format_(format), usage_(usage), pool_(pool) {}

    static StateTexture* create(std::uint32_t width, std::uint32_t height, std::uint32_t levels, std::uint32_t usage,
                                std::uint32_t format, std::uint32_t pool) {
        std::uint32_t block_rows;
        if (!width || !height || !d3d8::row_bytes(format, 1, block_rows)) return nullptr;
        std::uint32_t full = 1;
        for (std::uint32_t w = width, h = height; w > 1 || h > 1; w >>= 1, h >>= 1) ++full;
        if (levels == 0 || levels > full) levels = levels == 0 ? full : 0;
        if (!levels) return nullptr;
        StateTexture* t = new StateTexture(format, usage, pool);
        for (std::uint32_t l = 0; l < levels; ++l) {
            Level level;
            level.width = width >> l ? width >> l : 1;
            level.height = height >> l ? height >> l : 1;
            level.pitch = (d3d8::row_bytes(format, level.width, block_rows) + 3u) & ~3u;
            level.rows = (level.height + block_rows - 1) / block_rows;
            level.bytes.assign(static_cast<std::size_t>(level.pitch) * level.rows, 0);
            t->levels_.push_back(std::move(level));
        }
        return t;
    }

    std::uint32_t add_ref() override { return ++references_; }
    std::uint32_t release() override {
        const std::uint32_t count = --references_;
        if (!count) {
            if (destroy_hook()) destroy_hook()(this);
            delete this;
        }
        return count;
    }
    // Called before a texture is destroyed (the platform device frees its copy).
    static std::function<void(StateTexture*)>& destroy_hook() {
        static std::function<void(StateTexture*)> hook;
        return hook;
    }
    std::uint32_t level_count() override { return static_cast<std::uint32_t>(levels_.size()); }
    d3d8::HResult level_desc(std::uint32_t level, d3d8::SurfaceDesc& desc) override {
        if (level >= levels_.size()) return d3d8::kInvalidCall;
        const Level& l = levels_[level];
        desc.format = format_;
        desc.type = d3d8::kResourceSurface;
        desc.usage = usage_;
        desc.pool = pool_;
        desc.size = static_cast<std::uint32_t>(l.bytes.size());
        desc.multisample = 0;
        desc.width = l.width;
        desc.height = l.height;
        return d3d8::kOk;
    }
    d3d8::HResult lock_rect(std::uint32_t level, d3d8::LockedRect& locked, std::uint32_t flags) override {
        if (level >= levels_.size()) return d3d8::kInvalidCall;
        locked.pitch = static_cast<std::int32_t>(levels_[level].pitch);
        locked.bits = levels_[level].bytes.data();
        if (!(flags & 0x10)) ++revision;  // not D3DLOCK_READONLY: contents may change
        return d3d8::kOk;
    }
    d3d8::HResult unlock_rect(std::uint32_t level) override {
        return level < levels_.size() ? d3d8::kOk : d3d8::kInvalidCall;
    }
    std::uint32_t format() const { return format_; }
    const std::vector<Level>& levels() const { return levels_; }
    std::uint32_t revision = 0;           // bumped by writable locks
    std::uint32_t platform_name = 0;      // the platform device's copy
    std::uint32_t platform_revision = ~0u;

private:
    std::uint32_t format_, usage_, pool_;
    std::uint32_t references_ = 1;
    std::vector<Level> levels_;
};

// A Direct3D 8 vertex or index buffer kept in memory (zero-filled).
class StateBuffer : public d3d8::Buffer {
public:
    StateBuffer(bool index, std::uint32_t length, std::uint32_t usage, std::uint32_t format_or_fvf, std::uint32_t pool)
        : index_(index), usage_(usage), format_or_fvf_(format_or_fvf), pool_(pool), bytes_(length, 0) {}
    std::uint32_t add_ref() override { return ++references_; }
    std::uint32_t release() override {
        const std::uint32_t count = --references_;
        if (!count) {
            if (destroy_hook()) destroy_hook()(this);
            delete this;
        }
        return count;
    }
    // Called before a buffer is destroyed (the platform device frees its copy).
    static std::function<void(StateBuffer*)>& destroy_hook() {
        static std::function<void(StateBuffer*)> hook;
        return hook;
    }
    bool is_index() const override { return index_; }
    void desc(std::uint32_t words[6]) const override {
        words[0] = index_ ? format_or_fvf_ : d3d8::kFormatVertexData;
        words[1] = index_ ? d3d8::kResourceIndexBuffer : d3d8::kResourceVertexBuffer;
        words[2] = usage_;
        words[3] = pool_;
        words[4] = static_cast<std::uint32_t>(bytes_.size());
        words[5] = index_ ? 0 : format_or_fvf_;
    }
    d3d8::HResult lock(unsigned char*& data, std::uint32_t flags) override {
        data = bytes_.data();
        if (!(flags & 0x10)) ++revision;  // not D3DLOCK_READONLY: contents may change
        return d3d8::kOk;
    }
    d3d8::HResult unlock() override { return d3d8::kOk; }
    const std::vector<unsigned char>& bytes() const { return bytes_; }
    std::uint32_t fvf() const { return index_ ? 0 : format_or_fvf_; }
    std::uint32_t format() const { return index_ ? format_or_fvf_ : d3d8::kFormatVertexData; }
    std::uint32_t revision = 0;  // bumped by writable locks

private:
    bool index_;
    std::uint32_t usage_, format_or_fvf_, pool_;
    std::uint32_t references_ = 1;
    std::vector<unsigned char> bytes_;
};

// A device that keeps Direct3D state without drawing (host tests and the
// platform device's state layer).
class StateDevice : public d3d8::Device {
public:
    std::uint32_t render_state[256] = {};
    std::uint32_t stage_state[8][32] = {};
    d3d8::Texture* texture[8] = {};
    d3d8::Light lights[8];
    std::uint32_t light_enabled[8] = {};
    d3d8::Matrix transforms[512];
    d3d8::Viewport viewport;
    d3d8::LogFont font;
    d3d8::Direct3D* creator = nullptr;  // set by PortableAdapter::create_device

    // Direct3D 8's default states for a device with an automatic depth buffer
    // (only states the platform device reads; ZGP sets most of them itself).
    StateDevice() {
        // Direct3D starts every transform as the identity.
        for (d3d8::Matrix& m : transforms)
            for (int k = 0; k < 16; ++k) m.m[k] = k % 5 == 0 ? 1.0f : 0.0f;
        render_state[7] = 1;    // ZENABLE
        render_state[8] = 3;    // FILLMODE solid
        render_state[9] = 2;    // SHADEMODE Gouraud
        render_state[14] = 1;   // ZWRITEENABLE
        render_state[19] = 2;   // SRCBLEND one
        render_state[20] = 1;   // DESTBLEND zero
        render_state[22] = 3;   // CULLMODE counter-clockwise
        render_state[23] = 4;   // ZFUNC less-equal
        render_state[25] = 8;   // ALPHAFUNC always
        render_state[137] = 1;  // LIGHTING
        render_state[171] = 1;  // BLENDOP add
        for (int s = 0; s < 8; ++s) {
            stage_state[s][1] = s ? 1 : 4;  // COLOROP: modulate on stage 0, disabled after
            stage_state[s][2] = 2;          // COLORARG1 texture
            stage_state[s][3] = 1;          // COLORARG2 current
            stage_state[s][4] = s ? 1 : 2;  // ALPHAOP: select arg 1 on stage 0
            stage_state[s][5] = 2;          // ALPHAARG1 texture
            stage_state[s][6] = 1;          // ALPHAARG2 current
            stage_state[s][13] = 1;         // ADDRESSU wrap
            stage_state[s][14] = 1;         // ADDRESSV wrap
            stage_state[s][16] = 1;         // MAGFILTER point
            stage_state[s][17] = 1;         // MINFILTER point
        }
    }
    void attach_creator(d3d8::Direct3D* d3d) override { creator = d3d; }
    ~StateDevice() override {
        for (d3d8::Texture*& t : texture)
            if (t) { t->release(); t = nullptr; }
        if (stream) stream->release();
        if (index_buffer) index_buffer->release();
    }

    d3d8::HResult get_direct3d(d3d8::Direct3D*& d3d) override {
        d3d = creator;
        if (!d3d) return d3d8::kInvalidCall;
        d3d->add_ref();
        return d3d8::kOk;
    }
    d3d8::HResult get_device_caps(d3d8::Caps& caps) override {
        return creator ? creator->device_caps(0, 1, caps) : d3d8::kInvalidCall;
    }
    std::uint32_t references = 1;
    std::uint32_t add_ref() override { return ++references; }
    std::uint32_t release() override { return references > 1 ? --references : references; }  // owned by the renderer
    d3d8::HResult get_creation_parameters() override { return d3d8::kOk; }
    d3d8::HResult get_display_mode(d3d8::DisplayMode& mode) override {
        return creator ? creator->adapter_display_mode(0, mode) : d3d8::kInvalidCall;
    }
    d3d8::HResult create_texture(std::uint32_t w, std::uint32_t h, std::uint32_t levels, std::uint32_t usage,
                                 std::uint32_t format, std::uint32_t pool, d3d8::Texture*& t) override {
        t = nullptr;
        if (usage != 0 || pool > 2 || !reference_texture_format(format)) return d3d8::kInvalidCall;
        t = StateTexture::create(w, h, levels, usage, format, pool);
        return t ? d3d8::kOk : d3d8::kInvalidCall;
    }
    d3d8::HResult create_vertex_buffer(std::uint32_t length, std::uint32_t usage, std::uint32_t fvf,
                                       std::uint32_t pool, d3d8::Buffer*& b) override {
        b = nullptr;
        if (!length || pool > 2) return d3d8::kInvalidCall;
        b = new StateBuffer(false, length, usage, fvf, pool);
        return d3d8::kOk;
    }
    d3d8::HResult create_index_buffer(std::uint32_t length, std::uint32_t usage, std::uint32_t format,
                                      std::uint32_t pool, d3d8::Buffer*& b) override {
        b = nullptr;
        if (!length || pool > 2 || (format != d3d8::kFormatIndex16 && format != d3d8::kFormatIndex32))
            return d3d8::kInvalidCall;
        b = new StateBuffer(true, length, usage, format, pool);
        return d3d8::kOk;
    }
    d3d8::HResult set_texture_stage_state(std::uint32_t s, std::uint32_t t, std::uint32_t v) override {
        if (s >= 8 || t >= 32) return d3d8::kInvalidCall;
        stage_state[s][t] = v;
        return d3d8::kOk;
    }
    d3d8::HResult set_texture(std::uint32_t s, d3d8::Texture* t) override {
        if (s >= 8) return d3d8::kInvalidCall;
        if (t) t->add_ref();  // the device holds a reference while bound
        if (texture[s]) texture[s]->release();
        texture[s] = t;
        return d3d8::kOk;
    }
    d3d8::HResult set_render_state(std::uint32_t s, std::uint32_t v) override {
        if (s >= 256) return d3d8::kInvalidCall;
        render_state[s] = v;
        return d3d8::kOk;
    }
    d3d8::HResult light_enable(std::uint32_t i, std::uint32_t e) override {
        if (i >= 8) return d3d8::kInvalidCall;
        light_enabled[i] = e;
        return d3d8::kOk;
    }
    d3d8::HResult set_light(std::uint32_t i, const d3d8::Light& light) override {
        if (i >= 8) return d3d8::kInvalidCall;
        lights[i] = light;
        return d3d8::kOk;
    }
    d3d8::HResult set_transform(std::uint32_t t, const d3d8::Matrix& m) override {
        if (t >= 512) return d3d8::kInvalidCall;
        transforms[t] = m;
        return d3d8::kOk;
    }
    d3d8::HResult get_transform(std::uint32_t t, d3d8::Matrix& m) override {
        if (t >= 512) return d3d8::kInvalidCall;
        m = transforms[t];
        return d3d8::kOk;
    }
    d3d8::HResult set_viewport(const d3d8::Viewport& v) override {
        viewport = v;
        return d3d8::kOk;
    }
    d3d8::HResult get_viewport(d3d8::Viewport& v) override {
        v = viewport;
        return d3d8::kOk;
    }
    // Frame and drawing calls only keep counters here; the OpenGL ES device
    // draws them.
    std::uint32_t scenes = 0, presents = 0, draws = 0, vertex_shader = 0;
    bool in_scene = false;
    d3d8::HResult clear(std::uint32_t count, const d3d8::Rect* rects, std::uint32_t, std::uint32_t, float,
                        std::uint32_t) override {
        return (count && !rects) || (!count && rects) ? d3d8::kInvalidCall : d3d8::kOk;
    }
    d3d8::HResult begin_scene() override {
        if (in_scene) return d3d8::kInvalidCall;
        in_scene = true;
        ++scenes;
        return d3d8::kOk;
    }
    d3d8::HResult end_scene() override {
        if (!in_scene) return d3d8::kInvalidCall;
        in_scene = false;
        return d3d8::kOk;
    }
    d3d8::HResult present(const d3d8::Rect*, const d3d8::Rect*) override {
        ++presents;
        return d3d8::kOk;
    }
    d3d8::HResult reset(d3d8::PresentParameters&) override { return d3d8::kOk; }
    d3d8::HResult set_vertex_shader(std::uint32_t handle) override {
        vertex_shader = handle;
        return d3d8::kOk;
    }
    d3d8::HResult draw_primitive_up(std::uint32_t type, std::uint32_t primitives, const void* data,
                                    std::uint32_t) override {
        if (!data || !primitives || !d3d8::vertex_count(type, primitives)) return d3d8::kInvalidCall;
        ++draws;
        return d3d8::kOk;
    }
    d3d8::HResult create_text_font(const d3d8::LogFont& f) override {
        font = f;
        return d3d8::kOk;
    }
    float material[17] = {};
    d3d8::Buffer* stream = nullptr;
    std::uint32_t stream_stride = 0;
    d3d8::Buffer* index_buffer = nullptr;
    std::uint32_t base_vertex = 0;
    d3d8::HResult set_material(const float m[17]) override {
        if (!m) return d3d8::kInvalidCall;
        std::memcpy(material, m, sizeof(material));
        return d3d8::kOk;
    }
    // D3DXMatrixMultiply order, as the runtime multiplies: matrix * current.
    d3d8::HResult multiply_transform(std::uint32_t t, const d3d8::Matrix& m) override;
    d3d8::HResult set_stream_source(std::uint32_t s, d3d8::Buffer* b, std::uint32_t stride) override {
        if (s != 0) return d3d8::kInvalidCall;
        if (b) b->add_ref();  // the device holds a reference while bound
        if (stream) stream->release();
        stream = b;
        stream_stride = stride;
        return d3d8::kOk;
    }
    d3d8::HResult set_indices(d3d8::Buffer* b, std::uint32_t base) override {
        if (b) b->add_ref();
        if (index_buffer) index_buffer->release();
        index_buffer = b;
        base_vertex = base;
        return d3d8::kOk;
    }
    d3d8::HResult draw_indexed_primitive(std::uint32_t type, std::uint32_t, std::uint32_t vertices,
                                         std::uint32_t, std::uint32_t primitives) override {
        if (!stream || !index_buffer || !vertices || !primitives || !d3d8::vertex_count(type, primitives))
            return d3d8::kInvalidCall;
        ++draws;
        return d3d8::kOk;
    }
};

inline d3d8::HResult PortableAdapter::create_device(std::uint32_t adapter, std::uint32_t type, std::uint32_t window,
                                                    std::uint32_t, d3d8::PresentParameters& p,
                                                    d3d8::Device*& device) {
    device = nullptr;
    if (adapter != 0 || type != 1 || !window || !p.windowed || p.format != d3d8::kFormatX8R8G8B8)
        return d3d8::kInvalidCall;
    device = factory_(p);
    if (!device) return d3d8::kNotAvailable;
    device->attach_creator(this);
    add_ref();  // the device references its Direct3D object
    return d3d8::kOk;
}

// ---------------------------------------------------------------------------
// ZGP renderer state and commands.

constexpr std::uint32_t kLightDirectional = 3;
constexpr std::uint32_t kNoFog = 0xffffffffu;

struct Renderer {
    // Library manager (0x1005f910).
    d3d8::Direct3D* d3d = nullptr;                  // +0x00
    d3d8::Device* device = nullptr;                 // +0x04 (also 0x1005f914)
    d3d8::PresentParameters present;                // +0x08 (0x1005f918 = width, 0x1005f91c = height)
    std::uint32_t back_format = 0;                  // +0x3c
    std::uint32_t window = 0;                       // +0x1c0
    std::uint32_t alpha_blend = 0;                  // +0x1c4
    std::int32_t blend_mode_cache = -1;             // +0x1c8
    std::int32_t blend_mode = 0;                    // +0x1d4
    float blend_alpha = 0.0f;                       // +0x1d8
    std::uint32_t blend_format = 0;                 // +0x1dc
    std::int32_t parameters[11] = {};               // 0x1005fb00
    // Camera lights at camera +0x4c + i * 0x68; camera fog at +0x40/+0x44/+0x48.
    d3d8::Light lights[16];
    std::uint32_t fog_color = kNoFog;               // camera +0x48
    float fog_start = 0.0f, fog_end = 0.0f;         // camera +0x40, +0x44
    // 2D object (0x1005fb30): logical size and viewport rectangle (the
    // viewport width and height are its +0x64 and +0x68), and its font.
    std::int32_t logical_width = 0, logical_height = 0;  // +0x7c, +0x80
    d3d8::Viewport viewport;
    d3d8::LogFont font;                                  // +0x1c LOGFONTA
    std::uint32_t color = 0xffffffffu;                   // +0x58 2D drawing color (ARGB; constructor 0x10001910)
    std::uint32_t color4 = 0xffffffffu;                  // 0x1005bd2c, set by setcolor4
    std::int32_t blend_command = 0;                      // 0x100605c8, set by setblend
    // Texture load color key (0x100605c4), set by textrans.
    std::uint32_t texture_color_key = 0;
    // Surface table entries (setmaxsurf, 0x21c bytes each at 0x100605d0).
    struct Surface {
        d3d8::Texture* texture = nullptr;  // +0x000
        std::string path;                  // +0x004, as passed to texload
        std::string name;                  // +0x108, PathStripPathA(path)
        std::uint32_t name_length = 0;     // +0x20c, strlen + 1 of the last copy
        std::uint32_t color_key = 0;       // +0x210
        std::uint32_t format = 0;          // +0x214
        std::uint32_t mip_levels = 0;      // +0x218
    };
    std::vector<Surface> surfaces;
    // Character entries (first setmaxunit table, 0x30 bytes each at 0x100605dc).
    struct Character {
        std::int32_t surface = 0;             // +0x00 surface slot (a pointer in the original)
        float width = 0.0f, height = 0.0f;    // +0x04, +0x08
        std::int32_t diagonal = 0;            // +0x0c
        std::int32_t frame = 0;               // +0x10 character whose image is used (the entry itself)
        std::int32_t field14 = 0;             // +0x14
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0; // +0x18..+0x24
        std::vector<std::array<float, 6>> triangles;  // +0x28 count, +0x2c array of three points
    };
    std::vector<Character> characters;
    // chrsize defaults (0x1005bd78, 0x1005bd7c).
    std::int32_t character_width = 32, character_height = 32;
    // Sprite entries (second setmaxunit table, 0x98 bytes at 0x100605e0), kept
    // as the original's 38 DWORDs. Pointer fields hold table indices instead:
    // +0x28 and +0x2c characters, +0x30 and +0x40 sprites; +0x94 (a triangle
    // buffer) is 1 when allocated.
    struct Sprite {
        std::uint32_t w[38] = {};
    };
    std::vector<Sprite> sprites;
    // meshsz: size of the next generated mesh (0x1005f900..0x1005f908).
    float mesh_size[3] = {0.0f, 0.0f, 0.0f};
    // pos3: the start of the next line3d (0x1005faf0..0x1005faf8).
    float line_start[3] = {0.0f, 0.0f, 0.0f};
    // getfrt: calls since the last report (0x100605e8), the reported count
    // (0x100605e4) and GetTickCount at that report (0x100605f0). tick_count
    // replaces GetTickCount when set (tests).
    std::int32_t frame_count = 0, frame_rate = 0;
    std::uint32_t frame_report_tick = 0;
    std::function<std::uint32_t()> tick_count;
    // Mesh entries (fourth setmaxunit table, 0x40 bytes at 0x100605d8,
    // released by 0x10009c10 and filled by the mesh builders).
    struct Material {
        float m[17] = {};  // D3DMATERIAL8: diffuse, ambient, specular, emissive (RGBA), power
    };
    struct Mesh {
        std::uint32_t type = 0;                  // +0x00 (3 = generated box)
        std::uint32_t field04 = 0;               // +0x04
        float hit[3] = {0.0f, 0.0f, 0.0f};       // +0x08..+0x10
        std::uint32_t fvf = 0;                   // +0x14
        std::uint32_t vertex_size = 0;           // +0x18
        std::uint32_t attribute_count = 0;       // +0x1c
        std::uint32_t vertex_count = 0;          // +0x20
        std::uint32_t triangle_count = 0;        // +0x24
        // +0x28 D3DXATTRIBUTERANGE (id, face start, face count, vertex start, vertex count) per attribute.
        std::vector<std::array<std::uint32_t, 5>> ranges;
        std::vector<Material> materials;         // +0x2c
        d3d8::Buffer* indices = nullptr;         // +0x30
        d3d8::Buffer* vertices = nullptr;        // +0x34
        // +0x38 the mesh's own surfaces, one per attribute (0x10009780
        // allocates them for every mesh; loadx loads the material textures
        // into them).
        std::vector<Surface> own_surfaces;
        // +0x3c per-attribute surface pointers. The generated meshes point into
        // the global surface table (meshbox's slot, or slot 0 without one);
        // stored here as that slot index, -1 for null.
        std::vector<std::int32_t> textures;
    };
    std::vector<Mesh> meshes;
    // Object entries (third setmaxunit table, 0x12c bytes at 0x100605d4), kept
    // as the original's 75 DWORDs. +0x7c (a mesh pointer) holds the mesh index.
    struct Object {
        std::uint32_t w[75] = {};
    };
    std::vector<Object> objects;
    // Animation-library slots (0x1005f950, 96 bytes each; only ids 0-3 are
    // distinct) and the selected id (0x10060604). anilibload fills a slot.
    struct AnimationLibrary {
        bool loaded = false;
        std::string path;
        csmesh::Library library;
    };
    AnimationLibrary animation_libraries[4];
    std::int32_t animation_selected = 0;
    // Off-screen removal (0x1005f138) and its rectangle (0x1005f13c); no command
    // the game imports enables it.
    bool sprite_clip = false;
    std::int32_t sprite_clip_rect[4] = {0, 0, 0, 0};
    // Object clip box (0x1005ef68: enabled flag, then min xyz and max xyz);
    // objdraw removes flagged objects outside it. Set by clipset.
    bool object_clip = false;
    float object_clip_box[6] = {0, 0, 0, 0, 0, 0};
    // Reads a file for D3DXCreateTextureFromFileExA (CreateFileA relative to
    // the working directory on Windows; the asset resolver on iOS).
    std::function<bool(const std::string& path, std::vector<unsigned char>& data)> read_file;
};

inline Renderer& renderer() {
    static Renderer instance;
    return instance;
}

// Names known to ZGP's format table (0x10015f10); only these are probed.
inline bool format_named(std::uint32_t f) {
    return (f >= 20 && f <= 30) || f == 40 || f == 41 || (f >= 50 && f <= 52) || (f >= 60 && f <= 65) ||
           f == 70 || f == 71 || f == 73 || f == 75 || f == 77 || f == 79 || f == 80 || (f >= 100 && f <= 102);
}

// 0x100155b0: cached alpha-blend helper (mode, alpha, format).
inline void blend_state(Renderer& r, std::int32_t mode, float alpha, std::uint32_t format) {
    CP2_ZGP_STRICT_FP
    std::uint32_t alpha_bits;
    std::memcpy(&alpha_bits, &alpha, 4);
    if (mode == -1) mode = r.blend_mode;
    if (alpha_bits == 0xbf800000u) alpha = r.blend_alpha;
    if (format == 0) format = r.blend_format;
    r.blend_mode = mode;
    r.blend_alpha = alpha;
    r.blend_format = format;
    if (r.blend_alpha < 0.0f) r.blend_alpha = 0.0f;
    if (r.blend_alpha > 1.0f) r.blend_alpha = 1.0f;
    const bool alpha_format = format == 21 || format == 25 || format == 26 || format == 29 || format == 40;
    if (alpha_format) {
        if (!r.alpha_blend) { r.alpha_blend = 1; r.device->set_render_state(27, 1); }
    } else if (mode == 0) {
        if (!(alpha < 1.0f) && r.alpha_blend == 1) { r.alpha_blend = 0; r.device->set_render_state(27, 0); }
        if (alpha < 1.0f && !r.alpha_blend) { r.alpha_blend = 1; r.device->set_render_state(27, 1); }
    } else if (!r.alpha_blend) {
        r.alpha_blend = 1;
        r.device->set_render_state(27, 1);
    }
    if (mode != r.blend_mode_cache) {
        if (mode == 0) {
            r.device->set_render_state(171, 1); r.device->set_render_state(19, 5); r.device->set_render_state(20, 6);
        } else if (mode == 1) {
            r.device->set_render_state(171, 1); r.device->set_render_state(19, 5); r.device->set_render_state(20, 2);
        } else if (mode == 2) {
            r.device->set_render_state(171, 3); r.device->set_render_state(19, 5); r.device->set_render_state(20, 2);
        }
        r.blend_mode_cache = mode;
    }
}

// 0x10008800: camera update.
inline void camera_update(Renderer& r) {
    CP2_ZGP_STRICT_FP
    const Camera& c = state().camera;
    const Vec3 eye{c.eye[0], c.eye[1], c.eye[2]};
    const Vec3 at{c.eye[0] + c.direction[0], c.eye[1] + c.direction[1], c.eye[2] + c.direction[2]};
    const Vec3 up{c.up[0], c.up[1], c.up[2]};
    const d3d8::Matrix view = look_at(eye, at, up);
    const float fov = c.fov * 3.14159274101257f / 180.0f;
    const d3d8::Matrix projection = perspective(fov, c.aspect, c.near_plane, c.far_plane);
    r.device->set_transform(2, view);
    r.device->set_transform(3, projection);
}

// 0x10008ec0: store and apply the selected light.
inline void light_apply(Renderer& r, std::uint32_t type, const float diffuse[4], const float ambient[4],
                        const float specular[4]) {
    const int index = state().camera.light;
    d3d8::Light& light = r.lights[index];
    r.device->light_enable(static_cast<std::uint32_t>(index), type ? 1 : 0);
    light.words[0] = type;
    for (int i = 0; i < 4; ++i) {
        light.set(1 + i, diffuse[i]);
        light.set(9 + i, ambient[i]);
        light.set(5 + i, specular[i]);
    }
    r.device->set_light(static_cast<std::uint32_t>(index), light);
    const bool no_specular = specular[0] == 0.0f && specular[1] == 0.0f && specular[2] == 0.0f;
    r.device->set_render_state(29, no_specular ? 0 : 1);
}

// 0x10009020: light from 0xRRGGBB colors. The original leaves each color's
// alpha uninitialized (Direct3D ignores it); it is 0 here.
inline void light_colors(Renderer& r, std::uint32_t type, std::uint32_t diffuse, std::uint32_t ambient,
                         std::uint32_t specular) {
    CP2_ZGP_STRICT_FP
    auto unpack = [](std::uint32_t c, float out[4]) {
        out[0] = static_cast<float>((c >> 16) & 0xff) / 255.0f;
        out[1] = static_cast<float>((c >> 8) & 0xff) / 255.0f;
        out[2] = static_cast<float>(c & 0xff) / 255.0f;
        out[3] = 0.0f;
    };
    float d[4], a[4], s[4];
    unpack(diffuse, d);
    unpack(ambient, a);
    unpack(specular, s);
    light_apply(r, type, d, a, s);
}

// 0x10009220 / 0x100091c0: light direction.
inline void light_direction(Renderer& r, float x, float y, float z) {
    const int index = state().camera.light;
    const Vec3 v = normalize(Vec3{x, y, z});
    d3d8::Light& light = r.lights[index];
    light.set(16, v.x);
    light.set(17, v.y);
    light.set(18, v.z);
    r.device->set_light(static_cast<std::uint32_t>(index), light);
}

// 0x100093d0: fog (color -1 disables).
inline void fog(Renderer& r, std::uint32_t color, float start, float end) {
    r.fog_color = color;
    if (color == kNoFog) {
        r.device->set_render_state(28, 0);
        return;
    }
    if (start == 0.0f && end == 0.0f) {
        start = state().camera.near_plane;
        end = state().camera.far_plane;
    }
    r.fog_start = start;
    r.fog_end = end;
    r.device->set_render_state(28, 1);
    r.device->set_render_state(34, color);
    r.device->set_render_state(140, 3);
    r.device->set_render_state(36, d3d8::bits(start));
    r.device->set_render_state(37, d3d8::bits(end));
}

// 0x10001ab0: viewport from logical coordinates (right/bottom edges).
inline void set_viewport(Renderer& r, std::int32_t x, std::int32_t y, std::int32_t right, std::int32_t bottom) {
    const std::int32_t w = static_cast<std::int32_t>(r.present.width);
    const std::int32_t h = static_cast<std::int32_t>(r.present.height);
    if ((x | y | right | bottom) == 0) {
        x = 0;
        y = 0;
        right = w;
        bottom = h;
    }
    const std::int32_t top = y * h / r.logical_height;
    const std::int32_t left = x * w / r.logical_width;
    const std::int32_t far_x = right * w / r.logical_width;
    const std::int32_t far_y = bottom * h / r.logical_height;
    std::int32_t width = far_x - left, height = far_y - top;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    r.viewport.x = static_cast<std::uint32_t>(left);
    r.viewport.y = static_cast<std::uint32_t>(top);
    r.viewport.width = static_cast<std::uint32_t>(width);
    r.viewport.height = static_cast<std::uint32_t>(height);
    r.viewport.min_z = 0.0f;
    r.viewport.max_z = 1.0f;
    r.device->set_viewport(r.viewport);
}

// _createdx@16 (0x19f30 -> 0x10014500): Direct3DCreate8(120); error 2 on failure.
inline int createdx(d3d8::Direct3D* d3d) {
    renderer().d3d = d3d;
    return d3d ? 0 : -2;
}

// 0x10014580: device creation. Returns false on failure (the original shows
// a dialog). Only the windowed path the game uses is implemented.
inline bool create_device(Renderer& r, const std::int32_t a[11], const char*& failure) {
    r.window = static_cast<std::uint32_t>(a[0]);
    if (!r.d3d) { failure = "Direct3D was not created"; return false; }
    std::uint32_t back = static_cast<std::uint32_t>(a[1]);
    std::uint32_t depth = static_cast<std::uint32_t>(a[2]);
    if (back == 1) back = r.back_format;
    std::uint32_t type = static_cast<std::uint32_t>(a[5]);
    if (type != 1 && type != 2) type = 1;
    const std::uint32_t adapter = static_cast<std::uint32_t>(a[6]);
    std::uint32_t behavior = static_cast<std::uint32_t>(a[7]);
    if (!behavior) behavior = 0x20;
    std::uint32_t swap = static_cast<std::uint32_t>(a[8]);
    if (!swap) swap = 3;
    d3d8::PresentParameters p;
    d3d8::DisplayMode mode;
    r.d3d->adapter_display_mode(adapter, mode);
    r.d3d->adapter_identifier(adapter, 0);
    if (back != 0) { failure = "full-screen device creation is not implemented"; return false; }
    back = mode.format;
    const std::uint32_t windowed = 1;
    p.width = static_cast<std::uint32_t>(a[3]);
    p.height = static_cast<std::uint32_t>(a[4]);
    p.back_buffer_count = 1;
    p.auto_depth_stencil = 1;
    p.swap_effect = swap;
    auto back_supported = [&](std::uint32_t format) {  // 0x10015a70
        d3d8::DisplayMode current;
        r.d3d->adapter_display_mode(adapter, current);
        return r.d3d->check_device_type(adapter, type, current.format, format, windowed) == d3d8::kOk;
    };
    if (!back_supported(back)) {
        back = 0;
        for (std::uint32_t f = 0x14; f < 0x67; ++f)
            if (format_named(f) && back_supported(f)) { back = f; break; }
    }
    if (!back) { failure = "no usable back-buffer format"; return false; }
    p.format = back;
    auto depth_supported = [&](std::uint32_t format) {  // 0x10015ad0
        d3d8::DisplayMode current;
        r.d3d->adapter_display_mode(adapter, current);
        return r.d3d->check_device_format(adapter, type, back, d3d8::kUsageDepthStencil, d3d8::kResourceSurface,
                                          format) == d3d8::kOk;
    };
    if (!depth_supported(depth)) {
        depth = 0;
        for (std::uint32_t f = 0x14; f < 0x67; ++f)
            if (format_named(f) && depth_supported(f)) depth = f;  // the last supported format wins
    }
    if (!depth) { failure = "no usable depth-buffer format"; return false; }
    p.depth_format = depth;
    p.windowed = windowed;
    d3d8::Device* device = nullptr;
    if (r.d3d->create_device(adapter, type, r.window, behavior, p, device) != d3d8::kOk || !device) {
        failure = "CreateDevice failed";
        return false;
    }
    r.device = device;
    static const std::uint32_t stage[9][2] = {{1, 4}, {2, 2}, {3, 0}, {4, 4}, {5, 2}, {6, 0}, {15, 0}, {13, 1}, {14, 1}};
    for (const auto& s : stage) device->set_texture_stage_state(0, s[0], s[1]);
    device->set_texture(0, nullptr);
    static const std::uint32_t states[15][2] = {{27, 0}, {26, 0}, {137, 0}, {29, 0}, {7, 1}, {22, 1}, {15, 1}, {24, 0},
                                                {25, 5}, {139, 0}, {141, 1}, {145, 0}, {147, 0}, {146, 0}, {148, 0}};
    for (const auto& s : states) device->set_render_state(s[0], s[1]);
    r.back_format = back;
    r.present = p;
    r.blend_mode_cache = -1;
    r.alpha_blend = 0;
    blend_state(r, 0, 1.0f, 0x16);
    return true;
}

// 0x10001c70: fill the 2D object's LOGFONTA and recreate the D3DX font
// (0x10001c10 releases the previous font first). Sizes are scaled from the
// back buffer to the current viewport with integer division, as in the
// original; before the first viewport is set that makes them 0.
inline void font_create(Renderer& r, const std::string& face, std::int32_t height, std::int32_t width,
                        std::int32_t weight, std::uint8_t italic) {
    if (!r.device || !r.logical_width || !r.logical_height) return;  // the original divides by zero
    d3d8::LogFont f;
    f.height = static_cast<std::int32_t>(static_cast<std::uint32_t>(height) * r.viewport.width) / r.logical_width;
    f.width = static_cast<std::int32_t>(static_cast<std::uint32_t>(width) * r.viewport.height) / r.logical_height;
    f.weight = weight;
    f.italic = italic;
    f.charset = 1;  // DEFAULT_CHARSET
    f.face = face;
    r.font = f;
    r.device->create_text_font(f);
}

// CP932 "ＭＳ ゴシック" (0x1005b030), the font createdevm creates.
inline const char* default_font_face() { return "\x82\x6c\x82\x72 \x83\x53\x83\x56\x83\x62\x83\x4e"; }

// _createdevm@16 (0x10019f60): a points to the game's 11-DWORD array.
inline int createdevm(std::int32_t a[11], const char*& failure) {
    Renderer& r = renderer();
    if (state().bmscr_set) a[0] = static_cast<std::int32_t>(state().bmscr_window);
    if (!create_device(r, a, failure)) return -3;
    // Direct3D 8 (no FPU_PRESERVE) leaves the x87 unit in single precision.
    x87_single_precision() = true;
    std::memcpy(r.parameters, a, sizeof(r.parameters));
    // 0x10008070: camera, light and transforms.
    state().camera.light = 0;
    light_colors(r, kLightDirectional, 0xffffff, 0x666666, 0);
    light_direction(r, 64.0f, -128.0f, 256.0f);
    camera_update(r);
    // 0x10001a60: text font and full-screen viewport on the 2D object.
    r.logical_width = static_cast<std::int32_t>(r.present.width);
    r.logical_height = static_cast<std::int32_t>(r.present.height);
    font_create(r, default_font_face(), 16, 8, 0, 0);
    set_viewport(r, 0, 0, 0, 0);
    setmaxunit(256, 256, 128, 32);
    setmaxsurf(16, 16);
    return 0;
}

// Angle wrap used by the camera (0x10008594..): result in [0, 360).
inline float wrap_degrees(float v) {
    CP2_ZGP_STRICT_FP
    if (!(v < 0.0f) && v < 360.0f) return v;
    if (-360.0f < v && v < 0.0f) return v + 360.0f;
    if (!(720.0f <= v) && !(v < 360.0f)) return v - 360.0f;
    v = static_cast<float>(std::fmod(static_cast<double>(v), 360.0));
    if (v < 0.0f) v = v + 360.0f;
    return v;
}

// 0x10008490: point the camera at a target.
inline void camera_target(Renderer& r, float tx, float ty, float tz) {
    CP2_ZGP_STRICT_FP
    Camera& c = state().camera;
    c.direction[0] = tx - c.eye[0];
    c.direction[1] = ty - c.eye[1];
    c.direction[2] = tz - c.eye[2];
    const float at_z = c.eye[2] + c.direction[2];
    const float at_y = c.eye[1] + c.direction[1];
    const float at_x = c.eye[0] + c.direction[0];
    const float dx = c.eye[0] - at_x;
    const float dz = c.eye[2] - at_z;
    const float dy = c.eye[1] - at_y;
    const float horizontal = std::sqrt(dx * dx + dz * dz);
    const double two_pi = 6.28318548202515;  // float constant 0x100531c8
    const float pitch = static_cast<float>(std::atan2(static_cast<double>(dy), static_cast<double>(horizontal)) * 360.0);
    c.angles[0] = pitch / static_cast<float>(two_pi);
    const float yaw = static_cast<float>(
        std::atan2(static_cast<double>(at_x - c.eye[0]), static_cast<double>(at_z - c.eye[2])) * 360.0);
    c.angles[1] = yaw / static_cast<float>(two_pi);
    c.angles[0] = wrap_degrees(c.angles[0]);
    c.angles[1] = wrap_degrees(c.angles[1]);
    c.angles[2] = 0.0f;
    c.up[0] = 0.0f;
    c.up[1] = 1.0f;
    c.up[2] = 0.0f;
    if (c.direction[0] == 0.0f && c.direction[1] != 0.0f && c.direction[2] == 0.0f)
        c.direction[0] = c.direction[1] / 256.0f + c.direction[0];
    camera_update(r);
}

// _cat@16 (0x1001b1a0): target in ZGP units (divided by the standard scale).
inline int cat(std::int32_t x, std::int32_t y, std::int32_t z) {
    CP2_ZGP_STRICT_FP
    Renderer& r = renderer();
    if (!r.device) return 0;
    const float s = state().standard_scale_a;
    camera_target(r, static_cast<float>(x) / s, static_cast<float>(y) / s, static_cast<float>(z) / s);
    return 0;
}

// _setfovy@16 (0x1001b070): stored; applied at the next camera update.
inline int setfovy(std::int32_t degrees) {
    state().camera.fov = static_cast<float>(degrees);
    return 0;
}

// _setview@16 (0x1001a860).
inline int setview(std::int32_t x, std::int32_t y, std::int32_t right, std::int32_t bottom) {
    Renderer& r = renderer();
    if (!r.device) return 0;
    set_viewport(r, x, y, right, bottom);
    return 0;
}

// _lightdir@16 (0x1001b4f0).
inline int lightdir(std::int32_t x, std::int32_t y, std::int32_t z) {
    CP2_ZGP_STRICT_FP
    Renderer& r = renderer();
    if (!r.device) return 0;
    const float s = state().standard_scale_a;
    light_direction(r, static_cast<float>(x) / s, static_cast<float>(y) / s, static_cast<float>(z) / s);
    return 0;
}

// _lightamb@16 (0x1001b670): D3DRS_AMBIENT.
inline int lightamb(std::int32_t color) {
    Renderer& r = renderer();
    if (!r.device) return 0;
    r.device->set_render_state(139, static_cast<std::uint32_t>(color));
    return 0;
}

// _set3d@16 (0x1001b010 -> 0x10008a00). The second argument is unused.
inline int set3d(std::int32_t a, std::int32_t, std::int32_t near_value, std::int32_t far_value) {
    CP2_ZGP_STRICT_FP
    Renderer& r = renderer();
    if (!r.device) return 0;  // the original dereferences a null device
    const float near_plane = static_cast<float>(near_value) / state().standard_scale_a;
    const float far_plane = static_cast<float>(far_value) / state().standard_scale_a;
    r.device->set_texture(0, nullptr);
    r.device->set_texture_stage_state(0, 17, static_cast<std::uint32_t>(a));
    r.device->set_texture_stage_state(0, 16, static_cast<std::uint32_t>(a));
    r.device->set_texture_stage_state(0, 18, static_cast<std::uint32_t>(a));
    r.device->set_render_state(7, 1);
    r.device->set_render_state(137, 0);
    fog(r, r.fog_color, r.fog_start, r.fog_end);
    blend_state(r, 0, 1.0f, 0);
    if (near_plane == 0.0f || far_plane == 0.0f) return 0;
    state().camera.near_plane = near_plane;
    state().camera.far_plane = far_plane;
    camera_update(r);
    return 0;
}

// _setfont@16 (0x1001ae40): size, face, style (bit 0 bold = weight 600,
// bit 1 italic); the width is half the size.
inline int setfont(std::int32_t size, const std::string& face, std::int32_t style) {
    font_create(renderer(), face, size, size / 2, (style & 1) * 600, static_cast<std::uint8_t>((style >> 1) & 1));
    return 0;
}

// ---------------------------------------------------------------------------
// Surfaces (textures). Commands check the index against setmaxsurf's count;
// errors go through ZGP's error helper (0x10019640), which returns -code.

inline Renderer::Surface* surface(std::int32_t index) {
    const Table& t = state().surfaces;
    if (index < 1 || index >= t.count) return nullptr;
    Renderer& r = renderer();
    if (r.surfaces.size() != static_cast<std::size_t>(t.count)) r.surfaces.resize(static_cast<std::size_t>(t.count));
    return &r.surfaces[static_cast<std::size_t>(index)];
}

// 0x10013970: release the slot's texture.
inline void surface_release(Renderer::Surface& s) {
    if (s.texture) {
        s.texture->release();
        s.texture = nullptr;
    }
}

// PathStripPathA: the part after the last path separator.
inline std::string strip_path(const std::string& path) {
    const std::size_t at = path.find_last_of("\\/:");
    return at == std::string::npos ? path : path.substr(at + 1);
}

// 0x10013a60: D3DXCreateTextureFromFileExA(device, path, 0, 0, mips, 0,
// UNKNOWN, MANAGED, LINEAR, LINEAR, key, null, null) and the level-0 format.
inline d3d8::HResult surface_load(Renderer& r, Renderer::Surface& s, const std::string& path, std::uint32_t key,
                                  std::uint32_t mips) {
    surface_release(s);
    s.color_key = key;
    s.mip_levels = mips;
    s.path = path;
    s.name = strip_path(path);
    s.name_length = static_cast<std::uint32_t>(s.name.size() + 1);
    std::vector<unsigned char> data;
    if (!r.read_file || !r.read_file(path, data)) return 0x80070002u;  // ERROR_FILE_NOT_FOUND
    if (data.empty()) return d3d8::kInvalidCall;
    d3d8::Texture* texture = nullptr;
    const d3d8::HResult hr = d3dx::create_texture_from_memory(r.device, data.data(), data.size(), 0, 0, mips, 0, 0,
                                                             d3d8::kPoolManaged, 3, 3, key, texture);
    if (d3d8::failed(hr)) return hr;
    s.texture = texture;
    d3d8::SurfaceDesc desc;
    texture->level_desc(0, desc);
    s.format = desc.format;
    return hr;
}

// _textrans@16 (0x1001aa60): -1 disables the color key, any other value
// becomes an opaque key.
inline int textrans(std::int32_t color) {
    renderer().texture_color_key = color == -1 ? 0 : static_cast<std::uint32_t>(color) | 0xff000000u;
    return 0;
}

// _texload@16 (0x1001a930).
inline int texload(std::int32_t index, const std::string& path, std::int32_t mips) {
    Renderer::Surface* s = surface(index);
    if (!s) return -12;
    Renderer& r = renderer();
    if (!r.device) return -10;  // the original dereferences a null device
    if (d3d8::failed(surface_load(r, *s, path, r.texture_color_key, static_cast<std::uint32_t>(mips)))) return -10;
    return 0;
}

// _texture@16 (0x1001a8c0 -> 0x100139e0): an empty texture of one level.
inline int texture(std::int32_t index, std::int32_t width, std::int32_t height, std::int32_t format) {
    Renderer::Surface* s = surface(index);
    if (!s) return -12;
    Renderer& r = renderer();
    if (!r.device) return -10;
    surface_release(*s);
    s->format = static_cast<std::uint32_t>(format);
    d3d8::Texture* t = nullptr;
    const d3d8::HResult hr = d3dx::create_texture(r.device, static_cast<std::uint32_t>(width),
                                                  static_cast<std::uint32_t>(height), 1, 0,
                                                  static_cast<std::uint32_t>(format), d3d8::kPoolManaged, t);
    if (d3d8::failed(hr)) return -10;
    s->texture = t;
    d3d8::SurfaceDesc desc;
    t->level_desc(0, desc);
    s->format = desc.format;
    return 0;
}

// _texinfoex@16 (0x1001ab10): level 0's format, type, pool, size, width and
// height into six DWORDs.
inline int texinfoex(std::int32_t out[6], std::int32_t index) {
    Renderer::Surface* s = surface(index);
    if (!s) return -12;
    if (!s->texture) return -13;
    d3d8::SurfaceDesc desc;
    s->texture->level_desc(0, desc);
    out[0] = static_cast<std::int32_t>(desc.format);
    out[1] = static_cast<std::int32_t>(desc.type);
    out[2] = static_cast<std::int32_t>(desc.pool);
    out[3] = static_cast<std::int32_t>(desc.size);
    out[4] = static_cast<std::int32_t>(desc.width);
    out[5] = static_cast<std::int32_t>(desc.height);
    return 0;
}

// ---------------------------------------------------------------------------
// Frames and 2D drawing.

// _zclear@16 (0x1001a6c0 -> 0x10015370).
inline int zclear() {
    Renderer& r = renderer();
    if (!r.device) return 0;
    r.device->clear(0, nullptr, d3d8::kClearZBuffer, 0, 1.0f, 0);
    return 0;
}

// _bscene@16 (0x1001a700 -> 0x100153e0): clear to a color (-1 keeps the
// target), BeginScene, then the cull mode if non-zero.
inline int bscene(std::int32_t color, std::int32_t cull) {
    Renderer& r = renderer();
    if (!r.device) return 0;
    if (color != -1)  // 0x100153a0
        r.device->clear(0, nullptr, d3d8::kClearTarget | d3d8::kClearZBuffer, static_cast<std::uint32_t>(color),
                        1.0f, 0);
    r.device->begin_scene();
    if (cull) r.device->set_render_state(22, static_cast<std::uint32_t>(cull));
    return 0;
}

// _escene@16 (0x1001a740 -> 0x10015410): EndScene and Present. Without a
// rectangle, a full-screen viewport presents everything and any other
// viewport presents its own rectangle; a lost device is reset.
inline int escene(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom) {
    Renderer& r = renderer();
    if (!r.device) return 0;
    r.device->end_scene();
    d3d8::Viewport v;
    r.device->get_viewport(v);
    d3d8::Rect area;
    area.left = static_cast<std::int32_t>(v.x);
    area.top = static_cast<std::int32_t>(v.y);
    area.right = static_cast<std::int32_t>(v.x + v.width);
    area.bottom = static_cast<std::int32_t>(v.y + v.height);
    d3d8::HResult hr;
    if ((left | top | right | bottom) == 0) {
        if (v.y == 0 && v.x == 0 && v.width == r.present.width && v.height == r.present.height)
            hr = r.device->present(nullptr, nullptr);
        else
            hr = r.device->present(&area, &area);
    } else {
        d3d8::Rect given;
        given.left = left;
        given.top = top;
        given.right = right;
        given.bottom = bottom;
        hr = r.device->present(&given, &given);
    }
    if (hr == d3d8::kDeviceLost) r.device->reset(r.present);
    return 0;
}

// 0x10001800: a screen vertex from logical coordinates, scaled to the
// viewport with integer arithmetic.
inline d3d8::ScreenVertex screen_vertex(const Renderer& r, std::int32_t x, std::int32_t y, std::uint32_t color,
                                        std::uint32_t u, std::uint32_t v) {
    d3d8::ScreenVertex out;
    const std::int32_t sx = static_cast<std::int32_t>(static_cast<std::uint32_t>(x) * r.viewport.width) /
                                r.logical_width + static_cast<std::int32_t>(r.viewport.x);
    const std::int32_t sy = static_cast<std::int32_t>(static_cast<std::uint32_t>(y) * r.viewport.height) /
                                r.logical_height + static_cast<std::int32_t>(r.viewport.y);
    out.x = static_cast<float>(sx);
    out.y = static_cast<float>(sy);
    out.z = 0.0f;
    out.rhw = 1.0f;
    out.color = color;
    out.u = u;
    out.v = v;
    return out;
}

// 0x100025d0: a textured quad (corners in order), drawn as a fan.
inline void draw_quad(Renderer& r, d3d8::Texture* texture, const std::int32_t points[8]) {
    r.device->set_texture(0, texture);
    const std::uint32_t one = 0x3f800000u;
    d3d8::ScreenVertex v[4] = {screen_vertex(r, points[0], points[1], r.color, 0, 0),
                               screen_vertex(r, points[2], points[3], r.color, one, 0),
                               screen_vertex(r, points[4], points[5], r.color, one, one),
                               screen_vertex(r, points[6], points[7], r.color, 0, one)};
    r.device->set_vertex_shader(d3d8::kFvfScreen);
    r.device->draw_primitive_up(d3d8::kTriangleFan, 2, v, sizeof(d3d8::ScreenVertex));
    r.device->set_texture(0, nullptr);
}

// _fill2d@16 (0x1001af60 -> 0x100026f0): a filled rectangle in the current
// color; 0, 0, 0, 0 fills the logical screen.
inline int fill2d(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2) {
    Renderer& r = renderer();
    if (!r.device || !r.logical_width || !r.logical_height) return 0;  // the original divides by zero
    if (x1 == 0 && y1 == 0 && x2 == 0 && y2 == 0) {
        x2 = r.logical_width;
        y2 = r.logical_height;
    }
    if (x1 > x2) std::swap(x1, x2);
    if (y1 > y2) std::swap(y1, y2);
    const std::int32_t points[8] = {x1, y1, x2, y1, x2, y2, x1, y2};
    draw_quad(r, nullptr, points);
    return 0;
}

// _line2d@16 (0x1001af90 -> 0x100027b0): a line in the current color; the
// bound texture is left as it is.
inline int line2d(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2) {
    Renderer& r = renderer();
    if (!r.device || !r.logical_width || !r.logical_height) return 0;
    d3d8::ScreenVertex v[2] = {screen_vertex(r, x1, y1, r.color, 0, 0), screen_vertex(r, x2, y2, r.color, 0, 0)};
    r.device->set_vertex_shader(d3d8::kFvfScreen);
    r.device->draw_primitive_up(d3d8::kLineList, 1, v, sizeof(d3d8::ScreenVertex));
    return 0;
}

// _setcolor4@16 (0x1001a7a0): r, g, b, a (8 bits each) as the 2D color; the
// alpha also goes to the blend helper.
inline int setcolor4(std::int32_t red, std::int32_t green, std::int32_t blue, std::int32_t alpha) {
    CP2_ZGP_STRICT_FP
    Renderer& r = renderer();
    r.color4 = (static_cast<std::uint32_t>(alpha) & 0xff) << 24 | (static_cast<std::uint32_t>(red) & 0xff) << 16 |
               (static_cast<std::uint32_t>(green) & 0xff) << 8 | (static_cast<std::uint32_t>(blue) & 0xff);
    r.color = r.color4;  // 0x10001e20
    if (!r.device) return 0;  // the blend helper uses the device
    blend_state(r, -1, static_cast<float>(r.color4 >> 24) / 255.0f, 0);
    return 0;
}

// _setblend@16 (0x1001a830).
inline int setblend(std::int32_t mode) {
    Renderer& r = renderer();
    r.blend_command = mode;
    if (!r.device) return 0;
    const std::uint32_t keep = 0xbf800000u;  // -1.0f keeps the alpha
    float alpha;
    std::memcpy(&alpha, &keep, 4);
    blend_state(r, mode, alpha, 0);
    return 0;
}

// _set2d@16 (0x1001ae00 -> 0x10008920).
inline int set2d(std::int32_t filter) {
    Renderer& r = renderer();
    if (!r.device) return 0;
    r.device->set_texture(0, nullptr);
    r.device->set_texture_stage_state(0, 17, static_cast<std::uint32_t>(filter));
    r.device->set_texture_stage_state(0, 16, static_cast<std::uint32_t>(filter));
    r.device->set_texture_stage_state(0, 18, static_cast<std::uint32_t>(filter));
    r.device->set_render_state(7, 0);
    r.device->set_render_state(137, 0);
    r.device->set_render_state(28, 0);
    blend_state(r, 0, 1.0f, 0);
    return 0;
}

// ---------------------------------------------------------------------------
// Characters (images on a surface used by sprites).

inline Renderer::Character* character(std::int32_t index) {
    const Table& t = state().units[0];
    if (index < 1 || index >= t.count) return nullptr;
    Renderer& r = renderer();
    if (r.characters.size() != static_cast<std::size_t>(t.count))
        r.characters.resize(static_cast<std::size_t>(t.count));
    return &r.characters[static_cast<std::size_t>(index)];
}

// 0x100123b0: image rectangle (x, y, w, h) of a surface; a negative size
// selects the whole texture from its origin. Also records the diagonal, the
// UV rectangle and two hit triangles (0x10012100).
inline void character_setup(Renderer::Character& c, std::int32_t self, std::int32_t surface_index,
                            const Renderer::Surface& s, std::int32_t x, std::int32_t y, std::int32_t w,
                            std::int32_t h) {
    CP2_ZGP_STRICT_FP
    c.triangles.clear();  // 0x100120c0
    c.surface = surface_index;
    c.width = static_cast<float>(w);
    c.height = static_cast<float>(h);
    c.frame = self;
    c.field14 = 0;
    d3d8::SurfaceDesc desc;
    s.texture->level_desc(0, desc);
    if (c.width < 0.0f || c.height < 0.0f) {
        x = 0;
        y = 0;
        c.width = static_cast<float>(desc.width);
        c.height = static_cast<float>(desc.height);
    }
    // _CIsqrt and _ftol at 24-bit precision.
    const float squares = c.width * c.width + c.height * c.height;
    c.diagonal = static_cast<std::int32_t>(std::sqrt(squares));
    const float texture_width = static_cast<float>(static_cast<std::int32_t>(desc.width));
    const float texture_height = static_cast<float>(static_cast<std::int32_t>(desc.height));
    c.u0 = static_cast<float>(x) / texture_width;
    c.v0 = static_cast<float>(y) / texture_height;
    c.u1 = (static_cast<float>(x) + c.width) / texture_width;
    c.v1 = (static_cast<float>(y) + c.height) / texture_height;
    const float w2 = c.width, h2 = c.height;
    c.triangles.push_back({{0.0f, 0.0f, w2, 0.0f, w2, h2}});
    c.triangles.push_back({{w2, h2, 0.0f, h2, 0.0f, 0.0f}});
}

// ---------------------------------------------------------------------------
// Sprites.

inline float sprite_float(const Renderer::Sprite& s, int offset) {
    float f;
    std::memcpy(&f, &s.w[offset / 4], 4);
    return f;
}
inline void set_sprite_float(Renderer::Sprite& s, int offset, float f) { std::memcpy(&s.w[offset / 4], &f, 4); }
inline std::uint32_t& sprite_word(Renderer::Sprite& s, int offset) { return s.w[offset / 4]; }

inline Renderer::Sprite* sprite(std::int32_t index) {
    const Table& t = state().units[1];
    if (index < 1 || index >= t.count) return nullptr;
    Renderer& r = renderer();
    if (r.sprites.size() != static_cast<std::size_t>(t.count)) r.sprites.resize(static_cast<std::size_t>(t.count));
    return &r.sprites[static_cast<std::size_t>(index)];
}

// 0x10012a60: free the triangle buffer and zero the entry.
inline void sprite_clear(Renderer::Sprite& s) { std::memset(s.w, 0, sizeof(s.w)); }

// The angle wrap of 0x10012c50 (and the sprite update), into [0, 360).
inline float sprite_wrap(float v) { return wrap_degrees(v); }

// Logical angle units to degrees (sprang): a * 360 / setmaxangle's second period.
inline float sprite_degrees(std::int32_t a) {
    CP2_ZGP_STRICT_FP
    return static_cast<float>(a) * 360.0f / state().angle_period_b;
}

// _sprset@16 (0x100210f0 -> 0x10012aa0): sprite, character, type, flag; the
// position is the registered screen's current cx, cy.
inline int sprset(std::int32_t index, std::int32_t chr, std::int32_t type, std::int32_t flag,
                  std::int32_t position_x, std::int32_t position_y) {
    Renderer::Sprite* s = sprite(index);
    if (!s) return -42;
    Renderer::Character* c = character(chr);
    if (!c) return -32;
    if (!c->surface) return -33;
    sprite_clear(*s);
    sprite_word(*s, 0x00) = 1;
    set_sprite_float(*s, 0x10, static_cast<float>(position_x));
    set_sprite_float(*s, 0x14, static_cast<float>(position_y));
    sprite_word(*s, 0x28) = static_cast<std::uint32_t>(chr);
    set_sprite_float(*s, 0x54, 1.0f);
    set_sprite_float(*s, 0x58, 1.0f);
    set_sprite_float(*s, 0x6c, 1.0f);
    sprite_word(*s, 0x94) = 1;
    if (sprite_word(*s, 0x00)) sprite_word(*s, 0x08) = static_cast<std::uint32_t>(type);  // 0x10006c30
    sprite_word(*s, 0x04) = static_cast<std::uint32_t>(flag);                              // 0x10006c10
    return 0;
}

// _sprflag@16 (0x10021340).
inline int sprflag(std::int32_t index, std::int32_t flag) {
    Renderer::Sprite* s = sprite(index);
    if (!s) return -42;
    sprite_word(*s, 0x04) = static_cast<std::uint32_t>(flag);
    return 0;
}

// _sprpos@16 (0x10021470): position, then optionally a new character
// (0x10012b30, which also restarts its frame counter).
inline int sprpos(std::int32_t index, std::int32_t x, std::int32_t y, std::int32_t chr) {
    Renderer::Sprite* s = sprite(index);
    if (!s) return -42;
    set_sprite_float(*s, 0x10, static_cast<float>(x));
    set_sprite_float(*s, 0x14, static_cast<float>(y));
    if (!chr) return 0;
    Renderer::Character* c = character(chr);
    if (!c) return -32;
    if (!c->surface) return -33;
    sprite_word(*s, 0x28) = static_cast<std::uint32_t>(chr);
    sprite_word(*s, 0x90) = 0;
    return 0;
}

// _sprzoom@16 (0x10021580 -> 0x10012bf0): zoom in standard-scale units (y = x
// when 0); clears the zoom velocity and target.
inline int sprzoom(std::int32_t index, std::int32_t zx, std::int32_t zy) {
    CP2_ZGP_STRICT_FP
    Renderer::Sprite* s = sprite(index);
    if (!s) return -42;
    if (!zy) zy = zx;
    set_sprite_float(*s, 0x54, static_cast<float>(zx) / state().standard_scale_b);
    set_sprite_float(*s, 0x58, static_cast<float>(zy) / state().standard_scale_b);
    for (int o = 0x5c; o <= 0x68; o += 4) sprite_word(*s, o) = 0;
    return 0;
}

// _sprang@16 (0x10021670): with b = 0 the angle is set (0x10012c50);
// otherwise the sprite turns toward angle a by at most b per frame
// (0x10012d20: +0x34 = 1, +0x38 = wrapped a, +0x44 = b).
inline int sprang(std::int32_t index, std::int32_t a, std::int32_t b) {
    Renderer::Sprite* s = sprite(index);
    if (!s) return -42;
    if (!b) {
        set_sprite_float(*s, 0x18, sprite_wrap(sprite_degrees(a)));
        return 0;
    }
    sprite_word(*s, 0x34) = 1;
    set_sprite_float(*s, 0x38, sprite_wrap(sprite_degrees(a)));
    set_sprite_float(*s, 0x44, sprite_degrees(b));
    return 0;
}

// _sprlate@16 (0x10021ad0 -> 0x10013440): alpha, its per-frame change and the
// target it stops at, each / 255.
inline int sprlate(std::int32_t index, std::int32_t alpha, std::int32_t speed, std::int32_t target) {
    CP2_ZGP_STRICT_FP
    Renderer::Sprite* s = sprite(index);
    if (!s) return -42;
    set_sprite_float(*s, 0x6c, static_cast<float>(alpha) / 255.0f);
    set_sprite_float(*s, 0x70, static_cast<float>(speed) / 255.0f);
    set_sprite_float(*s, 0x74, static_cast<float>(target) / 255.0f);
    return 0;
}

// _sprblend@16 (0x10021bb0 -> 0x100134a0).
inline int sprblend(std::int32_t index, std::int32_t mode) {
    Renderer::Sprite* s = sprite(index);
    if (!s) return -42;
    sprite_word(*s, 0x84) = static_cast<std::uint32_t>(mode);
    return 0;
}

// _sprdel@16 (0x10021ca0).
inline int sprdel(std::int32_t index) {
    Renderer::Sprite* s = sprite(index);
    if (!s) return -42;
    sprite_clear(*s);
    return 0;
}

inline void sprite_report(const char* what) {
    static std::string reported;
    if (reported.find(what) != std::string::npos) return;
    reported += what;
    std::fprintf(stderr, "[CP2 ZGP] sprite %s is not implemented (no imported command enables it)\n", what);
}

// One animated value (value, per-frame change, target): it moves and stops
// when it reaches or crosses the target (sprite update, 0x10010c79 and after).
inline void sprite_animate(Renderer::Sprite& s, int value, int speed, int target) {
    CP2_ZGP_STRICT_FP
    if (sprite_float(s, speed) == 0.0f) return;
    const float before = sprite_float(s, value) - sprite_float(s, target);
    set_sprite_float(s, value, sprite_float(s, value) + sprite_float(s, speed));
    const float after = sprite_float(s, value) - sprite_float(s, target);
    if (!(before < 0.0f) && after < 0.0f) {
        sprite_word(s, value) = sprite_word(s, target);
        sprite_word(s, speed) = 0;
    }
    if (!(before > 0.0f) && after > 0.0f) {
        sprite_word(s, value) = sprite_word(s, target);
        sprite_word(s, speed) = 0;
    }
}

// D3DXMatrixMultiply (0x1002427a): each element (a0 b0 + a2 b2) + (a1 b1 + a3 b3).
inline d3d8::Matrix matrix_multiply(const d3d8::Matrix& a, const d3d8::Matrix& b) {
    CP2_ZGP_STRICT_FP
    d3d8::Matrix out;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            const float* r = a.m + i * 4;
            out.m[i * 4 + j] = (r[0] * b.m[j] + r[2] * b.m[8 + j]) + (r[1] * b.m[4 + j] + r[3] * b.m[12 + j]);
        }
    return out;
}

// IDirect3DDevice8::MultiplyTransform: the new transform is matrix *
// current. The trace records only the argument; the runtime's own operation
// order is not observable there, so D3DXMatrixMultiply's is used.
inline d3d8::HResult StateDevice::multiply_transform(std::uint32_t t, const d3d8::Matrix& m) {
    if (t >= 512) return d3d8::kInvalidCall;
    transforms[t] = matrix_multiply(m, transforms[t]);
    return d3d8::kOk;
}

// 0x10011c30 for a sprite without a parent: identity, RotationZ (0x1002480f)
// for a non-zero angle, times Scaling (0x10024757) for a zoom other than 1,
// then the position as translation.
inline d3d8::Matrix sprite_matrix(Renderer::Sprite& s) {
    CP2_ZGP_STRICT_FP
    d3d8::Matrix m;
    std::memset(m.m, 0, sizeof(m.m));
    m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0f;
    const float angle = sprite_float(s, 0x18);
    if (angle != 0.0f) {
        const float radians = angle * 3.14159274101257f / 180.0f;
        const float c = static_cast<float>(std::cos(static_cast<double>(radians)));  // fsincos
        const float sn = static_cast<float>(std::sin(static_cast<double>(radians)));
        m.m[0] = c;
        m.m[1] = sn;
        m.m[4] = -sn;
        m.m[5] = c;
    }
    if (sprite_float(s, 0x54) != 1.0f || sprite_float(s, 0x58) != 1.0f) {
        d3d8::Matrix scaling;
        std::memset(scaling.m, 0, sizeof(scaling.m));
        scaling.m[0] = sprite_float(s, 0x54);
        scaling.m[5] = sprite_float(s, 0x58);
        scaling.m[10] = 1.0f;
        scaling.m[15] = 1.0f;
        m = matrix_multiply(m, scaling);
    }
    m.m[12] = sprite_float(s, 0x10);
    m.m[13] = sprite_float(s, 0x14);
    return m;
}

// 0x10024b7a: transforms (x, y, z) and divides x, y by w when w is not 1
// within 1e-5. ZGP passes a 2D point; z is the float that follows it in
// memory, which only meets the matrix's zero third row.
inline void sprite_transform(const d3d8::Matrix& m, float x, float y, float z, float& out_x, float& out_y) {
    CP2_ZGP_STRICT_FP
    float tx = ((x * m.m[0] + y * m.m[4]) + z * m.m[8]) + m.m[12];
    float ty = ((x * m.m[1] + y * m.m[5]) + z * m.m[9]) + m.m[13];
    const float w = ((x * m.m[3] + y * m.m[7]) + z * m.m[11]) + m.m[15];
    const float d = w - 1.0f;
    if (d < -9.99999974737875e-06f || d > 9.99999974737875e-06f) {
        const float r = 1.0f / w;
        tx = r * tx;
        ty = r * ty;
    }
    out_x = tx;
    out_y = ty;
}

// 0x100017a0: logical to viewport coordinates in float.
inline void sprite_to_viewport(const Renderer& r, float& x, float& y) {
    CP2_ZGP_STRICT_FP
    x = static_cast<float>(static_cast<std::int32_t>(r.viewport.width)) * x / static_cast<float>(r.logical_width) +
        static_cast<float>(static_cast<std::int32_t>(r.viewport.x));
    y = static_cast<float>(static_cast<std::int32_t>(r.viewport.height)) * y / static_cast<float>(r.logical_height) +
        static_cast<float>(static_cast<std::int32_t>(r.viewport.y));
}

// 0x10010b10: update (unless mode bit 0) and draw (unless mode bit 1) count
// sprites from first.
inline void sprites_draw(Renderer& r, std::int32_t first, std::int32_t count, std::int32_t mode) {
    CP2_ZGP_STRICT_FP
    r.device->set_vertex_shader(d3d8::kFvfScreen);
    for (std::int32_t i = 0; i < count; ++i) {
        Renderer::Sprite& s = r.sprites[static_cast<std::size_t>(first + i)];
        const std::uint32_t flags = sprite_word(s, 0x04);
        if (!sprite_word(s, 0x00)) continue;
        if (sprite_word(s, 0x30)) {
            sprite_report("parent link");
            if (!r.sprites[sprite_word(s, 0x30)].w[0]) { sprite_clear(s); continue; }
        }
        if (!(mode & 1)) {
            // Character frames: advance to the linked character when the
            // counter passes the current one's length.
            sprite_word(s, 0x90) += 1;
            const Renderer::Character& current = r.characters[sprite_word(s, 0x28)];
            if (static_cast<std::int32_t>(sprite_word(s, 0x90)) > current.field14) {
                if (!current.frame) { sprite_clear(s); continue; }
                sprite_word(s, 0x28) = static_cast<std::uint32_t>(current.frame);
                sprite_word(s, 0x90) = 0;
            }
            if (sprite_word(s, 0x8c)) sprite_word(s, 0x8c) -= 1;
            if (sprite_word(s, 0x88)) {
                sprite_word(s, 0x88) -= 1;
                if (static_cast<std::int32_t>(sprite_word(s, 0x88)) <= 0) { sprite_clear(s); continue; }
            }
            sprite_animate(s, 0x6c, 0x70, 0x74);  // alpha
            sprite_animate(s, 0x78, 0x7c, 0x80);  // emission
            if (sprite_float(s, 0x5c) != 0.0f) {  // zoom
                sprite_animate(s, 0x54, 0x5c, 0x64);
                sprite_word(s, 0x58) = sprite_word(s, 0x54);
            }
            sprite_animate(s, 0x48, 0x4c, 0x50);  // speed along the angle
            set_sprite_float(s, 0x10, sprite_float(s, 0x10) + sprite_float(s, 0x1c));
            set_sprite_float(s, 0x14, sprite_float(s, 0x14) + sprite_float(s, 0x20));
            if (sprite_float(s, 0x24) != 0.0f) {
                set_sprite_float(s, 0x18, sprite_float(s, 0x18) + sprite_float(s, 0x24));
                set_sprite_float(s, 0x18, sprite_wrap(sprite_float(s, 0x18)));
            }
            if (sprite_float(s, 0x48) != 0.0f) {
                // _CIsin/_CIcos of angle * pi / 180 (double constants at 0x100531a0, 0x10053198).
                const float t = static_cast<float>(static_cast<double>(sprite_wrap(sprite_float(s, 0x18))) *
                                                   3.1415927410125732 / 180.0);
                const float sn = static_cast<float>(std::sin(static_cast<double>(t)));
                const float cs = static_cast<float>(std::cos(static_cast<double>(t)));
                set_sprite_float(s, 0x10, sn * sprite_float(s, 0x48) + sprite_float(s, 0x10));
                set_sprite_float(s, 0x14, sprite_float(s, 0x14) - cs * sprite_float(s, 0x48));
            }
            const float px = sprite_float(s, 0x10), py = sprite_float(s, 0x14);  // 0x10013670
            const std::uint32_t turn = sprite_word(s, 0x34);
            if (turn == 1) {
                const float target = sprite_float(s, 0x38);
                const float raw = target - sprite_wrap(sprite_float(s, 0x18));  // 0x100136f0
                float difference = raw, step = raw;
                if (difference < 0.0f) difference = difference + 360.0f;
                if (step < 0.0f) step = -step;
                if (!(step <= sprite_float(s, 0x44))) step = sprite_float(s, 0x44);
                if (!(difference <= 180.0f))
                    set_sprite_float(s, 0x18, sprite_float(s, 0x18) - step);
                else
                    set_sprite_float(s, 0x18, step + sprite_float(s, 0x18));
                set_sprite_float(s, 0x18, sprite_wrap(sprite_float(s, 0x18)));
            } else if (turn == 2 || turn == 3) {
                sprite_report("aiming at a sprite or point");
            }
            if (r.sprite_clip && (flags & 1)) {
                const Renderer::Character& c =
                    r.characters[sprite_word(s, 0x8c) ? sprite_word(s, 0x2c) : sprite_word(s, 0x28)];
                std::int32_t x = static_cast<std::int32_t>(px), y = static_cast<std::int32_t>(py);
                const std::int32_t w = static_cast<std::int32_t>(c.width * sprite_float(s, 0x54));
                const std::int32_t h = static_cast<std::int32_t>(c.height * sprite_float(s, 0x58));
                if (flags & 4) { x -= w >> 1; y -= h >> 1; }
                const std::int32_t* clip = r.sprite_clip_rect;
                if (x > clip[2] || y > clip[3] || x + w < clip[0] || y + h < clip[1]) { sprite_clear(s); continue; }
            }
        }
        if (mode & 2) continue;
        // Drawing.
        const d3d8::Matrix m = sprite_matrix(s);  // 0x10013610
        const Renderer::Character& c =
            r.characters[sprite_word(s, 0x8c) ? sprite_word(s, 0x2c) : sprite_word(s, 0x28)];
        const Renderer::Surface& surface = r.surfaces[static_cast<std::size_t>(c.surface)];
        r.device->set_texture(0, surface.texture);
        blend_state(r, static_cast<std::int32_t>(sprite_word(s, 0x84)), sprite_float(s, 0x6c), surface.format);
        const std::uint32_t alpha =
            static_cast<std::uint32_t>(static_cast<std::int32_t>(sprite_float(s, 0x6c) * 255.0f)) & 0xff;
        const std::uint32_t color = alpha << 24 | 0xffffffu;
        const float w = c.width, h = c.height;
        float ox = 0.0f, oy = 0.0f;
        if (flags & 4) { ox = w / 2.0f; oy = h / 2.0f; }
        const float corners[4][2] = {{-ox, -oy}, {w - ox, -oy}, {w - ox, h - oy}, {-ox, h - oy}};
        // The "z" read after each corner: the matrix copy's first element,
        // then the previous corner's x.
        const float zs[4] = {m.m[0], corners[0][0], corners[1][0], corners[2][0]};
        d3d8::ScreenVertex v[4];
        for (int k = 0; k < 4; ++k) {
            float x, y;
            sprite_transform(m, corners[k][0], corners[k][1], zs[k], x, y);
            sprite_to_viewport(r, x, y);
            v[k].x = x;
            v[k].y = y;
            v[k].z = 0.0f;
            v[k].rhw = 1.0f;
            v[k].color = color;
        }
        std::uint32_t u0, v0, u1, v1;
        std::memcpy(&u0, &c.u0, 4);
        std::memcpy(&v0, &c.v0, 4);
        std::memcpy(&u1, &c.u1, 4);
        std::memcpy(&v1, &c.v1, 4);
        v[0].u = u0; v[0].v = v0;
        v[1].u = u1; v[1].v = v0;
        v[2].u = u1; v[2].v = v1;
        v[3].u = u0; v[3].v = v1;
        if (flags & 0x1000) { v[0].u = u1; v[1].u = u0; v[2].u = u0; v[3].u = u1; }
        if (flags & 0x2000) { v[0].v = v1; v[1].v = v1; v[2].v = v0; v[3].v = v0; }
        r.device->draw_primitive_up(d3d8::kTriangleFan, 2, v, sizeof(d3d8::ScreenVertex));
    }
    r.device->set_texture(0, nullptr);
}

// _sprdraw@16 (0x10021d30): first sprite (at least 1), count (0 = all),
// mode; then the blend state of setblend and setcolor4 is restored.
inline int sprdraw(std::int32_t first, std::int32_t count, std::int32_t mode) {
    CP2_ZGP_STRICT_FP
    Renderer& r = renderer();
    if (!r.device) return 0;
    const std::int32_t total = state().units[1].count;
    if (first < 1) first = 1;
    if (!count) count = total;
    if (first + count > total) count = total - first;
    if (r.sprites.size() != static_cast<std::size_t>(total)) r.sprites.resize(static_cast<std::size_t>(total));
    if (r.characters.size() != static_cast<std::size_t>(state().units[0].count))
        r.characters.resize(static_cast<std::size_t>(state().units[0].count));
    sprites_draw(r, first, count, mode);
    blend_state(r, r.blend_command, static_cast<float>(r.color4 >> 24) / 255.0f, 0);
    return 0;
}

// _chrset@16 (0x10020c30): character index, surface, width, height. 0, 0
// takes the chrsize defaults; the origin is the current position (cx, cy) of
// the screen registered by setbmscr, read when chrset runs.
inline int chrset(std::int32_t index, std::int32_t surface_index, std::int32_t w, std::int32_t h,
                  std::int32_t position_x, std::int32_t position_y) {
    Renderer::Character* c = character(index);
    if (!c) return -32;
    Renderer::Surface* s = surface(surface_index);
    if (!s) return -12;
    if (!s->texture) return -13;
    Renderer& r = renderer();
    if (w == 0 && h == 0) {
        w = r.character_width;
        h = r.character_height;
    }
    character_setup(*c, index, surface_index, *s, position_x, position_y, w, h);
    return 0;
}

// ---------------------------------------------------------------------------
// Meshes.

// D3DXGetFVFVertexSize (0x10023ffc).
inline std::uint32_t fvf_vertex_size(std::uint32_t fvf) {
    // By (fvf & 0xe) / 2: none, XYZ, XYZRHW, XYZB1 ... XYZB5.
    static const std::uint32_t position[8] = {0, 12, 16, 16, 20, 24, 28, 32};
    std::uint32_t size = position[(fvf & 0xe) >> 1];
    if (fvf & 0x10) size += 12;  // NORMAL
    if (fvf & 0x20) size += 4;   // PSIZE
    if (fvf & 0x40) size += 4;   // DIFFUSE
    if (fvf & 0x80) size += 4;   // SPECULAR
    const std::uint32_t sets = (fvf >> 8) & 0xf;
    for (std::uint32_t i = 0; i < sets; ++i) {
        static const std::uint32_t coordinates[4] = {8, 12, 16, 4};  // D3DFVF_TEXTUREFORMAT2/3/4/1
        size += coordinates[(fvf >> (16 + 2 * i)) & 3];
    }
    return size;
}

// GetTickCount: milliseconds of a monotonic clock, wrapping at 32 bits.
inline std::uint32_t default_tick_count() {
    return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

inline Renderer::Mesh* mesh(std::int32_t index) {
    const Table& t = state().units[3];
    if (index < 1 || index >= t.count) return nullptr;
    Renderer& r = renderer();
    if (r.meshes.size() != static_cast<std::size_t>(t.count)) r.meshes.resize(static_cast<std::size_t>(t.count));
    return &r.meshes[static_cast<std::size_t>(index)];
}

// 0x10009c10: frees the arrays and the own surfaces (releasing their
// textures), releases the vertex then the index buffer and zeroes the entry.
inline void mesh_release(Renderer::Mesh& m) {
    for (Renderer::Surface& s : m.own_surfaces) surface_release(s);
    if (m.vertices) m.vertices->release();
    if (m.indices) m.indices->release();
    m = Renderer::Mesh();
}

// 0x100166c0: a material from an ARGB colour. Each channel / 255 goes to
// both the diffuse and the ambient colour; everything else is zero.
inline Renderer::Material material_from_color(std::uint32_t color) {
    CP2_ZGP_STRICT_FP
    Renderer::Material material;
    const float a = static_cast<float>((color >> 24) & 0xff) / 255.0f;
    const float red = static_cast<float>((color >> 16) & 0xff) / 255.0f;
    const float green = static_cast<float>((color >> 8) & 0xff) / 255.0f;
    const float blue = static_cast<float>(color & 0xff) / 255.0f;
    material.m[7] = a;
    material.m[3] = a;
    material.m[4] = red;
    material.m[0] = red;
    material.m[5] = green;
    material.m[1] = green;
    material.m[6] = blue;
    material.m[2] = blue;
    return material;
}

// 0x10009780: per-attribute arrays (zeroed ranges, the default material of
// 0x10016650 with white diffuse and ambient, own surfaces as constructed by
// 0x10013910, null textures) and the hit size 100, 100, 100.
inline void mesh_attributes(Renderer::Mesh& m, std::uint32_t count) {
    m.ranges.assign(count, {{0, 0, 0, 0, 0}});
    m.own_surfaces.assign(count, Renderer::Surface());
    Renderer::Material white;
    for (int i = 0; i < 8; ++i) white.m[i] = 1.0f;
    m.materials.assign(count, white);
    m.textures.assign(count, -1);
    m.attribute_count = count;
    m.hit[0] = m.hit[1] = m.hit[2] = 100.0f;
}

// 0x10018d40: turns the triangle list in `vertices` into an indexed mesh. The
// vertices are copied out and sorted as byte strings (the bitwise radix sort
// 0x100011d0/0x100015b0 orders them like memcmp, most significant byte
// first). Adjacent identical vertices are merged and every original vertex
// gets the index of its merged copy (16-bit, as the original counts).
// New managed vertex and 16-bit index buffers replace the old ones. The order
// among identical vertices does not change the result, so any correct sort
// produces the same buffers.
inline bool mesh_index(Renderer& r, d3d8::Buffer*& vertices, d3d8::Buffer*& indices, std::uint32_t triangles,
                       std::uint32_t& vertex_count, std::uint32_t fvf) {
    const std::size_t size = fvf_vertex_size(fvf);
    const std::uint32_t n = triangles * 3;
    if (!n || !size) return false;
    std::vector<unsigned char> data(n * size);
    unsigned char* locked = nullptr;
    if (d3d8::failed(vertices->lock(locked, 0))) return false;
    std::memcpy(data.data(), locked, data.size());
    vertices->unlock();
    std::vector<std::uint32_t> order(n);
    for (std::uint32_t i = 0; i < n; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
        return std::memcmp(&data[a * size], &data[b * size], size) < 0;
    });
    std::vector<unsigned char> sorted(data.size());
    for (std::uint32_t i = 0; i < n; ++i) std::memcpy(&sorted[i * size], &data[order[i] * size], size);
    std::vector<std::uint16_t> index(n);
    std::uint16_t out = 0;
    for (std::uint32_t i = 0; i + 1 < n; ++i) {
        index[order[i]] = out;
        if (std::memcmp(&sorted[(i + 1) * size], &sorted[i * size], size) != 0) {
            std::memmove(&sorted[out * size], &sorted[i * size], size);
            ++out;
        }
    }
    index[order[n - 1]] = out;
    std::memmove(&sorted[out * size], &sorted[(n - 1) * size], size);
    ++out;
    d3d8::Buffer* new_vertices = nullptr;
    d3d8::Buffer* new_indices = nullptr;
    r.device->create_vertex_buffer(static_cast<std::uint32_t>(out * size), 0, fvf, d3d8::kPoolManaged, new_vertices);
    r.device->create_index_buffer(n * 2, 0, d3d8::kFormatIndex16, d3d8::kPoolManaged, new_indices);
    if (!new_vertices || !new_indices) {
        if (new_vertices) new_vertices->release();
        if (new_indices) new_indices->release();
        return false;
    }
    unsigned char* vertex_data = nullptr;
    unsigned char* index_data = nullptr;
    new_vertices->lock(vertex_data, 0);
    new_indices->lock(index_data, 0);
    std::memcpy(index_data, index.data(), static_cast<std::size_t>(n) * 2);
    std::memcpy(vertex_data, sorted.data(), out * size);
    new_indices->unlock();
    new_vertices->unlock();
    if (vertices) vertices->release();
    if (indices) indices->release();
    indices = new_indices;
    vertices = new_vertices;
    vertex_count = out;
    return true;
}

// 0x10009df0: the bounding box of the vertex positions (min x, y, z, then
// max x, y, z), with the original's x87 comparisons: a minimum is replaced
// only by a smaller value, a maximum by a larger or unordered one.
inline void mesh_bounds(Renderer::Mesh& m, float box[6]) {
    unsigned char* data = nullptr;
    if (d3d8::failed(m.vertices->lock(data, 0))) return;
    const std::size_t stride = (m.vertex_size >> 2) * 4;
    for (std::uint32_t i = 0; i < m.vertex_count; ++i) {
        float v[3];
        std::memcpy(v, data + i * stride, 12);
        for (int k = 0; k < 3; ++k) {
            if (i == 0) {
                box[k] = v[k];
                box[3 + k] = v[k];
                continue;
            }
            if (box[k] > v[k]) box[k] = v[k];
            if (!(box[3 + k] >= v[k])) box[3 + k] = v[k];
        }
    }
    m.vertices->unlock();
}

// 0x1000a8c0 (centre from 0x10009f70): moves the mesh so that its
// bounding-box centre, (max - min) / 2 + min, is the origin on the selected
// axes.
inline void mesh_center(Renderer::Mesh& m, bool x, bool y, bool z) {
    CP2_ZGP_STRICT_FP
    float box[6] = {0, 0, 0, 0, 0, 0};
    mesh_bounds(m, box);
    float center[3];
    for (int k = 0; k < 3; ++k) center[k] = (box[3 + k] - box[k]) / 2.0f + box[k];
    unsigned char* data = nullptr;
    if (d3d8::failed(m.vertices->lock(data, 0))) return;
    const std::size_t stride = (m.vertex_size >> 2) * 4;
    const bool axis[3] = {x, y, z};
    for (std::uint32_t i = 0; i < m.vertex_count; ++i) {
        for (int k = 0; k < 3; ++k) {
            if (!axis[k]) continue;
            unsigned char* p = data + i * stride + 4 * k;
            float v;
            std::memcpy(&v, p, 4);
            v = v - center[k];
            std::memcpy(p, &v, 4);
        }
    }
    m.vertices->unlock();
}

// 0x1000b7a0: a box from (0, 0, 0) to `size`. Bits 0-5 of `faces` (0 = all)
// select the +Y, -Y, -X, +X, +Z and -Z faces, two triangles each, in FVF
// 0x112 (position, normal, one UV set). Returns false without faces, or when
// the vertex buffer cannot be created (after the old mesh was released).
inline bool mesh_box(Renderer& r, Renderer::Mesh& m, const float size[3], std::int32_t texture, std::int32_t faces,
                     std::int32_t center, std::uint32_t color) {
    if (faces == 0) faces = 0x3f;
    std::uint32_t count = 0;
    for (std::int32_t i = 0, bits = faces; i < 6; ++i, bits >>= 1) count += static_cast<std::uint32_t>(bits & 1);
    if (!count) return false;
    mesh_release(m);
    m.triangle_count = count * 2;
    m.vertex_count = count * 2 * 3;
    if (!r.device ||
        r.device->create_vertex_buffer(m.triangle_count * 32 * 3, 0, 0x112, d3d8::kPoolSystemMem, m.vertices) !=
            d3d8::kOk)
        return false;
    // Each corner: x, y, z selectors (0 = zero, 1 = the size component), then u, v.
    struct Corner {
        std::uint8_t x, y, z, u, v;
    };
    struct Face {
        std::int8_t normal[3];
        Corner corners[6];
    };
    static const Face table[6] = {
        {{0, 1, 0}, {{0, 1, 0, 0, 1}, {0, 1, 1, 0, 0}, {1, 1, 1, 1, 0}, {1, 1, 1, 1, 0}, {1, 1, 0, 1, 1}, {0, 1, 0, 0, 1}}},
        {{0, -1, 0}, {{1, 0, 0, 0, 1}, {1, 0, 1, 0, 0}, {0, 0, 1, 1, 0}, {0, 0, 1, 1, 0}, {0, 0, 0, 1, 1}, {1, 0, 0, 0, 1}}},
        {{-1, 0, 0}, {{0, 0, 1, 0, 1}, {0, 1, 1, 0, 0}, {0, 1, 0, 1, 0}, {0, 1, 0, 1, 0}, {0, 0, 0, 1, 1}, {0, 0, 1, 0, 1}}},
        {{1, 0, 0}, {{1, 0, 0, 0, 1}, {1, 1, 0, 0, 0}, {1, 1, 1, 1, 0}, {1, 1, 1, 1, 0}, {1, 0, 1, 1, 1}, {1, 0, 0, 0, 1}}},
        {{0, 0, 1}, {{1, 0, 1, 0, 1}, {1, 1, 1, 0, 0}, {0, 1, 1, 1, 0}, {0, 1, 1, 1, 0}, {0, 0, 1, 1, 1}, {1, 0, 1, 0, 1}}},
        {{0, 0, -1}, {{0, 0, 0, 0, 1}, {0, 1, 0, 0, 0}, {1, 1, 0, 1, 0}, {1, 1, 0, 1, 0}, {1, 0, 0, 1, 1}, {0, 0, 0, 0, 1}}},
    };
    unsigned char* data = nullptr;
    m.vertices->lock(data, 0);
    std::size_t written = 0;
    for (int f = 0; f < 6; ++f) {
        if (!(faces & (1 << f))) continue;
        for (const Corner& c : table[f].corners) {
            // The original stores the size components and the constants
            // 0.0f/1.0f/-1.0f as DWORDs; a zero is +0.0.
            const float v[8] = {c.x ? size[0] : 0.0f, c.y ? size[1] : 0.0f, c.z ? size[2] : 0.0f,
                                static_cast<float>(table[f].normal[0]), static_cast<float>(table[f].normal[1]),
                                static_cast<float>(table[f].normal[2]), c.u ? 1.0f : 0.0f, c.v ? 1.0f : 0.0f};
            std::memcpy(data + written * 32, v, sizeof(v));
            ++written;
        }
    }
    m.vertices->unlock();
    if (!mesh_index(r, m.vertices, m.indices, m.triangle_count, m.vertex_count, 0x112)) return false;
    m.type = 3;
    m.fvf = 0x112;
    m.vertex_size = fvf_vertex_size(m.fvf);
    mesh_attributes(m, 1);
    m.materials[0] = material_from_color(color);
    m.ranges[0] = {{0, 0, m.triangle_count, 0, m.vertex_count}};
    m.textures[0] = texture;
    if (center) mesh_center(m, true, true, true);
    return true;
}

// _meshsz@16 (0x1001bd30): the size of generated meshes, each value divided
// by the standard scale (256 unless setstdscale changes it, which the game
// never imports; the quotient is then exact, so the x87 precision mode does
// not matter). No Direct3D calls.
inline int meshsz(std::int32_t x, std::int32_t y, std::int32_t z) {
    Renderer& r = renderer();
    const double scale = state().standard_scale_a;
    r.mesh_size[0] = static_cast<float>(static_cast<double>(x) / scale);
    r.mesh_size[1] = static_cast<float>(static_cast<double>(y) / scale);
    r.mesh_size[2] = static_cast<float>(static_cast<double>(z) / scale);
    return 0;
}

// _meshbox@16 (0x1001b810): mesh, surface, faces, centre. Without a surface
// the box takes setcolor4's colour and points at surface slot 0; with one it
// is white.
inline int meshbox(std::int32_t index, std::int32_t surface_index, std::int32_t faces, std::int32_t center) {
    Renderer& r = renderer();
    Renderer::Mesh* m = mesh(index);
    if (!m) return -62;
    std::uint32_t color = r.color4;
    if (surface_index != 0) {
        Renderer::Surface* s = surface(surface_index);
        if (!s) return -12;
        if (!s->texture) return -13;
        color = 0xffffffffu;
    }
    if (!mesh_box(r, *m, r.mesh_size, surface_index, faces, center, color)) return -60;
    return 0;
}

// _meshboard@16 (0x1001b940 -> 0x1000c8f0): a board, the box builder
// 0x1000b7a0 with size (meshsz x, y, 0), only the +Z face (0x10), centred,
// white, on the given surface (0 = slot 0 without checks; otherwise -12
// out of range, -13 without a texture). Mesh out of range -62, failure -60.
inline int meshboard(std::int32_t index, std::int32_t surface_index) {
    Renderer& r = renderer();
    Renderer::Mesh* m = mesh(index);
    if (!m) return -62;
    if (surface_index != 0) {
        Renderer::Surface* s = surface(surface_index);
        if (!s) return -12;
        if (!s->texture) return -13;
    }
    const float size[3] = {r.mesh_size[0], r.mesh_size[1], 0.0f};
    if (!mesh_box(r, *m, size, surface_index, 0x10, 1, 0xffffffffu)) return -60;
    return 0;
}

// 0x1000cfc0: a normal packed into one int as three 9-bit sign-magnitude
// fields (x bits 18-26, y 9-17, z 0-8; bit 8 of a field is the sign),
// divided by its length (sqrt of the exact integer sum, rounded to single
// precision). A zero vector gives 0/0 = NaN components.
inline void unpack_normal(std::int32_t packed, float out[3]) {
    CP2_ZGP_STRICT_FP
    std::int32_t c[3] = {(packed >> 18) & 0x1ff, (packed >> 9) & 0x1ff, packed & 0x1ff};
    for (std::int32_t& v : c)
        if (v & 0x100) v = -(v & 0xff);
    const std::int32_t sum = c[0] * c[0] + c[1] * c[1] + c[2] * c[2];
    const float length = std::sqrt(static_cast<float>(sum));
    for (int k = 0; k < 3; ++k) out[k] = static_cast<float>(c[k]) / length;
}

// 0x10017240: the face normal of triangle (a, b, c) from positions:
// cross(b - a, c - a), normalized, negated when `flip` is set. Single
// precision throughout (x87 in Direct3D's precision mode).
inline void face_normal(const float a[3], const float b[3], const float c[3], bool flip, float out[3]) {
    CP2_ZGP_STRICT_FP
    const float nx = (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]);
    const float ny = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]);
    const float nz = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    float sum = nx * nx + ny * ny;
    sum = sum + nz * nz;
    const float length = std::sqrt(sum);
    out[0] = (flip ? -nx : nx) / length;
    out[1] = (flip ? -ny : ny) / length;
    out[2] = (flip ? -nz : nz) / length;
}

// 0x1000c9c0: a mesh from the game's int array (meshmemset). data[0] flags,
// data[1] quad count, data[2] surface slot; from data[6], four records of
// six ints per quad: x, y, z, u, v (each / 256) and a packed normal.
//  - The entry is released; a SYSTEMMEM vertex buffer of FVF 0x112 holds six
//    vertices per quad, triangles (0, 1, 2) and (2, 3, 0).
//  - Vertices whose normal compares equal to 0 (the x87 test is also true
//    for NaN, so a packed 0 counts) take a face normal (flip = flags bit 0):
//    triangle 1's own, and for triangle 2 its own with flags bit 1,
//    otherwise triangle 1's.
//  - flags bit 2 smooths the normals (0x10019090); no game call sets it and
//    it is not transcribed.
//  - The indexed mesh is built as for meshbox (0x10018d40); type 3, one
//    attribute over the whole mesh, the default material and the surface.
// Returns false when the vertex buffer cannot be created (-60).
inline bool mesh_from_memory(Renderer& r, Renderer::Mesh& m, const std::int32_t* data, std::int32_t surface_index) {
    CP2_ZGP_STRICT_FP
    const std::int32_t flags = data[0];
    if (flags & 4) {
        std::fprintf(stderr, "[CP2 ZGP] meshmemset: normal smoothing (flags bit 2, 0x10019090) is not implemented\n");
        return false;
    }
    mesh_release(m);
    const std::uint32_t triangles = static_cast<std::uint32_t>(data[1]) * 2u;
    m.triangle_count = triangles;
    if (!r.device ||
        r.device->create_vertex_buffer(triangles * 32 * 3, 0, 0x112, d3d8::kPoolSystemMem, m.vertices) != d3d8::kOk)
        return false;
    unsigned char* locked = nullptr;
    m.vertices->lock(locked, 0);
    const std::int32_t* records = data + 6;
    auto vertex = [&](std::uint32_t v) { return reinterpret_cast<float*>(locked + static_cast<std::size_t>(v) * 32); };
    auto write = [&](std::uint32_t v, std::uint32_t record) {
        const std::int32_t* rec = records + static_cast<std::size_t>(record) * 6;
        float* out = vertex(v);
        float f[8];
        for (int k = 0; k < 3; ++k) f[k] = static_cast<float>(static_cast<double>(rec[k]) / 256.0);
        unpack_normal(rec[5], f + 3);
        f[6] = static_cast<float>(static_cast<double>(rec[3]) / 256.0);
        f[7] = static_cast<float>(static_cast<double>(rec[4]) / 256.0);
        std::memcpy(out, f, sizeof(f));
    };
    // fcomps / testb $0x40: equal to 0, or unordered (NaN).
    auto is_zero = [](float x) { return !(x < 0.0f || x > 0.0f); };
    float n[3] = {0.0f, 0.0f, 0.0f};  // the face normal last computed
    auto fill_normals = [&](std::uint32_t first, bool compute) {
        if (compute) {
            float a[3], b[3], c[3];
            std::memcpy(a, vertex(first), 12);
            std::memcpy(b, vertex(first + 1), 12);
            std::memcpy(c, vertex(first + 2), 12);
            face_normal(a, b, c, (flags & 1) != 0, n);
        }
        for (std::uint32_t v = first; v < first + 3; ++v) {
            float* p = vertex(v) + 3;
            if (is_zero(p[0]) && is_zero(p[1]) && is_zero(p[2])) std::memcpy(p, n, 12);
        }
    };
    std::uint32_t written = 0;
    for (std::uint32_t q = 0; q < triangles / 2; ++q) {
        const std::uint32_t first = written, base = q * 4;
        static const std::uint32_t order[6] = {0, 1, 2, 2, 3, 0};
        for (std::uint32_t k = 0; k < 6; ++k) write(written++, base + order[k]);
        // Triangle 2 always fills its zero normals; flags bit 1 only decides
        // whether its own face normal is computed first (otherwise triangle
        // 1's is used).
        fill_normals(first, true);
        fill_normals(first + 3, (flags & 2) != 0);
    }
    m.vertices->unlock();
    // The original ignores 0x10018d40's result.
    if (!mesh_index(r, m.vertices, m.indices, triangles, m.vertex_count, 0x112))
        std::fprintf(stderr, "[CP2 ZGP] meshmemset: indexed mesh creation failed\n");
    m.type = 3;
    m.fvf = 0x112;
    m.vertex_size = fvf_vertex_size(m.fvf);
    mesh_attributes(m, 1);
    m.ranges[0] = {{0, 0, triangles, 0, m.vertex_count}};
    m.textures[0] = surface_index;
    return true;
}

// _meshmemset@16 (0x1001ba50): array, mesh. A mesh out of range gives -62,
// a failed build -60. The surface slot data[2] is not checked by the original
// (it computes the pointer from it); `count` is the number of ints the
// variable holds.
inline int meshmemset(const std::int32_t* data, std::size_t count, std::int32_t index) {
    Renderer::Mesh* m = mesh(index);
    if (!m) return -62;
    if (count < 6 || data[1] < 0 || count < 6 + static_cast<std::size_t>(data[1]) * 24) {
        std::fprintf(stderr, "[CP2 ZGP] meshmemset: the array holds %zu ints, fewer than its %d quads need "
                             "(the original reads past it)\n", count, count >= 2 ? data[1] : 0);
        return -60;
    }
    const Table& surfaces = state().surfaces;
    if (data[2] < 0 || data[2] >= surfaces.count)
        std::fprintf(stderr, "[CP2 ZGP] meshmemset: surface %d outside the table (the original uses the address)\n",
                     data[2]);
    if (!mesh_from_memory(renderer(), *m, data, data[2])) return -60;
    return 0;
}

// 0x10018ba0: copies `size` bytes from one buffer into another (both locked
// whole).
inline void buffer_copy(d3d8::Buffer* to, d3d8::Buffer* from, std::uint32_t size) {
    unsigned char* destination = nullptr;
    unsigned char* source = nullptr;
    if (d3d8::failed(to->lock(destination, 0))) return;
    if (!d3d8::failed(from->lock(source, 0))) {
        std::memcpy(destination, source, size);
        from->unlock();
    }
    to->unlock();
}

inline void mesh_load_report(const std::string& file, const std::string& error) {
    std::fprintf(stderr, "[CP2 ZGP] loadx(%s): %s\n", file.c_str(), error.c_str());
}

// 0x1000b040: loads a mesh file into the entry; false on failure.
//  1. The whole file is read (_open in binary mode, relative to the working
//     directory); a file that cannot be opened fails with the entry kept.
//  2. A file starting with "ZPM" (0x10009730) goes to ZGP's own mesh format
//     (0x1000ac60), which no game file uses; it is not transcribed and stops
//     here. Otherwise the entry is released.
//  3. D3DXLoadMeshFromX(file, SYSTEMMEM 0x110, device). The original then
//     compares GetOptions() with the FVF 0x112, so the clone always happens:
//     CloneMeshFVF(options, 0x112) replaces the mesh. If the loaded mesh had
//     no normals (FVF bit 0x10), D3DXComputeNormals fills them. Optimize(2)
//     gives the attribute-sorted mesh.
//  4. Per attribute-table entry i: range i with the attribute id replaced by
//     i, material i = the D3DXMATERIAL of the entry's original id with ambient
//     = diffuse, and the material's texture loaded into own surface i (key 0,
//     the given mip levels; 0x10013a60 does nothing without a file name).
//  5. Type 1, FVF 0x112, MANAGED vertex then index buffers filled from the
//     mesh (index buffer copied first); the D3DX mesh is released.
inline bool mesh_load(Renderer& r, Renderer::Mesh& m, const std::string& file, std::uint32_t mips) {
    CP2_ZGP_STRICT_FP
    std::vector<unsigned char> bytes;
    if (!r.read_file || !r.read_file(file, bytes)) return false;
    if (bytes.size() >= 3 && bytes[0] == 'Z' && bytes[1] == 'P' && bytes[2] == 'M') {
        mesh_load_report(file, "ZGP's own mesh format (ZPM, 0x1000ac60) is not implemented");
        return false;
    }
    mesh_release(m);
    if (!r.device) {
        mesh_load_report(file, "no device (the original would dereference a null device)");
        return false;
    }
    d3d8::Device& device = *r.device;
    xfile::Document document;
    std::string error;
    if (!xfile::parse(bytes.data(), bytes.size(), document, error)) {
        mesh_load_report(file, "D3DXLoadMeshFromX fails: " + error);
        return false;
    }
    std::unique_ptr<d3dx::Mesh> mesh(new d3dx::Mesh());
    std::vector<d3dx::Material> materials;
    if (!d3dx::load_mesh_from_x(document, 0x110, device, *mesh, materials, error)) {
        mesh_load_report(file, "D3DXLoadMeshFromX: " + error);
        return false;
    }
    const bool has_normals = (mesh->fvf & 0x10) != 0;
    if (mesh->options != 0x112) {
        std::unique_ptr<d3dx::Mesh> clone(new d3dx::Mesh());
        const bool cloned = d3dx::clone_mesh_fvf(*mesh, mesh->options, 0x112, device, *clone, error);
        mesh = std::move(clone);  // the old mesh is released either way
        if (!cloned) {
            mesh_load_report(file, "CloneMeshFVF: " + error);
            return false;
        }
    }
    if (!has_normals) {
        auto cross = [](const float a[3], const float b[3], float out[3]) {
            const Vec3 c = zgp::cross(Vec3{a[0], a[1], a[2]}, Vec3{b[0], b[1], b[2]});
            out[0] = c.x;
            out[1] = c.y;
            out[2] = c.z;
        };
        auto normalize = [](float v[3]) {
            const Vec3 n = zgp::normalize(Vec3{v[0], v[1], v[2]});
            v[0] = n.x;
            v[1] = n.y;
            v[2] = n.z;
        };
        if (!d3dx::compute_normals(*mesh, cross, normalize, error)) mesh_load_report(file, "D3DXComputeNormals: " + error);
    }
    {
        std::unique_ptr<d3dx::Mesh> optimized(new d3dx::Mesh());
        const bool done = d3dx::optimize(*mesh, 2, device, *optimized, error);
        mesh = std::move(optimized);
        if (!done) {
            mesh_load_report(file, "Optimize: " + error);
            return false;
        }
    }
    const std::uint32_t count = static_cast<std::uint32_t>(mesh->table.size());
    mesh_attributes(m, count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const d3dx::AttributeRange& a = mesh->table[i];
        m.ranges[i] = {{i, a.face_start, a.face_count, a.vertex_start, a.vertex_count}};
        const d3dx::Material& source = materials[a.id];
        Renderer::Material& material = m.materials[i];
        std::memcpy(material.m, source.d3d, sizeof(material.m));
        for (int k = 0; k < 4; ++k) material.m[4 + k] = material.m[k];
        if (source.has_texture) surface_load(r, m.own_surfaces[i], source.texture, 0, mips);
    }
    m.type = 1;
    m.fvf = 0x112;
    m.vertex_size = fvf_vertex_size(mesh->fvf);
    m.vertex_count = mesh->vertex_count;
    m.triangle_count = mesh->face_count;
    if (device.create_vertex_buffer(m.vertex_count * m.vertex_size, 0, m.fvf, d3d8::kPoolManaged, m.vertices) != d3d8::kOk ||
        device.create_index_buffer(m.triangle_count * 3 * 2, 0, d3d8::kFormatIndex16, d3d8::kPoolManaged, m.indices) !=
            d3d8::kOk) {
        // The original does not check these results.
        mesh_load_report(file, "managed buffer creation failed (the original would use a null buffer)");
        return false;
    }
    buffer_copy(m.indices, mesh->indices, m.triangle_count * 3 * 2);
    buffer_copy(m.vertices, mesh->vertices, m.vertex_count * m.vertex_size);
    return true;
}

// _loadx@16 (0x1001b6e0): mesh, file, mip levels for the material textures.
// A mesh out of range gives -62, a failed load -61.
inline int loadx(std::int32_t index, const std::string& file, std::int32_t mips) {
    Renderer::Mesh* m = mesh(index);
    if (!m) return -62;
    if (!mesh_load(renderer(), *m, file, static_cast<std::uint32_t>(mips))) return -61;
    return 0;
}

// ---------------------------------------------------------------------------
// Objects (instances of meshes).

inline Renderer::Object* object(std::int32_t index) {
    const Table& t = state().units[2];
    if (index < 1 || index >= t.count) return nullptr;
    Renderer& r = renderer();
    if (r.objects.size() != static_cast<std::size_t>(t.count)) r.objects.resize(static_cast<std::size_t>(t.count));
    return &r.objects[static_cast<std::size_t>(index)];
}

inline void set_object_float(Renderer::Object& o, int offset, float f) { std::memcpy(&o.w[offset / 4], &f, 4); }

// 0x10018170: shrinks a bounding box by a mesh's hit size (100 = unchanged):
// each axis moves in by (max - min) * (100 - hit) / 200 at both ends.
inline void hit_box(float box[6], const float hit[3]) {
    CP2_ZGP_STRICT_FP
    float d[3];
    for (int k = 0; k < 3; ++k) d[k] = (box[3 + k] - box[k]) * (100.0f - hit[k]) / 200.0f;
    for (int k = 0; k < 3; ++k) box[k] = d[k] + box[k];
    for (int k = 0; k < 3; ++k) box[3 + k] = box[3 + k] - d[k];
}

// 0x10018370: the bounding radius from the larger absolute bound on each axis,
// sqrt((x * x + y * y) + z * z), each operation rounded like the x87 unit in
// its single-precision mode (_CIsqrt included).
inline float bounding_radius(const float box[6]) {
    CP2_ZGP_STRICT_FP
    float m[3];
    for (int k = 0; k < 3; ++k) {
        const float low = std::fabs(box[k]), high = std::fabs(box[3 + k]);
        m[k] = low < high ? high : low;
    }
    float sum = m[0] * m[0] + m[1] * m[1];
    sum = sum + m[2] * m[2];
    return std::sqrt(sum);
}

// 0x10007310: attaches a mesh to an object. The entry is cleared (0x10006d90)
// and gets type 3, flag 1, the mesh (+0x7c), +0xf0 and +0xfc = 1, the hit box
// (+0x94), the mesh's bounding box (+0xb0), the bounding radius (+0xc8) and an
// identity world matrix at +0x30, whose translation row is the position.
inline void object_mesh(Renderer::Object& o, Renderer::Mesh& m, std::int32_t mesh_index) {
    if (!m.type) return;
    float bounds[6] = {0, 0, 0, 0, 0, 0};
    mesh_bounds(m, bounds);
    float hit[6];
    std::memcpy(hit, bounds, sizeof(hit));
    hit_box(hit, m.hit);
    o = Renderer::Object();
    o.w[0x7c / 4] = static_cast<std::uint32_t>(mesh_index);
    o.w[0x00 / 4] = 3;
    o.w[0x04 / 4] = 1;
    set_object_float(o, 0xf0, 1.0f);
    set_object_float(o, 0xfc, 1.0f);
    for (int k = 0; k < 6; ++k) set_object_float(o, 0x94 + 4 * k, hit[k]);
    for (int k = 0; k < 6; ++k) set_object_float(o, 0xb0 + 4 * k, bounds[k]);
    set_object_float(o, 0xc8, bounding_radius(bounds));
    for (int k = 0; k < 16; ++k) set_object_float(o, 0x30 + 4 * k, k % 5 == 0 ? 1.0f : 0.0f);
}

// _objmeshset@16 (0x1001f310): object, mesh.
inline int objmeshset(std::int32_t index, std::int32_t mesh_index) {
    Renderer::Object* o = object(index);
    if (!o) return -52;
    Renderer::Mesh* m = mesh(mesh_index);
    if (!m) return -62;
    if (!m->type) return -63;
    object_mesh(*o, *m, mesh_index);
    return 0;
}

// D3DXQuaternionRotationYawPitchRoll (0x10024aa1): half angles through
// fsincos (rounded to single precision when stored), then
//   x = cy*sp*cr + sy*cp*sr, y = sy*cp*cr - cy*sp*sr,
//   z = cy*cp*sr - sy*sp*cr, w = sy*sp*sr + cy*cp*cr
// with the original's products and rounding order.
inline void quaternion_yaw_pitch_roll(float yaw, float pitch, float roll, float q[4]) {
    CP2_ZGP_STRICT_FP
    auto sincos = [](float half, float& s, float& c) {
        s = static_cast<float>(std::sin(static_cast<double>(half)));
        c = static_cast<float>(std::cos(static_cast<double>(half)));
    };
    float sr, cr, sp, cp, sy, cy;
    sincos(roll * 0.5f, sr, cr);
    sincos(pitch * 0.5f, sp, cp);
    sincos(yaw * 0.5f, sy, cy);
    const float a = sy * cp, b = cy * sp;
    q[0] = cr * b + a * sr;
    q[1] = a * cr - b * sr;
    const float d = cy * cp, e = sy * sp;
    q[2] = d * sr - e * cr;
    q[3] = e * sr + d * cr;
}

// D3DXMatrixRotationQuaternion (0x1002488b), in the original's order.
inline d3d8::Matrix matrix_from_quaternion(const float q[4]) {
    CP2_ZGP_STRICT_FP
    const float x2 = q[0] * 2.0f, y2 = q[1] * 2.0f, z2 = q[2] * 2.0f;
    const float wx = x2 * q[3], wy = y2 * q[3], wz = z2 * q[3];
    const float xx = x2 * q[0], xy = y2 * q[0], xz = z2 * q[0];
    const float yy = y2 * q[1], yz = z2 * q[1], zz = z2 * q[2];
    const float one_xx = 1.0f - xx;
    d3d8::Matrix m;
    float* o = m.m;
    o[0] = (1.0f - yy) - zz; o[1] = xy + wz;           o[2] = xz - wy;          o[3] = 0.0f;
    o[4] = xy - wz;          o[5] = one_xx - zz;       o[6] = yz + wx;          o[7] = 0.0f;
    o[8] = xz + wy;          o[9] = yz - wx;           o[10] = one_xx - yy;     o[11] = 0.0f;
    o[12] = 0.0f;            o[13] = 0.0f;             o[14] = 0.0f;            o[15] = 1.0f;
    return m;
}

// 0x10006180's angle wrap into [0, 360) degrees, branch for branch: values
// already in range stay; (-360, 0) gets +360; [360, 720) gets -360; anything
// else is fmod 360 (exact), +360 when negative. Single precision.
inline float wrap_object_degrees(float a) {
    CP2_ZGP_STRICT_FP
    if (!(a < 0.0f) && a < 360.0f) return a;
    if (-360.0f < a && a < 0.0f) return a + 360.0f;
    if (!(720.0f <= a) && !(a < 360.0f)) return a - 360.0f;
    a = std::fmod(a, 360.0f);
    if (a < 0.0f) a = a + 360.0f;
    return a;
}

// 0x10006180: the object's angles (degrees). Each is wrapped, the world
// matrix +0x30 becomes D3DXMatrixRotationYawPitchRoll(yaw = y, pitch = x,
// roll = z, each * 0.0174532924f) (0x10024eb9 = quaternion then matrix)
// with the object's position (+0x60..+0x68) as its translation, and the
// wrapped angles are stored at +0x18..+0x20.
inline void object_angles(Renderer::Object& o, float x, float y, float z) {
    CP2_ZGP_STRICT_FP
    x = wrap_object_degrees(x);
    y = wrap_object_degrees(y);
    z = wrap_object_degrees(z);
    const float k = 0.0174532924f;
    float q[4];
    quaternion_yaw_pitch_roll(y * k, x * k, z * k, q);
    d3d8::Matrix m = matrix_from_quaternion(q);
    std::memcpy(&m.m[12], &o.w[0x60 / 4], 12);
    std::memcpy(&o.w[0x30 / 4], m.m, 64);
    set_object_float(o, 0x18, x);
    set_object_float(o, 0x1c, y);
    set_object_float(o, 0x20, z);
}

// 0x100167a0: the pick ray through screen point (x, y) of a width x height
// target. From GetTransform(PROJECTION) and GetTransform(VIEW):
//   v = (((2x / w) - 1) / P11, -((2y / h) - 1) / P22, 1),
//   dir = v times the inverse view's 3x3 (D3DXMatrixInverse), each component
//   summed left to right, and `point` = dir + the inverse's translation.
// `angles` (when given) gets pitch = atan2(-dir.y, sqrt(dx^2 + dz^2)) and
// yaw = atan2(dir.x, dir.z) with dx = 0 - dir.x, dz = 0 - dir.z, each
// * 360 / 2pi and wrapped like object angles; angles[2] is not written.
// `direction` (when given) gets dir. Single precision throughout; the x87
// transcendentals are taken in double and rounded once, as for fsincos.
inline void pick_ray(d3d8::Device& device, std::uint32_t width, std::uint32_t height, std::int32_t x, std::int32_t y,
                     float point[3], float* angles, float* direction) {
    CP2_ZGP_STRICT_FP
    d3d8::Matrix projection, view;
    // A singular view leaves the inverse unwritten (uninitialized stack in
    // the original); the port starts it at zero.
    d3d8::Matrix inverse = {};
    device.get_transform(3, projection);
    const float vx = ((static_cast<float>(x * 2) / static_cast<float>(width)) - 1.0f) / projection.m[0];
    const float vy = (-((static_cast<float>(y * 2) / static_cast<float>(height)) - 1.0f)) / projection.m[5];
    const float vz = 1.0f;
    device.get_transform(2, view);
    matrix_inverse(view.m, inverse.m, nullptr);
    float dir[3];
    for (int c = 0; c < 3; ++c) dir[c] = ((vx * inverse.m[c]) + (vy * inverse.m[4 + c])) + (vz * inverse.m[8 + c]);
    for (int c = 0; c < 3; ++c) point[c] = dir[c] + inverse.m[12 + c];
    if (angles) {
        const float ax = 0.0f - dir[0], az = 0.0f - dir[2], ay = 0.0f - dir[1];
        const float s = std::sqrt((ax * ax) + (az * az));
        const float pitch = static_cast<float>(std::atan2(static_cast<double>(ay), static_cast<double>(s)));
        angles[0] = wrap_object_degrees((pitch * 360.0f) / 6.28318548f);
        const float yaw = static_cast<float>(std::atan2(static_cast<double>(dir[0] - 0.0f), static_cast<double>(dir[2] - 0.0f)));
        angles[1] = wrap_object_degrees((yaw * 360.0f) / 6.28318548f);
    }
    if (direction) std::memcpy(direction, dir, sizeof(dir));
}

// _unproje@16 (0x1001df80): var, x, y. The pick ray through (x, y) of the
// back buffer: var(0..2) = point * standard scale, var(3..5) = angles *
// setmaxangle's first period / 360, var(6..8) = direction * standard scale,
// each truncated (_ftol). The original converts an uninitialized third
// angle into var(5); the port stores 0 there. `count` is the number of ints
// the variable holds. The original always writes nine; HSP allocates every
// variable at least 64 bytes (sbAlloc), so for a smaller int array, such as
// block_check's dim temp_pos, 6, the rest lands in the variable's own unused
// block, where the program never sees it. The port writes the first `count`.
inline int unproje(std::int32_t* out, std::size_t count, std::int32_t x, std::int32_t y) {
    CP2_ZGP_STRICT_FP
    Renderer& r = renderer();
    if (!r.device) {
        std::fprintf(stderr, "[CP2 ZGP] unproje: no device (the original dereferences a null device)\n");
        return 0;
    }
    float point[3], angles[3] = {0.0f, 0.0f, 0.0f}, direction[3];
    pick_ray(*r.device, r.present.width, r.present.height, x, y, point, angles, direction);
    const float scale = state().standard_scale_a, period = state().angle_period_a;
    std::int32_t values[9];
    for (int k = 0; k < 3; ++k) values[k] = zgp_truncate(point[k] * scale);
    for (int k = 0; k < 2; ++k) values[3 + k] = zgp_truncate((angles[k] * period) / 360.0f);
    values[5] = 0;
    for (int k = 0; k < 3; ++k) values[6 + k] = zgp_truncate(direction[k] * scale);
    std::memcpy(out, values, std::min<std::size_t>(count, 9) * sizeof(std::int32_t));
    return 0;
}

// _objang@16 (0x1001ea90): object, x, y, z angles in setmaxangle's first
// period (0x1005bd1c, default 256): each value * 360.0f / period in single
// precision. Out of range: -52.
inline int objang(std::int32_t index, std::int32_t x, std::int32_t y, std::int32_t z) {
    CP2_ZGP_STRICT_FP
    Renderer::Object* o = object(index);
    if (!o) return -52;
    const float period = state().angle_period_a;
    auto degrees = [&](std::int32_t v) { return static_cast<float>(v) * 360.0f / period; };
    object_angles(*o, degrees(x), degrees(y), degrees(z));
    return 0;
}

// _objpos@16 (0x1001e9f0 -> 0x10006120): the world matrix's translation row
// (+0x60..+0x68) = each value / the standard scale (exact for 256). The entry
// does not have to be in use.
inline int objpos(std::int32_t index, std::int32_t x, std::int32_t y, std::int32_t z) {
    Renderer::Object* o = object(index);
    if (!o) return -52;
    const double scale = state().standard_scale_a;
    set_object_float(*o, 0x60, static_cast<float>(static_cast<double>(x) / scale));
    set_object_float(*o, 0x64, static_cast<float>(static_cast<double>(y) / scale));
    set_object_float(*o, 0x68, static_cast<float>(static_cast<double>(z) / scale));
    return 0;
}

// _objlate@16 (0x1001e770 -> 0x10006c80): +0xfc, +0x100, +0x104 = each
// value / 255.0f. The x87 unit loads the integer exactly and rounds the
// quotient once to single precision, which equals float division whenever
// the integer is exact as a float (|value| <= 2^24; the game passes 0).
// Larger values may round differently and are not covered. The original checks
// no index and writes any entry address, including entry 0; outside the
// table it would corrupt memory, so the port reports that and does nothing.
inline int objlate(std::int32_t index, std::int32_t a, std::int32_t b, std::int32_t c) {
    CP2_ZGP_STRICT_FP
    const Table& t = state().units[2];
    if (index < 0 || index >= t.count) {
        std::fprintf(stderr, "[CP2 ZGP] objlate(%d): object outside the table (the original writes out of bounds)\n",
                     index);
        return 0;
    }
    Renderer& r = renderer();
    if (r.objects.size() != static_cast<std::size_t>(t.count)) r.objects.resize(static_cast<std::size_t>(t.count));
    Renderer::Object& o = r.objects[static_cast<std::size_t>(index)];
    const std::int32_t values[3] = {a, b, c};
    for (int k = 0; k < 3; ++k)
        set_object_float(o, 0xfc + 4 * k, static_cast<float>(values[k]) / 255.0f);
    return 0;
}

// _objblend@16 (0x1001e810 -> 0x10006d00): +0x114 = mode. No index check,
// as objlate.
inline int objblend(std::int32_t index, std::int32_t mode) {
    const Table& t = state().units[2];
    if (index < 0 || index >= t.count) {
        std::fprintf(stderr, "[CP2 ZGP] objblend(%d): object outside the table (the original writes out of bounds)\n",
                     index);
        return 0;
    }
    Renderer& r = renderer();
    if (r.objects.size() != static_cast<std::size_t>(t.count)) r.objects.resize(static_cast<std::size_t>(t.count));
    r.objects[static_cast<std::size_t>(index)].w[0x114 / 4] = static_cast<std::uint32_t>(mode);
    return 0;
}

// _objdel@16 (0x1001f270 -> 0x10006d90): zeroes the whole entry (75 DWORDs).
// No index check, as objlate.
inline int objdel(std::int32_t index) {
    const Table& t = state().units[2];
    if (index < 0 || index >= t.count) {
        std::fprintf(stderr, "[CP2 ZGP] objdel(%d): object outside the table (the original writes out of bounds)\n",
                     index);
        return 0;
    }
    Renderer& r = renderer();
    if (r.objects.size() != static_cast<std::size_t>(t.count)) r.objects.resize(static_cast<std::size_t>(t.count));
    r.objects[static_cast<std::size_t>(index)] = Renderer::Object();
    return 0;
}

// _objtype@16 (0x1001e950 -> 0x10006c30): the pick type mask (+0x8) that
// objinst tests, stored only when the entry is in use (+0x00 non-zero).
// Out of range: -52.
inline int objtype(std::int32_t index, std::int32_t type) {
    Renderer::Object* o = object(index);
    if (!o) return -52;
    if (o->w[0]) o->w[0x8 / 4] = static_cast<std::uint32_t>(type);
    return 0;
}

// _objclno@16 (0x1001e840 -> 0x10006d20): links an object to a parent,
// whose local matrix the world matrix multiplies in (object_world_matrix).
// Parent 0 unlinks (+0x8c = 0, +0x90 = 0) without an in-use check; otherwise
// the object must be in use (-53) and +0x8c = the parent (a pointer in the
// original, here its index), +0x90 = the parent index. Both clear bit 0 of
// +0x4 (by subtracting 1 when set). The parent index is not checked (the
// original's second range check tests the object again); the port reports
// one outside the table when the world matrix reaches it. Object out of
// range: -52.
inline int objclno(std::int32_t index, std::int32_t parent) {
    Renderer::Object* o = object(index);
    if (!o) return -52;
    if (parent != 0 && !o->w[0]) return -53;
    o->w[0x8c / 4] = static_cast<std::uint32_t>(parent);
    o->w[0x90 / 4] = static_cast<std::uint32_t>(parent);
    if (o->w[0x4 / 4] & 1u) o->w[0x4 / 4] -= 1;
    return 0;
}

// _objhittype@16 (0x1001e9a0 -> 0x10006c50): +0xac = type, or 0 for an
// animated-model object (+0x88 non-zero). Out of range: -52.
inline int objhittype(std::int32_t index, std::int32_t type) {
    Renderer::Object* o = object(index);
    if (!o) return -52;
    o->w[0xac / 4] = o->w[0x88 / 4] ? 0u : static_cast<std::uint32_t>(type);
    return 0;
}

// ---------------------------------------------------------------------------
// objinst: the nearest object hit by a ray (docs/zgp-renderer.md, "objinst").

// D3DXVec3TransformCoord (0x10024ccc): each component ((x m0j + y m1j) +
// z m2j) + m3j; divided by w (times 1 / w) unless w - 1 lies within
// [-1e-5, 1e-5] (an unordered w divides too).
inline void transform_coord(float out[3], const float v[3], const d3d8::Matrix& m) {
    CP2_ZGP_STRICT_FP
    float r[4];
    for (int j = 0; j < 4; ++j) r[j] = ((v[0] * m.m[j] + v[1] * m.m[4 + j]) + v[2] * m.m[8 + j]) + m.m[12 + j];
    const float d = r[3] - 1.0f;
    if (!(d >= -9.99999974737875e-06f && d <= 9.99999974737875e-06f)) {
        const float inverse = 1.0f / r[3];
        for (int j = 0; j < 3; ++j) r[j] = inverse * r[j];
    }
    std::memcpy(out, r, 12);
}

// 0x10007040: an object's local matrix, Scaling(s, s, s) times +0x30 when
// the scale +0xf0 is not 1 (an unordered scale counts as 1), else +0x30.
inline d3d8::Matrix object_local_matrix(const Renderer::Object& o) {
    CP2_ZGP_STRICT_FP
    d3d8::Matrix m;
    std::memcpy(m.m, &o.w[0x30 / 4], 64);
    float s;
    std::memcpy(&s, &o.w[0xf0 / 4], 4);
    if (s < 1.0f || s > 1.0f) {
        d3d8::Matrix scaling;
        std::memset(scaling.m, 0, sizeof(scaling.m));
        scaling.m[0] = scaling.m[5] = scaling.m[10] = s;
        scaling.m[15] = 1.0f;
        m = matrix_multiply(scaling, m);
    }
    return m;
}

// 0x100070e0: the world matrix, the local matrix times each parent's
// (+0x8c, at most 256 links; set by objclno). The original links by
// pointer; the port holds the parent's object index there.
inline d3d8::Matrix object_world_matrix(const Renderer::Object& o) {
    d3d8::Matrix m = object_local_matrix(o);
    const Renderer::Object* link = &o;
    for (int k = 0; k < 256 && link->w[0x8c / 4]; ++k) {
        const std::size_t parent = link->w[0x8c / 4];
        Renderer& r = renderer();
        if (parent >= r.objects.size()) {
            std::fprintf(stderr, "[CP2 ZGP] object parent %zu is outside the table\n", parent);
            break;
        }
        link = &r.objects[parent];
        m = matrix_multiply(m, object_local_matrix(*link));
    }
    return m;
}

// Box corners and triangles of 0x10017ec0 (the table at 0x1005bb18).
constexpr int kBoxCorners[8][3] = {{0, 1, 2}, {3, 1, 2}, {3, 1, 5}, {0, 1, 5},
                                   {0, 4, 2}, {3, 4, 2}, {3, 4, 5}, {0, 4, 5}};
constexpr int kBoxTriangles[36] = {0, 1, 2, 2, 3, 0, 7, 6, 5, 5, 4, 7, 0, 3, 7, 7, 4, 0,
                                   2, 1, 5, 5, 6, 2, 4, 5, 1, 1, 0, 4, 3, 2, 6, 6, 7, 3};

// 0x10002b90: the object's world-space triangles (three points each). With
// no hit type (+0xac) or an animated model (+0x88): twelve from the hit box
// +0x94 (min x, y, z, max x, y, z; 0x10017ec0 transforms its eight corners).
// Otherwise the mesh (+0x7c): 0x10002a70 locks its index and vertex buffers
// and transforms each 16-bit-indexed position (vertex stride +0x18) for
// triangle count +0x24. Returns false where the original would dereference
// a missing mesh.
inline bool object_triangles(Renderer::Object& o, const d3d8::Matrix& world, std::vector<float>& points) {
    if (!o.w[0xac / 4] || o.w[0x88 / 4]) {
        float box[6], corners[8][3];
        std::memcpy(box, &o.w[0x94 / 4], sizeof(box));
        for (int c = 0; c < 8; ++c) {
            const float p[3] = {box[kBoxCorners[c][0]], box[kBoxCorners[c][1]], box[kBoxCorners[c][2]]};
            transform_coord(corners[c], p, world);
        }
        points.resize(36 * 3);
        for (int k = 0; k < 36; ++k) std::memcpy(&points[k * 3], corners[kBoxTriangles[k]], 12);
        return true;
    }
    Renderer::Mesh* m = mesh(static_cast<std::int32_t>(o.w[0x7c / 4]));
    if (!m || !m->indices || !m->vertices) {
        std::fprintf(stderr, "[CP2 ZGP] objinst: object mesh %u has no buffers (the original dereferences null)\n",
                     o.w[0x7c / 4]);
        return false;
    }
    unsigned char* index_data = nullptr;
    unsigned char* vertex_data = nullptr;
    m->indices->lock(index_data, 0);
    m->vertices->lock(vertex_data, 0);
    const std::size_t count = static_cast<std::size_t>(m->triangle_count) * 3;
    points.resize(count * 3);
    for (std::size_t k = 0; k < count; ++k) {
        std::uint16_t index;
        std::memcpy(&index, index_data + 2 * k, 2);
        float p[3];
        std::memcpy(p, vertex_data + static_cast<std::size_t>(index) * m->vertex_size, 12);
        transform_coord(&points[k * 3], p, world);
    }
    m->indices->unlock();
    m->vertices->unlock();
    return true;
}

// 0x10017700: a one-sided ray/triangle test (Moller-Trumbore without the
// early division). e1 = b - a, e2 = c - a, p = dir x e2, det = e1 . p; no
// hit unless det >= 1e-4, 0 <= u <= det, v >= 0 and u + v <= det; t =
// (e2 . q) * (1 / det) must be > 0 and is stored either way. Each dot
// product is z + (y + x) as the x87 code sums it.
inline bool ray_triangle(const float o[3], const float d[3], const float* a, const float* b, const float* c,
                         float& t) {
    CP2_ZGP_STRICT_FP
    const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const float p[3] = {e2[2] * d[1] - e2[1] * d[2], e2[0] * d[2] - e2[2] * d[0], e2[1] * d[0] - e2[0] * d[1]};
    const float det = e1[2] * p[2] + (e1[1] * p[1] + e1[0] * p[0]);
    if (!(det >= 9.99999974737875e-05f)) return false;
    const float s[3] = {o[0] - a[0], o[1] - a[1], o[2] - a[2]};
    const float u = s[2] * p[2] + (s[1] * p[1] + s[0] * p[0]);
    if (!(u >= 0.0f) || !(u <= det)) return false;
    const float q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
    const float v = q[2] * d[2] + (q[1] * d[1] + q[0] * d[0]);
    if (!(v >= 0.0f) || !(u + v <= det)) return false;
    t = e2[2] * q[2] + (e2[1] * q[1] + e2[0] * q[0]);
    const float inverse = 1.0f / det;
    t = inverse * t;
    return t > 0.0f;
}

// "Not yet set" distance (-1.0f by its bits, as the original compares it).
inline bool unset_distance(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits == 0xbf800000u;
}

// 0x10005c10: the nearest hit of one object. A hit replaces the best when
// nearer (an unordered compare counts as nearer) or while none is set. The
// hit point is origin + dir * distance.
inline bool object_ray(Renderer::Object& o, const float origin[3], const float dir[3], float hit[3], float& distance) {
    CP2_ZGP_STRICT_FP
    std::vector<float> points;
    if (!object_triangles(o, object_world_matrix(o), points)) return false;
    float best = -1.0f;
    bool any = false;
    for (std::size_t k = 0; k + 9 <= points.size(); k += 9) {
        float t;
        if (!ray_triangle(origin, dir, &points[k], &points[k + 3], &points[k + 6], t)) continue;
        any = true;
        if (!(t >= best) || unset_distance(best)) {
            best = t;
            distance = t;
        }
    }
    if (!any) return false;
    for (int k = 0; k < 3; ++k) hit[k] = origin[k] + dir[k] * distance;
    return true;
}

// _objinst@16 (0x1001f990 -> 0x10003320): var, first object, count, type
// mask. The ray's origin is var(0..2) / the standard scale and its
// direction var(3..5) (normalized, D3DXVec3Normalize). Objects first ..
// first + count - 1 (count 0: all; clipped to the table) whose pick type
// (+0x8) shares a bit with the mask (0: any) are tested; the nearest hit
// sets var(0..2) = the point and var(3) = the distance, each * the
// standard scale, truncated, and returns -index (stat = index). No hit:
// var(0..3) = 0, returns 0. First outside 1 .. table size - 1: -52.
// `count_ints` is the number of ints the variable holds.
inline int objinst(std::int32_t* v, std::size_t count_ints, std::int32_t first, std::int32_t count, std::int32_t mask) {
    CP2_ZGP_STRICT_FP
    const std::int32_t size = state().units[2].count;
    if (first < 1 || first >= size) return -52;
    if (count_ints < 6) {
        std::fprintf(stderr, "[CP2 ZGP] objinst: the variable holds %zu ints, fewer than 6\n", count_ints);
        return 0;
    }
    if (count == 0) count = size;
    if (first + count >= size) count = size - first;
    const float scale = state().standard_scale_a;
    float origin[3], direction[3];
    for (int k = 0; k < 3; ++k) {
        origin[k] = static_cast<float>(static_cast<double>(v[k]) / scale);
        direction[k] = static_cast<float>(v[3 + k]);
    }
    const Vec3 n = normalize(Vec3{direction[0], direction[1], direction[2]});
    direction[0] = n.x;
    direction[1] = n.y;
    direction[2] = n.z;
    const std::uint32_t types = mask ? static_cast<std::uint32_t>(mask) : 0xffffffffu;
    object(first);  // sizes the table
    Renderer& r = renderer();
    float best = -1.0f, best_hit[3] = {0.0f, 0.0f, 0.0f};
    std::int32_t found = -1;
    for (std::int32_t k = 0; k < count; ++k) {
        Renderer::Object& o = r.objects[static_cast<std::size_t>(first + k)];
        if (!(o.w[0x8 / 4] & types)) continue;
        float hit[3], distance = 0.0f;
        if (!object_ray(o, origin, direction, hit, distance)) continue;
        if (!(distance >= best) || unset_distance(best)) {
            found = first + k;
            best = distance;
            std::memcpy(best_hit, hit, sizeof(hit));
        }
    }
    if (found == -1) {
        for (int k = 0; k < 4; ++k) v[k] = 0;
        return 0;
    }
    for (int k = 0; k < 3; ++k) v[k] = zgp_truncate(best_hit[k] * scale);
    v[3] = zgp_truncate(best * scale);
    return -found;
}

// 0x100071a0: an object's world position, the world matrix's translation
// with a parent (+0x8c), else the position +0x60..+0x68 itself.
inline void object_world_position(const Renderer::Object& o, float p[3]) {
    if (o.w[0x8c / 4]) {
        const d3d8::Matrix m = object_world_matrix(o);
        std::memcpy(p, &m.m[12], 12);
    } else {
        std::memcpy(p, &o.w[0x60 / 4], 12);
    }
}

// _objcat@16 (0x1001eb40 -> 0x10006480 with a zero fourth argument): turns
// an object toward (x, y, z) / the standard scale. +0xcc = 0; from the
// world position p, pitch = atan2(p.y - y, sqrt((p.x - x)^2 + (p.z - z)^2))
// and yaw = atan2(x - p.x, z - p.z), each (x87: the unrounded atan2 * 360,
// then / 2pi in single precision) wrapped like object angles and passed
// with roll 0 to the objang core (0x10006180). No in-use check. Out of
// range: -52.
inline int objcat(std::int32_t index, std::int32_t x, std::int32_t y, std::int32_t z) {
    CP2_ZGP_STRICT_FP
    Renderer::Object* o = object(index);
    if (!o) return -52;
    const float scale = state().standard_scale_a;
    const float tx = static_cast<float>(static_cast<double>(x) / scale);
    const float ty = static_cast<float>(static_cast<double>(y) / scale);
    const float tz = static_cast<float>(static_cast<double>(z) / scale);
    o->w[0xcc / 4] = 0;
    float p[3];
    object_world_position(*o, p);
    const float a = p[0] - tx, b = p[2] - tz, dy = p[1] - ty;
    const float horizontal = std::sqrt((a * a) + (b * b));
    const float pitch_turns =
        static_cast<float>(std::atan2(static_cast<double>(dy), static_cast<double>(horizontal)) * 360.0);
    const float yaw_turns =
        static_cast<float>(std::atan2(static_cast<double>(tx - p[0]), static_cast<double>(tz - p[2])) * 360.0);
    const float pitch = wrap_object_degrees(pitch_turns / 6.28318548f);
    const float yaw = wrap_object_degrees(yaw_turns / 6.28318548f);
    object_angles(*o, pitch, yaw, 0.0f);
    return 0;
}

// msvcrt's _CIasin (x87 path; its SSE2 path is off unless enabled). It keeps
// the caller's precision bits ((cw & 0x300) | 0x7f, 0x100a55d5 in the
// system's 32-bit msvcrt), so under Direct3D's single precision
// fpatan(x, fsqrt((1 + x) * (1 - x))) rounds the sum, difference, product
// and root to single precision; fpatan itself is not rounded by the
// precision control, and the result stays unrounded in st0.
inline double x87_asin(float x) {
    CP2_ZGP_STRICT_FP
    const float sum = 1.0f + x, difference = 1.0f - x;
    const float root = std::sqrt(sum * difference);
    return std::atan2(static_cast<double>(x), static_cast<double>(root));
}

// 0x10009670 (and the inline copies): wraps into [0, period) like object
// angles (wrap_object_degrees for a period of 360).
inline float wrap_period(float a, float period) {
    CP2_ZGP_STRICT_FP
    if (!(a < 0.0f) && a < period) return a;
    if (-period < a && a < 0.0f) return period + a;
    if (!(period + period <= a) && !(a < period)) return a - period;
    a = static_cast<float>(std::fmod(static_cast<double>(a), static_cast<double>(period)));
    if (a < 0.0f) a = period + a;
    return a;
}

// 0x10017370: angles (degrees) from a rotation matrix: yaw = atan2(m20, m22),
// pitch = -asin(m21), roll = asin(m01 / sqrt(1 - m21^2)), or pi minus that
// when m11 < 0 (an unordered m11 takes only that branch). Each stored in
// single precision, then * 180 / pi and wrapped into [0, 360).
inline void matrix_angles(const d3d8::Matrix& m, float out[3]) {
    CP2_ZGP_STRICT_FP
    out[1] = static_cast<float>(std::atan2(static_cast<double>(m.m[8]), static_cast<double>(m.m[10])));
    out[0] = static_cast<float>(-x87_asin(m.m[9]));
    const float ratio = m.m[1] / std::sqrt(1.0f - m.m[9] * m.m[9]);
    if (m.m[5] >= 0.0f) out[2] = static_cast<float>(x87_asin(ratio));
    if (!(m.m[5] >= 0.0f)) out[2] = static_cast<float>(3.14159274101257 - x87_asin(ratio));
    for (int k = 0; k < 3; ++k) out[k] = wrap_period((out[k] * 180.0f) / 3.14159274101257f, 360.0f);
}

// 0x10008140: sets the camera angles (pitch, yaw, roll in degrees, wrapped),
// its direction (-sin(pitch) * 256 for y; cos(pitch) * 256 times cos(yaw) for
// z and sin(yaw) for x) and its up vector, normalize(TransformCoord((0, 1,
// 0), RotationYawPitchRoll)), each angle * pi / 180; then the camera update.
inline void camera_set_angles(Renderer& r, const float angles[3]) {
    CP2_ZGP_STRICT_FP
    Camera& c = state().camera;
    for (int k = 0; k < 3; ++k) c.angles[k] = wrap_period(angles[k], 360.0f);
    const float pi = 3.14159274101257f;
    const float pitch = (c.angles[0] * pi) / 180.0f;
    const float yaw = (c.angles[1] * pi) / 180.0f;
    const float roll = (c.angles[2] * pi) / 180.0f;
    c.direction[1] = static_cast<float>(-std::sin(static_cast<double>(pitch)) * 256.0);
    const float horizontal = static_cast<float>(std::cos(static_cast<double>(pitch)) * 256.0);
    c.direction[2] = static_cast<float>(std::cos(static_cast<double>(yaw)) * static_cast<double>(horizontal));
    c.direction[0] = static_cast<float>(std::sin(static_cast<double>(yaw)) * static_cast<double>(horizontal));
    float q[4];
    quaternion_yaw_pitch_roll(yaw, pitch, roll, q);
    const d3d8::Matrix rotation = matrix_from_quaternion(q);
    const float y_axis[3] = {0.0f, 1.0f, 0.0f};
    float up[3];
    transform_coord(up, y_axis, rotation);
    const Vec3 n = normalize(Vec3{up[0], up[1], up[2]});
    c.up[0] = n.x;
    c.up[1] = n.y;
    c.up[2] = n.z;
    camera_update(r);
}

// _z_mat@16 (0x1001d940): var(0..2), rotated by D3DXMatrixRotationYawPitchRoll
// (0x10024eb9) with yaw = y, pitch = x, roll = z, each value * 360 /
// setmaxangle's first period * pi / 180 in single precision; the vector
// (each int as a float) goes through D3DXVec3TransformCoord and is stored
// truncated. Needs no device. `count` is the number of ints the variable holds.
inline int z_mat(std::int32_t* v, std::size_t count, std::int32_t x, std::int32_t y, std::int32_t z) {
    CP2_ZGP_STRICT_FP
    if (count < 3) {
        std::fprintf(stderr, "[CP2 ZGP] z_mat: the variable holds %zu ints, fewer than 3\n", count);
        return 0;
    }
    const float period = state().angle_period_a;
    auto radians = [&](std::int32_t a) { return ((static_cast<float>(a) * 360.0f / period) * 3.14159274101257f) / 180.0f; };
    float q[4];
    quaternion_yaw_pitch_roll(radians(y), radians(x), radians(z), q);
    const d3d8::Matrix m = matrix_from_quaternion(q);
    const float in[3] = {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2])};
    float out[3];
    transform_coord(out, in, m);
    for (int k = 0; k < 3; ++k) v[k] = zgp_truncate(out[k]);
    return 0;
}

// _meshzoom@16 (0x1001c440 -> 0x1000a450): mesh, x, y, z. Scales the
// positions (the first three floats of every vertex) in the mesh's vertex
// buffer by each value / standard scale; with y and z both 0, all three use
// x. Out of range -62, an empty entry -63. The buffer is locked whole with
// flags 0 (the original ignores a failed lock).
inline int meshzoom(std::int32_t index, std::int32_t x, std::int32_t y, std::int32_t z) {
    CP2_ZGP_STRICT_FP
    Renderer::Mesh* m = mesh(index);
    if (!m) return -62;
    if (!m->type) return -63;
    const double scale = state().standard_scale_a;
    float s[3] = {static_cast<float>(static_cast<double>(x) / scale), static_cast<float>(static_cast<double>(y) / scale),
                  static_cast<float>(static_cast<double>(z) / scale)};
    if (s[1] == 0.0f && s[2] == 0.0f) s[1] = s[2] = s[0];
    unsigned char* data = nullptr;
    if (!m->vertices || d3d8::failed(m->vertices->lock(data, 0))) {
        std::fprintf(stderr, "[CP2 ZGP] meshzoom: mesh %d has no lockable vertex buffer\n", index);
        return 0;
    }
    const std::size_t stride = static_cast<std::size_t>(m->vertex_size >> 2) * 4;
    for (std::uint32_t i = 0; i < m->vertex_count; ++i) {
        float p[3];
        std::memcpy(p, data + i * stride, sizeof(p));
        for (int k = 0; k < 3; ++k) p[k] = s[k] * p[k];
        std::memcpy(data + i * stride, p, sizeof(p));
    }
    m->vertices->unlock();
    return 0;
}

// _meshmaterial@16 (0x1001be40): var, mesh, material. Sets one D3DMATERIAL8
// of a mesh from five ints: var(0) diffuse, var(1) ambient, var(2) specular,
// var(4) emissive, each 0xAARRGGBB with every byte / 255; var(3) / 100 is the
// power. Out of range -62, an empty entry -63, a material index not below
// the attribute count (unsigned) -4. `count` is the number of ints the
// variable holds.
inline int meshmaterial(const std::int32_t* v, std::size_t count, std::int32_t index, std::int32_t material) {
    CP2_ZGP_STRICT_FP
    Renderer::Mesh* m = mesh(index);
    if (!m) return -62;
    if (!m->type) return -63;
    if (static_cast<std::uint32_t>(material) >= m->attribute_count) return -4;
    if (count < 5) {
        std::fprintf(stderr, "[CP2 ZGP] meshmaterial: the variable holds %zu ints, fewer than 5 (the original reads past it)\n",
                     count);
        return 0;
    }
    if (static_cast<std::size_t>(material) >= m->materials.size()) m->materials.resize(static_cast<std::size_t>(material) + 1);
    float* out = m->materials[static_cast<std::size_t>(material)].m;
    auto color = [](std::int32_t value, float* rgba) {
        rgba[3] = static_cast<float>((value >> 24) & 0xff) / 255.0f;
        rgba[0] = static_cast<float>((value >> 16) & 0xff) / 255.0f;
        rgba[1] = static_cast<float>((value >> 8) & 0xff) / 255.0f;
        rgba[2] = static_cast<float>(value & 0xff) / 255.0f;
    };
    color(v[0], out);
    color(v[1], out + 4);
    color(v[2], out + 8);
    // fild, then a single-precision fdiv: one rounding of the exact quotient.
    const std::int32_t power = v[3];
    out[16] = (power > -16777216 && power < 16777216) ? static_cast<float>(power) / 100.0f
                                                       : static_cast<float>(static_cast<double>(power) / 100.0);
    color(v[4], out + 12);
    return 0;
}

// _pos3@16 (0x1001ce60): the start of the next line3d (0x1005faf0), each
// value / standard scale. No Direct3D calls.
inline int pos3(std::int32_t x, std::int32_t y, std::int32_t z) {
    Renderer& r = renderer();
    const double scale = state().standard_scale_a;
    r.line_start[0] = static_cast<float>(static_cast<double>(x) / scale);
    r.line_start[1] = static_cast<float>(static_cast<double>(y) / scale);
    r.line_start[2] = static_cast<float>(static_cast<double>(z) / scale);
    return 0;
}

// _line3d@16 (0x1001b310 -> 0x100094e0): a line from pos3's point to (x, y,
// z) / standard scale in setcolor4's colour: SetTransform(WORLD, identity),
// SetVertexShader(XYZ | DIFFUSE), DrawPrimitiveUP(LINELIST, 1, 16-byte
// vertices). pos3's point is not moved.
inline int line3d(std::int32_t x, std::int32_t y, std::int32_t z) {
    Renderer& r = renderer();
    const double scale = state().standard_scale_a;
    const float end[3] = {static_cast<float>(static_cast<double>(x) / scale),
                          static_cast<float>(static_cast<double>(y) / scale),
                          static_cast<float>(static_cast<double>(z) / scale)};
    if (!r.device) {
        std::fprintf(stderr, "[CP2 ZGP] line3d: no device (the original dereferences a null device)\n");
        return 0;
    }
    d3d8::Matrix identity = {};
    for (int k = 0; k < 4; ++k) identity.m[k * 5] = 1.0f;
    r.device->set_transform(256, identity);
    unsigned char vertices[32];
    std::memcpy(vertices, r.line_start, 12);
    std::memcpy(vertices + 12, &r.color4, 4);
    std::memcpy(vertices + 16, end, 12);
    std::memcpy(vertices + 28, &r.color4, 4);
    r.device->set_vertex_shader(0x42);
    r.device->draw_primitive_up(2, 1, vertices, 16);
    return 0;
}

// _proje@16 (0x1001dee0 -> 0x10002950): var, x, y, z. The point (x, y, z) /
// standard scale projected by D3DXVec3Project (0x10024d9c) with the device's
// viewport, projection and view (no world: view * projection, then
// D3DXVec3TransformCoord and the viewport mapping
//   x = ((x + 1) * W) * 0.5 + X, y = ((1 - y) * H) * 0.5 + Y,
//   z = (MaxZ - MinZ) * z + MinZ),
// then mapped to logical coordinates through the 2D object:
// x = logical width * x / viewport width - viewport left (likewise y), each
// step in single precision. var(0..2) get the three values truncated.
inline int proje(std::int32_t* out, std::size_t count, std::int32_t x, std::int32_t y, std::int32_t z) {
    CP2_ZGP_STRICT_FP
    Renderer& r = renderer();
    const double scale = state().standard_scale_a;
    const float v[3] = {static_cast<float>(static_cast<double>(x) / scale),
                        static_cast<float>(static_cast<double>(y) / scale),
                        static_cast<float>(static_cast<double>(z) / scale)};
    if (!r.device) {
        std::fprintf(stderr, "[CP2 ZGP] proje: no device (the original dereferences a null device)\n");
        return 0;
    }
    d3d8::Viewport viewport;
    d3d8::Matrix projection, view;
    r.device->get_viewport(viewport);
    r.device->get_transform(3, projection);
    r.device->get_transform(2, view);
    float p[3];
    transform_coord(p, v, matrix_multiply(view, projection));
    p[0] = (((p[0] + 1.0f) * static_cast<float>(viewport.width)) * 0.5f) + static_cast<float>(viewport.x);
    p[1] = (((1.0f - p[1]) * static_cast<float>(viewport.height)) * 0.5f) + static_cast<float>(viewport.y);
    p[2] = ((viewport.max_z - viewport.min_z) * p[2]) + viewport.min_z;
    const float sx = ((static_cast<float>(r.logical_width) * p[0]) /
                      static_cast<float>(static_cast<std::int32_t>(r.viewport.width))) -
                     static_cast<float>(static_cast<std::int32_t>(r.viewport.x));
    const float sy = ((static_cast<float>(r.logical_height) * p[1]) /
                      static_cast<float>(static_cast<std::int32_t>(r.viewport.height))) -
                     static_cast<float>(static_cast<std::int32_t>(r.viewport.y));
    const std::int32_t values[3] = {zgp_truncate(sx), zgp_truncate(sy), zgp_truncate(p[2])};
    if (count < 3)
        std::fprintf(stderr, "[CP2 ZGP] proje: the variable holds %zu ints, fewer than 3 (the original writes past it)\n",
                     count);
    std::memcpy(out, values, std::min<std::size_t>(count, 3) * sizeof(std::int32_t));
    return 0;
}

// _getfrt@16 (0x1001cef0): var. Counts calls (0x100605e8); once
// GetTickCount has advanced 1000 ms or more (signed difference) since the
// last report (0x100605f0, initially 0), stores the count in var, restarts
// it and returns 0; otherwise returns -1 and leaves var.
inline int getfrt(std::int32_t* out, std::size_t count) {
    Renderer& r = renderer();
    ++r.frame_count;
    const std::uint32_t now = r.tick_count ? r.tick_count() : default_tick_count();
    if (static_cast<std::int32_t>(now - r.frame_report_tick) < 1000) return -1;
    r.frame_rate = r.frame_count;
    r.frame_count = 0;
    r.frame_report_tick = now;
    if (count >= 1) *out = r.frame_rate;
    return 0;
}

// _cursor@16 (0x1001ce40): SetCursor(NULL), then ShowCursor(flag). The
// mouse cursor of a Windows window; iOS has none, so there is nothing to
// show or hide.
inline int cursor(std::int32_t) { return 0; }

// _clink@16 (0x1001e6c0): links the camera to an object. Mode 0 means 3;
// for an object in use, bit 0 sets the camera eye to its world position
// (0x100080e0, then the camera update) and bit 1 the camera angles to its
// world matrix's (0x10007270 -> 0x10017370, then 0x10008140). Out of range:
// -52.
inline int clink(std::int32_t index, std::int32_t mode) {
    CP2_ZGP_STRICT_FP
    Renderer::Object* o = object(index);
    if (!o) return -52;
    if (mode == 0) mode = 3;
    if (!o->w[0]) return 0;
    Renderer& r = renderer();
    if (!r.device) {
        std::fprintf(stderr, "[CP2 ZGP] clink: no device (the original dereferences a null device)\n");
        return 0;
    }
    if (mode & 1) {
        object_world_position(*o, state().camera.eye);
        camera_update(r);
    }
    if (mode & 2) {
        float angles[3];
        matrix_angles(object_world_matrix(*o), angles);
        camera_set_angles(r, angles);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Animated models (the animation-library slots; docs/csmesh-abi.md).

// The mesh builder CSMesh calls from LoadMesh: D3DX resources on ZGP's
// current device (CSMesh reads ZGP's device pointer when it loads), files
// relative to the working directory.
inline void install_mesh_builder(csmesh::Library& library) {
    library.set_mesh_builder([](const xfile::Object& mesh, const xfile::Document& document,
                                std::shared_ptr<void>& resources, std::string& error) {
        Renderer& r = renderer();
        if (!r.device) {
            error = "a model was loaded before createdevm (CSMesh would dereference a null device)";
            return false;
        }
        std::shared_ptr<csmesh::MeshResources> built(new csmesh::MeshResources());
        csmesh::FileReader reader = [&r](const std::string& path, std::vector<std::uint8_t>& data) {
            std::vector<unsigned char> bytes;
            if (!r.read_file || !r.read_file(path, bytes)) return false;
            data.assign(bytes.begin(), bytes.end());
            return true;
        };
        if (!csmesh::load_mesh_resources(mesh, document, *r.device, reader, *built, error)) return false;
        resources = built;
        return true;
    });
}

// anilibload's registration of the original CSMesh in slot id (0-3; the
// caller checks the id and the plugin file): an occupied slot is shut down
// first (Delete, 0x1000e580), then Init(&device, count) and the slot is
// selected. Returns 0.
inline int register_animation_library(std::int32_t id, const std::string& path, std::int32_t count) {
    Renderer& r = renderer();
    Renderer::AnimationLibrary& slot = r.animation_libraries[id & 3];
    if (slot.loaded) {
        slot.library.destroy();
        slot.loaded = false;
        slot.path.clear();
    }
    slot.library.init(&r.device, count);
    install_mesh_builder(slot.library);
    slot.loaded = true;
    slot.path = path;
    r.animation_selected = id;
    return 0;
}

// The selected slot's library, or null when no plugin was registered there.
// The original calls through the slot's function pointers without checking
// them, so an empty slot would crash it; the port reports it.
inline csmesh::Library* animation_library(const char* command) {
    Renderer& r = renderer();
    Renderer::AnimationLibrary& slot = r.animation_libraries[r.animation_selected & 3];
    if (slot.loaded) return &slot.library;
    std::fprintf(stderr, "[CP2 ZGP] %s: animation-library slot %d is empty (the original would call a null pointer)\n",
                 command, r.animation_selected);
    return nullptr;
}

// _loadax@16 (0x10020490): Load(model, file, p3) in the selected library
// (CSMesh opens the file relative to the working directory); on failure ZGP
// error 5. Then SetRate(model, 1.0) and SetHitSize(model, 100, 100, 100).
inline int loadax(std::int32_t model, const std::string& file, std::int32_t) {
    Renderer& r = renderer();
    csmesh::Library* library = animation_library("loadax");
    if (!library) return -5;
    std::vector<unsigned char> bytes;
    const bool found = r.read_file && r.read_file(file, bytes);
    std::string error;
    if (!library->load(model, file, found ? bytes.data() : nullptr, bytes.size(), error)) {
        std::fprintf(stderr, "[CP2 ZGP] loadax(%d, %s): %s\n", model, file.c_str(), error.c_str());
        return -5;
    }
    library->set_rate(model, 1.0f);
    library->set_hit_size(model, 100.0f, 100.0f, 100.0f);
    return 0;
}

// _anirate@16 (0x10020650): SetRate(model, rate / 1000); ZGP error 5 when
// the slot has no model. The quotient is rounded once to single precision,
// as the x87 unit does in Direct3D's precision mode (exact for the game's
// rates; float(rate) is exact up to 2^24).
inline int anirate(std::int32_t model, std::int32_t rate) {
    CP2_ZGP_STRICT_FP
    csmesh::Library* library = animation_library("anirate");
    if (!library) return -5;
    return library->set_rate(model, static_cast<float>(rate) / 1000.0f) ? 0 : -5;
}

// 0x10007490: attaches an animated model to an object. As objmeshset, from
// the model's bounds: GetMeshMinMax(model, box, all meshes, unanimated),
// shrunk by GetHitSize; the entry is cleared and gets type 5, flag 1, the
// library id (+0x84) and model (+0x88), the boxes, the bounding radius and an
// identity world matrix.
inline void object_animated_mesh(Renderer::Object& o, csmesh::Library& library, std::int32_t id, std::int32_t model) {
    float bounds[6] = {0, 0, 0, 0, 0, 0};
    library.get_mesh_minmax(model, bounds, nullptr, 0);
    float hit_size[3] = {0, 0, 0};
    library.get_hit_size(model, &hit_size[0], &hit_size[1], &hit_size[2]);
    float hit[6];
    std::memcpy(hit, bounds, sizeof(hit));
    hit_box(hit, hit_size);
    o = Renderer::Object();
    o.w[0x84 / 4] = static_cast<std::uint32_t>(id);
    o.w[0x88 / 4] = static_cast<std::uint32_t>(model);
    o.w[0x00 / 4] = 5;
    o.w[0x04 / 4] = 1;
    set_object_float(o, 0xf0, 1.0f);
    set_object_float(o, 0xfc, 1.0f);
    for (int k = 0; k < 6; ++k) set_object_float(o, 0x94 + 4 * k, hit[k]);
    for (int k = 0; k < 6; ++k) set_object_float(o, 0xb0 + 4 * k, bounds[k]);
    set_object_float(o, 0xc8, bounding_radius(bounds));
    for (int k = 0; k < 16; ++k) set_object_float(o, 0x30 + 4 * k, k % 5 == 0 ? 1.0f : 0.0f);
}

// _objameshset@16 (0x1001f3a0): object, model of the selected library.
inline int objameshset(std::int32_t index, std::int32_t model) {
    Renderer::Object* o = object(index);
    if (!o) return -52;
    csmesh::Library* library = animation_library("objameshset");
    if (!library) return -5;
    if (!library->model(model)) {
        std::fprintf(stderr, "[CP2 ZGP] objameshset(%d, %d): no model loaded (the original reads uninitialized bounds)\n",
                     index, model);
        return -5;
    }
    object_animated_mesh(*o, *library, renderer().animation_selected, model);
    return 0;
}


// _texwrite24@16 (0x1001ac60 -> 0x10013e10): var (24-bit B, G, R pixels,
// rows of width * 3 bytes), surface, alpha, mode. Surface outside 1 .. table
// size - 1: -12; without a texture: -13. Level 0 is locked and written by
// format, rows advancing by the width (not the pitch): R8G8B8 copied;
// A8R8G8B8 / X8R8G8B8 with alpha << 24; R5G6B5; X1R5G5B5 / A1R5G5B5 with the
// top bit set; A4R4G4B4 with the alpha's high nibble, except the last pixel
// of each row, which gets 0xf. Other formats are only locked. Mode -1 reads
// the rows bottom-up (a DIB). The last pixel of a row is read as the dword
// ending at its last byte, so nothing past the data is read. `size` is the
// variable's byte size; reading beyond it is reported instead.
inline int texwrite24(const unsigned char* data, std::size_t size, std::int32_t slot, std::int32_t alpha,
                      std::int32_t mode) {
    Renderer& r = renderer();
    const Table& table = state().surfaces;
    if (slot < 1 || slot >= table.count) return -12;
    if (static_cast<std::size_t>(slot) >= r.surfaces.size() || !r.surfaces[static_cast<std::size_t>(slot)].texture)
        return -13;
    d3d8::Texture* t = r.surfaces[static_cast<std::size_t>(slot)].texture;
    d3d8::SurfaceDesc desc;
    t->level_desc(0, desc);
    d3d8::LockedRect locked;
    const d3d8::HResult hr = t->lock_rect(0, locked, 0);
    if (d3d8::failed(hr)) return -14;  // the original passes the HRESULT to its error helper
    const std::uint32_t w = desc.width, h = desc.height;
    if (static_cast<std::size_t>(w) * h * 3 > size || !w) {
        std::fprintf(stderr, "[CP2 ZGP] texwrite24: the variable holds %zu bytes, %ux%u pixels need %zu\n", size, w, h,
                     static_cast<std::size_t>(w) * h * 3);
        t->unlock_rect(0);
        return 0;
    }
    const std::uint32_t a = static_cast<std::uint32_t>(alpha) & 0xff;
    std::ptrdiff_t step = static_cast<std::ptrdiff_t>(w) * 3;
    const unsigned char* row = data;
    if (mode == -1) {
        row = data + static_cast<std::size_t>(h - 1) * w * 3;
        step = -step;
    }
    unsigned char* out = static_cast<unsigned char*>(locked.bits);
    auto pixel = [](const unsigned char* p, bool last) -> std::uint32_t {
        // A dword read: from the pixel, or ending at its last byte (>> 8).
        if (last) return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
                         static_cast<std::uint32_t>(p[2]) << 16;
        return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
               static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
    };
    for (std::uint32_t y = 0; y < h; ++y, row += step) {
        switch (desc.format) {
        case 20:
            std::memcpy(out, row, static_cast<std::size_t>(w) * 3);
            out += static_cast<std::size_t>(w) * 3;
            break;
        case 21: case 22:
            for (std::uint32_t x = 0; x < w; ++x, out += 4) {
                const std::uint32_t v = (pixel(row + x * 3, x + 1 == w) & 0xffffffu) | a << 24;
                std::memcpy(out, &v, 4);
            }
            break;
        case 23: case 24: case 25: case 26:
            for (std::uint32_t x = 0; x < w; ++x, out += 2) {
                const std::uint32_t v = pixel(row + x * 3, x + 1 == w);
                std::uint16_t o;
                if (desc.format == 23) o = static_cast<std::uint16_t>(((v >> 8) & 0xf800) | ((v >> 5) & 0x7e0) | ((v >> 3) & 0x1f));
                else if (desc.format == 26)
                    o = static_cast<std::uint16_t>(((v >> 12) & 0xf00) | ((v >> 8) & 0xf0) | ((v >> 4) & 0xf) |
                                                   (x + 1 == w ? 0xf000u : (a << 8) & 0xf000));
                else o = static_cast<std::uint16_t>(((v >> 9) & 0x7c00) | ((v >> 6) & 0x3e0) | ((v >> 3) & 0x1f) | 0x8000);
                std::memcpy(out, &o, 2);
            }
            break;
        default:
            break;
        }
    }
    t->unlock_rect(0);
    return 0;
}

// ---------------------------------------------------------------------------
// objdraw: the per-frame object pass (docs/zgp-renderer.md, "objdraw").

inline float object_float(const Renderer::Object& o, int offset) {
    float f;
    std::memcpy(&f, &o.w[offset / 4], 4);
    return f;
}

// One eased value of an object (value, per-frame change, target): it moves
// and snaps to the target when it reaches or crosses it (objdraw,
// 0x100039dc and the three copies after it). A change of 0 (or unordered)
// does nothing.
inline void object_ease(Renderer::Object& o, int value, int speed, int target) {
    CP2_ZGP_STRICT_FP
    const float v = object_float(o, speed);
    if (!(v < 0.0f || v > 0.0f)) return;
    const float before = object_float(o, value) - object_float(o, target);
    set_object_float(o, value, object_float(o, value) + object_float(o, speed));
    const float after = object_float(o, value) - object_float(o, target);
    if (before >= 0.0f && !(after >= 0.0f)) {
        o.w[value / 4] = o.w[target / 4];
        o.w[speed / 4] = 0;
    }
    if (!(before > 0.0f) && after > 0.0f) {
        o.w[value / 4] = o.w[target / 4];
        o.w[speed / 4] = 0;
    }
}

// Pitch and yaw (degrees, wrapped) from p toward t, as objcat computes them:
// atan2(p.y - t.y, sqrt((p.x - t.x)^2 + (p.z - t.z)^2)) and
// atan2(t.x - p.x, t.z - p.z), each unrounded atan2 * 360, then / 2pi.
inline void angles_toward(const float p[3], const float t[3], float& pitch, float& yaw) {
    CP2_ZGP_STRICT_FP
    const float a = p[0] - t[0], b = p[2] - t[2], dy = p[1] - t[1];
    const float horizontal = std::sqrt((a * a) + (b * b));
    const float pitch_turns =
        static_cast<float>(std::atan2(static_cast<double>(dy), static_cast<double>(horizontal)) * 360.0);
    const float yaw_turns =
        static_cast<float>(std::atan2(static_cast<double>(t[0] - p[0]), static_cast<double>(t[2] - p[2])) * 360.0);
    pitch = wrap_object_degrees(pitch_turns / 6.28318548f);
    yaw = wrap_object_degrees(yaw_turns / 6.28318548f);
}

// 0x10008b70: the box objdraw tests objects against. The eight corners of a
// view pyramid (far half-size f at z = f, near half-size f / 4 at z = 0, f =
// the far plane) are rotated by the camera's yaw, pitch and roll (each * pi /
// 180) with D3DXVec3TransformCoord, and 0x10018240 takes their bounds. The
// original loads the camera position but never adds it: the box stays
// around the origin.
inline void camera_view_box(float box[6]) {
    CP2_ZGP_STRICT_FP
    const Camera& c = state().camera;
    const float f = c.far_plane, n = f / 4.0f, m = -f / 4.0f;
    float p[8][3] = {{-f, f, f}, {f, f, f}, {n, n, 0.0f}, {m, n, 0.0f},
                     {-f, -f, f}, {f, -f, f}, {n, m, 0.0f}, {m, m, 0.0f}};
    const float pi = 3.14159274101257f;
    float q[4];
    quaternion_yaw_pitch_roll((c.angles[1] * pi) / 180.0f, (c.angles[0] * pi) / 180.0f, (c.angles[2] * pi) / 180.0f, q);
    const d3d8::Matrix rotation = matrix_from_quaternion(q);
    for (int k = 0; k < 8; ++k) {
        float out[3];
        transform_coord(out, p[k], rotation);
        std::memcpy(p[k], out, sizeof(out));
    }
    // 0x10018240: a coordinate replaces the minimum when the minimum is
    // greater, the maximum when the maximum is less (or unordered).
    for (int k = 0; k < 8; ++k)
        for (int a = 0; a < 3; ++a) {
            if (k == 0) {
                box[a] = box[3 + a] = p[k][a];
                continue;
            }
            if (box[a] > p[k][a]) box[a] = p[k][a];
            if (!(box[3 + a] >= p[k][a])) box[3 + a] = p[k][a];
        }
}

// 0x10018430: strict overlap of two boxes (min xyz, max xyz).
inline bool boxes_overlap(const float a[6], const float b[6]) {
    for (int k = 0; k < 3; ++k)
        if (!(a[3 + k] > b[k]) || !(a[k] < b[3 + k])) return false;
    return true;
}

// The animated-model library of an object (+0x84), or null when its slot
// is empty (the original calls through a null pointer).
inline csmesh::Library* object_library(const Renderer::Object& o) {
    Renderer::AnimationLibrary& slot = renderer().animation_libraries[o.w[0x84 / 4] & 3];
    if (slot.loaded) return &slot.library;
    std::fprintf(stderr, "[CP2 ZGP] objdraw: animated object uses empty library slot %u\n", o.w[0x84 / 4] & 3);
    return nullptr;
}

// A character entry by index (a pointer in the original), or null.
inline const Renderer::Character* character_entry(std::int32_t index) {
    const Renderer& r = renderer();
    if (index < 0 || static_cast<std::size_t>(index) >= r.characters.size()) return nullptr;
    return &r.characters[static_cast<std::size_t>(index)];
}

// A character's surface (its +0x00 surface pointer, a slot here), or null.
inline const Renderer::Surface* character_surface(const Renderer::Character& c) {
    const Renderer& r = renderer();
    if (c.surface < 0 || static_cast<std::size_t>(c.surface) >= r.surfaces.size()) return nullptr;
    return &r.surfaces[static_cast<std::size_t>(c.surface)];
}

// The update step of objdraw for one object (0x1000373b-0x10004ac6).
// Returns false when the object was removed.
inline bool object_update(Renderer& r, Renderer::Object& o, const float eye[3]) {
    CP2_ZGP_STRICT_FP
    const std::uint32_t flags = o.w[0x4 / 4];
    // The previous position.
    for (int k = 0; k < 3; ++k) o.w[0x70 / 4 + k] = o.w[0x60 / 4 + k];
    // Lifetime.
    if (o.w[0x128 / 4]) {
        o.w[0x128 / 4] -= 1;
        if (static_cast<std::int32_t>(o.w[0x128 / 4]) <= 0) {
            o = Renderer::Object();
            return false;
        }
    }
    // Animated model: its clock (+0x124), playing flag (+0x118) and loop
    // counter (+0x11c), against GetMaxTime and GetRate.
    if (o.w[0x88 / 4]) {
        csmesh::Library* library = object_library(o);
        float length = 0.0f, rate = 0.0f;
        if (library) {
            library->get_max_time(static_cast<int>(o.w[0x88 / 4]), &length);
            library->get_rate(static_cast<int>(o.w[0x88 / 4]), &rate);
        }
        if (o.w[0x118 / 4]) set_object_float(o, 0x124, object_float(o, 0x124) + rate);
        float t = object_float(o, 0x124);
        if (t > length) {
            if (length < 0.0f || length > 0.0f) {
                set_object_float(o, 0x124, static_cast<float>(std::fmod(static_cast<double>(t), static_cast<double>(length))));
                o.w[0x11c / 4] -= 1;
            } else {
                o.w[0x11c / 4] = 0xffffffffu;
            }
        }
        t = object_float(o, 0x124);
        if (!(t >= 0.0f)) {
            if (length < 0.0f || length > 0.0f) {
                const float period = length + 1.0f;
                const float wrapped = static_cast<float>(std::fmod(static_cast<double>(t), static_cast<double>(period)));
                set_object_float(o, 0x124, period + wrapped);
                o.w[0x11c / 4] -= 1;
            } else {
                o.w[0x11c / 4] = 0xffffffffu;
            }
        }
        if (static_cast<std::int32_t>(o.w[0x11c / 4]) < 0) {
            o.w[0x124 / 4] = 0;
            o.w[0x120 / 4] = 0;
            o.w[0x118 / 4] = 0;
            o.w[0x11c / 4] = 0;
        }
    }
    // Character animation (+0x80, a character entry: +0x14 frames, +0x10 the
    // next entry; indices here): the clock counts frames.
    if (o.w[0x80 / 4]) {
        set_object_float(o, 0x124, object_float(o, 0x124) + 1.0f);
        const Renderer::Character* c = character_entry(static_cast<std::int32_t>(o.w[0x80 / 4]));
        if (c && static_cast<float>(c->field14) < object_float(o, 0x124)) {
            if (!c->frame) {
                o = Renderer::Object();
                return false;
            }
            o.w[0x80 / 4] = static_cast<std::uint32_t>(c->frame);
            o.w[0x124 / 4] = 0;
        }
    }
    object_ease(o, 0xe4, 0xe8, 0xec);    // forward speed
    object_ease(o, 0xfc, 0x100, 0x104);  // alpha
    object_ease(o, 0x108, 0x10c, 0x110); // emission
    object_ease(o, 0xf0, 0xf4, 0xf8);    // scale
    // Velocity, angular velocity, forward motion along the matrix's z row.
    for (int k = 0; k < 3; ++k)
        set_object_float(o, 0x60 + 4 * k, object_float(o, 0x60 + 4 * k) + object_float(o, 0xc + 4 * k));
    for (int k = 0; k < 3; ++k)
        set_object_float(o, 0x18 + 4 * k, object_float(o, 0x18 + 4 * k) + object_float(o, 0x24 + 4 * k));
    const float forward = object_float(o, 0xe4);
    if (forward < 0.0f || forward > 0.0f)
        for (int k = 0; k < 3; ++k)
            set_object_float(o, 0x60 + 4 * k, object_float(o, 0x50 + 4 * k) * forward + object_float(o, 0x60 + 4 * k));
    float p[3];
    object_world_position(o, p);
    // Turning toward a target (+0xcc: 2 an object +0xdc, 3 a point
    // +0xd0..+0xd8) at most +0xe0 degrees per frame.
    if (o.w[0xcc / 4]) {
        float t[3] = {0, 0, 0};
        bool known = true;
        if (o.w[0xcc / 4] == 2) {
            const std::size_t target = o.w[0xdc / 4];
            if (target < r.objects.size()) {
                if (!r.objects[target].w[0]) o.w[0xcc / 4] = 0;
                object_world_position(r.objects[target], t);
            } else {
                std::fprintf(stderr, "[CP2 ZGP] objdraw: turn target %zu is outside the object table\n", target);
                known = false;
            }
        } else if (o.w[0xcc / 4] == 3) {
            std::memcpy(t, &o.w[0xd0 / 4], sizeof(t));
        } else {
            // The original reads a target left in uninitialized stack memory.
            static bool reported = false;
            if (!reported) std::fprintf(stderr, "[CP2 ZGP] objdraw: turn mode %u is not implemented\n", o.w[0xcc / 4]);
            reported = true;
            known = false;
        }
        if (known) {
            float pitch, yaw;
            angles_toward(p, t, pitch, yaw);
            const float limit = object_float(o, 0xe0);
            const std::int32_t height = zgp_truncate((p[1] - t[1]) * 1000.0f);
            const std::uint32_t magnitude =
                height < 0 ? 0u - static_cast<std::uint32_t>(height) : static_cast<std::uint32_t>(height);
            if (static_cast<std::int32_t>(magnitude) < zgp_truncate(forward * 1000.0f) &&
                object_float(o, 0x18) <= limit && -limit <= object_float(o, 0x18))
                pitch = 0.0f;
            for (int k = 0; k < 3; ++k)
                set_object_float(o, 0x18 + 4 * k, wrap_object_degrees(object_float(o, 0x18 + 4 * k)));
            // Yaw (+0x1c), then pitch (+0x18): the shorter way, clamped.
            const int fields[2] = {0x1c, 0x18};
            const float goals[2] = {yaw, pitch};
            for (int k = 0; k < 2; ++k) {
                float d = goals[k] - object_float(o, fields[k]);
                float step = d;
                if (d < 0.0f) d = d + 360.0f;
                if (step < 0.0f) step = -step;
                if (!(step <= limit)) step = limit;
                if (!(d <= 180.0f)) set_object_float(o, fields[k], object_float(o, fields[k]) - step);
                else set_object_float(o, fields[k], step + object_float(o, fields[k]));
            }
        }
    }
    // Facing the camera (+0x4 bits 0x100 pitch, 0x200 yaw, 0x400 no roll).
    if (flags & 0x700) {
        float pitch, yaw;
        angles_toward(p, eye, pitch, yaw);
        if (flags & 0x100) set_object_float(o, 0x18, pitch);
        if (flags & 0x200) set_object_float(o, 0x1c, yaw);
        if (flags & 0x400) o.w[0x20 / 4] = 0;
    }
    // The clip box removes objects with +0x4 bit 0 outside it.
    if (r.object_clip && (flags & 1)) {
        const float* b = r.object_clip_box;
        if (!(p[0] >= b[0]) || !(p[1] >= b[1]) || !(p[2] >= b[2]) || p[0] > b[3] || p[1] > b[4] || !(p[2] <= b[5])) {
            o = Renderer::Object();
            return false;
        }
    }
    // 0x10006450: the angles become the matrix (the objang core).
    object_angles(o, object_float(o, 0x18), object_float(o, 0x1c), object_float(o, 0x20));
    return true;
}

// CSMesh's material merge (0x10006c80): the model material with its diffuse
// alpha times Render's and Render's emissive RGB added.
inline void csmesh_material(const float model[17], const float render[17], float out[17]) {
    CP2_ZGP_STRICT_FP
    std::memcpy(out, model, 17 * sizeof(float));
    out[3] = out[3] * render[3];
    for (int k = 12; k < 15; ++k) out[k] = out[k] + render[k];
}

// ID3DXMesh::DrawSubset on the device. With an attribute table (the
// converted mesh's bone combinations): its range. Without (the original
// mesh): each run of consecutive faces with the attribute, over all
// vertices.
inline void d3dx_draw_subset(d3d8::Device& device, std::uint32_t fvf, d3d8::Buffer* vertices, d3d8::Buffer* indices,
                             const std::vector<d3dx::BoneCombination>* table,
                             const std::vector<std::uint32_t>& attributes, std::uint32_t vertex_count,
                             std::uint32_t subset) {
    device.set_vertex_shader(fvf);
    device.set_stream_source(0, vertices, d3d8::fvf_stride(fvf));
    device.set_indices(indices, 0);
    if (table) {
        if (subset < table->size()) {
            const d3dx::BoneCombination& c = (*table)[subset];
            if (c.face_count) device.draw_indexed_primitive(4, c.vertex_start, c.vertex_count, c.face_start * 3, c.face_count);
        }
        return;
    }
    for (std::size_t f = 0; f < attributes.size();) {
        if (attributes[f] != subset) {
            ++f;
            continue;
        }
        std::size_t end = f;
        while (end < attributes.size() && attributes[end] == subset) ++end;
        device.draw_indexed_primitive(4, 0, vertex_count, static_cast<std::uint32_t>(f * 3),
                                      static_cast<std::uint32_t>(end - f));
        f = end;
    }
}

// CSMesh's DrawMeshContainer (0x10006ce5) for one container: method 1
// (indexed blending) or without bones.
inline void csmesh_draw_container(d3d8::Device& device, csmesh::Model& model, csmesh::MeshContainer& container,
                                  const float render_material[17]) {
    csmesh::MeshResources* res = static_cast<csmesh::MeshResources*>(container.resources.get());
    if (!res || !res->skin) return;
    d3dx::SkinMesh& skin = *res->skin;
    float material[17];
    if (skin.bones.empty()) {
        // The original mesh (GetOriginalMesh), one subset per material.
        for (std::size_t i = 0; i < res->materials.size(); ++i) {
            csmesh_material(res->materials[i].d3d, render_material, material);
            device.set_material(material);
            device.set_texture(0, res->textures[i]);
            d3dx_draw_subset(device, skin.fvf, skin.vertices, skin.indices, nullptr, skin.attributes,
                             skin.vertex_count, static_cast<std::uint32_t>(i));
        }
        return;
    }
    // FindBones: each bone's frame by name, pre-order from the root.
    if (res->bone_frames.size() != skin.bones.size()) {
        res->bone_frames.clear();
        for (const d3dx::Bone& bone : skin.bones) res->bone_frames.push_back(model.find_frame(0, bone.name));
    }
    d3d8::Caps caps;
    device.get_device_caps(caps);
    const bool software = caps.words[43] < skin.bones.size();  // MaxVertexBlendMatrixIndex < GetNumBones
    if (software) device.set_render_state(0x99, 1);              // SOFTWAREVERTEXPROCESSING
    // Blend matrices per vertex, from the converted mesh's position format.
    const std::uint32_t position = skin.blended_fvf & 0xe;
    const std::uint32_t matrices = position >= 6 ? (position - 4) / 2 : 1;
    device.set_render_state(0x97, matrices - 1);                 // VERTEXBLEND
    if (matrices - 1) device.set_render_state(0xa7, 1);          // INDEXEDVERTEXBLENDENABLE
    for (std::size_t i = 0; i < skin.combinations.size(); ++i) {
        const d3dx::BoneCombination& c = skin.combinations[i];
        for (std::size_t k = 0; k < c.bones.size() && k < 255; ++k) {
            const std::uint32_t bone = c.bones[k];
            if (bone == 0xffffffffu || bone >= skin.bones.size()) continue;
            const int frame = res->bone_frames[bone];
            if (frame < 0) {
                std::fprintf(stderr, "[CP2 CSMesh] bone %s has no frame (FindBones fails in the original)\n",
                             skin.bones[bone].name.c_str());
                continue;
            }
            d3d8::Matrix combined, offset;
            std::memcpy(combined.m, model.frames[static_cast<std::size_t>(frame)].combined.m, sizeof(combined.m));
            std::memcpy(offset.m, skin.bones[bone].offset, sizeof(offset.m));
            device.set_transform(256 + static_cast<std::uint32_t>(k), combined);
            device.multiply_transform(256 + static_cast<std::uint32_t>(k), offset);
        }
        csmesh_material(res->materials[c.attribute].d3d, render_material, material);
        device.set_material(material);
        device.set_texture(0, res->textures[c.attribute]);
        d3dx_draw_subset(device, skin.blended_fvf, skin.blended_vertices, skin.blended_indices, &skin.combinations,
                         skin.attributes, skin.vertex_count, static_cast<std::uint32_t>(i));
    }
    device.set_render_state(0xa7, 0);
    device.set_render_state(0x97, 0);
    if (software) device.set_render_state(0x99, 0);
}

// CSMesh's DrawFrames (0x10007aa8): a frame with meshes sets the world
// matrix to its combined matrix and draws them, then its children.
inline void csmesh_draw_frames(d3d8::Device& device, csmesh::Model& model, int frame, const float material[17]) {
    if (model.frames[static_cast<std::size_t>(frame)].meshes >= 0) {
        d3d8::Matrix world;
        std::memcpy(world.m, model.frames[static_cast<std::size_t>(frame)].combined.m, sizeof(world.m));
        device.set_transform(256, world);
        for (int c = model.frames[static_cast<std::size_t>(frame)].meshes; c >= 0;
             c = model.meshes[static_cast<std::size_t>(c)].next)
            csmesh_draw_container(device, model, model.meshes[static_cast<std::size_t>(c)], material);
    }
    for (int child = model.frames[static_cast<std::size_t>(frame)].child; child >= 0;
         child = model.frames[static_cast<std::size_t>(child)].sibling)
        csmesh_draw_frames(device, model, child, material);
}

// CSMesh Render (0x100068ff -> 0x10005bdf): the slot's time, world and
// material, UpdateFrames, then DrawFrames from the root.
inline void csmesh_render(d3d8::Device& device, csmesh::Library& library, int model, const d3d8::Matrix& world,
                          const float material[17]) {
    if (!library.render(model, world.m, material)) return;
    csmesh::Slot* slot = library.slot(model);
    csmesh_draw_frames(device, *slot->model, 0, slot->material);
}

// The draw step of objdraw for one object (0x10004ad9-0x10005317).
inline void object_draw(Renderer& r, Renderer::Object& o, const float view_box[6], std::uint32_t draw_flags) {
    CP2_ZGP_STRICT_FP
    const float alpha = object_float(o, 0xfc);
    if (!(alpha < 0.0f || alpha > 0.0f)) return;
    const d3d8::Matrix world = object_world_matrix(o);
    if (draw_flags & 4) {
        const float radius = object_float(o, 0xc8) * object_float(o, 0xf0);
        const float center[3] = {world.m[12], world.m[13], world.m[14]};
        float box[6];
        const bool plain = !(object_float(o, 0x18) < 0.0f || object_float(o, 0x18) > 0.0f) &&
                           !(object_float(o, 0x1c) < 0.0f || object_float(o, 0x1c) > 0.0f) &&
                           !(object_float(o, 0x20) < 0.0f || object_float(o, 0x20) > 0.0f) &&
                           !(object_float(o, 0xf0) < 1.0f || object_float(o, 0xf0) > 1.0f);
        for (int k = 0; k < 3; ++k) {
            if (plain) {
                box[k] = center[k] + object_float(o, 0xb0 + 4 * k);
                box[3 + k] = center[k] + object_float(o, 0xbc + 4 * k);
            } else {
                box[k] = center[k] - radius;
                box[3 + k] = center[k] + radius;
            }
        }
        if (!boxes_overlap(view_box, box)) return;
    }
    d3d8::Device& device = *r.device;
    const float emission = object_float(o, 0x108);
    const std::int32_t mode = static_cast<std::int32_t>(o.w[0x114 / 4]);
    if (o.w[0] == 3) {
        Renderer::Mesh* m = mesh(static_cast<std::int32_t>(o.w[0x7c / 4]));
        if (!m || !m->vertices || !m->indices) {
            std::fprintf(stderr, "[CP2 ZGP] objdraw: object mesh %u has no buffers (the original dereferences null)\n",
                         o.w[0x7c / 4]);
            return;
        }
        device.set_transform(256, world);
        device.set_vertex_shader(m->fvf);
        device.set_stream_source(0, m->vertices, m->vertex_size);
        device.set_indices(m->indices, 0);
        for (int pass = 0; pass < 2; ++pass)
            for (std::uint32_t i = 0; i < m->attribute_count; ++i) {
                float material[17];
                std::memcpy(material, m->materials[i].m, sizeof(material));
                const float a = material[3] * alpha;
                // Opaque subsets first, then translucent (or unordered) ones.
                if (pass == 0 && !(a >= 1.0f)) continue;
                if (pass == 1 && a >= 1.0f) continue;
                material[3] = a;
                for (int k = 12; k < 15; ++k) material[k] = material[k] + emission;
                device.set_material(material);
                const Renderer::Surface* surface = nullptr;
                const std::int32_t slot = i < m->textures.size() ? m->textures[i] : -1;
                if (slot >= 0 && static_cast<std::size_t>(slot) < r.surfaces.size() &&
                    r.surfaces[static_cast<std::size_t>(slot)].texture)
                    surface = &r.surfaces[static_cast<std::size_t>(slot)];
                else if (i < m->own_surfaces.size())
                    surface = &m->own_surfaces[i];
                device.set_texture(0, surface ? surface->texture : nullptr);
                blend_state(r, mode, alpha, surface ? surface->format : 0);
                const std::array<std::uint32_t, 5>& range = m->ranges[i];
                device.draw_indexed_primitive(4, range[3], range[4], range[1] * 3, range[2]);
            }
        return;
    }
    if (o.w[0] == 5) {
        float material[17] = {};
        material[3] = alpha;
        for (int k = 12; k < 15; ++k) material[k] = emission;
        blend_state(r, mode, alpha, 22);
        csmesh::Library* library = object_library(o);
        if (!library) return;
        const int model = static_cast<int>(o.w[0x88 / 4]);
        library->set_time(model, object_float(o, 0x124));
        csmesh_render(device, *library, model, world, material);
        return;
    }
    if (o.w[0] == 6) {
        const Renderer::Character* c = character_entry(static_cast<std::int32_t>(o.w[0x80 / 4]));
        if (!c) {
            std::fprintf(stderr, "[CP2 ZGP] objdraw: board object character %u is outside the table\n", o.w[0x80 / 4]);
            return;
        }
        const float x0 = object_float(o, 0x94), y0 = object_float(o, 0x98), x1 = object_float(o, 0xa0),
                    y1 = object_float(o, 0xa4);
        // Four vertices of 32 bytes: position (z 0), normal (0, 0, 1), UV.
        const float corners[4][4] = {
            {x0, y0, c->u1, c->v1}, {x1, y0, c->u0, c->v1}, {x1, y1, c->u0, c->v0}, {x0, y1, c->u1, c->v0}};
        float vertices[4][8];
        for (int k = 0; k < 4; ++k) {
            const float v[8] = {corners[k][0], corners[k][1], 0.0f, 0.0f, 0.0f, 1.0f, corners[k][2], corners[k][3]};
            std::memcpy(vertices[k], v, sizeof(v));
        }
        device.set_transform(256, world);
        device.set_vertex_shader(0x112);
        // 0x10016650: white diffuse and ambient.
        float material[17] = {1, 1, 1, 1, 1, 1, 1, 1};
        material[3] = alpha;
        for (int k = 12; k < 15; ++k) material[k] = material[k] + emission;
        device.set_material(material);
        const Renderer::Surface* surface = character_surface(*c);
        device.set_texture(0, surface ? surface->texture : nullptr);
        blend_state(r, mode, alpha, surface ? surface->format : 0);
        device.draw_primitive_up(6, 2, vertices, 32);
    }
}

// 0x10003680: objects first .. first + count - 1. Flag 1 skips the update
// step, 2 the draw step, 4 tests each object against the view box.
inline void object_pass(Renderer& r, std::int32_t first, std::int32_t count, std::uint32_t flags) {
    float view_box[6];
    camera_view_box(view_box);
    const float* eye = state().camera.eye;
    object(first);  // sizes the table
    for (std::int32_t k = 0; k < count; ++k) {
        Renderer::Object& o = r.objects[static_cast<std::size_t>(first + k)];
        if (!o.w[0]) continue;
        // A child whose parent was removed is removed.
        const std::size_t parent = o.w[0x8c / 4];
        if (parent && (parent >= r.objects.size() || !r.objects[parent].w[0])) {
            o = Renderer::Object();
            continue;
        }
        if (!(flags & 1) && !object_update(r, o, eye)) continue;
        if (!(flags & 2)) object_draw(r, o, view_box, flags);
    }
    r.device->set_texture(0, nullptr);
}

// _objdraw@16 (0x1001f4e0): first (below 1: 1), count (0: all; clipped to
// the table), flags (0x100: no lighting). The camera update, LIGHTING on
// (off with 0x100), the object pass, LIGHTING off, then the blend state of
// setblend and setcolor4's alpha.
inline int objdraw(std::int32_t first, std::int32_t count, std::int32_t flags) {
    CP2_ZGP_STRICT_FP
    Renderer& r = renderer();
    if (!r.device) {
        std::fprintf(stderr, "[CP2 ZGP] objdraw: no device (the original dereferences a null device)\n");
        return 0;
    }
    const std::int32_t size = state().units[2].count;
    if (first < 1) first = 1;
    if (count == 0) count = size;
    if (first + count > size) count = size - first;
    camera_update(r);
    r.device->set_render_state(137, 1);
    if (flags & 0x100) r.device->set_render_state(137, 0);
    object_pass(r, first, count, static_cast<std::uint32_t>(flags));
    r.device->set_render_state(137, 0);
    blend_state(r, r.blend_command, static_cast<float>(static_cast<double>(r.color4 >> 24) / 255.0), 0);
    return 0;
}


// ---------------------------------------------------------------------------
// Object commands whose fields objdraw's update step reads, and readers.

// _objflag@16 (0x1001e900 -> 0x10006c10): +0x4 = flags (bit 0 the clip box,
// 0x100/0x200/0x400 facing the camera). No in-use check. Out of range: -52.
inline int objflag(std::int32_t index, std::int32_t flags) {
    Renderer::Object* o = object(index);
    if (!o) return -52;
    o->w[0x4 / 4] = static_cast<std::uint32_t>(flags);
    return 0;
}

// _objrot@16 (0x1001ef50 -> 0x10006b70): the angular velocity +0x24..+0x2c,
// each value * 360 / setmaxangle's first period, as objang converts.
inline int objrot(std::int32_t index, std::int32_t x, std::int32_t y, std::int32_t z) {
    CP2_ZGP_STRICT_FP
    Renderer::Object* o = object(index);
    if (!o) return -52;
    const float period = state().angle_period_a;
    const std::int32_t v[3] = {x, y, z};
    for (int k = 0; k < 3; ++k) set_object_float(*o, 0x24 + 4 * k, static_cast<float>(v[k]) * 360.0f / period);
    return 0;
}

// _objzoom@16 (0x1001f000 -> 0x10006bd0): the scale (+0xf0), its per-frame
// change (+0xf4) and target (+0xf8), each value / the standard scale.
inline int objzoom(std::int32_t index, std::int32_t scale, std::int32_t speed, std::int32_t target) {
    Renderer::Object* o = object(index);
    if (!o) return -52;
    const float s = state().standard_scale_a;
    const std::int32_t v[3] = {scale, speed, target};
    for (int k = 0; k < 3; ++k) set_object_float(*o, 0xf0 + 4 * k, static_cast<float>(static_cast<double>(v[k]) / s));
    return 0;
}

// _objemi@16 (0x1001e7c0 -> 0x10006cc0): the emission (+0x108), its change
// (+0x10c) and target (+0x110), each value / 255. No range check: the
// original writes outside the table, which the port reports.
inline int objemi(std::int32_t index, std::int32_t value, std::int32_t speed, std::int32_t target) {
    const Table& t = state().units[2];
    if (index < 0 || index >= t.count) {
        std::fprintf(stderr, "[CP2 ZGP] objemi(%d): object outside the table (the original writes past it)\n", index);
        return 0;
    }
    Renderer& r = renderer();
    if (r.objects.size() != static_cast<std::size_t>(t.count)) r.objects.resize(static_cast<std::size_t>(t.count));
    Renderer::Object& o = r.objects[static_cast<std::size_t>(index)];
    const std::int32_t v[3] = {value, speed, target};
    for (int k = 0; k < 3; ++k) set_object_float(o, 0x108 + 4 * k, static_cast<float>(static_cast<double>(v[k]) / 255.0));
    return 0;
}

// _objplay@16 (0x1001f070 -> 0x10007cc0): plays the animated model from
// time / 1000 (+0x124), loops (+0x11c; -1 means 0x700000), playing (+0x118).
inline int objplay(std::int32_t index, std::int32_t time, std::int32_t loops) {
    Renderer::Object* o = object(index);
    if (!o) return -52;
    if (loops == -1) loops = 0x700000;
    o->w[0x118 / 4] = 1;
    o->w[0x11c / 4] = static_cast<std::uint32_t>(loops);
    set_object_float(*o, 0x124, static_cast<float>(static_cast<double>(time) / 1000.0));
    return 0;
}

// _objpause@16 (0x1001f150 -> 0x10007d70): toggles playing (+0x118 ^= 1).
inline int objpause(std::int32_t index) {
    Renderer::Object* o = object(index);
    if (!o) return -52;
    o->w[0x118 / 4] ^= 1u;
    return 0;
}

// _anilist@16 (0x10020690): SetList(model, list) in the selected library;
// failure gives -5.
inline int anilist(std::int32_t model, std::int32_t list) {
    csmesh::Library* library = animation_library("anilist");
    if (!library) return -5;
    return library->set_list(model, list) ? 0 : -5;
}

// _getobjang@16 (0x1001ffb0): var(0..2) = the angles (+0x18..+0x20) *
// setmaxangle's first period / 360, truncated.
inline int getobjang(std::int32_t* out, std::size_t count, std::int32_t index) {
    CP2_ZGP_STRICT_FP
    Renderer::Object* o = object(index);
    if (!o) return -52;
    const float period = state().angle_period_a;
    std::int32_t v[3];
    for (int k = 0; k < 3; ++k) v[k] = zgp_truncate((object_float(*o, 0x18 + 4 * k) * period) / 360.0f);
    std::memcpy(out, v, std::min<std::size_t>(count, 3) * sizeof(std::int32_t));
    return 0;
}

// _getobjsize@16 (0x10020150): var(0..2) = the hit box size (+0xa0 - +0x94,
// ...) and var(3) = the bounding radius (+0xc8), each * the standard scale,
// truncated.
inline int getobjsize(std::int32_t* out, std::size_t count, std::int32_t index) {
    CP2_ZGP_STRICT_FP
    Renderer::Object* o = object(index);
    if (!o) return -52;
    const float s = state().standard_scale_a;
    std::int32_t v[4];
    for (int k = 0; k < 3; ++k) {
        const float size = object_float(*o, 0xa0 + 4 * k) - object_float(*o, 0x94 + 4 * k);
        v[k] = zgp_truncate(size * s);
    }
    v[3] = zgp_truncate(object_float(*o, 0xc8) * s);
    std::memcpy(out, v, std::min<std::size_t>(count, 4) * sizeof(std::int32_t));
    return 0;
}

// _getanitime@16 (0x100207f0): var(0) = fmod(GetTime, GetMaxTime) * 1000 and
// var(1) = GetMaxTime * 1000, truncated, for a model of the selected library.
inline int getanitime(std::int32_t* out, std::size_t count, std::int32_t model) {
    CP2_ZGP_STRICT_FP
    csmesh::Library* library = animation_library("getanitime");
    if (!library) return 0;
    float time = 0.0f, length = 0.0f;
    library->get_time(model, &time);
    library->get_max_time(model, &length);
    const float wrapped = static_cast<float>(std::fmod(static_cast<double>(time), static_cast<double>(length)));
    const std::int32_t v[2] = {zgp_truncate(wrapped * 1000.0f), zgp_truncate(length * 1000.0f)};
    std::memcpy(out, v, std::min<std::size_t>(count, 2) * sizeof(std::int32_t));
    return 0;
}

// _objfreeno@16 (0x1001fd00 -> 0x10002c90): var(0) = the first of n (below
// 1: 1) consecutive unused entries among first .. first + count - 1 (count
// 0: all; clipped like objinst), or 0 when there is none.
inline int objfreeno(std::int32_t* out, std::size_t count_ints, std::int32_t first, std::int32_t count,
                     std::int32_t n) {
    const std::int32_t size = state().units[2].count;
    if (first < 1 || first >= size) return -52;
    if (count == 0) count = size;
    if (first + count >= size) count = size - first;
    if (n <= 0) n = 1;
    object(first);  // sizes the table
    const Renderer& r = renderer();
    std::int32_t found = -1;
    for (std::int32_t i = 0; i < count - (n - 1); ++i) {
        std::uint32_t used = 0;
        for (std::int32_t j = 0; j < n; ++j) used |= r.objects[static_cast<std::size_t>(first + i + j)].w[0];
        if (!used) {
            found = first + i;
            break;
        }
    }
    if (count_ints) out[0] = found == -1 ? 0 : found;
    return 0;
}

}  // namespace zgp
}  // namespace cp2
