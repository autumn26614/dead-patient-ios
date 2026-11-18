#include "asset_files.hpp"
#include <algorithm>
#include <iostream>

static unsigned checks = 0;
static void require(bool condition) {
    ++checks;
    if (!condition) throw std::runtime_error("Asset-file compatibility check failed: " + std::to_string(checks));
}
template<class Function> static void rejects(Function function) {
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    require(rejected);
}
static void directory(const std::string& path) {
#ifdef _WIN32
    require(_mkdir(path.c_str()) == 0);
#else
    require(mkdir(path.c_str(), 0755) == 0);
#endif
}
static void put(const std::string& path, const std::string& data) {
    FILE* stream = std::fopen(path.c_str(), "wb");
    require(stream != nullptr);
    require(std::fwrite(data.data(), 1, data.size(), stream) == data.size());
    require(std::fclose(stream) == 0);
}
static std::string get(const std::string& path) {
    FILE* stream = std::fopen(path.c_str(), "rb");
    require(stream != nullptr);
    std::string result;
    char data[256];
    while (std::size_t size = std::fread(data, 1, sizeof(data), stream)) result.append(data, size);
    require(!std::ferror(stream));
    require(std::fclose(stream) == 0);
    return result;
}

int main(int argc, char** argv) {
    if (argc != 2) throw std::invalid_argument("Pass an existing empty fixture directory");
    const std::string root = argv[1], assets = root + "/assets", writable = root + "/writable";
    directory(assets);
    directory(writable);
    directory(assets + "/BGM");
    directory(assets + "/chap1");
    const std::string binary("RIFF\0\x80\xff", 7);
    put(assets + "/BGM/Track.PAK", binary);
    put(assets + "/chap1/project.dat", "original-data");
    cp2::AssetFiles files(assets, writable);
    require(get(files.read_path("bgm\\TRACK.pak")) == binary);
    require(get(files.read_path(".//BGM/../bgm/Track.PAK")) == binary);
    require(files.read_path("absent").empty());
    require(files.read_path("BGM").empty());
    require(files.read_path("BGM/Track.PAK/child").empty());
    for (const std::string& bad : {"../outside", "a/../../outside", "/absolute", "\\\\server\\share", "C:\\x"})
        rejects([&] { files.read_path(bad); });
    require(files.read_path(".").empty());
    require(files.read_path("a/..").empty());
    rejects([&] { files.write_path(std::string("bad\0file", 8)); });
    rejects([&] { files.write_path("bad\nfile"); });
    rejects([&] { files.write_path("BGM/Track.PAK/child"); });
    std::string updated = files.write_path("CHAP1\\project.DAT", true);
    require(get(updated) == "original-data");
    FILE* stream = std::fopen(updated.c_str(), "r+b");
    require(stream != nullptr);
    require(std::fseek(stream, 3, SEEK_SET) == 0);
    require(std::fwrite("X", 1, 1, stream) == 1);
    require(std::fclose(stream) == 0);
    require(get(files.read_path("chap1/PROJECT.dat")) == "oriXinal-data");
    require(get(assets + "/chap1/project.dat") == "original-data");
    std::string saved = files.write_path("Save\\Slot1\\State.dat");
    put(saved, binary);
    require(get(files.read_path("save/slot1/state.DAT")) == binary);
    require(files.write_path("SAVE/slot1/STATE.dat") == saved);
    rejects([&] { files.write_path("SAVE"); });
    rejects([&] { cp2::AssetFiles same(assets, assets); });
    rejects([&] { cp2::AssetFiles nested(assets, assets + "/BGM"); });
    directory(assets + "/chap1/SubDir");
    put(assets + "/chap1/.hidden", "hidden");
    put(assets + "/chap1/README", "no suffix");
    put(files.write_path("chap1/Extra.DAT"), "new");
    auto listed = files.list("CHAP1\\*.d?t", 1);
    require(listed.size() == 2);
    require(std::find(listed.begin(), listed.end(), "project.DAT") != listed.end());
    require(std::find(listed.begin(), listed.end(), "Extra.DAT") != listed.end());
    require(files.list("chap1/*.*").size() == 5);
    require(files.list("chap1/*", 1).size() == 4);
    require(files.list("chap1/*", 2).size() == 4);
    require(files.list("chap1/*", 3).size() == 3);
    require(files.list("chap1/*", 4).empty());
    require(files.list("chap1/*", 5) == std::vector<std::string>{"SubDir"});
    require(files.list("chap1/*", 6) == std::vector<std::string>{".hidden"});
    require(files.list("chap1/*", 7).size() == 2);
    require(files.list("missing/*").empty());
    require(files.list("BGM/Track.PAK/*").empty());
    rejects([&] { files.list("sAvE/sLoT?/state.*"); });
    rejects([&] { files.list("*/file"); });
    rejects([&] { files.list("../*"); });
    rejects([&] { files.list("*", 8); });
    rejects([&] { files.list("*", -1); });
    const std::string initial = files.current_directory();
    require(files.change_directory("cHaP1"));
    require(get(files.read_path("PROJECT.dat")) == "oriXinal-data");
    require(files.list("*.dat", 1).size() == 2);
    require(!files.change_directory("missing"));
    require(!files.change_directory("README"));
    require(get(files.read_path("PROJECT.dat")) == "oriXinal-data");
    const std::string chapter = files.current_directory();
    require(files.change_directory("..\\BGM"));
    require(get(files.read_path("track.pak")) == binary);
    require(files.change_directory(chapter));
    require(get(files.read_path("README")) == "no suffix");
    require(get(files.read_path(initial + "/BGM/Track.PAK")) == binary);
    require(files.change_directory(initial));
    require(files.change_directory("SAVE/slot1"));
    require(get(files.read_path("state.dat")) == binary);
    std::string cwd_save = files.write_path("another.dat");
    put(cwd_save, "cwd-save");
    require(get(files.read_path("../slot1/another.DAT")) == "cwd-save");
    require(files.change_directory(initial));
    rejects([&] { files.change_directory(".."); });
    const std::string outside = files.asset_root().substr(0, files.asset_root().find_last_of('/'));
    rejects([&] { files.change_directory(outside); });
    require(files.current_directory() == initial);
#ifndef _WIN32
    require(symlink((files.asset_root() + "/BGM/Track.PAK").c_str(), (files.writable_root() + "/link").c_str()) == 0);
    rejects([&] { files.read_path("link"); });
    rejects([&] { files.write_path("link"); });
    require(symlink((files.asset_root() + "/BGM").c_str(), (files.writable_root() + "/linkdir").c_str()) == 0);
    rejects([&] { files.read_path("linkdir/Track.PAK"); });
#endif
    std::cout << "Asset paths/overlay checks passed: " << checks << '\n';
}
