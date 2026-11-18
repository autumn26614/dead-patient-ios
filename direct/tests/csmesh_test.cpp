// Tests for the native CSMesh backend.
//
//   csmesh_test                          self-contained parser/semantics tests
//   csmesh_test MODEL_ROOT REFERENCE     replays a reference recorded from the
//                                        original x86 CSMesh.dll by
//                                        csmesh_windows_reference.cpp and
//                                        compares every value bit for bit
#include "csmesh.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::printf("FAIL line %d: %s\n", line, expression);
        ++failures;
    }
}
#define CHECK(x) check((x), #x, __LINE__)

bool near(float a, float b, float tolerance = 1e-5f) { return std::fabs(a - b) <= tolerance; }

// Generated with Python zlib: two MSZIP blocks, the second using the first as dictionary.
const unsigned char kCompressed[] = {
    120, 111, 102, 32, 48, 51, 48, 51, 98, 122, 105, 112, 48, 48, 51, 50, 0, 1, 0, 0, 120, 0, 67, 0,
    67, 75, 99, 100, 96, 101, 96, 96, 112, 43, 74, 204, 77, 101, 100, 96, 1, 50, 131, 242, 243, 75, 184, 24,
    24, 25, 68, 96, 194, 33, 69, 137, 121, 197, 105, 249, 69, 185, 190, 137, 37, 69, 153, 21, 92, 12, 236, 12,
    2, 12, 32, 208, 96, 207, 128, 1, 240, 137, 29, 0, 209, 14, 64, 4, 196, 13, 0, 120, 0, 63, 0, 67,
    75, 179, 23, 1, 218, 192, 205, 0, 177, 211, 55, 181, 56, 131, 145, 129, 25, 200, 114, 202, 7, 90, 192, 6,
    20, 102, 0, 115, 217, 25, 56, 177, 27, 7, 51, 102, 63, 200, 92, 32, 6, 26, 7, 209, 5, 194, 108, 96,
    67, 153, 161, 234, 65, 34, 76, 64, 12, 178, 142, 155, 1, 0,
};

const unsigned char kBinary[] = {
    120, 111, 102, 32, 48, 51, 48, 51, 98, 105, 110, 32, 48, 48, 51, 50, 1, 0, 5, 0, 0, 0, 70, 114,
    97, 109, 101, 1, 0, 4, 0, 0, 0, 82, 111, 111, 116, 10, 0, 1, 0, 20, 0, 0, 0, 70, 114, 97,
    109, 101, 84, 114, 97, 110, 115, 102, 111, 114, 109, 77, 97, 116, 114, 105, 120, 10, 0, 7, 0, 16, 0, 0,
    0, 0, 0, 128, 63, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128,
    63, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 63, 0, 0, 0,
    0, 0, 0, 192, 63, 0, 0, 0, 64, 0, 0, 64, 64, 0, 0, 128, 63, 20, 0, 20, 0, 11, 0, 1,
    0, 4, 0, 0, 0, 77, 101, 115, 104, 1, 0, 3, 0, 0, 0, 66, 111, 120, 10, 0, 6, 0, 1, 0,
    0, 0, 3, 0, 0, 0, 7, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 128, 63, 0, 0, 0, 64, 0, 0, 64, 64, 0, 0, 128, 191, 0, 0, 0, 192, 0, 0, 0, 63,
    20, 0, 6, 0, 1, 0, 0, 0, 1, 0, 0, 0, 6, 0, 4, 0, 0, 0, 3, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 20, 0, 11, 0, 11, 0,
};

const char kText[] =
    "xof 0303txt 0032\n"
    "template Frame { <3d82ab46-62da-11cf-ab39-0020af71e433> [...] }\n"
    "// comment\n"
    "Frame Root {\n"
    " FrameTransformMatrix { 1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0, 0.0,0.0,1.0,0.0, 1.5,2.0,3.0,1.0;; }\n"
    " Mesh Box { 3; 0.0;0.0;0.0;, 1.0;2.0;3.0;, -1.0;-2.0;0.5;; 1; 3;0,1,2;; }\n"
    "}\n";

void check_box_document(const cp2::xfile::Document& document) {
    CHECK(document.objects.size() == 1);
    if (document.objects.size() != 1) return;
    const cp2::xfile::Object& root = *document.objects[0];
    CHECK(root.type == "Frame" && root.has_name && root.name == "Root");
    CHECK(root.children.size() == 2);
    if (root.children.size() != 2) return;
    const cp2::xfile::Object& matrix = *root.children[0].object;
    CHECK(matrix.type == "FrameTransformMatrix" && matrix.values.size() == 16);
    CHECK(matrix.values.size() == 16 && matrix.values[12].as_float() == 1.5f && matrix.values[14].as_float() == 3.0f);
    const cp2::xfile::Object& mesh = *root.children[1].object;
    CHECK(mesh.type == "Mesh" && mesh.name == "Box");
    CHECK(mesh.values.size() == 1 + 9 + 1 + 4);
    CHECK(mesh.values.size() > 9 && mesh.values[0].word == 3 && mesh.values[9].as_float() == 0.5f);
}

void test_encodings() {
    std::string error;
    cp2::xfile::Document text, binary, compressed;
    CHECK(cp2::xfile::parse(reinterpret_cast<const std::uint8_t*>(kText), sizeof(kText) - 1, text, error));
    CHECK(text.format == "txt ");
    check_box_document(text);
    CHECK(cp2::xfile::parse(kBinary, sizeof(kBinary), binary, error));
    CHECK(binary.format == "bin ");
    check_box_document(binary);
    CHECK(cp2::xfile::parse(kCompressed, sizeof(kCompressed), compressed, error));
    CHECK(compressed.format == "bzip");
    check_box_document(compressed);

    // A corrupted MSZIP signature and a truncated stream are rejected.
    std::vector<std::uint8_t> bad(kCompressed, kCompressed + sizeof(kCompressed));
    bad[24] = 'X';
    CHECK(!cp2::xfile::parse(bad.data(), bad.size(), compressed, error));
    bad.assign(kCompressed, kCompressed + sizeof(kCompressed) - 20);
    CHECK(!cp2::xfile::parse(bad.data(), bad.size(), compressed, error));
}

const char kAnimated[] =
    "xof 0303txt 0032\n"
    "Frame Root {\n"
    " FrameTransformMatrix { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1;; }\n"
    " Frame A { FrameTransformMatrix { 1,0,0,0, 0,1,0,0, 0,0,1,0, 5,6,7,1;; }\n"
    "   Mesh MA { 2; 1.0;0.0;0.0;, -1.0;2.0;0.0;; 0;; } }\n"
    " Frame B { FrameTransformMatrix { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1;; }\n"
    "   Mesh MB { 1; 0.0;0.0;0.0;; 0;; } }\n"
    " Frame C { FrameTransformMatrix { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1;; }\n"
    "   Mesh MC { 1; 0.0;0.0;0.0;; 0;; } }\n"
    "}\n"
    "AnimationSet Walk {\n"
    " Animation { {A} AnimationKey { 2; 3; 0;3;0.0,0.0,0.0;;, 10;3;10.0,0.0,0.0;;, 20;3;20.0,0.0,0.0;;; } }\n"
    " Animation { {B} AnimationKey { 0; 2; 0;4;1.0,0.0,0.0,0.0;;, 8;4;0.0,0.0,0.0,1.0;;; } }\n"
    "}\n"
    "AnimationSet Idle {\n"
    " Animation { {C} AnimationKey { 4; 1; 0;16;1,0,0,0, 0,1,0,0, 0,0,1,0, 9,9,9,1;;; } }\n"
    "}\n";

void test_semantics() {
    cp2::csmesh::Library library;
    int device = 0;
    CHECK(library.init(&device, 0) == 1 && library.capacity() == 256);
    CHECK(library.init(&device, 300) == 1 && library.capacity() == 256);
    CHECK(library.init(&device, 10) == 1 && library.capacity() == 10 && library.device_reference() == &device);
    CHECK(library.slot(10) == nullptr && library.slot(-1) == nullptr);

    std::string error;
    CHECK(library.load(3, "model.x", reinterpret_cast<const std::uint8_t*>(kAnimated), sizeof(kAnimated) - 1, error) == 1);
    int lists = 0, meshes = 0;
    CHECK(library.get_num_list(3, &lists) == 1 && lists == 2);
    char names[3 * 256];
    CHECK(library.get_mesh_names(3, names, &meshes) == 1 && meshes == 3);
    // AddFrame inserts later children second: Root children are A, C, B.
    CHECK(std::strcmp(names, "MA") == 0 && std::strcmp(names + 256, "MC") == 0 && std::strcmp(names + 512, "MB") == 0);
    char list_names[2 * 256];
    CHECK(library.get_list_names(3, list_names) == 1 && std::strcmp(list_names + 256, "AnimationSet1") == 0);

    int keys = 0;
    float time = 0, maximum = 0;
    CHECK(library.get_num_key(3, &keys) == 1 && keys == 3);
    CHECK(library.get_key_time(3, &time, 1) == 1 && time == 10.0f);
    CHECK(library.get_key_time(3, &time, -4) == 1 && time == 0.0f);
    CHECK(library.get_key_time(3, &time, 99) == 1 && time == 20.0f);
    CHECK(library.get_max_time(3, &maximum) == 1 && maximum == 20.0f);

    float matrix[16];
    CHECK(library.set_time(3, 5.0f) == 1 && library.render(3, nullptr, nullptr) == 1);
    CHECK(library.get_mesh_matrix(3, matrix, "MA", 1) == 1 && matrix[12] == 5.0f && matrix[13] == 0.0f);
    CHECK(library.get_mesh_matrix(3, matrix, "MA", 0) == 1 && matrix[12] == 5.0f && matrix[13] == 6.0f);
    library.set_time(3, 25.0f);  // fmod(25, 21) = 4
    library.render(3, nullptr, nullptr);
    CHECK(library.get_mesh_matrix(3, matrix, "MA", 1) == 1 && matrix[12] == 4.0f);
    library.set_time(3, 20.5f);  // past the last key: interpolates back toward key 0
    library.render(3, nullptr, nullptr);
    CHECK(library.get_mesh_matrix(3, matrix, "MA", 1) == 1 && matrix[12] == 10.0f);
    library.set_time(3, 4.0f);   // halfway between identity and a half turn about z
    library.render(3, nullptr, nullptr);
    CHECK(library.get_mesh_matrix(3, matrix, "MB", 1) == 1);
    CHECK(near(matrix[0], 0.0f) && near(matrix[1], -1.0f) && near(matrix[4], 1.0f) && near(matrix[10], 1.0f));
    float box[6];
    CHECK(library.get_mesh_minmax(3, box, "MA", 0) == 1);
    CHECK(box[0] == 4.0f && box[1] == 6.0f && box[3] == 6.0f && box[4] == 8.0f);

    // Animation state persists per frame across list switches.
    CHECK(library.set_list(3, 1) == 1);
    CHECK(library.get_num_key(3, &keys) == 1 && keys == 1);
    CHECK(library.get_max_time(3, &maximum) == 1 && maximum == 0.0f);
    library.set_time(3, 0.0f);
    library.render(3, nullptr, nullptr);
    CHECK(library.get_mesh_matrix(3, matrix, "MC", 1) == 1 && matrix[12] == 9.0f);
    CHECK(library.get_mesh_matrix(3, matrix, "MA", 1) == 1 && matrix[12] == 4.0f);  // last sampled at t=4

    // A list without animations reports no keys.
    CHECK(library.set_list(3, 7) == 1 && library.get_num_key(3, &keys) == 1 && keys == 0);
    CHECK(library.get_max_time(3, &maximum) == 1 && maximum == 0.0f);

    // An unresolved reference is skipped by d3dxof/CSMesh; the animation simply has no target.
    std::string unresolved(kAnimated);
    unresolved.replace(unresolved.find("{C}"), 3, "{Z}");
    CHECK(library.load(3, "model.x", reinterpret_cast<const std::uint8_t*>(unresolved.data()), unresolved.size(), error) == 1);
    library.set_list(3, 1);
    CHECK(library.set_time(3, 0.0f) == 1);

    // Two key tracks of one type in an Animation fail the whole load and leave the slot empty.
    std::string duplicate(kAnimated);
    const std::size_t idle = duplicate.find("AnimationKey { 4;");
    duplicate.insert(idle, "AnimationKey { 4; 1; 0;16;1,0,0,0, 0,1,0,0, 0,0,1,0, 1,1,1,1;;; } ");
    CHECK(library.load(3, "model.x", reinterpret_cast<const std::uint8_t*>(duplicate.data()), duplicate.size(), error) == 0);
    CHECK(library.model(3) == nullptr);
    int none = -1;
    CHECK(library.get_num_list(3, &none) == 0 && none == -1);
}

// ---------------------------------------------------------------------------
// Reference replay.

std::uint32_t parse_hex(const std::string& text) { return static_cast<std::uint32_t>(std::strtoul(text.c_str(), nullptr, 16)); }

std::uint32_t bits(float value) {
    std::uint32_t result;
    std::memcpy(&result, &value, 4);
    return result;
}

float from_bits(std::uint32_t value) {
    float result;
    std::memcpy(&result, &value, 4);
    return result;
}

std::string unhex(const std::string& text) {
    if (text == "-") return std::string();
    std::string result;
    for (std::size_t i = 0; i + 1 < text.size(); i += 2)
        result.push_back(static_cast<char>(std::strtoul(text.substr(i, 2).c_str(), nullptr, 16)));
    return result;
}

std::int64_t ulp_distance(std::uint32_t a, std::uint32_t b) {
    auto ordered = [](std::uint32_t v) -> std::int64_t {
        return (v & 0x80000000u) ? -static_cast<std::int64_t>(v & 0x7fffffffu) : static_cast<std::int64_t>(v);
    };
    const std::int64_t d = ordered(a) - ordered(b);
    return d < 0 ? -d : d;
}

struct Stats {
    std::map<std::string, std::uint64_t> compared, exact;
    std::map<std::string, std::int64_t> worst;
    std::map<std::string, std::string> worst_where;
    std::uint64_t structural_failures = 0;

    void value(const std::string& category, std::uint32_t expected, float actual, const std::string& where) {
        ++compared[category];
        const std::uint32_t got = bits(actual);
        if (got == expected) {
            ++exact[category];
            return;
        }
        const std::int64_t distance = ulp_distance(expected, got);
        if (distance > worst[category]) {
            worst[category] = distance;
            char text[160];
            std::snprintf(text, sizeof(text), "%s expected %.9g got %.9g", where.c_str(), from_bits(expected), actual);
            worst_where[category] = text;
        }
    }

    void structural(bool ok, const std::string& what) {
        if (ok) return;
        if (structural_failures < 20) std::printf("MISMATCH %s\n", what.c_str());
        ++structural_failures;
    }
};

// Same traversal as the harness: root, then every non-animation frame in
// child-then-sibling pre-order, hashing the local and combined matrix bytes.
void digest_frames(const cp2::csmesh::Model& model, int frame, unsigned& count, std::uint64_t& hash,
                   std::vector<float>& values) {
    for (; frame >= 0; frame = model.frames[frame].sibling) {
        const cp2::csmesh::Frame& f = model.frames[frame];
        if (!f.animation) {
            for (const cp2::csmesh::Matrix* matrix : {&f.rot, &f.combined}) {
                const unsigned char* bytes = reinterpret_cast<const unsigned char*>(matrix->m);
                for (int i = 0; i < 64; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
                values.insert(values.end(), matrix->m, matrix->m + 16);
            }
            ++count;
        }
        digest_frames(model, f.child, count, hash, values);
        if (frame == 0) break;
    }
}

bool read_file(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

int replay(const std::string& root, const std::string& reference, std::int64_t tolerance) {
    std::ifstream in(reference);
    if (!in) { std::printf("cannot open %s\n", reference.c_str()); return 2; }
    cp2::csmesh::Library library;
    library.init(nullptr, 0);
    const int slot = 1;
    Stats stats;
    std::string line, model;
    std::vector<std::string> mesh_names;
    int models = 0, lists_checked = 0;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string tag;
        fields >> tag;
        if (tag == "MODEL") {
            const std::size_t last = line.rfind(' ');
            model = line.substr(6, last - 6);
            const int expected = std::atoi(line.substr(last + 1).c_str());
            std::vector<std::uint8_t> bytes;
            std::string error;
            const bool present = read_file(root + "/" + model + "/model.x", bytes);
            const int loaded = library.load(slot, "model.x", present ? bytes.data() : nullptr, bytes.size(), error);
            stats.structural(loaded == expected, model + " load " + error);
            mesh_names.clear();
            ++models;
        } else if (tag == "COUNTS") {
            int lists, meshes, actual_lists = -1, actual_meshes = -1;
            fields >> lists >> meshes;
            library.get_num_list(slot, &actual_lists);
            library.get_mesh_names(slot, nullptr, &actual_meshes);
            stats.structural(lists == actual_lists && meshes == actual_meshes, model + " counts");
            std::vector<char> names(static_cast<std::size_t>(std::max(actual_meshes, 1)) * 256);
            library.get_mesh_names(slot, names.data(), nullptr);
            for (int i = 0; i < actual_meshes; ++i) mesh_names.push_back(names.data() + i * 256);
        } else if (tag == "MESH") {
            int index;
            std::string hex;
            fields >> index >> hex;
            stats.structural(index < static_cast<int>(mesh_names.size()) && mesh_names[index] == unhex(hex),
                             model + " mesh name " + std::to_string(index));
        } else if (tag == "ALL" || tag == "STATIC" || tag == "POSE") {
            int index = -1;
            if (tag != "ALL") fields >> index;
            std::vector<std::uint32_t> expected;
            std::string word;
            while (fields >> word) expected.push_back(parse_hex(word));
            const char* name = index >= 0 && index < static_cast<int>(mesh_names.size()) ? mesh_names[index].c_str() : nullptr;
            const int animated = tag == "POSE" ? 1 : 0;
            float matrix[16] = {}, box[6] = {};
            library.get_mesh_matrix(slot, matrix, name, animated);
            for (int i = 0; i < 16 && i < static_cast<int>(expected.size()); ++i)
                stats.value(tag == "POSE" ? "pose matrix" : "static matrix", expected[i], matrix[i], model + " " + tag);
            if (expected.size() >= 22) {
                library.get_mesh_minmax(slot, box, name, animated);
                for (int i = 0; i < 6; ++i)
                    stats.value(tag == "POSE" ? "pose bounds" : "static bounds", expected[16 + i], box[i], model + " " + tag);
            }
        } else if (tag == "LIST") {
            int list, keys;
            std::string maximum, low, high;
            fields >> list >> keys >> maximum >> low >> high;
            library.set_list(slot, list);
            int actual_keys = -1;
            library.get_num_key(slot, &actual_keys);
            stats.structural(keys == actual_keys, model + " list " + std::to_string(list) + " key count");
            float value = 0;
            library.get_max_time(slot, &value);
            stats.structural(bits(value) == parse_hex(maximum), model + " max time");
            library.get_key_time(slot, &value, -5);
            stats.structural(bits(value) == parse_hex(low), model + " clamped low key");
            library.get_key_time(slot, &value, keys + 7);
            stats.structural(bits(value) == parse_hex(high), model + " clamped high key");
            for (int k = 0; k < keys; ++k) {
                std::string word;
                fields >> word;
                library.get_key_time(slot, &value, k);
                stats.structural(bits(value) == parse_hex(word), model + " key time");
            }
            ++lists_checked;
        } else if (tag == "TIME") {
            std::string time, stored;
            fields >> time >> stored;
            library.set_time(slot, from_bits(parse_hex(time)));
            float value = 0;
            library.get_time(slot, &value);
            stats.structural(bits(value) == parse_hex(stored), model + " stored time");
            library.render(slot, nullptr, nullptr);
        } else if (tag == "FRAMES") {
            unsigned expected_count;
            std::string expected_hash, word;
            fields >> expected_count >> expected_hash;
            std::vector<std::uint32_t> expected;
            while (fields >> word) expected.push_back(parse_hex(word));
            unsigned count = 0;
            std::uint64_t hash = 1469598103934665603ull;
            std::vector<float> values;
            if (const cp2::csmesh::Model* m = library.model(slot)) digest_frames(*m, 0, count, hash, values);
            char text[17];
            std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
            stats.structural(count == expected_count, model + " frame count");
            ++stats.compared["frame poses"];
            if (expected_hash == text) ++stats.exact["frame poses"];
            else stats.structural(!expected.empty(), model + " frame digest " + expected_hash + " != " + text);
            for (std::size_t i = 0; i < expected.size() && i < values.size(); ++i)
                stats.value("frame matrix", expected[i], values[i], model + " frame " + std::to_string(i / 16));
        }
    }
    std::printf("models %d, animation lists %d, structural mismatches %llu\n", models, lists_checked,
                static_cast<unsigned long long>(stats.structural_failures));
    bool ok = stats.structural_failures == 0;
    for (const auto& entry : stats.compared) {
        const std::string& category = entry.first;
        const std::int64_t worst = stats.worst[category];
        std::printf("%-14s %10llu values, %10llu bit-exact, worst %lld ulp %s\n", category.c_str(),
                    static_cast<unsigned long long>(entry.second), static_cast<unsigned long long>(stats.exact[category]),
                    static_cast<long long>(worst), stats.worst_where[category].c_str());
        if (worst > tolerance) ok = false;
    }
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3) return replay(argv[1], argv[2], argc >= 4 ? std::atoll(argv[3]) : 0);
    test_encodings();
    test_semantics();
    if (failures) {
        std::printf("%d CSMesh checks failed\n", failures);
        return 1;
    }
    std::printf("CSMesh parser and animation semantics: PASS\n");
    return 0;
}
