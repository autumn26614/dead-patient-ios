// Runs a ZGP command script through the native transcription with a recording
// Direct3D 8 device and compares the call trace with one recorded from the
// original x86 zgp1807.hpi (direct/tests/zgp_windows_reference.cpp).
//
//   zgp_trace_test SCRIPT REFERENCE [NATIVE_OUTPUT]
//
// Run it from the original installation directory when the script loads
// files (texload reads paths relative to the working directory, as on
// Windows). After each command, every texture it created is dumped (TEXTURE
// and LEVEL lines with each level's description and texel hash), like the
// Windows harness does, then every vertex and index buffer (BUFFER lines).
//
// Normalization, documented in docs/zgp-renderer.md:
//   - D3DXCreateFontIndirect's internal calls (the D3DX8 font's glyph-cache
//     textures, their dumps and its sprite state blocks) in the original are
//     collapsed to one marker line, in createdevm and in setfont;
//   - object ids are renumbered in order of first appearance (the font's
//     internal objects take ids in the original only);
//   - display-mode sizes and refresh rates (the machine's desktop) are masked;
//   - SetLight color alpha words (4, 8, 12) are uninitialized stack memory in
//     the original and are ignored;
//   - createdevm's returned window handle (a[0]) is ignored;
//   - AddRef and Release counts are masked: the device's count includes the
//     runtime's internal objects, and D3DX meshes release their device
//     reference with the same "Release" line as other objects.
#include "zgp_device.hpp"
#include <cstdio>
#include <cctype>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> read_lines(FILE* file) {
    std::vector<std::string> lines;
    std::rewind(file);
    char buffer[8192];
    while (std::fgets(buffer, sizeof(buffer), file)) {
        std::string line(buffer);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> read_lines(const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return {};
    std::vector<std::string> lines = read_lines(file);
    std::fclose(file);
    return lines;
}

bool starts(const std::string& line, const char* prefix) { return line.compare(0, std::strlen(prefix), prefix) == 0; }

std::string normalize(const std::string& line, const std::string& command) {
    // Device reference counts include the runtime's internal objects.
    if (starts(line, "AddRef -> ")) return "AddRef -> *";
    if (starts(line, "Release -> ")) return "Release -> *";
    // unproje's sixth result is an uninitialized angle in the original.
    if (command == "_unproje@16" && starts(line, "RET ") && line.find('[') != std::string::npos) {
        std::string out = line;
        std::size_t at = out.find('[') + 1;
        for (int field = 0; field < 5 && at != std::string::npos; ++field) at = out.find(',', at) + 1;
        const std::size_t end = out.find_first_of(",]", at);
        if (at != std::string::npos && end != std::string::npos) out.replace(at, end - at, "*");
        return out;
    }
    if (starts(line, "SetLight ")) {
        std::istringstream in(line);
        std::string word, out;
        int field = -2;  // "SetLight", index, then 26 words
        while (in >> word) {
            if (field >= 0 && field < 26) {
                std::string value = word;
                if (!value.empty() && value[0] == '{') value = value.substr(1);
                if (!value.empty() && value.back() == '}') value.pop_back();
                if (field == 4 || field == 8 || field == 12) value = "*";
                word = (field == 0 ? "{" : "") + value + (field == 25 ? "}" : "");
            }
            out += (out.empty() ? "" : " ") + word;
            ++field;
        }
        return out;
    }
    if (starts(line, "GetDisplayMode ") || starts(line, "GetAdapterDisplayMode ")) {
        const std::size_t open = line.find("out {");
        const std::size_t close = line.find('}', open);
        if (open != std::string::npos && close != std::string::npos) {
            const std::string body = line.substr(open + 5, close - open - 5);
            const std::size_t last = body.rfind(' ');
            return line.substr(0, open) + "out {* * * " + body.substr(last + 1) + line.substr(close);
        }
    }
    if (starts(line, "RET ") && command == "_createdevm@16" && line.find('[') != std::string::npos) {
        const std::size_t open = line.find('['), comma = line.find(',', open);
        if (comma != std::string::npos) return line.substr(0, open + 1) + "*" + line.substr(comma);
    }
    return line;
}

std::string command_of(const std::string& line) {
    std::istringstream in(line.substr(5));
    std::string name;
    in >> name;
    return name;
}

// Object ids (#n, n > 0) in order of first appearance. Command lines are
// left alone: file names may contain '#'.
void renumber(std::vector<std::string>& lines) {
    std::map<std::string, std::string> ids;
    for (std::string& line : lines) {
        if (starts(line, "CALL ")) continue;
        std::string out;
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '#' && i + 1 < line.size() && std::isdigit(static_cast<unsigned char>(line[i + 1]))) {
                std::size_t end = i + 1;
                while (end < line.size() && std::isdigit(static_cast<unsigned char>(line[end]))) ++end;
                const std::string id = line.substr(i + 1, end - i - 1);
                std::string mapped = id;
                if (id != "0") {
                    auto it = ids.find(id);
                    if (it == ids.end()) it = ids.emplace(id, std::to_string(ids.size() + 1)).first;
                    mapped = it->second;
                }
                out += "#" + mapped;
                i = end - 1;
            } else {
                out += line[i];
            }
        }
        line = out;
    }
}

std::vector<std::string> normalize_native(const std::vector<std::string>& lines) {
    std::vector<std::string> out;
    std::string command;
    for (const std::string& line : lines) {
        if (starts(line, "CALL ")) command = command_of(line);
        out.push_back(normalize(line, command));
    }
    renumber(out);
    return out;
}

std::vector<std::string> normalize_reference(const std::vector<std::string>& lines) {
    std::vector<std::string> out;
    std::string command;
    bool in_font = false, font_done = false, skipping_dump = false;
    std::vector<std::string> font_textures;
    auto font_texture = [&](const std::string& line) {
        const std::size_t at = line.find('#');
        if (at == std::string::npos) return false;
        const std::string id = line.substr(at + 1, line.find(' ', at) - at - 1);
        for (const std::string& t : font_textures)
            if (t == id) return true;
        return false;
    };
    for (const std::string& line : lines) {
        if (starts(line, "CALL ")) {
            command = command_of(line);
            in_font = false;
            font_done = false;
            skipping_dump = false;
            out.push_back(line);
            continue;
        }
        if (skipping_dump) {
            if (starts(line, "LEVEL ")) continue;
            skipping_dump = false;
        }
        if (starts(line, "TEXTURE ") && font_texture(line)) {
            skipping_dump = true;
            continue;
        }
        // The D3DX font: in createdevm from its first AddRef to SetViewport;
        // in setfont the whole command (release of the old font, then the new).
        const bool font_command = command == "_setfont@16";
        if (!in_font && !font_done &&
            ((command == "_createdevm@16" && starts(line, "AddRef")) ||
             (font_command && !starts(line, "RET ") && !starts(line, "TEXTURE ")))) {
            in_font = true;
            out.push_back("D3DXCreateFontIndirect");
        }
        if (in_font) {
            const bool end = font_command ? (starts(line, "RET ") || starts(line, "TEXTURE "))
                                          : starts(line, "SetViewport ");
            if (!end) {
                if (starts(line, "CreateTexture ")) {
                    const std::size_t at = line.rfind('#');
                    font_textures.push_back(line.substr(at + 1, line.find(' ', at) - at - 1));
                }
                continue;
            }
            in_font = false;
            font_done = true;
            if (starts(line, "TEXTURE ") && font_texture(line)) {
                skipping_dump = true;
                continue;
            }
        }
        out.push_back(normalize(line, command));
    }
    renumber(out);
    return out;
}

// s:<text> with %XX byte escapes, as in the Windows harness.
std::string unescape(const std::string& token) {
    std::string text;
    for (std::size_t i = 2; i < token.size(); ++i) {
        if (token[i] == '%' && i + 2 < token.size()) {
            text += static_cast<char>(std::strtol(token.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            text += token[i];
        }
    }
    return text;
}

bool read_file(const std::string& path, std::vector<unsigned char>& data) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    data.clear();
    unsigned char buffer[65536];
    std::size_t n;
    while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) data.insert(data.end(), buffer, buffer + n);
    std::fclose(file);
    return true;
}

// SPR line in the Windows harness's format.
std::string sprite_text(int n) {
    const cp2::zgp::Renderer::Sprite* s = cp2::zgp::sprite(n);
    const cp2::zgp::Renderer::Sprite empty;
    if (!s) s = &empty;
    std::string out = "SPR " + std::to_string(n);
    char word[16];
    for (int i = 0; i < 38; ++i) {
        std::snprintf(word, sizeof(word), " %08x", s->w[i]);
        out += word;
    }
    return out;
}

// CHR line in the Windows harness's format (zgp_windows_reference.cpp).
std::string character_text(int n) {
    using cp2::d3d8::bits;
    const cp2::zgp::Renderer::Character* c = cp2::zgp::character(n);
    const cp2::zgp::Renderer::Character empty;
    if (!c) c = &empty;
    char text[512];
    std::snprintf(text, sizeof(text),
                  "CHR %d surface %d w f%08x h f%08x diagonal %d frame %d f14 %d uv {f%08x f%08x f%08x f%08x} triangles %u {",
                  n, c->surface, bits(c->width), bits(c->height), c->diagonal, c->frame, c->field14, bits(c->u0),
                  bits(c->v0), bits(c->u1), bits(c->v1), static_cast<unsigned>(c->triangles.size()));
    std::string out = text;
    bool first = true;
    for (const auto& t : c->triangles)
        for (float f : t) {
            std::snprintf(text, sizeof(text), "%sf%08x", first ? "" : " ", bits(f));
            out += text;
            first = false;
        }
    return out + "}";
}

// MESH line in the Windows harness's format (zgp_windows_reference.cpp).
std::string mesh_text(int n) {
    using cp2::d3d8::bits;
    const cp2::zgp::Renderer::Mesh* m = cp2::zgp::mesh(n);
    const cp2::zgp::Renderer::Mesh empty;
    if (!m) m = &empty;
    char text[512];
    std::snprintf(text, sizeof(text),
                  "MESH %d type %u f04 %08x hit {f%08x f%08x f%08x} fvf %u size %u attributes %u vertices %u triangles %u "
                  "ib #%u vb #%u ranges {",
                  n, m->type, m->field04, bits(m->hit[0]), bits(m->hit[1]), bits(m->hit[2]), m->fvf, m->vertex_size,
                  m->attribute_count, m->vertex_count, m->triangle_count, m->indices ? m->indices->trace_id : 0u,
                  m->vertices ? m->vertices->trace_id : 0u);
    std::string out = text;
    bool first = true;
    for (const auto& range : m->ranges)
        for (std::uint32_t v : range) {
            out += (first ? "" : " ") + std::to_string(v);
            first = false;
        }
    out += "} materials {";
    first = true;
    for (const auto& material : m->materials)
        for (float f : material.m) {
            std::snprintf(text, sizeof(text), "%sf%08x", first ? "" : " ", bits(f));
            out += text;
            first = false;
        }
    out += "} textures {";
    first = true;
    for (std::int32_t t : m->textures) {
        out += (first ? "" : " ") + (t < 0 ? std::string("null") : "s" + std::to_string(t));
        first = false;
    }
    return out + "}";
}

// Bytes of a string as text; bytes outside printable ASCII (and '%') as %XX,
// as the Windows harness prints them.
std::string escaped(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        char hex[4];
        if (c > 0x20 && c < 0x7f && c != '%') out += static_cast<char>(c);
        else { std::snprintf(hex, sizeof(hex), "%%%02X", c); out += hex; }
    }
    return out;
}

// MESHSURF line in the Windows harness's format: the mesh's own surfaces.
// Fields the original leaves uninitialized until a load are printed only
// after one (a non-zero name length).
std::string mesh_surfaces_text(int n) {
    const cp2::zgp::Renderer::Mesh* m = cp2::zgp::mesh(n);
    std::string out = "MESHSURF " + std::to_string(n);
    if (!m) return out;
    for (const cp2::zgp::Renderer::Surface& s : m->own_surfaces) {
        char text[96];
        const unsigned texture = s.texture ? s.texture->trace_id : 0u;
        if (!s.name_length) {
            std::snprintf(text, sizeof(text), " {#%u format %u unloaded}", texture, s.format);
            out += text;
            continue;
        }
        std::snprintf(text, sizeof(text), " {#%u %08x %u %u %u ", texture, s.color_key, s.format, s.mip_levels,
                      s.name_length);
        out += text + escaped(s.path) + " " + escaped(s.name) + "}";
    }
    return out;
}

// OBJ line in the Windows harness's format.
std::string object_text(int n) {
    // Entry 0 exists in the original's table (objlate writes it) even though
    // most commands reject index 0, so the table is read directly.
    const std::vector<cp2::zgp::Renderer::Object>& objects = cp2::zgp::renderer().objects;
    const cp2::zgp::Renderer::Object empty;
    const cp2::zgp::Renderer::Object* o =
        n >= 0 && static_cast<std::size_t>(n) < objects.size() ? &objects[static_cast<std::size_t>(n)] : &empty;
    std::string out = "OBJ " + std::to_string(n);
    char word[16];
    for (int i = 0; i < 75; ++i) {
        std::snprintf(word, sizeof(word), " %08x", o->w[i]);
        out += word;
    }
    return out;
}

// MEM line for the globals the native state models.
bool memory_text(const std::string& va, int count, std::string& out) {
    using cp2::d3d8::bits;
    char word[16];
    out = "MEM " + va;
    if (va == "1005f900" && count >= 0 && count <= 3) {  // meshsz
        for (int i = 0; i < count; ++i) {
            std::snprintf(word, sizeof(word), " %08x", bits(cp2::zgp::renderer().mesh_size[i]));
            out += word;
        }
        return true;
    }
    if (va == "1005fee0" && count >= 0 && count <= 12) {  // camera: up, eye, direction, angles
        const cp2::zgp::Camera& c = cp2::zgp::state().camera;
        const float* fields[4] = {c.up, c.eye, c.direction, c.angles};
        for (int i = 0; i < count; ++i) {
            std::snprintf(word, sizeof(word), " %08x", bits(fields[i / 3][i % 3]));
            out += word;
        }
        return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s script reference [native-output]\n", argv[0]);
        return 2;
    }
    using namespace cp2;
    FILE* trace = std::tmpfile();
    if (!trace) return 2;
    d3d8::TraceSink sink(trace);
    std::vector<std::unique_ptr<zgp::StateDevice>> devices;
    zgp::PortableAdapter adapter(960, 540, [&](const d3d8::PresentParameters&) {
        devices.emplace_back(new zgp::StateDevice());
        return devices.back().get();
    });
    std::unique_ptr<d3d8::RecordingDirect3D> d3d;
    const std::uint32_t window = 0x80000000u;
    std::int32_t position_x = 0, position_y = 0;  // the registered screen's cx, cy
    zgp::renderer().read_file = read_file;

    std::fprintf(trace, "ZGP-REFERENCE 1\n");
    std::ifstream script(argv[1]);
    std::string text;
    while (std::getline(script, text)) {
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) text.pop_back();
        if (text.empty() || text[0] == '#') continue;
        std::istringstream fields(text);
        std::string name, token;
        fields >> name;
        if (name == "@pos") {
            fields >> position_x >> position_y;
            continue;
        }
        if (name == "@spr") {
            int n = 0;
            fields >> n;
            std::fprintf(trace, "%s\n", sprite_text(n).c_str());
            continue;
        }
        if (name == "@chdir") {  // the game's chdir before loadax
            std::string path;
            std::getline(fields >> std::ws, path);
            if (chdir(path.c_str()) != 0) std::fprintf(trace, "CHDIR %s failed\n", path.c_str());
            continue;
        }
        if (name == "@ani") {  // the selected library's model n: time, rate, hit size
            int n = 0;
            fields >> n;
            zgp::Renderer& zr = zgp::renderer();
            csmesh::Slot* slot = zr.animation_libraries[zr.animation_selected & 3].library.slot(n);
            std::fprintf(trace, "ANI %d", n);
            if (slot) {
                const float values[5] = {slot->time, slot->rate, slot->hit[0], slot->hit[1], slot->hit[2]};
                for (float v : values) std::fprintf(trace, " %08x", d3d8::bits(v));
            }
            std::fprintf(trace, "\n");
            continue;
        }
        if (name == "@surf") {  // a surface's texture levels
            int n = 0;
            fields >> n;
            zgp::Renderer& r = zgp::renderer();
            if (n >= 0 && static_cast<std::size_t>(n) < r.surfaces.size() && r.surfaces[static_cast<std::size_t>(n)].texture) {
                if (d3d && d3d->device()) d3d->device()->dump_texture(*r.surfaces[static_cast<std::size_t>(n)].texture);
            } else
                std::fprintf(trace, "SURFACE %d no texture\n", n);
            continue;
        }
        if (name == "@obj") {
            int n = 0;
            fields >> n;
            std::fprintf(trace, "%s\n", object_text(n).c_str());
            continue;
        }
        if (name == "@mesh") {
            int n = 0;
            fields >> n;
            std::fprintf(trace, "%s\n", mesh_text(n).c_str());
            continue;
        }
        if (name == "@meshvb") {  // a mesh's vertex buffer contents
            int n = 0;
            fields >> n;
            const zgp::Renderer::Mesh* m = zgp::mesh(n);
            unsigned char* data = nullptr;
            if (!m || !m->vertices || d3d8::failed(m->vertices->lock(data, 0x10))) {
                std::fprintf(trace, "MESHVB %d none\n", n);
                continue;
            }
            std::uint32_t d[6] = {};
            m->vertices->desc(d);
            std::vector<unsigned char> copy(data, data + d[4]);
            m->vertices->unlock();
            std::uint64_t hash = 1469598103934665603ull;
            for (unsigned char c : copy) hash = (hash ^ c) * 1099511628211ull;
            std::fprintf(trace, "MESHVB %d #%u data %zu:%016llx", n, m->vertices->trace_id, copy.size(),
                         static_cast<unsigned long long>(hash));
            if (copy.size() <= 4096) {
                std::fprintf(trace, " {");
                for (std::size_t i = 0; i + 4 <= copy.size(); i += 4) {
                    std::uint32_t w;
                    std::memcpy(&w, copy.data() + i, 4);
                    std::fprintf(trace, "%s%08x", i ? " " : "", w);
                }
                std::fprintf(trace, "}");
            }
            std::fprintf(trace, "\n");
            continue;
        }
        if (name == "@meshsurf") {
            int n = 0;
            fields >> n;
            std::fprintf(trace, "%s\n", mesh_surfaces_text(n).c_str());
            continue;
        }
        if (name == "@mem") {
            std::string va, line;
            int n = 0;
            fields >> va >> n;
            if (!memory_text(va, n, line)) {
                std::fprintf(stderr, "unsupported @mem %s %d\n", va.c_str(), n);
                return 2;
            }
            std::fprintf(trace, "%s\n", line.c_str());
            continue;
        }
        if (name == "@chr") {
            int n = 0;
            fields >> n;
            std::fprintf(trace, "%s\n", character_text(n).c_str());
            continue;
        }
        std::vector<std::int32_t> ints, array;
        std::vector<std::string> strings;
        bool has_array = false;
        while (fields >> token) {
            if (token.compare(0, 2, "i:") == 0) ints.push_back(std::atoi(token.c_str() + 2));
            else if (token.compare(0, 2, "s:") == 0) { strings.push_back(unescape(token)); ints.push_back(0); }
            else if (token.compare(0, 2, "v:") == 0) {
                has_array = true;
                ints.push_back(0);
                std::stringstream list(token.substr(2));
                std::string item;
                while (std::getline(list, item, ',')) array.push_back(std::atoi(item.c_str()));
            }
        }
        ints.resize(4, 0);
        strings.resize(1);
        const std::size_t given = array.size();
        array.resize(given < 11 ? 11 : given, 0);
        std::fprintf(trace, "CALL %s\n", text.c_str());
        std::fflush(trace);
        int result = 0;
        const char* failure = "";
        if (name == "_createdx@16") {
            d3d.reset(new d3d8::RecordingDirect3D(&adapter, sink));
            result = zgp::createdx(d3d.get());
        } else if (name == "_setaspect@16") result = zgp::setaspect(ints[0]);
        else if (name == "_setmaxsurf@16") result = zgp::setmaxsurf(ints[0], ints[1]);
        else if (name == "_setmaxunit@16") result = zgp::setmaxunit(ints[0], ints[1], ints[2], ints[3]);
        else if (name == "_setbmscr@16") result = zgp::setbmscr(window);
        else if (name == "_createdevm@16") result = zgp::createdevm(array.data(), failure);
        else if (name == "_set3d@16") result = zgp::set3d(ints[0], ints[1], ints[2], ints[3]);
        else if (name == "_setmaxangle@16") result = zgp::setmaxangle(ints[0], ints[1]);
        else if (name == "_cat@16") result = zgp::cat(ints[0], ints[1], ints[2]);
        else if (name == "_unproje@16") result = zgp::unproje(array.data(), given, ints[1], ints[2]);
        else if (name == "_objinst@16") result = zgp::objinst(array.data(), given, ints[1], ints[2], ints[3]);
        else if (name == "_setfovy@16") result = zgp::setfovy(ints[0]);
        else if (name == "_setview@16") result = zgp::setview(ints[0], ints[1], ints[2], ints[3]);
        else if (name == "_lightdir@16") result = zgp::lightdir(ints[0], ints[1], ints[2]);
        else if (name == "_lightamb@16") result = zgp::lightamb(ints[0]);
        else if (name == "_textrans@16") result = zgp::textrans(ints[0]);
        else if (name == "_texload@16") result = zgp::texload(ints[0], strings[0], ints[2]);
        else if (name == "_texture@16") result = zgp::texture(ints[0], ints[1], ints[2], ints[3]);
        else if (name == "_texinfoex@16") result = zgp::texinfoex(array.data(), ints[1]);
        else if (name == "_meshmemset@16") result = zgp::meshmemset(array.data(), given, ints[1]);
        else if (name == "_meshboard@16") result = zgp::meshboard(ints[0], ints[1]);
        else if (name == "_objang@16") result = zgp::objang(ints[0], ints[1], ints[2], ints[3]);
        else if (name == "_setfont@16") result = zgp::setfont(ints[0], strings[0], ints[2]);
        else if (name == "_zclear@16") result = zgp::zclear();
        else if (name == "_bscene@16") result = zgp::bscene(ints[0], ints[1]);
        else if (name == "_escene@16") result = zgp::escene(ints[0], ints[1], ints[2], ints[3]);
        else if (name == "_fill2d@16") result = zgp::fill2d(ints[0], ints[1], ints[2], ints[3]);
        else if (name == "_line2d@16") result = zgp::line2d(ints[0], ints[1], ints[2], ints[3]);
        else if (name == "_setcolor4@16") result = zgp::setcolor4(ints[0], ints[1], ints[2], ints[3]);
        else if (name == "_setblend@16") result = zgp::setblend(ints[0]);
        else if (name == "_set2d@16") result = zgp::set2d(ints[0]);
        else if (name == "_sprset@16") result = zgp::sprset(ints[0], ints[1], ints[2], ints[3], position_x, position_y);
        else if (name == "_sprflag@16") result = zgp::sprflag(ints[0], ints[1]);
        else if (name == "_sprpos@16") result = zgp::sprpos(ints[0], ints[1], ints[2], ints[3]);
        else if (name == "_sprzoom@16") result = zgp::sprzoom(ints[0], ints[1], ints[2]);
        else if (name == "_sprang@16") result = zgp::sprang(ints[0], ints[1], ints[2]);
        else if (name == "_sprlate@16") result = zgp::sprlate(ints[0], ints[1], ints[2], ints[3]);
        else if (name == "_sprblend@16") result = zgp::sprblend(ints[0], ints[1]);
        else if (name == "_sprdel@16") result = zgp::sprdel(ints[0]);
        else if (name == "_sprdraw@16") result = zgp::sprdraw(ints[0], ints[1], ints[2]);
        else if (name == "_meshsz@16") result = zgp::meshsz(ints[0], ints[1], ints[2]);
        else if (name == "_meshbox@16") result = zgp::meshbox(ints[0], ints[1], ints[2], ints[3]);
        else if (name == "_loadx@16") result = zgp::loadx(ints[0], strings[0], ints[2]);
        else if (name == "_anilibload@16") {
            // The harness registers the original plugin/CSMesh.dll; the port's
            // native backend stands in for it (anilibload checks the file on iOS).
            result = ints[0] >= 0 && ints[0] < 4 ? zgp::register_animation_library(ints[0], strings[0], ints[2]) : -5;
        }
        else if (name == "_loadax@16") result = zgp::loadax(ints[0], strings[0], ints[2]);
        else if (name == "_anirate@16") result = zgp::anirate(ints[0], ints[1]);
        else if (name == "_objameshset@16") result = zgp::objameshset(ints[0], ints[1]);
        else if (name == "_objmeshset@16") result = zgp::objmeshset(ints[0], ints[1]);
        else if (name == "_objpos@16") result = zgp::objpos(ints[0], ints[1], ints[2], ints[3]);
        else if (name == "_objlate@16") result = zgp::objlate(ints[0], ints[1], ints[2], ints[3]);
        else if (name == "_objblend@16") result = zgp::objblend(ints[0], ints[1]);
        else if (name == "_objdel@16") result = zgp::objdel(ints[0]);
        else if (name == "_objtype@16") result = zgp::objtype(ints[0], ints[1]);
        else if (name == "_objclno@16") result = zgp::objclno(ints[0], ints[1]);
        else if (name == "_objcat@16") result = zgp::objcat(ints[0], ints[1], ints[2], ints[3]);
        else if (name == "_clink@16") result = zgp::clink(ints[0], ints[1]);
        else if (name == "_objdraw@16") result = zgp::objdraw(ints[0], ints[1], ints[2]);
        else if (name == "_z_mat@16") result = zgp::z_mat(array.data(), given, ints[1], ints[2], ints[3]);
        else if (name == "_meshzoom@16") result = zgp::meshzoom(ints[0], ints[1], ints[2], ints[3]);
        else if (name == "_meshmaterial@16") result = zgp::meshmaterial(array.data(), given, ints[1], ints[2]);
        else if (name == "_pos3@16") result = zgp::pos3(ints[0], ints[1], ints[2]);
        else if (name == "_line3d@16") result = zgp::line3d(ints[0], ints[1], ints[2]);
        else if (name == "_proje@16") result = zgp::proje(array.data(), given, ints[1], ints[2], ints[3]);
        else if (name == "_getfrt@16") result = zgp::getfrt(array.data(), given);
        else if (name == "_cursor@16") result = zgp::cursor(ints[0]);
        else if (name == "_texwrite24@16")
            result = zgp::texwrite24(reinterpret_cast<const unsigned char*>(array.data()), given * 4, ints[1], ints[2], ints[3]);
        else if (name == "_objflag@16") result = zgp::objflag(ints[0], ints[1]);
        else if (name == "_objrot@16") result = zgp::objrot(ints[0], ints[1], ints[2], ints[3]);
        else if (name == "_objzoom@16") result = zgp::objzoom(ints[0], ints[1], ints[2], ints[3]);
        else if (name == "_objemi@16") result = zgp::objemi(ints[0], ints[1], ints[2], ints[3]);
        else if (name == "_objplay@16") result = zgp::objplay(ints[0], ints[1], ints[2]);
        else if (name == "_objpause@16") result = zgp::objpause(ints[0]);
        else if (name == "_anilist@16") result = zgp::anilist(ints[0], ints[1]);
        else if (name == "_getobjang@16") result = zgp::getobjang(array.data(), given, ints[1]);
        else if (name == "_getobjsize@16") result = zgp::getobjsize(array.data(), given, ints[1]);
        else if (name == "_getanitime@16") result = zgp::getanitime(array.data(), given, ints[1]);
        else if (name == "_objfreeno@16") result = zgp::objfreeno(array.data(), given, ints[1], ints[2], ints[3]);
        else if (name == "_objhittype@16") result = zgp::objhittype(ints[0], ints[1]);
        else if (name == "_chrset@16") result = zgp::chrset(ints[0], ints[1], ints[2], ints[3], position_x, position_y);
        else { std::fprintf(stderr, "unsupported command %s\n", name.c_str()); return 2; }
        if (d3d && d3d->device()) {
            d3d->device()->dump_created_textures();
            d3d->device()->dump_created_buffers();
        }
        std::fflush(trace);
        std::fprintf(trace, "RET %d", result);
        if (has_array) {
            std::fprintf(trace, " [");
            for (std::size_t i = 0; i < given; ++i)
                std::fprintf(trace, "%s%d", i ? "," : "", static_cast<int>(array[i]));
            std::fprintf(trace, "]");
        }
        std::fprintf(trace, "\n");
        if (*failure) std::fprintf(stderr, "%s: %s\n", name.c_str(), failure);
    }
    std::fprintf(trace, "END\n");

    const std::vector<std::string> native = normalize_native(read_lines(trace));
    // Models hold device objects: release them while the devices still exist
    // (after the trace was read; the releases are not part of the comparison).
    for (auto& slot : zgp::renderer().animation_libraries) slot.library.destroy();
    if (argc >= 4) {
        FILE* out = std::fopen(argv[3], "wb");
        for (const std::string& line : native) if (out) std::fprintf(out, "%s\n", line.c_str());
        if (out) std::fclose(out);
    }
    const std::vector<std::string> reference = normalize_reference(read_lines(argv[2]));
    const std::size_t count = native.size() > reference.size() ? native.size() : reference.size();
    int mismatches = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const std::string a = i < reference.size() ? reference[i] : "<missing>";
        const std::string b = i < native.size() ? native[i] : "<missing>";
        if (a == b) continue;
        if (mismatches < 10) std::printf("line %zu\n  original: %s\n  native:   %s\n", i + 1, a.c_str(), b.c_str());
        ++mismatches;
    }
    std::printf("ZGP trace %s: %zu calls compared, %d mismatches\n", argv[1], reference.size(), mismatches);
    return mismatches ? 1 : 0;
}
