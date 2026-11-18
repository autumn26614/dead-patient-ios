#pragma once
// CSMesh's LoadMesh (0x1000a690): a Mesh data object becomes a mesh
// container with D3DX8's skin mesh, the indexed blended mesh when it has
// bones (GenerateMesh, 0x1000a006, method 1), its materials and their
// textures. See docs/csmesh-abi.md, "Meshes".
#include "d3dx_mesh.hpp"
#include "d3dx_texture.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cp2 {
namespace csmesh {

// Reads a file relative to the process working directory (CreateFileA on
// Windows; the asset layer's virtual working directory on iOS).
using FileReader = std::function<bool(const std::string& path, std::vector<std::uint8_t>& data)>;

struct MeshResources {
    std::unique_ptr<d3dx::SkinMesh> skin;
    std::vector<d3dx::Material> materials;  // ambient = diffuse, as LoadMesh sets it
    std::vector<d3d8::Texture*> textures;   // null where the file is missing or fails to load
    // FindBones: the frame of each bone (-1 when no frame has its name),
    // filled by the renderer on the first draw.
    std::vector<int> bone_frames;
    MeshResources() = default;
    MeshResources(const MeshResources&) = delete;
    MeshResources& operator=(const MeshResources&) = delete;
    ~MeshResources() {
        for (d3d8::Texture* t : textures)
            if (t) t->release();
    }
};

inline bool load_mesh_resources(const xfile::Object& mesh, const xfile::Document& document, d3d8::Device& device,
                                const FileReader& read_file, MeshResources& out, std::string& error) {
    out.skin.reset(new d3dx::SkinMesh());
    if (!d3dx::load_skin_mesh(mesh, document, device, *out.skin, error)) return false;
    // With bones: GenerateMesh with the indexed method and a 255-entry palette.
    // Without: GetOriginalMesh keeps the loaded mesh.
    if (!out.skin->bones.empty() && !d3dx::convert_to_indexed_blended(*out.skin, 255, error)) return false;
    if (out.skin->materials.empty()) {
        // No material list: one white material without a texture.
        d3dx::Material white;
        white.d3d[0] = white.d3d[1] = white.d3d[2] = 1.0f;
        out.materials.push_back(white);
        out.textures.push_back(nullptr);
        return true;
    }
    for (const d3dx::Material& source : out.skin->materials) {
        d3dx::Material m = source;
        for (int k = 0; k < 4; ++k) m.d3d[4 + k] = m.d3d[k];  // Ambient = Diffuse
        out.materials.push_back(m);
        d3d8::Texture* texture = nullptr;
        if (m.has_texture) {
            // DXUtil_FindMediaFile (0x10005eeb): the name as given if it opens,
            // otherwise the DirectX SDK media directory from the registry,
            // which does not exist here; then D3DXCreateTextureFromFileA with
            // D3DX's defaults (full mip chain, TRIANGLE|DITHER, BOX mips).
            std::vector<std::uint8_t> data;
            if (read_file && read_file(m.texture, data) && !data.empty()) {
                const d3dx::HResult r = d3dx::create_texture_from_memory(
                    &device, data.data(), data.size(), 0xffffffffu, 0xffffffffu, 0xffffffffu, 0, 0,
                    d3d8::kPoolManaged, 0xffffffffu, 0xffffffffu, 0, texture);
                if (d3d8::failed(r)) texture = nullptr;
            }
        }
        out.textures.push_back(texture);
    }
    return true;
}

}  // namespace csmesh
}  // namespace cp2
