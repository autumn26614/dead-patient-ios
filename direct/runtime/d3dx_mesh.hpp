#pragma once
// D3DX8 mesh loading as statically linked into the original plugin/CSMesh.dll:
// D3DXLoadSkinMeshFromXof (0x100199c5, loader 0x10018b01, skin mesh
// 0x1001268f) and ID3DXSkinMesh::ConvertToIndexedBlendedMesh, issuing the same
// Direct3D 8 calls in the same order and producing the same buffer contents.
//
// The rules below were recovered from the original's code paths and checked
// byte for byte against the original on every model the game loads (112
// models; docs/csmesh-abi.md, "Meshes"). Paths no game model reaches stop
// with an explicit "not implemented" error instead of guessing.
#include "d3d8.hpp"
#include "xfile.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cp2 {
namespace d3dx {

constexpr std::uint32_t kFvfXyz = 0x002, kFvfNormal = 0x010, kFvfDiffuse = 0x040, kFvfTex1 = 0x100,
                        kFvfLastBetaUbyte4 = 0x1000;

// D3DXMATERIAL: the Direct3D material and the texture file name.
struct Material {
    float d3d[17] = {};  // diffuse, ambient, specular, emissive (RGBA), power
    std::string texture;
    bool has_texture = false;
};

// One SkinWeights object (bone): influenced vertices, weights, offset matrix.
struct Bone {
    std::string name;
    std::vector<std::uint32_t> vertices;
    std::vector<float> weights;
    float offset[16] = {};
};

// D3DXBONECOMBINATION: an attribute range of the converted mesh and the bones
// of its matrix palette (palette entry k = bones[k]).
struct BoneCombination {
    std::uint32_t attribute = 0, face_start = 0, face_count = 0, vertex_start = 0, vertex_count = 0;
    std::vector<std::uint32_t> bones;
};

// ID3DXSkinMesh with its original mesh, and the converted mesh once built.
struct SkinMesh {
    d3d8::Device* device = nullptr;
    d3d8::Direct3D* d3d = nullptr;           // GetDirect3D reference held by the mesh
    d3d8::Buffer* indices = nullptr;         // original mesh, SYSTEMMEM, 16-bit
    d3d8::Buffer* vertices = nullptr;
    std::uint32_t fvf = 0, vertex_count = 0, face_count = 0;
    std::vector<std::uint32_t> faces;        // three indices per triangle
    std::vector<std::uint32_t> attributes;   // per triangle
    std::vector<std::uint32_t> source;       // file vertex of each mesh vertex
    std::vector<Material> materials;
    std::vector<Bone> bones;
    // ConvertToIndexedBlendedMesh output.
    d3d8::Direct3D* blended_d3d = nullptr;
    d3d8::Buffer* blended_indices = nullptr;
    d3d8::Buffer* blended_vertices = nullptr;
    std::uint32_t blended_fvf = 0, max_influences = 0;
    std::vector<BoneCombination> combinations;

    SkinMesh() = default;
    SkinMesh(const SkinMesh&) = delete;
    SkinMesh& operator=(const SkinMesh&) = delete;
    ~SkinMesh() {
        for (d3d8::Buffer* b : {indices, vertices, blended_indices, blended_vertices})
            if (b) b->release();
        if (d3d) d3d->release();
        if (blended_d3d) blended_d3d->release();
    }
};

namespace detail {

inline std::uint32_t word(const xfile::Value& v) { return v.kind == xfile::Value::Float ? static_cast<std::uint32_t>(v.as_float()) : v.word; }
inline float real(const xfile::Value& v) { return v.as_float(); }

// Reads `count` values starting at `at`; false if the object is too short.
inline bool have(const xfile::Object& o, std::size_t at, std::size_t count) { return at + count <= o.values.size(); }

// A face list: count, then for each face a corner count and the corners.
inline bool read_faces(const xfile::Object& o, std::size_t& at, std::vector<std::vector<std::uint32_t>>& faces,
                       std::string& error) {
    if (!have(o, at, 1)) { error = o.type + ": missing face count"; return false; }
    const std::uint32_t count = word(o.values[at++]);
    faces.resize(count);
    for (std::uint32_t f = 0; f < count; ++f) {
        if (!have(o, at, 1)) { error = o.type + ": face list truncated"; return false; }
        const std::uint32_t n = word(o.values[at++]);
        if (!have(o, at, n)) { error = o.type + ": face list truncated"; return false; }
        if (n < 3) { error = o.type + ": a face with fewer than three corners is not implemented"; return false; }
        faces[f].resize(n);
        for (std::uint32_t k = 0; k < n; ++k) faces[f][k] = word(o.values[at++]);
    }
    return true;
}

// Material template: faceColor RGBA, power, specularColor RGB, emissiveColor
// RGB, optional TextureFilename child.
inline bool read_material(const xfile::Object& o, Material& m, std::string& error) {
    if (!have(o, 0, 11)) { error = "Material truncated"; return false; }
    for (int k = 0; k < 4; ++k) m.d3d[k] = real(o.values[k]);          // diffuse
    m.d3d[16] = real(o.values[4]);                                      // power
    for (int k = 0; k < 3; ++k) m.d3d[8 + k] = real(o.values[5 + k]);  // specular
    for (int k = 0; k < 3; ++k) m.d3d[12 + k] = real(o.values[8 + k]); // emissive
    // Specular and emissive alpha and the ambient colour are not given by the
    // file; their D3DX values are confirmed when materials reach SetMaterial.
    m.d3d[11] = 1.0f;
    m.d3d[15] = 1.0f;
    for (const xfile::Child& c : o.children) {
        if (c.object && c.object->type == "TextureFilename" && !c.object->values.empty()) {
            m.texture = c.object->values[0].text;
            m.has_texture = true;
        }
    }
    return true;
}

// Fan triangulation of a polygon: (0, 1, 2), (0, 2, 3), ...
inline void fan(const std::vector<std::uint32_t>& f, std::vector<std::uint32_t>& out) {
    for (std::size_t j = 1; j + 1 < f.size(); ++j) {
        out.push_back(f[0]);
        out.push_back(f[j]);
        out.push_back(f[j + 1]);
    }
}

}  // namespace detail

// The geometry of a Mesh data object as D3DX8's loaders build it:
//   - vertices in file order; each vertex takes the normal of its first face
//     corner (file face order); a later corner with a normal of another value
//     gets a duplicate vertex appended at the end, shared by later corners
//     with the same vertex and normal value;
//   - polygons fan-triangulated, faces in file order with their material;
//   - FVF XYZ, plus NORMAL with MeshNormals, plus DIFFUSE with
//     MeshVertexColors (where the caller accepts them), plus TEX1 with
//     MeshTextureCoords.
struct Geometry {
    std::uint32_t fvf = 0, vertex_count = 0, face_count = 0;
    std::vector<std::uint32_t> faces;       // three indices per triangle
    std::vector<std::uint32_t> attributes;  // per triangle
    std::vector<std::uint32_t> source;      // file vertex of each mesh vertex
    std::vector<Material> materials;
    std::vector<Bone> bones;
    std::vector<unsigned char> vertex_data;  // vertex_count vertices of fvf
};

// Reads the geometry; colours = whether MeshVertexColors is accepted.
inline bool read_geometry(const xfile::Object& mesh, const xfile::Document& document, bool colours, Geometry& out,
                          std::string& error) {
    using namespace detail;
    if (!have(mesh, 0, 1)) { error = "Mesh without vertex count"; return false; }
    const std::uint32_t file_vertices = word(mesh.values[0]);
    if (!have(mesh, 1, static_cast<std::size_t>(file_vertices) * 3)) { error = "Mesh vertex data truncated"; return false; }
    std::vector<float> positions(static_cast<std::size_t>(file_vertices) * 3);
    for (std::size_t i = 0; i < positions.size(); ++i) positions[i] = real(mesh.values[1 + i]);
    std::size_t at = 1 + positions.size();
    std::vector<std::vector<std::uint32_t>> faces;
    if (!read_faces(mesh, at, faces, error)) return false;
    for (const auto& f : faces)
        for (std::uint32_t v : f)
            if (v >= file_vertices) { error = "Mesh face index out of range"; return false; }

    const xfile::Object* normals = nullptr;
    const xfile::Object* coordinates = nullptr;
    const xfile::Object* material_list = nullptr;
    const xfile::Object* vertex_colours = nullptr;
    std::vector<const xfile::Object*> skin_weights;
    for (const xfile::Child& c : mesh.children) {
        if (!c.object) continue;
        const std::string& t = c.object->type;
        if (t == "MeshNormals") normals = c.object.get();
        else if (t == "MeshTextureCoords") coordinates = c.object.get();
        else if (t == "MeshMaterialList") material_list = c.object.get();
        else if (t == "SkinWeights") skin_weights.push_back(c.object.get());
        else if (t == "MeshVertexColors" && colours) vertex_colours = c.object.get();
        else if (t == "MeshVertexColors" || t == "FVFData" || t == "DeclData") {
            error = "Mesh child " + t + " is not implemented";
            return false;
        }
    }

    // Normals and vertex splitting.
    std::vector<float> normal_vectors;
    std::vector<std::vector<std::uint32_t>> normal_faces;
    std::vector<std::uint32_t> source(file_vertices);
    for (std::uint32_t v = 0; v < file_vertices; ++v) source[v] = v;
    std::vector<std::int64_t> normal_of(file_vertices, -1);
    if (normals) {
        if (!have(*normals, 0, 1)) { error = "MeshNormals truncated"; return false; }
        const std::uint32_t n = word(normals->values[0]);
        if (!have(*normals, 1, static_cast<std::size_t>(n) * 3)) { error = "MeshNormals truncated"; return false; }
        normal_vectors.resize(static_cast<std::size_t>(n) * 3);
        for (std::size_t i = 0; i < normal_vectors.size(); ++i) normal_vectors[i] = real(normals->values[1 + i]);
        std::size_t nat = 1 + normal_vectors.size();
        if (!read_faces(*normals, nat, normal_faces, error)) return false;
        if (normal_faces.size() != faces.size()) { error = "MeshNormals face count differs from the mesh"; return false; }
        // Normals are compared as float values: two indices of equal vectors
        // (+0 equal to -0) do not split a vertex, which keeps the normal of its
        // first corner. Comparing indices or bit patterns instead gives 53
        // and 27 mismatches on the static models.
        auto same = [&](std::int64_t a, std::uint32_t b) {
            const float* x = &normal_vectors[static_cast<std::size_t>(a) * 3];
            const float* y = &normal_vectors[static_cast<std::size_t>(b) * 3];
            return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
        };
        std::vector<std::vector<std::uint32_t>> duplicates(file_vertices);  // copies of each file vertex
        for (std::size_t f = 0; f < faces.size(); ++f) {
            if (normal_faces[f].size() != faces[f].size()) { error = "MeshNormals face shape differs from the mesh"; return false; }
            for (std::size_t k = 0; k < faces[f].size(); ++k) {
                const std::uint32_t v = faces[f][k], nidx = normal_faces[f][k];
                if (nidx >= n) { error = "MeshNormals index out of range"; return false; }
                if (normal_of[v] < 0) {
                    normal_of[v] = nidx;
                } else if (!same(normal_of[v], nidx)) {
                    std::uint32_t copy = 0;
                    bool found = false;
                    for (std::uint32_t d : duplicates[v])
                        if (same(normal_of[d], nidx)) { copy = d; found = true; break; }
                    if (!found) {
                        copy = static_cast<std::uint32_t>(source.size());
                        duplicates[v].push_back(copy);
                        source.push_back(v);
                        normal_of.push_back(nidx);
                    }
                    faces[f][k] = copy;
                }
            }
        }
    }
    const std::size_t vertex_count = source.size();
    if (vertex_count > 0xffff) { error = "meshes with more than 65535 vertices (32-bit indices) are not implemented"; return false; }

    std::vector<float> uv;
    if (coordinates) {
        if (!have(*coordinates, 0, 1)) { error = "MeshTextureCoords truncated"; return false; }
        const std::uint32_t n = word(coordinates->values[0]);
        if (n != file_vertices || !have(*coordinates, 1, static_cast<std::size_t>(n) * 2)) {
            error = "MeshTextureCoords count differs from the vertex count";
            return false;
        }
        uv.resize(static_cast<std::size_t>(n) * 2);
        for (std::size_t i = 0; i < uv.size(); ++i) uv[i] = real(coordinates->values[1 + i]);
    }

    // MeshVertexColors: an index and an RGBA colour per entry. Every coloured
    // vertex in the game's files is opaque white, which D3DX stores as
    // 0x00ffffff: the alpha is not kept. How other colours are rounded is not
    // determined by the game's data, so they stop here.
    std::vector<std::uint32_t> colour_of;
    if (vertex_colours) {
        if (!have(*vertex_colours, 0, 1)) { error = "MeshVertexColors truncated"; return false; }
        const std::uint32_t n = word(vertex_colours->values[0]);
        if (!have(*vertex_colours, 1, static_cast<std::size_t>(n) * 5)) { error = "MeshVertexColors truncated"; return false; }
        std::vector<bool> seen(file_vertices, false);
        colour_of.assign(file_vertices, 0);
        for (std::uint32_t k = 0; k < n; ++k) {
            const std::uint32_t v = word(vertex_colours->values[1 + 5 * k]);
            if (v >= file_vertices) { error = "MeshVertexColors index out of range"; return false; }
            for (int c = 0; c < 4; ++c) {
                if (real(vertex_colours->values[2 + 5 * k + c]) != 1.0f) {
                    error = "vertex colours other than opaque white are not implemented";
                    return false;
                }
            }
            colour_of[v] = 0x00ffffffu;
            seen[v] = true;
        }
        for (bool s : seen)
            if (!s) { error = "MeshVertexColors without a colour for every vertex is not implemented"; return false; }
    }

    // Materials and the per-face attribute.
    std::vector<std::uint32_t> face_material(faces.size(), 0);
    if (material_list) {
        if (!have(*material_list, 0, 2)) { error = "MeshMaterialList truncated"; return false; }
        const std::uint32_t count = word(material_list->values[0]), n = word(material_list->values[1]);
        if (!have(*material_list, 2, n)) { error = "MeshMaterialList truncated"; return false; }
        for (std::uint32_t f = 0; f < faces.size(); ++f) {
            // A shorter list repeats its last entry, as the .x format allows.
            const std::uint32_t i = f < n ? f : n - 1;
            face_material[f] = n ? word(material_list->values[2 + i]) : 0;
            if (face_material[f] >= count) { error = "MeshMaterialList index out of range"; return false; }
        }
        for (const xfile::Child& c : material_list->children) {
            std::shared_ptr<xfile::Object> object = c.object ? c.object : document.find(c.reference);
            if (!object || object->type != "Material") continue;
            Material m;
            if (!read_material(*object, m, error)) return false;
            out.materials.push_back(m);
        }
        if (out.materials.size() != count) { error = "MeshMaterialList material count differs from its materials"; return false; }
    }

    for (std::size_t f = 0; f < faces.size(); ++f) {
        const std::size_t before = out.faces.size();
        fan(faces[f], out.faces);
        for (std::size_t t = before; t < out.faces.size(); t += 3) out.attributes.push_back(face_material[f]);
    }
    out.face_count = static_cast<std::uint32_t>(out.attributes.size());
    out.vertex_count = static_cast<std::uint32_t>(vertex_count);
    out.source = source;
    out.fvf = kFvfXyz | (normals ? kFvfNormal : 0) | (vertex_colours ? kFvfDiffuse : 0) | (coordinates ? kFvfTex1 : 0);

    for (const xfile::Object* w : skin_weights) {
        Bone b;
        if (!have(*w, 0, 2)) { error = "SkinWeights truncated"; return false; }
        b.name = w->values[0].text;
        const std::uint32_t n = word(w->values[1]);
        if (!have(*w, 2, static_cast<std::size_t>(n) * 2 + 16)) { error = "SkinWeights truncated"; return false; }
        for (std::uint32_t k = 0; k < n; ++k) {
            const std::uint32_t v = word(w->values[2 + k]);
            if (v >= file_vertices) { error = "SkinWeights vertex out of range"; return false; }
            b.vertices.push_back(v);
            b.weights.push_back(real(w->values[2 + n + k]));
        }
        for (int k = 0; k < 16; ++k) b.offset[k] = real(w->values[2 + 2 * n + k]);
        out.bones.push_back(std::move(b));
    }

    // Vertex data: position, normal, colour, texture coordinates.
    const std::uint32_t stride = d3d8::fvf_stride(out.fvf);
    out.vertex_data.assign(vertex_count * stride, 0);
    for (std::size_t v = 0; v < vertex_count; ++v) {
        unsigned char* p = out.vertex_data.data() + v * stride;
        const std::uint32_t s = source[v];
        std::memcpy(p, &positions[static_cast<std::size_t>(s) * 3], 12);
        p += 12;
        if (normals) {
            if (normal_of[v] >= 0) std::memcpy(p, &normal_vectors[static_cast<std::size_t>(normal_of[v]) * 3], 12);
            p += 12;
        }
        if (vertex_colours) {
            std::memcpy(p, &colour_of[s], 4);
            p += 4;
        }
        if (coordinates) std::memcpy(p, &uv[static_cast<std::size_t>(s) * 2], 8);
    }
    return true;
}

// Creates a mesh's 16-bit index and vertex buffers in D3DX's order (index
// buffer first) and fills them.
inline bool create_mesh_buffers(d3d8::Device& device, std::uint32_t pool, std::uint32_t fvf,
                                const std::vector<std::uint32_t>& faces, const unsigned char* vertex_data,
                                std::uint32_t vertex_count, d3d8::Buffer*& indices, d3d8::Buffer*& vertices,
                                std::string& error) {
    const std::uint32_t stride = d3d8::fvf_stride(fvf);
    if (device.create_index_buffer(static_cast<std::uint32_t>(faces.size()) * 2, 0, d3d8::kFormatIndex16, pool,
                                   indices) != d3d8::kOk ||
        device.create_vertex_buffer(vertex_count * stride, 0, fvf, pool, vertices) != d3d8::kOk) {
        error = "mesh buffer creation failed";
        return false;
    }
    unsigned char* ib = nullptr;
    unsigned char* vb = nullptr;
    indices->lock(ib, 0);
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const std::uint16_t v = static_cast<std::uint16_t>(faces[i]);
        std::memcpy(ib + 2 * i, &v, 2);
    }
    indices->unlock();
    vertices->lock(vb, 0);
    if (vertex_count) std::memcpy(vb, vertex_data, static_cast<std::size_t>(vertex_count) * stride);
    vertices->unlock();
    return true;
}

// D3DXLoadSkinMeshFromXof (0x100199c5) with D3DXMESH_SYSTEMMEM, as CSMesh
// calls it: the geometry above, without vertex colours.
// Device calls: AddRef, GetDirect3D, CreateIndexBuffer, CreateVertexBuffer
// (both SYSTEMMEM), then GetDeviceCaps and GetCreationParameters.
inline bool load_skin_mesh(const xfile::Object& mesh, const xfile::Document& document, d3d8::Device& device,
                           SkinMesh& out, std::string& error) {
    Geometry g;
    if (!read_geometry(mesh, document, false, g, error)) return false;
    out.fvf = g.fvf;
    out.vertex_count = g.vertex_count;
    out.face_count = g.face_count;
    out.faces = std::move(g.faces);
    out.attributes = std::move(g.attributes);
    out.source = std::move(g.source);
    out.materials = std::move(g.materials);
    out.bones = std::move(g.bones);
    // Device objects, in the original's order.
    out.device = &device;
    device.add_ref();
    device.get_direct3d(out.d3d);
    if (!create_mesh_buffers(device, d3d8::kPoolSystemMem, out.fvf, out.faces, g.vertex_data.data(), out.vertex_count,
                             out.indices, out.vertices, error))
        return false;
    d3d8::Caps caps;
    device.get_device_caps(caps);
    device.get_creation_parameters();
    return true;
}

// D3DXATTRIBUTERANGE.
struct AttributeRange {
    std::uint32_t id = 0, face_start = 0, face_count = 0, vertex_start = 0, vertex_count = 0;
};

// ID3DXMesh with 16-bit indices, as D3DXLoadMeshFromX, CloneMeshFVF and
// Optimize create it in ZGP's copy of D3DX8. It holds a device reference and
// the device's Direct3D object; destroying it releases both (Direct3D first).
struct Mesh {
    d3d8::Device* device = nullptr;
    d3d8::Direct3D* d3d = nullptr;
    d3d8::Buffer* indices = nullptr;
    d3d8::Buffer* vertices = nullptr;
    std::uint32_t options = 0, fvf = 0, vertex_count = 0, face_count = 0;
    std::vector<std::uint32_t> faces;       // three indices per triangle
    std::vector<std::uint32_t> attributes;  // per triangle
    std::vector<AttributeRange> table;      // GetAttributeTable (after Optimize)

    Mesh() = default;
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    ~Mesh() {
        if (indices) indices->release();
        if (vertices) vertices->release();
        if (d3d) d3d->release();
        if (device) device->release();
    }

    // A copy of the vertex data (a whole-buffer read).
    std::vector<unsigned char> vertex_data() const {
        std::vector<unsigned char> data(static_cast<std::size_t>(vertex_count) * d3d8::fvf_stride(fvf));
        unsigned char* locked = nullptr;
        if (!data.empty() && vertices && !d3d8::failed(vertices->lock(locked, 0x10))) {
            std::memcpy(data.data(), locked, data.size());
            vertices->unlock();
        }
        return data;
    }
};

namespace detail {

// A mesh's device objects: AddRef, GetDirect3D, then the buffers. Only
// D3DXMESH_SYSTEMMEM (0x110), as ZGP loads, is implemented.
inline bool create_mesh(d3d8::Device& device, std::uint32_t options, std::uint32_t fvf,
                        const std::vector<std::uint32_t>& faces, const std::vector<std::uint32_t>& attributes,
                        const unsigned char* vertex_data, std::uint32_t vertex_count, Mesh& out, std::string& error) {
    if ((options & 0x330u) != 0x110u) { error = "D3DX meshes outside system memory are not implemented"; return false; }
    out.device = &device;
    device.add_ref();
    device.get_direct3d(out.d3d);
    out.options = options;
    out.fvf = fvf;
    out.vertex_count = vertex_count;
    out.face_count = static_cast<std::uint32_t>(attributes.size());
    out.faces = faces;
    out.attributes = attributes;
    return create_mesh_buffers(device, d3d8::kPoolSystemMem, fvf, faces, vertex_data, vertex_count, out.indices,
                               out.vertices, error);
}

}  // namespace detail

namespace detail {

// Whether a frame hierarchy holds a mesh.
inline bool holds_mesh(const xfile::Object& o) {
    for (const xfile::Child& c : o.children)
        if (c.object && (c.object->type == "Mesh" || holds_mesh(*c.object))) return true;
    return false;
}

}  // namespace detail

// D3DXLoadMeshFromX (0x1002ef19) as ZGP's loadx calls it. D3DX merges every
// mesh of the file into one, each transformed by its frames; the game's
// static models have exactly one mesh, at the top level, so it is used as it
// is. Animation sets, the tick rate and frames without meshes contribute
// nothing; skin weights and other mesh children (VertexDuplicationIndices,
// XSkinMeshHeader) are not used. The mesh is the geometry above (vertex
// colours included) with the given options; the materials are those of the
// material list (D3DXMATERIAL; the ambient colour is 0). ZGP requests no
// adjacency. Frames holding meshes and several meshes stop here.
inline bool load_mesh_from_x(const xfile::Document& document, std::uint32_t options, d3d8::Device& device,
                             Mesh& out, std::vector<Material>& materials, std::string& error) {
    const xfile::Object* mesh = nullptr;
    for (const auto& object : document.objects) {
        const std::string& t = object->type;
        if (t == "Header" || t == "AnimTicksPerSecond" || t == "AnimationSet") continue;
        if (t == "Frame" && !detail::holds_mesh(*object)) continue;
        if (t != "Mesh" || mesh) {
            error = "files with " + (mesh ? std::string("several meshes") : t + " objects") +
                    " (merged by D3DXLoadMeshFromX) are not implemented";
            return false;
        }
        mesh = object.get();
    }
    if (!mesh) { error = "no Mesh in the file"; return false; }
    Geometry g;
    if (!read_geometry(*mesh, document, true, g, error)) return false;
    if (g.materials.empty()) { error = "meshes without a material list (D3DX's default material) are not implemented"; return false; }
    // The mesh is merged with its frame's transformation, the identity for a
    // top-level mesh: positions as D3DXVec3TransformCoord, x*m11 + y*m21 +
    // z*m31 + m41, and normals as D3DXVec3TransformNormal, x*m11 + y*m21 +
    // z*m31, summed left to right. With the identity only the signs of zeros
    // change: -0 + (+0) is +0, so a position never stays -0, and a normal
    // component stays -0 only when every product is -0. Texture coordinates
    // are not transformed.
    {
        const std::uint32_t stride = d3d8::fvf_stride(g.fvf);
        const bool has_normals = (g.fvf & kFvfNormal) != 0;
        for (std::uint32_t v = 0; v < g.vertex_count; ++v) {
            unsigned char* p = g.vertex_data.data() + static_cast<std::size_t>(v) * stride;
            for (int part = 0; part < (has_normals ? 2 : 1); ++part) {
                float in[3], out[3];
                std::memcpy(in, p + 12 * part, 12);
                for (int c = 0; c < 3; ++c) {
                    float sum = in[0] * (c == 0 ? 1.0f : 0.0f);
                    sum = sum + in[1] * (c == 1 ? 1.0f : 0.0f);
                    sum = sum + in[2] * (c == 2 ? 1.0f : 0.0f);
                    if (part == 0) sum = sum + 0.0f;
                    out[c] = sum;
                }
                std::memcpy(p + 12 * part, out, 12);
            }
        }
    }
    materials = g.materials;
    return detail::create_mesh(device, options, g.fvf, g.faces, g.attributes, g.vertex_data.data(), g.vertex_count,
                               out, error);
}

// ID3DXMesh::CloneMeshFVF(options, fvf): a mesh of the same faces and
// attributes whose vertices are converted to `fvf`. Components both formats
// have are copied; a normal the source lacks is zero (D3DXComputeNormals
// follows in ZGP); a colour the target lacks is dropped. Device calls as for
// any new mesh.
inline bool clone_mesh_fvf(const Mesh& source, std::uint32_t options, std::uint32_t fvf, d3d8::Device& device,
                           Mesh& out, std::string& error) {
    const std::uint32_t known = kFvfXyz | kFvfNormal | kFvfDiffuse | kFvfTex1;
    if ((source.fvf & ~known) || (fvf & ~known) || !(source.fvf & kFvfXyz) || !(fvf & kFvfXyz)) {
        error = "CloneMeshFVF between these vertex formats is not implemented";
        return false;
    }
    // Byte offset of a component in a vertex of format f, or -1.
    auto offset_of = [](std::uint32_t f, std::uint32_t component) -> int {
        int offset = 12;
        if (component == kFvfNormal) return (f & kFvfNormal) ? offset : -1;
        if (f & kFvfNormal) offset += 12;
        if (component == kFvfDiffuse) return (f & kFvfDiffuse) ? offset : -1;
        if (f & kFvfDiffuse) offset += 4;
        return (f & kFvfTex1) ? offset : -1;
    };
    const std::uint32_t from = d3d8::fvf_stride(source.fvf), to = d3d8::fvf_stride(fvf);
    const std::vector<unsigned char> data = source.vertex_data();
    std::vector<unsigned char> converted(static_cast<std::size_t>(source.vertex_count) * to, 0);
    const std::pair<std::uint32_t, std::uint32_t> components[] = {{kFvfNormal, 12}, {kFvfDiffuse, 4}, {kFvfTex1, 8}};
    for (std::uint32_t v = 0; v < source.vertex_count; ++v) {
        const unsigned char* s = data.data() + static_cast<std::size_t>(v) * from;
        unsigned char* d = converted.data() + static_cast<std::size_t>(v) * to;
        std::memcpy(d, s, 12);
        for (const auto& c : components) {
            const int a = offset_of(source.fvf, c.first), b = offset_of(fvf, c.first);
            if (a >= 0 && b >= 0) std::memcpy(d + b, s + a, c.second);
        }
    }
    return detail::create_mesh(device, options, fvf, source.faces, source.attributes, converted.data(),
                               source.vertex_count, out, error);
}

// D3DXComputeNormals (0x100253ca, the one-argument D3DX 8.0 form): every
// normal is zeroed; each face (i0, i1, i2) adds Cross(P0 - P1, P0 - P2) to
// the normals of its three vertices, in face order; then every normal is
// normalized in place (D3DXVec3Normalize). Only the vertices a face indexes
// count: coincident vertices are not merged. All arithmetic is single
// precision (the x87 unit in Direct3D's precision mode). The caller supplies
// D3DXVec3Cross and D3DXVec3Normalize (zgp_device.hpp has the transcriptions).
template <typename Cross, typename Normalize>
inline bool compute_normals(Mesh& mesh, Cross cross, Normalize normalize, std::string& error) {
    if (!(mesh.fvf & kFvfNormal)) { error = "D3DXComputeNormals on a mesh without normals"; return false; }
    const std::uint32_t stride = d3d8::fvf_stride(mesh.fvf);
    unsigned char* data = nullptr;
    if (d3d8::failed(mesh.vertices->lock(data, 0))) { error = "vertex buffer lock failed"; return false; }
    auto at = [&](std::uint32_t v, int offset) { return data + static_cast<std::size_t>(v) * stride + offset; };
    auto load = [&](std::uint32_t v, int offset, float out[3]) { std::memcpy(out, at(v, offset), 12); };
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    for (std::uint32_t v = 0; v < mesh.vertex_count; ++v) std::memcpy(at(v, 12), zero, 12);
    for (std::uint32_t f = 0; f < mesh.face_count; ++f) {
        const std::uint32_t* i = &mesh.faces[3 * static_cast<std::size_t>(f)];
        float p0[3], p1[3], p2[3];
        load(i[0], 0, p0);
        load(i[1], 0, p1);
        load(i[2], 0, p2);
        const float e1[3] = {p0[0] - p1[0], p0[1] - p1[1], p0[2] - p1[2]};
        const float e2[3] = {p0[0] - p2[0], p0[1] - p2[1], p0[2] - p2[2]};
        float n[3];
        cross(e1, e2, n);
        for (int k = 0; k < 3; ++k) {
            float sum[3];
            load(i[k], 12, sum);
            for (int c = 0; c < 3; ++c) sum[c] = n[c] + sum[c];
            std::memcpy(at(i[k], 12), sum, 12);
        }
    }
    for (std::uint32_t v = 0; v < mesh.vertex_count; ++v) {
        float n[3];
        load(v, 12, n);
        normalize(n);
        std::memcpy(at(v, 12), n, 12);
    }
    mesh.vertices->unlock();
    return true;
}

// ID3DXMesh::Optimize as ZGP calls it (flags 2, D3DXMESH_DONOTCLIP, with no
// adjacency or remap outputs). D3DX8 returns an attribute-sorted copy in the
// source's pool; this is what the original produces for every static model:
//   - faces grouped by attribute in ascending order, keeping their order;
//   - each vertex belongs to the attribute of the first face (in the source's
//     face order) that indexes it; an attribute's own vertices come first, in
//     ascending source order, then copies of vertices it shares with an
//     attribute that owns them, in the order its faces first index them;
//   - the attribute table holds one range per attribute present.
// Vertices no face indexes do not occur in the game's models and stop here.
inline bool optimize(const Mesh& source, std::uint32_t flags, d3d8::Device& device, Mesh& out, std::string& error) {
    if (flags != 2) { error = "ID3DXMesh::Optimize flags other than 2 are not implemented"; return false; }
    const std::uint32_t unowned = 0xffffffffu;
    std::vector<std::uint32_t> owner(source.vertex_count, unowned);
    for (std::uint32_t f = 0; f < source.face_count; ++f)
        for (int k = 0; k < 3; ++k) {
            std::uint32_t& o = owner[source.faces[3 * static_cast<std::size_t>(f) + k]];
            if (o == unowned) o = source.attributes[f];
        }
    for (std::uint32_t o : owner)
        if (o == unowned) { error = "Optimize with vertices no face indexes is not implemented"; return false; }
    std::vector<std::uint32_t> ids(source.attributes);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    const std::uint32_t stride = d3d8::fvf_stride(source.fvf);
    const std::vector<unsigned char> data = source.vertex_data();
    std::vector<unsigned char> vertices;
    std::vector<std::uint32_t> faces, attributes;
    std::vector<AttributeRange> table;
    std::uint32_t count = 0;
    for (std::uint32_t id : ids) {
        AttributeRange range;
        range.id = id;
        range.face_start = static_cast<std::uint32_t>(attributes.size());
        range.vertex_start = count;
        std::map<std::uint32_t, std::uint32_t> local;
        auto add = [&](std::uint32_t v) {
            local[v] = count++;
            const unsigned char* p = data.data() + static_cast<std::size_t>(v) * stride;
            vertices.insert(vertices.end(), p, p + stride);
        };
        for (std::uint32_t v = 0; v < source.vertex_count; ++v)
            if (owner[v] == id) add(v);
        for (std::uint32_t f = 0; f < source.face_count; ++f) {
            if (source.attributes[f] != id) continue;
            for (int k = 0; k < 3; ++k) {
                const std::uint32_t v = source.faces[3 * static_cast<std::size_t>(f) + k];
                if (!local.count(v)) add(v);
                faces.push_back(local[v]);
            }
            attributes.push_back(id);
        }
        range.face_count = static_cast<std::uint32_t>(attributes.size()) - range.face_start;
        range.vertex_count = count - range.vertex_start;
        table.push_back(range);
    }
    if (count > 0xffff) { error = "Optimize splitting a mesh beyond 65535 vertices is not implemented"; return false; }
    if (!detail::create_mesh(device, source.options, source.fvf, faces, attributes, vertices.data(), count, out, error))
        return false;
    out.table = table;
    return true;
}

// ID3DXSkinMesh::ConvertToIndexedBlendedMesh(D3DXMESH_SYSTEMMEM, adjacency,
// palette size 255, ...), as CSMesh's GenerateMesh calls it (0x1000a4d9):
//   - the most influences on any vertex selects the layout: one keeps the
//     original FVF; two or three give XYZB2/XYZB3 | LASTBETA_UBYTE4 (one or
//     two stored weights, then four palette-index bytes);
//   - one bone combination per attribute, whose palette is the bones its
//     faces use in ascending order;
//   - vertices are grouped stably by the attribute of the first face that
//     uses them; vertices no face uses go last and are not written (the
//     original copies them from out of range); faces keep their order;
//   - a vertex's influences are written in bone (SkinWeights) order; weight
//     slots beyond its influence count and the last, implicit weight are not
//     written.
// Device calls: AddRef, GetDirect3D, CreateIndexBuffer, CreateVertexBuffer
// (both SYSTEMMEM).
inline bool convert_to_indexed_blended(SkinMesh& m, std::uint32_t palette_size, std::string& error) {
    const std::size_t nv = m.vertex_count;
    struct Influence {
        std::uint32_t bone;
        float weight;
    };
    std::vector<std::vector<std::uint32_t>> by_source(nv ? *std::max_element(m.source.begin(), m.source.end()) + 1 : 0);
    for (std::size_t v = 0; v < nv; ++v) by_source[m.source[v]].push_back(static_cast<std::uint32_t>(v));
    std::vector<std::vector<Influence>> influences(nv);
    for (std::uint32_t b = 0; b < m.bones.size(); ++b)
        for (std::size_t k = 0; k < m.bones[b].vertices.size(); ++k)
            for (std::uint32_t v : by_source[m.bones[b].vertices[k]]) influences[v].push_back({b, m.bones[b].weights[k]});
    std::uint32_t max_influences = 0;
    for (const auto& i : influences) max_influences = std::max<std::uint32_t>(max_influences, static_cast<std::uint32_t>(i.size()));
    if (max_influences > 3) { error = "more than three influences per vertex are not implemented"; return false; }

    const std::uint32_t unused = 0xffffffffu;
    std::vector<std::uint32_t> first(nv, unused);
    for (std::size_t t = 0; t < m.attributes.size(); ++t) {
        for (int k = 0; k < 3; ++k) {
            const std::uint32_t v = m.faces[3 * t + k];
            if (first[v] == unused) first[v] = m.attributes[t];
            else if (first[v] != m.attributes[t]) {
                error = "vertices shared between attributes (split by the conversion) are not implemented";
                return false;
            }
        }
    }
    std::vector<std::uint32_t> order(nv);
    for (std::size_t v = 0; v < nv; ++v) order[v] = static_cast<std::uint32_t>(v);
    std::stable_sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) { return first[a] < first[b]; });
    std::vector<std::uint32_t> remap(nv);
    for (std::size_t k = 0; k < nv; ++k) remap[order[k]] = static_cast<std::uint32_t>(k);

    // Bone combinations: one per attribute present, in attribute order.
    std::map<std::uint32_t, std::vector<bool>> used;
    for (std::size_t t = 0; t < m.attributes.size(); ++t) {
        std::vector<bool>& bones = used[m.attributes[t]];
        bones.resize(m.bones.size(), false);
        for (int k = 0; k < 3; ++k)
            for (const Influence& i : influences[m.faces[3 * t + k]]) bones[i.bone] = true;
    }
    std::map<std::uint32_t, std::vector<std::uint32_t>> palette_index;  // attribute -> bone -> entry
    for (auto& entry : used) {
        BoneCombination c;
        c.attribute = entry.first;
        std::vector<std::uint32_t>& index = palette_index[entry.first];
        index.assign(m.bones.size(), 0);
        for (std::uint32_t b = 0; b < entry.second.size(); ++b) {
            if (!entry.second[b]) continue;
            index[b] = static_cast<std::uint32_t>(c.bones.size());
            c.bones.push_back(b);
        }
        if (c.bones.size() > palette_size) { error = "bone palettes larger than the palette size (split) are not implemented"; return false; }
        bool started = false;
        for (std::size_t t = 0; t < m.attributes.size(); ++t) {
            if (m.attributes[t] != c.attribute) continue;
            if (!started) { c.face_start = static_cast<std::uint32_t>(t); started = true; }
            c.face_count = static_cast<std::uint32_t>(t) - c.face_start + 1;
        }
        bool vstarted = false;
        for (std::size_t k = 0; k < nv; ++k) {
            if (first[order[k]] != c.attribute) continue;
            if (!vstarted) { c.vertex_start = static_cast<std::uint32_t>(k); vstarted = true; }
            c.vertex_count = static_cast<std::uint32_t>(k) - c.vertex_start + 1;
        }
        m.combinations.push_back(std::move(c));
    }

    const std::uint32_t weights = max_influences > 1 ? max_influences - 1 : 0;
    const std::uint32_t fvf = max_influences > 1
                                  ? ((m.fvf & ~0xeu) | (0x6u + 2u * max_influences - 2u) | kFvfLastBetaUbyte4)
                                  : m.fvf;
    const std::uint32_t stride = d3d8::fvf_stride(fvf), source_stride = d3d8::fvf_stride(m.fvf);
    m.max_influences = max_influences;
    m.blended_fvf = fvf;
    m.device->add_ref();
    m.device->get_direct3d(m.blended_d3d);
    if (m.device->create_index_buffer(m.face_count * 3 * 2, 0, d3d8::kFormatIndex16, d3d8::kPoolSystemMem, m.blended_indices) != d3d8::kOk ||
        m.device->create_vertex_buffer(static_cast<std::uint32_t>(nv) * stride, 0, fvf, d3d8::kPoolSystemMem, m.blended_vertices) != d3d8::kOk) {
        error = "converted mesh buffer creation failed";
        return false;
    }
    unsigned char* src = nullptr;
    unsigned char* dst = nullptr;
    unsigned char* ib = nullptr;
    m.vertices->lock(src, 0x10);
    m.blended_vertices->lock(dst, 0);
    for (std::size_t k = 0; k < nv; ++k) {
        const std::uint32_t v = order[k];
        if (first[v] == unused) continue;  // never written
        const unsigned char* s = src + static_cast<std::size_t>(v) * source_stride;
        unsigned char* d = dst + k * stride;
        std::memcpy(d, s, 12);
        if (max_influences > 1) {
            const std::vector<Influence>& inf = influences[v];
            for (std::uint32_t j = 0; j < weights && j < inf.size(); ++j) std::memcpy(d + 12 + 4 * j, &inf[j].weight, 4);
            for (std::uint32_t j = 0; j < inf.size() && j < 4; ++j)
                d[12 + 4 * weights + j] = static_cast<unsigned char>(palette_index[first[v]][inf[j].bone]);
            std::memcpy(d + 16 + 4 * weights, s + 12, source_stride - 12);
        } else {
            std::memcpy(d + 12, s + 12, source_stride - 12);
        }
    }
    m.blended_vertices->unlock();
    m.vertices->unlock();
    m.blended_indices->lock(ib, 0);
    for (std::size_t i = 0; i < m.faces.size(); ++i) {
        const std::uint16_t v = static_cast<std::uint16_t>(remap[m.faces[i]]);
        std::memcpy(ib + 2 * i, &v, 2);
    }
    m.blended_indices->unlock();
    return true;
}

}  // namespace d3dx
}  // namespace cp2
