// Tests for the native dsoundex backend.
//
//   dsound_test                        self-contained semantics tests
//   dsound_test GAME_ROOT REFERENCE    replays a reference recorded from the
//                                      original x86 dsoundex.hpi by
//                                      dsound_windows_reference.cpp
#include "dsound.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
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

std::vector<std::uint8_t> wav(std::uint16_t channels, std::uint32_t rate, std::uint16_t bits,
                              const std::vector<std::uint8_t>& pcm) {
    std::vector<std::uint8_t> out;
    auto u16 = [&](std::uint32_t v) { out.push_back(v & 255); out.push_back((v >> 8) & 255); };
    auto u32 = [&](std::uint32_t v) { u16(v & 0xffff); u16(v >> 16); };
    auto tag = [&](const char* t) { out.insert(out.end(), t, t + 4); };
    const std::uint16_t align = static_cast<std::uint16_t>(channels * bits / 8);
    tag("RIFF"); u32(36 + static_cast<std::uint32_t>(pcm.size())); tag("WAVE");
    tag("fmt "); u32(16); u16(1); u16(channels); u32(rate); u32(rate * align); u16(align); u16(bits);
    tag("data"); u32(static_cast<std::uint32_t>(pcm.size()));
    out.insert(out.end(), pcm.begin(), pcm.end());
    return out;
}

void test_semantics() {
    using cp2::dsound::Device;
    CHECK(cp2::dsound::volume_from_level(0) == -10000);
    CHECK(cp2::dsound::volume_from_level(1) == -4816);
    CHECK(cp2::dsound::volume_from_level(100) == -816);
    CHECK(cp2::dsound::volume_from_level(256) == 0 && cp2::dsound::volume_from_level(999) == 0);
    CHECK(cp2::dsound::pan_from_level(127) == 10000 && cp2::dsound::pan_from_level(-64) == -103);

    Device device;
    std::string error;
    const std::vector<std::uint8_t> tone = wav(1, 8000, 16, std::vector<std::uint8_t>(800, 0));  // 400 frames
    CHECK(device.load(0, tone.data(), tone.size(), error) == 0);  // before ds_init
    CHECK(device.init(0, 0) == -1);
    CHECK(device.load(0, tone.data(), tone.size(), error) == -800);
    CHECK(device.play(9, 0, 0) == -1 && device.stop(9) == 0 && device.status(9) == 0 && device.position(9) == 0);
    CHECK(device.status(0) == 0);
    CHECK(device.play(0, 100, 0) == 0 && device.status(0) == -1 && device.position(0) == -100);
    CHECK(device.play(0, 99999, 1) == 0 && device.position(0) == -100 && device.status(0) == -5);  // bad seek ignored
    CHECK(device.stop(0) == 0 && device.status(0) == 0 && device.position(0) == -100);
    CHECK(device.volume(0, 100) == 0 && device.inspect(0)->volume == -816);
    CHECK(device.duplicate(5, 0) == 0 && device.inspect(5)->storage == device.inspect(0)->storage &&
          device.inspect(5)->volume == -816 && device.status(5) == 0);

    // Rendering advances the cursor; a non-looping buffer stops and rewinds.
    std::vector<float> mix(2 * 1000);
    device.play(0, 0, 0);
    device.render(mix.data(), 200, 8000.0);
    CHECK(device.position(0) == -400 && device.status(0) == -1);
    device.render(mix.data(), 1000, 8000.0);
    CHECK(device.status(0) == 0 && device.position(0) == 0);
    device.play(0, 0, 1);
    device.render(mix.data(), 1000, 8000.0);
    CHECK(device.status(0) == -5 && device.position(0) == -400);  // 1000 frames = 2.5 loops
    CHECK(device.stop_all() == 0 && device.status(0) == 0);

    // Unsupported encodings and malformed images fail like the original.
    std::vector<std::uint8_t> bad = tone;
    bad[20] = 0x55;  // format tag 0x55 (MPEG layer 3) needs an ACM codec
    CHECK(device.memload(1, bad.data(), bad.size(), error) == -1 && device.status(1) == 0);
    bad = tone;
    bad[0] = 'X';
    CHECK(device.memload(1, bad.data(), bad.size(), error) == -1);
}

std::uint64_t fnv(const std::vector<std::uint8_t>& data) {
    std::uint64_t hash = 1469598103934665603ull;
    for (std::uint8_t b : data) hash = (hash ^ b) * 1099511628211ull;
    return hash;
}

bool read_file(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

int replay(const std::string& root, const std::string& reference) {
    std::ifstream in(reference);
    if (!in) { std::printf("cannot open %s\n", reference.c_str()); return 2; }
    cp2::dsound::Device device;
    device.init(0, 0);
    int files = 0, exact = 0, mismatches = 0, levels = 0;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string tag;
        fields >> tag;
        if (tag == "FILE") {
            std::string path, kind;
            int expected;
            fields >> path >> kind >> expected;
            std::vector<std::uint8_t> file;
            std::string error;
            int result = 1;
            if (read_file(root + "/" + path, file)) {
                if (kind == "memload") {
                    // The game copies size-5 bytes into an int array of (size-5)/4 elements.
                    const std::size_t bytes = file.size() >= 5 ? (file.size() - 5) / 4 * 4 : 0;
                    std::vector<std::uint8_t> variable(file.begin() + 5, file.begin() + 5 + bytes);
                    result = device.memload(0, variable.data(), variable.size(), error);
                } else {
                    result = device.load(0, file.data(), file.size(), error);
                }
            }
            std::string word, fmt_tag;
            unsigned tag_value = 0, channels = 0, block = 0, bits = 0;
            unsigned long rate = 0, average = 0, flags = 0, bytes = 0;
            std::string hash;
            fields >> fmt_tag >> tag_value >> channels >> rate >> average >> block >> bits >> word >> std::hex >> flags >>
                std::dec >> word >> bytes >> word >> hash;
            const cp2::dsound::Buffer* b = device.inspect(0);
            bool ok = result == expected && b;
            if (ok) {
                const cp2::dsound::Format& f = b->storage->format;
                char text[17];
                std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(fnv(b->storage->bytes)));
                ok = f.tag == tag_value && f.channels == channels && f.rate == rate && f.bytes_per_second == average &&
                     f.block_align == block && f.bits == bits && flags == cp2::dsound::kBufferFlags &&
                     b->storage->bytes.size() == bytes && hash == text;
                if (!ok && mismatches < 10)
                    std::printf("MISMATCH %s: bytes %lu/%zu hash %s/%s\n", path.c_str(), bytes,
                                b->storage->bytes.size(), hash.c_str(), text);
            } else if (mismatches < 10) {
                std::printf("MISMATCH %s: result %d expected %d (%s)\n", path.c_str(), result, expected, error.c_str());
            }
            ++files;
            if (ok) ++exact; else ++mismatches;
        } else if (tag == "VOL" || tag == "PAN") {
            int level, result;
            long value;
            fields >> level >> result >> value;
            const long actual = tag == "VOL" ? cp2::dsound::volume_from_level(level) : cp2::dsound::pan_from_level(level);
            ++levels;
            if (actual != value || result != 0) {
                ++mismatches;
                if (mismatches < 20) std::printf("MISMATCH %s %d: %ld expected %ld\n", tag.c_str(), level, actual, value);
            }
        } else if (tag == "INIT") {
            int expected;
            fields >> expected;
            cp2::dsound::Device fresh;
            if (fresh.init(0, 0) != expected) { ++mismatches; std::printf("MISMATCH ds_init result\n"); }
        }
    }
    std::printf("audio files %d, buffers bit-exact %d, volume/pan levels %d, mismatches %d\n", files, exact, levels,
                mismatches);
    return mismatches ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3) return replay(argv[1], argv[2]);
    test_semantics();
    if (failures) {
        std::printf("%d DirectSound checks failed\n", failures);
        return 1;
    }
    std::printf("DirectSound plugin semantics: PASS\n");
    return 0;
}
