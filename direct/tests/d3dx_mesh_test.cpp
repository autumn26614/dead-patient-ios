// Compares the native D3DX8 mesh loading (direct/runtime/d3dx_mesh.hpp) with
// the original plugin/CSMesh.dll on every model in a loadax corpus script.
//
//   d3dx_mesh_test INSTALL_DIR SCRIPT REFERENCE
//
// SCRIPT is a ZGP reference script whose models are loaded with
// "@chdir model/<name>" followed by "_loadax@16 ...". REFERENCE is the
// original's trace of that script (direct/tests/zgp_windows_reference.cpp).
// For each model, CSMesh's LoadMesh runs natively on a recording device:
// D3DXLoadSkinMeshFromXof, ConvertToIndexedBlendedMesh when the mesh has
// bones, and D3DXCreateTextureFromFileA for each material texture (files
// relative to the model directory, as after the game's chdir). Every device
// call, texture dump and buffer dump is compared with the original's lines
// for that loadax call. Object ids are renumbered per model; AddRef and
// Release counts (runtime objects, earlier models) and display-mode sizes
// (the desktop) are masked.
#include "csmesh_mesh.hpp"
#include "zgp_device.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool starts(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }

std::string renumber(const std::string& line, std::map<std::string, std::string>& ids) {
    std::string out;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '#' && i + 1 < line.size() && std::isdigit(static_cast<unsigned char>(line[i + 1]))) {
            std::size_t end = i + 1;
            while (end < line.size() && std::isdigit(static_cast<unsigned char>(line[end]))) ++end;
            const std::string id = line.substr(i + 1, end - i - 1);
            auto it = ids.find(id);
            if (it == ids.end()) it = ids.emplace(id, std::to_string(ids.size() + 1)).first;
            out += "#" + it->second;
            i = end - 1;
        } else {
            out += line[i];
        }
    }
    if (starts(out, "AddRef -> ")) out = "AddRef -> *";
    // IDirect3D8 reference counts include every earlier model's meshes in the
    // original's single run; each model here gets a fresh device.
    if (starts(out, "Release -> ")) out = "Release -> *";
    // Display-mode sizes and refresh rates are the machine's desktop.
    if (starts(out, "GetDisplayMode ptr out {")) {
        const std::size_t close = out.find('}');
        const std::size_t last = out.rfind(' ', close);
        out = "GetDisplayMode ptr out {* * *" + out.substr(last, close - last) + out.substr(close);
    }
    return out;
}

// Lines of one loadax call, without its RET line.
std::vector<std::string> call_lines(const std::vector<std::string>& call) {
    std::vector<std::string> out;
    for (const std::string& l : call) {
        if (starts(l, "RET ")) break;
        out.push_back(l);
    }
    return out;
}

bool read_file(const std::string& path, std::vector<std::uint8_t>& data) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s install-dir script reference\n", argv[0]);
        return 2;
    }
    const std::string root = argv[1];
    std::vector<std::string> models;
    {
        std::ifstream script(argv[2]);
        std::string line, pending;
        while (std::getline(script, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            if (starts(line, "@chdir model/")) pending = line.substr(13);
            else if (starts(line, "_loadax@16")) models.push_back(pending);
        }
    }
    std::vector<std::vector<std::string>> calls;
    {
        std::ifstream reference(argv[3]);
        std::string line;
        bool in_call = false;
        while (std::getline(reference, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            if (starts(line, "CALL _loadax@16")) { calls.emplace_back(); in_call = true; continue; }
            if (starts(line, "CALL ")) { in_call = false; continue; }
            if (in_call) calls.back().push_back(line);
        }
    }
    if (models.size() != calls.size() || models.empty()) {
        std::fprintf(stderr, "script has %zu models, reference %zu loadax calls\n", models.size(), calls.size());
        return 2;
    }

    using namespace cp2;
    int failed_models = 0;
    std::size_t compared = 0;
    for (std::size_t n = 0; n < models.size(); ++n) {
        FILE* trace = std::tmpfile();
        d3d8::TraceSink sink(trace);
        zgp::StateDevice* state = nullptr;
        zgp::PortableAdapter adapter(960, 540, [&](const d3d8::PresentParameters&) {
            state = new zgp::StateDevice();
            return state;
        });
        d3d8::RecordingDirect3D d3d(&adapter, sink);
        d3d8::PresentParameters p;
        p.width = 960; p.height = 540; p.format = d3d8::kFormatX8R8G8B8; p.windowed = 1;
        d3d8::Device* device = nullptr;
        d3d.create_device(0, 1, 1, 0x20, p, device);
        std::fprintf(trace, "BEGIN\n");
        std::vector<std::uint8_t> bytes;
        xfile::Document document;
        std::string error;
        bool ok = read_file(root + "/" + models[n] + "/model.x", bytes) && xfile::parse(bytes.data(), bytes.size(), document, error);
        std::shared_ptr<xfile::Object> mesh;
        std::vector<std::shared_ptr<xfile::Object>> pending = document.objects;
        while (ok && !mesh && !pending.empty()) {
            std::shared_ptr<xfile::Object> o = pending.front();
            pending.erase(pending.begin());
            if (o->type == "Mesh") { mesh = o; break; }
            for (const xfile::Child& c : o->children)
                if (c.object) pending.push_back(c.object);
        }
        {
            const std::string directory = root + "/" + models[n] + "/";
            csmesh::FileReader reader = [&](const std::string& path, std::vector<std::uint8_t>& data) {
                return read_file(directory + path, data);
            };
            csmesh::MeshResources resources;
            ok = ok && mesh && csmesh::load_mesh_resources(*mesh, document, *d3d.device(), reader, resources, error);
            d3d.device()->dump_created_textures();
            d3d.device()->dump_created_buffers();
            std::fprintf(trace, "END\n");  // the mesh's own releases follow
            if (!ok) std::printf("%s: %s\n", models[n].c_str(), error.c_str());
        }
        std::rewind(trace);
        std::vector<std::string> native;
        char buffer[1 << 16];
        std::string text;
        while (std::fgets(buffer, sizeof(buffer), trace)) {
            text += buffer;
            if (!text.empty() && text.back() == '\n') {
                text.pop_back();
                native.push_back(text);
                text.clear();
            }
        }
        std::fclose(trace);
        std::vector<std::string> mine;
        bool begun = false;
        for (const std::string& l : native) {
            if (l == "BEGIN") { begun = true; continue; }
            if (l == "END") break;
            if (begun) mine.push_back(l);
        }
        const std::vector<std::string> theirs = call_lines(calls[n]);
        std::map<std::string, std::string> a_ids, b_ids;
        int mismatches = 0;
        const std::size_t count = std::max(mine.size(), theirs.size());
        for (std::size_t i = 0; i < count; ++i) {
            const std::string a = i < theirs.size() ? renumber(theirs[i], a_ids) : "<missing>";
            const std::string b = i < mine.size() ? renumber(mine[i], b_ids) : "<missing>";
            if (a == b) continue;
            if (mismatches < 3)
                std::printf("%s line %zu\n  original: %.300s\n  native:   %.300s\n", models[n].c_str(), i + 1, a.c_str(), b.c_str());
            ++mismatches;
        }
        compared += count;
        if (mismatches || !ok) ++failed_models;
    }
    std::printf("CSMesh LoadMesh: %zu models, %zu lines compared, %d models differ\n", models.size(), compared, failed_models);
    return failed_models ? 1 : 0;
}
