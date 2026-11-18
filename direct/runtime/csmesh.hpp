#pragma once
// Native implementation of the original plugin/CSMesh.dll animation backend.
//
// ZGP (plugin/zgp1807.hpi) loads CSMesh through anilibload and calls its 23
// cdecl exports through a 96-byte function table. This header reproduces the
// observable behavior of those exports: model ownership per slot, .x frame
// hierarchy construction, animation-set lists, keyframe sampling and the mesh
// queries. Contracts were recovered from the original x86 code; see
// docs/csmesh-abi.md for addresses and the deliberate divergences where the
// original behavior is undefined.
//
// All animation math follows the original D3DX8 instruction order. Direct3D 8
// leaves the x87 FPU in 24-bit precision after ZGP creates its device, so each
// add/sub/mul/div/sqrt rounds to binary32 exactly like the float operations
// below. Transcendentals (fsin, fpatan) are evaluated in double precision here.
//
// Rendering is not part of this header. Render() updates the same state the
// original updates before drawing (sampled pose, world matrix, material and
// combined frame matrices); issuing draw calls belongs to the renderer layer.
#include "xfile.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// The original rounds every multiply and add separately. Apple clang fuses
// a*b+c into FMA on arm64 by default, so each math routine opts out locally.
#if defined(__clang__)
#define CP2_CSMESH_STRICT_FP _Pragma("clang fp contract(off)")
#else
#define CP2_CSMESH_STRICT_FP
#endif

namespace cp2 {
namespace csmesh {

// ---------------------------------------------------------------------------
// D3DX8 math, transcribed from the copies statically linked into CSMesh.dll.

struct Matrix {
    float m[16];
    static Matrix identity() {
        Matrix r;
        std::memset(r.m, 0, sizeof(r.m));
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
};

// D3DXMatrixMultiply (0x1000c22f): out[i][j] = (a2*b2j + a0*b0j) + (a1*b1j + a3*b3j).
inline Matrix multiply(const Matrix& a, const Matrix& b) {
    CP2_CSMESH_STRICT_FP
    Matrix out;
    for (int i = 0; i < 4; ++i) {
        const float a0 = a.m[i * 4 + 0], a1 = a.m[i * 4 + 1], a2 = a.m[i * 4 + 2], a3 = a.m[i * 4 + 3];
        for (int j = 0; j < 4; ++j) {
            const float p0 = a0 * b.m[0 * 4 + j];
            const float p1 = a1 * b.m[1 * 4 + j];
            const float p2 = a2 * b.m[2 * 4 + j];
            const float p3 = a3 * b.m[3 * 4 + j];
            const float left = p2 + p0;
            const float right = p1 + p3;
            out.m[i * 4 + j] = left + right;
        }
    }
    return out;
}

// D3DXMatrixScaling (0x1000c312).
inline Matrix scaling(float x, float y, float z) {
    Matrix r = Matrix::identity();
    r.m[0] = x;
    r.m[5] = y;
    r.m[10] = z;
    return r;
}

// D3DXMatrixTranslation (0x1000c36e).
inline Matrix translation(float x, float y, float z) {
    Matrix r = Matrix::identity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

// D3DXMatrixRotationQuaternion (0x1000c3ca); q = {x, y, z, w}.
inline Matrix rotation_quaternion(const float q[4]) {
    CP2_CSMESH_STRICT_FP
    const float x2 = q[0] * 2.0f, y2 = q[1] * 2.0f, z2 = 2.0f * q[2];
    const float wx = x2 * q[3], wy = y2 * q[3], wz = z2 * q[3];
    const float xx = x2 * q[0], xy = y2 * q[0], xz = z2 * q[0];
    const float yy = y2 * q[1], yz = z2 * q[1], zz = z2 * q[2];
    Matrix r;
    r.m[0] = (1.0f - yy) - zz;
    r.m[1] = xy + wz;
    r.m[2] = xz - wy;
    r.m[3] = 0.0f;
    r.m[4] = xy - wz;
    const float one_minus_xx = 1.0f - xx;
    r.m[5] = one_minus_xx - zz;
    r.m[6] = yz + wx;
    r.m[7] = 0.0f;
    r.m[8] = xz + wy;
    r.m[9] = yz - wx;
    r.m[10] = one_minus_xx - yy;
    r.m[11] = 0.0f;
    r.m[12] = 0.0f;
    r.m[13] = 0.0f;
    r.m[14] = 0.0f;
    r.m[15] = 1.0f;
    return r;
}

// D3DXQuaternionDot (0x1000c4f8): ((w*w' + z*z') + y*y') + x*x'.
inline float quaternion_dot(const float a[4], const float b[4]) {
    CP2_CSMESH_STRICT_FP
    const float w = a[3] * b[3];
    const float z = a[2] * b[2];
    float sum = w + z;
    sum = sum + a[1] * b[1];
    return sum + a[0] * b[0];
}

// D3DXQuaternionSlerp (0x1000c51d).
inline void slerp(float out[4], const float a[4], const float b[4], float t) {
    CP2_CSMESH_STRICT_FP
    float s0 = 1.0f - t;
    float dot = quaternion_dot(a, b);
    const float sign = dot < 0.0f ? -1.0f : 1.0f;
    dot = dot * sign;
    float s1;
    if (1.0f - dot > 9.99999974737875e-06f) {
        const float sine = std::sqrt(1.0f - dot * dot);
        const float theta = static_cast<float>(std::atan2(static_cast<double>(sine), static_cast<double>(dot)));
        const float inverse = 1.0f / sine;
        s0 = static_cast<float>(std::sin(static_cast<double>(theta * s0)) * static_cast<double>(inverse));
        s1 = static_cast<float>(std::sin(static_cast<double>(theta * t)) * static_cast<double>(inverse));
    } else {
        s1 = t;
    }
    s1 = sign * s1;
    for (int i = 0; i < 4; ++i) out[i] = s0 * a[i] + s1 * b[i];
}

// D3DXVec3Lerp (0x10008830): ((b - a) * s) + a.
inline void lerp3(float out[3], const float a[3], const float b[3], float s) {
    CP2_CSMESH_STRICT_FP
    for (int i = 0; i < 3; ++i) out[i] = (b[i] - a[i]) * s + a[i];
}

// D3DXVec3TransformCoord (0x1000c5f7).
inline void transform_coord(float out[3], const float v[3], const Matrix& m) {
    CP2_CSMESH_STRICT_FP
    float r[4];
    for (int k = 0; k < 4; ++k) {
        float sum = v[0] * m.m[k] + v[1] * m.m[4 + k];
        sum = sum + v[2] * m.m[8 + k];
        r[k] = sum + m.m[12 + k];
    }
    const float delta = r[3] - 1.0f;
    if (delta < -9.99999974737875e-06f || delta > 9.99999974737875e-06f) {
        const float inverse = 1.0f / r[3];
        r[0] = inverse * r[0];
        r[1] = inverse * r[1];
        r[2] = inverse * r[2];
    }
    out[0] = r[0];
    out[1] = r[1];
    out[2] = r[2];
}

// ---------------------------------------------------------------------------
// Model data (SFrame, 0xfc bytes, and mesh containers in the original).

struct RotationKey {
    std::uint32_t time;
    float q[4];  // x, y, z, w as stored by CSMesh
};

struct VectorKey {
    std::uint32_t time;
    float v[3];
};

struct MatrixKey {
    std::uint32_t time;
    Matrix m;
};

struct MeshContainer {
    std::string name;
    bool has_name = false;
    std::vector<float> positions;  // xyz triples in file order
    std::shared_ptr<const xfile::Object> source;  // retained for geometry/material upload
    std::shared_ptr<void> resources;  // device resources built by the mesh builder
    int next = -1;
};

// Builds a mesh container's device resources (D3DX skin mesh, converted mesh,
// materials and textures; csmesh_mesh.hpp) where LoadMesh does, before the
// container is added. Set by the renderer, which owns the device; without
// one, only the frame structure is loaded.
using MeshBuilder = std::function<bool(const xfile::Object& mesh, const xfile::Document& document,
                                       std::shared_ptr<void>& resources, std::string& error)>;

struct Frame {
    std::string name;
    bool has_name = false;
    Matrix rot = Matrix::identity();    // +0x04, animated local transform
    Matrix trans = Matrix::identity();  // +0x44, FrameTransformMatrix as loaded
    Matrix combined = Matrix::identity();  // +0x84, written by Render (uninitialized before)
    int meshes = -1;                    // +0x00, head of container list
    int child = -1;                     // +0xf0
    int sibling = -1;                   // +0xec
    bool animation = false;             // +0xf4
    // Animation frames only.
    bool has_position = false, has_rotation = false, has_scale = false, has_matrix = false;
    std::vector<VectorKey> positions;   // +0xd4 / +0xc4
    std::vector<RotationKey> rotations; // +0xd8 / +0xc8
    std::vector<VectorKey> scales;      // +0xdc / +0xcc
    std::vector<MatrixKey> matrices;    // +0xe0 / +0xd0
    int target = -1;                    // +0xe8, frame to animate
    int next_in_list = -1;              // +0xe4
};

constexpr int kMaxLists = 256;
constexpr int kInitializedLists = 16;  // the original clears only these heads

struct Model {
    std::vector<Frame> frames;  // frames[0] is the unnamed root
    std::vector<MeshContainer> meshes;
    int lists[kMaxLists];
    int list_count = 0;  // +0x408
    int current = 0;     // +0x404
    std::vector<std::string> diagnostics;

    Model() {
        for (int& head : lists) head = -1;
        frames.emplace_back();
    }

    // FindFrame (0x1000af80): pre-order search, self, then children, then siblings.
    int find_frame(int index, const std::string& name) const {
        while (index >= 0) {
            const Frame& frame = frames[index];
            if (frame.has_name && frame.name == name) return index;
            if (frame.child >= 0) {
                const int found = find_frame(frame.child, name);
                if (found >= 0) return found;
            }
            index = frame.sibling;
        }
        return -1;
    }

    // AddFrame (0x1000b5c0): the new child becomes the second child.
    void add_child(int parent, int child) {
        Frame& owner = frames[parent];
        if (owner.child < 0) {
            owner.child = child;
        } else {
            frames[child].sibling = frames[owner.child].sibling;
            frames[owner.child].sibling = child;
        }
    }

    int list_head() const {
        if (current < 0 || current >= kMaxLists) return -1;
        return lists[current];
    }
};

// ---------------------------------------------------------------------------
// Loader (0x10008979 and its LoadFrames/LoadAnimation helpers).

class Loader {
public:
    Loader(const xfile::Document& document, Model& model, const MeshBuilder* builder = nullptr)
        : document_(document), model_(model), builder_(builder) {}

    bool load(std::string& error) {
        for (const auto& object : document_.objects)
            if (!load_object(object, 0, error)) return false;
        return true;
    }

private:
    const xfile::Document& document_;
    Model& model_;
    const MeshBuilder* builder_;

    static bool words(const xfile::Object& object, std::size_t index, std::size_t count) {
        return index <= object.values.size() && object.values.size() - index >= count;
    }

    static std::uint32_t word(const xfile::Value& value) { return value.word; }
    static float as_float(const xfile::Value& value) {
        float result;
        std::memcpy(&result, &value.word, sizeof(result));
        return value.kind == xfile::Value::Float ? result : static_cast<float>(value.word);
    }

    int new_frame(bool animation) {
        model_.frames.emplace_back();
        model_.frames.back().animation = animation;
        return static_cast<int>(model_.frames.size() - 1);
    }

    bool load_object(const std::shared_ptr<xfile::Object>& object, int parent, std::string& error) {
        const std::string& type = object->type;
        if (type == "Mesh") return load_mesh(object, parent, error);
        if (type == "FrameTransformMatrix") {
            if (!words(*object, 0, 16)) { error = "FrameTransformMatrix has fewer than 16 values"; return false; }
            Matrix matrix;
            for (int i = 0; i < 16; ++i) matrix.m[i] = as_float(object->values[i]);
            model_.frames[parent].rot = matrix;
            model_.frames[parent].trans = matrix;
            return true;
        }
        if (type == "AnimationSet") return load_animation_set(*object, parent, error);
        if (type == "Animation") return load_animation(*object, parent, error);
        if (type == "Frame") {
            const int frame = new_frame(false);
            if (object->has_name && !object->name.empty()) {
                model_.frames[frame].name = object->name;
                model_.frames[frame].has_name = true;
            }
            model_.add_child(parent, frame);
            for (const xfile::Child& child : object->children)
                if (child.object && !load_object(child.object, frame, error)) return false;
            return true;
        }
        return true;  // other templates are ignored by CSMesh
    }

    bool load_mesh(const std::shared_ptr<xfile::Object>& object, int parent, std::string& error) {
        MeshContainer mesh;
        if (object->has_name && !object->name.empty()) {
            mesh.name = object->name;
            mesh.has_name = true;
        }
        if (!words(*object, 0, 1)) { error = "Mesh without vertex count"; return false; }
        const std::uint32_t count = word(object->values[0]);
        if (!words(*object, 1, static_cast<std::size_t>(count) * 3)) { error = "Mesh vertex data truncated"; return false; }
        mesh.positions.resize(static_cast<std::size_t>(count) * 3);
        for (std::size_t i = 0; i < mesh.positions.size(); ++i) mesh.positions[i] = as_float(object->values[1 + i]);
        mesh.source = object;
        if (builder_ && *builder_ && !(*builder_)(*object, document_, mesh.resources, error)) return false;
        // AddMesh (0x1000b770) prepends to the frame's container list.
        mesh.next = model_.frames[parent].meshes;
        model_.meshes.push_back(std::move(mesh));
        model_.frames[parent].meshes = static_cast<int>(model_.meshes.size() - 1);
        return true;
    }

    bool load_animation_set(const xfile::Object& object, int parent, std::string& error) {
        const int frame = new_frame(true);
        if (object.has_name && !object.name.empty()) {
            model_.frames[frame].name = object.name;
            model_.frames[frame].has_name = true;
        }
        model_.add_child(parent, frame);
        for (const xfile::Child& child : object.children)
            if (child.object && child.object->type == "Animation" && !load_animation(*child.object, frame, error))
                return false;
        ++model_.list_count;
        return true;
    }

    bool load_animation(const xfile::Object& object, int parent, std::string& error) {
        const int frame = new_frame(true);
        model_.add_child(parent, frame);
        const int list = model_.list_count;
        if (list >= kMaxLists) { error = "more than 256 animation sets"; return false; }
        if (list >= kInitializedLists)
            model_.diagnostics.push_back("animation set " + std::to_string(list) +
                                         " uses a list head the original leaves uninitialized");
        model_.frames[frame].next_in_list = model_.lists[list];
        model_.lists[list] = frame;
        for (const xfile::Child& child : object.children) {
            if (!child.object) {
                // Data reference: must resolve to a named Frame present in the hierarchy.
                const std::shared_ptr<xfile::Object> target = document_.find(child.reference);
                if (!target || target->type != "Frame") continue;
                if (model_.frames[frame].target >= 0) { error = "Animation references two frames"; return false; }
                if (!target->has_name || target->name.empty()) { error = "Animation references an unnamed frame"; return false; }
                const int found = model_.find_frame(0, target->name);
                if (found < 0) { error = "Animation references unknown frame " + target->name; return false; }
                model_.frames[frame].target = found;
                continue;
            }
            const xfile::Object& data = *child.object;
            if (data.type == "Frame") {
                if (!load_object(child.object, frame, error)) return false;
            } else if (data.type == "AnimationKey") {
                if (!load_key(data, model_.frames[frame], error)) return false;
            }
        }
        return true;
    }

    bool load_key(const xfile::Object& object, Frame& frame, std::string& error) {
        if (!words(object, 0, 2)) { error = "AnimationKey header truncated"; return false; }
        const std::uint32_t type = word(object.values[0]);
        const std::uint32_t count = word(object.values[1]);
        // The original walks the packed data with a fixed stride per key type
        // (time, nValues, values...) and does not consult nValues.
        const std::size_t stride = type == 0 ? 6 : (type == 1 || type == 2) ? 5 : type == 4 ? 18 : 0;
        if (!stride) { error = "AnimationKey type " + std::to_string(type) + " is not supported"; return false; }
        if (!words(object, 2, stride * count)) { error = "AnimationKey data truncated"; return false; }
        const xfile::Value* p = object.values.data() + 2;
        if (type == 0) {
            if (frame.has_rotation) { error = "duplicate rotation keys"; return false; }
            frame.has_rotation = true;
            frame.rotations.resize(count);
            for (std::uint32_t i = 0; i < count; ++i, p += stride) {
                RotationKey& key = frame.rotations[i];
                key.time = word(p[0]);
                key.q[0] = as_float(p[3]);
                key.q[1] = as_float(p[4]);
                key.q[2] = as_float(p[5]);
                key.q[3] = as_float(p[2]);
            }
        } else if (type == 1 || type == 2) {
            bool& present = type == 1 ? frame.has_scale : frame.has_position;
            std::vector<VectorKey>& keys = type == 1 ? frame.scales : frame.positions;
            if (present) { error = "duplicate vector keys"; return false; }
            present = true;
            keys.resize(count);
            for (std::uint32_t i = 0; i < count; ++i, p += stride) {
                keys[i].time = word(p[0]);
                for (int k = 0; k < 3; ++k) keys[i].v[k] = as_float(p[2 + k]);
            }
        } else {
            if (frame.has_matrix) { error = "duplicate matrix keys"; return false; }
            frame.has_matrix = true;
            frame.matrices.resize(count);
            for (std::uint32_t i = 0; i < count; ++i, p += stride) {
                frame.matrices[i].time = word(p[0]);
                for (int k = 0; k < 16; ++k) frame.matrices[i].m.m[k] = as_float(p[2 + k]);
            }
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
// Keyframe sampling (SFrame::SetTime, 0x10007b5d).

struct KeySpan {
    std::size_t first;
    std::size_t second;
    float time;
    float lerp;
};

template <typename Key>
inline KeySpan key_span(const std::vector<Key>& keys, float global_time) {
    CP2_CSMESH_STRICT_FP
    const std::size_t count = keys.size();
    const std::uint32_t period = keys[count - 1].time + 1u;
    const float time = static_cast<float>(std::fmod(static_cast<double>(global_time), static_cast<double>(period)));
    std::size_t index = 0;
    while (index < count && !(static_cast<double>(keys[index].time) > static_cast<double>(time))) ++index;
    KeySpan span;
    span.time = time;
    span.first = index ? index - 1 : 0;
    span.second = index == count ? 0 : index;
    const float t1 = static_cast<float>(keys[span.first].time);
    float t2 = static_cast<float>(keys[span.second].time);
    if (index == count) t2 = static_cast<float>(static_cast<double>(keys[span.first].time) + 1.0);
    const float range = t2 - t1;
    span.lerp = range == 0.0f ? 0.0f : (time - t1) / (t2 - t1);
    return span;
}

inline void sample_frame(Model& model, const Frame& animation, float global_time) {
    CP2_CSMESH_STRICT_FP
    if (animation.target < 0) return;  // the original dereferences a null target here
    Frame& target = model.frames[animation.target];
    if (animation.has_matrix) {
        if (animation.matrices.empty()) return;
        const KeySpan span = key_span(animation.matrices, global_time);
        const std::size_t chosen = static_cast<double>(span.lerp) > 0.5 ? span.second : span.first;
        target.rot = animation.matrices[chosen].m;
        return;
    }
    Matrix result = Matrix::identity();
    bool animated = false;
    if (animation.has_scale && !animation.scales.empty()) {
        const KeySpan span = key_span(animation.scales, global_time);
        float v[3];
        lerp3(v, animation.scales[span.first].v, animation.scales[span.second].v, span.lerp);
        result = multiply(result, scaling(v[0], v[1], v[2]));
        animated = true;
    }
    if (animation.has_rotation && !animation.rotations.empty()) {
        const KeySpan span = key_span(animation.rotations, global_time);
        const float* a = animation.rotations[span.first].q;
        const float* b = animation.rotations[span.second].q;
        const float first[4] = {-a[0], -a[1], -a[2], a[3]};
        const float second[4] = {-b[0], -b[1], -b[2], b[3]};
        float q[4];
        slerp(q, first, second, span.lerp);
        result = multiply(result, rotation_quaternion(q));
        animated = true;
    }
    if (animation.has_position && !animation.positions.empty()) {
        const KeySpan span = key_span(animation.positions, global_time);
        float v[3];
        lerp3(v, animation.positions[span.first].v, animation.positions[span.second].v, span.lerp);
        result = multiply(result, translation(v[0], v[1], v[2]));
        animated = true;
    } else {
        result = multiply(result, translation(target.trans.m[12], target.trans.m[13], target.trans.m[14]));
    }
    if (animated) target.rot = result;
}

// ---------------------------------------------------------------------------
// One CSMesh object slot (0x1a4 bytes in the original).

struct Slot {
    std::unique_ptr<Model> model;  // +0x08
    Matrix world = Matrix::identity();  // +0x0c, uninitialized until Render in the original
    float material[17] = {};       // +0x4c, D3DMATERIAL8
    std::string filename;          // +0x90
    float time = 0.0f;             // +0x190
    float rate = 0.0f;             // +0x194
    float hit[3] = {};             // +0x198
    bool rendered = false;

    Slot() {
        material[0] = material[1] = material[2] = material[3] = 1.0f;  // diffuse
        material[4] = material[5] = material[6] = material[7] = 1.0f;  // ambient
    }
};

class Library {
public:
    // Init (0x1000670c): count outside 1..255 becomes 256. The device argument
    // is the address where ZGP stores its device pointer; it is only kept.
    int init(const void* device_reference, int count) {
        if (count < 1 || count >= 256) count = 256;
        slots_.clear();
        slots_.resize(static_cast<std::size_t>(count));
        device_reference_ = device_reference;
        return 1;
    }

    // Delete (0x100067b0).
    int destroy() {
        slots_.clear();
        return 1;
    }

    bool initialized() const { return !slots_.empty(); }
    int capacity() const { return static_cast<int>(slots_.size()); }
    const void* device_reference() const { return device_reference_; }
    void set_mesh_builder(MeshBuilder builder) { mesh_builder_ = std::move(builder); }

    Slot* slot(int index) {
        if (index < 0 || static_cast<std::size_t>(index) >= slots_.size()) return nullptr;
        return &slots_[static_cast<std::size_t>(index)];
    }

    Model* model(int index) {
        Slot* s = slot(index);
        return s ? s->model.get() : nullptr;
    }

    // Load (0x10005b5f): releases the slot first, then parses the file bytes.
    int load(int index, const std::string& filename, const std::uint8_t* data, std::size_t size, std::string& error) {
        Slot* s = slot(index);
        if (!s) { error = "slot outside the initialized CSMesh array"; return 0; }
        s->model.reset();
        s->filename.clear();
        if (!data) { error = "file not found"; return 0; }
        xfile::Document document;
        if (!xfile::parse(data, size, document, error)) return 0;
        std::unique_ptr<Model> model(new Model());
        Loader loader(document, *model, &mesh_builder_);
        if (!loader.load(error)) return 0;
        s->model = std::move(model);
        s->filename = filename;
        return 1;
    }

    int set_time(int index, float time) {
        Slot* s = slot(index);
        if (!s || !s->model) return 0;
        s->time = time;
        apply_time(*s->model, time);
        return 1;
    }

    // Render (0x100068ff): applies the stored time, then the world matrix and
    // material. A null material pointer also means "use identity world".
    int render(int index, const float* world, const float* material) {
        Slot* s = slot(index);
        if (!s || !s->model) return 0;
        apply_time(*s->model, s->time);
        if (material && world) std::memcpy(s->world.m, world, sizeof(s->world.m));
        else s->world = Matrix::identity();
        if (material) std::memcpy(s->material, material, sizeof(s->material));
        update_frames(*s->model, 0, s->world);
        s->rendered = true;
        return 1;
    }

    int set_rate(int index, float rate) {
        Slot* s = slot(index);
        if (!s) return 0;
        s->rate = rate;
        return 1;
    }

    int get_rate(int index, float* out) {
        Slot* s = slot(index);
        if (!s || !out) return 0;
        *out = s->rate;
        return 1;
    }

    int get_time(int index, float* out) {
        Slot* s = slot(index);
        if (!s || !out) return 0;
        *out = s->time;
        return 1;
    }

    int set_hit_size(int index, float x, float y, float z) {
        Slot* s = slot(index);
        if (!s) return 0;
        s->hit[0] = x;
        s->hit[1] = y;
        s->hit[2] = z;
        return 1;
    }

    int get_hit_size(int index, float* x, float* y, float* z) {
        Slot* s = slot(index);
        if (!s || !x || !y || !z) return 0;
        *x = s->hit[0];
        *y = s->hit[1];
        *z = s->hit[2];
        return 1;
    }

    int set_list(int index, int list) {
        Model* m = model(index);
        if (!m) return 0;
        m->current = list;
        return 1;
    }

    int get_num_list(int index, int* out) {
        Model* m = model(index);
        if (!m || !out) return 0;
        *out = m->list_count;
        return 1;
    }

    // GetListName (0x10006a1a): CSMesh ignores the file names.
    int get_list_names(int index, char* out) {
        Model* m = model(index);
        if (!m || !out) return 0;
        for (int i = 0; i < m->list_count; ++i) std::snprintf(out + i * 256, 256, "AnimationSet%d", i);
        return 1;
    }

    int get_num_key(int index, int* out) {
        Model* m = model(index);
        if (!m || !out) return 0;
        *out = static_cast<int>(num_key(*m));
        return 1;
    }

    // GetKeyTime (0x10005d1c / 0x1000bf7c).
    int get_key_time(int index, float* out, int key) {
        Model* m = model(index);
        if (!m || !out) return 0;
        *out = static_cast<float>(key_time(*m, key));
        return 1;
    }

    // GetMaxTime (0x10005d6e): the time of the last key in the longest track.
    int get_max_time(int index, float* out) {
        Model* m = model(index);
        if (!m || !out) return 0;
        *out = static_cast<float>(key_time(*m, static_cast<int>(num_key(*m)) - 1));
        return 1;
    }

    // GetNumMesh/GetMeshName (0x1000be99, 0x1000bb22): pre-order over frames.
    int get_mesh_names(int index, char* out, int* count) {
        Model* m = model(index);
        if (!m) return 0;
        int written = 0;
        names(*m, 0, out, written);
        if (count) *count = written;
        return 1;
    }

    // GetMeshMatrix (0x10006b83 / 0x1000bd6b).
    int get_mesh_matrix(int index, float* out, const char* name, int animated) {
        Slot* s = slot(index);
        if (!s || !s->model || !out) return 0;
        Matrix parent = animated ? s->world : s->model->frames[0].trans;
        Matrix result;
        bool found = false;
        mesh_matrix(*s->model, 0, parent, name, animated != 0, result, found);
        if (found) std::memcpy(out, result.m, sizeof(result.m));
        return 1;
    }

    // GetMeshMinMax (0x10006bcf / 0x1000bdfa): out = min xyz, max xyz.
    int get_mesh_minmax(int index, float* out, const char* name, int animated) {
        Slot* s = slot(index);
        if (!s || !s->model || !out) return 0;
        Matrix parent = animated ? s->world : s->model->frames[0].trans;
        int count = 0;
        minmax(*s->model, 0, parent, name, animated != 0, out, count);
        return 1;
    }

private:
    std::vector<Slot> slots_;
    const void* device_reference_ = nullptr;
    MeshBuilder mesh_builder_;

    // UpdateFrames (0x10007a49): combined = rot * parent, depth first.
    static void update_frames(Model& model, int frame, const Matrix& parent) {
        Frame& f = model.frames[frame];
        f.combined = multiply(f.rot, parent);
        for (int child = f.child; child >= 0; child = model.frames[child].sibling)
            update_frames(model, child, f.combined);
    }

    static void apply_time(Model& model, float time) {
        for (int frame = model.list_head(); frame >= 0; frame = model.frames[frame].next_in_list)
            sample_frame(model, model.frames[frame], time);
    }

    static std::uint32_t num_key(const Model& model) {
        std::uint32_t result = 0;
        for (int index = model.list_head(); index >= 0; index = model.frames[index].next_in_list) {
            const Frame& f = model.frames[index];
            if (result < f.rotations.size()) result = static_cast<std::uint32_t>(f.rotations.size());
            if (result < f.positions.size()) result = static_cast<std::uint32_t>(f.positions.size());
            if (result < f.scales.size()) result = static_cast<std::uint32_t>(f.scales.size());
            if (result < f.matrices.size()) result = static_cast<std::uint32_t>(f.matrices.size());
        }
        return result;
    }

    static std::uint32_t key_time(const Model& model, int key) {
        const int count = static_cast<int>(num_key(model));
        if (key < 0) key = 0;
        if (key >= count) key = count - 1;
        if (key < 0) return 0;  // empty list: the original reads key[-1]
        const std::size_t k = static_cast<std::size_t>(key);
        const std::size_t n = static_cast<std::size_t>(count);
        for (int index = model.list_head(); index >= 0; index = model.frames[index].next_in_list) {
            const Frame& f = model.frames[index];
            if (f.has_rotation && f.rotations.size() == n) return f.rotations[k].time;
            if (f.has_position && f.positions.size() == n) return f.positions[k].time;
            if (f.has_scale && f.scales.size() == n) return f.scales[k].time;
            if (f.has_matrix && f.matrices.size() == n) return f.matrices[k].time;
        }
        return 0;
    }

    static void names(const Model& model, int frame, char* out, int& count) {
        for (; frame >= 0; frame = model.frames[frame].sibling) {
            for (int mesh = model.frames[frame].meshes; mesh >= 0; mesh = model.meshes[mesh].next) {
                if (out) {
                    char* entry = out + static_cast<std::size_t>(count) * 256;
                    std::memset(entry, 0, 256);
                    const MeshContainer& container = model.meshes[mesh];
                    if (container.has_name)
                        std::memcpy(entry, container.name.data(), container.name.size() < 255 ? container.name.size() : 255);
                }
                ++count;
            }
            names(model, model.frames[frame].child, out, count);
            if (frame == 0) break;  // the root has no siblings
        }
    }

    static bool matches(const MeshContainer& mesh, const char* name) {
        if (!name) return true;
        return mesh.has_name && mesh.name == name;  // the original strcmp()s a null name
    }

    static void mesh_matrix(const Model& model, int frame, const Matrix& parent, const char* name, bool animated,
                            Matrix& result, bool& found) {
        const Frame& f = model.frames[frame];
        const Matrix local = multiply(animated ? f.rot : f.trans, parent);
        for (int mesh = f.meshes; mesh >= 0; mesh = model.meshes[mesh].next) {
            if (matches(model.meshes[mesh], name)) {
                result = local;
                found = true;
            }
        }
        for (int child = f.child; child >= 0; child = model.frames[child].sibling)
            mesh_matrix(model, child, local, name, animated, result, found);
    }

    static void minmax(const Model& model, int frame, const Matrix& parent, const char* name, bool animated,
                       float* out, int& count) {
        const Frame& f = model.frames[frame];
        const Matrix local = multiply(animated ? f.rot : f.trans, parent);
        for (int mesh = f.meshes; mesh >= 0; mesh = model.meshes[mesh].next) {
            const MeshContainer& container = model.meshes[mesh];
            if (!matches(container, name)) continue;
            for (std::size_t i = 0; i + 2 < container.positions.size(); i += 3) {
                float v[3];
                transform_coord(v, &container.positions[i], local);
                if (count == 0) {
                    out[0] = out[3] = v[0];
                    out[1] = out[4] = v[1];
                    out[2] = out[5] = v[2];
                } else {
                    for (int k = 0; k < 3; ++k) {
                        if (v[k] < out[k]) out[k] = v[k];
                        if (v[k] > out[3 + k]) out[3 + k] = v[k];
                    }
                }
                ++count;
            }
        }
        for (int child = f.child; child >= 0; child = model.frames[child].sibling)
            minmax(model, child, local, name, animated, out, count);
    }
};

}  // namespace csmesh
}  // namespace cp2
