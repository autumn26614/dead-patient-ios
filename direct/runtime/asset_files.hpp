#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <map>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace cp2 {

// The original paths use Win32 separators and case-insensitive ASCII names.
// Bundle files remain immutable; writes and later reads use a private overlay.
// The package verifier rejects case/Unicode collisions before these files ship.
class AssetFiles {
public:
    AssetFiles(const std::string& assets, const std::string& writable)
        : assets_(canonical_root(assets)), writable_(canonical_root(writable)) {
        if (assets_ == writable_ || within(assets_, writable_) || within(writable_, assets_))
            throw std::invalid_argument("Asset and writable roots must be separate");
    }

    static std::string normalize(const std::string& input, bool allow_root = false) {
        if (input.empty() || input.front() == '/' || input.front() == '\\')
            throw std::invalid_argument("Expected relative game path");
        std::vector<std::string> parts;
        std::string part;
        for (std::size_t index = 0; index <= input.size(); ++index) {
            unsigned char value = index == input.size() ? '/' : (unsigned char)input[index];
            if (value == '/' || value == '\\') {
                if (part == "..") {
                    if (parts.empty()) throw std::invalid_argument("Game path escapes its root");
                    parts.pop_back();
                } else if (!part.empty() && part != ".") parts.push_back(part);
                part.clear();
            } else {
                if (value < 32 || value == ':' || value == 127)
                    throw std::invalid_argument("Invalid game path character");
                part += (char)value;
            }
        }
        std::string result;
        for (const auto& item : parts) {
            if (!result.empty()) result += '/';
            result += item;
        }
        if (result.empty() && !allow_root) throw std::invalid_argument("Game path names the resource root");
        return result;
    }

    // Maintain a game-relative cwd without changing the process cwd. Absolute
    // paths returned by current_directory() can be stored and re-entered; only
    // paths under one of the two trusted roots are mapped into this view.
    bool change_directory(const std::string& path) {
        const std::string name = logical_path(path, true);
        if (name.empty()) { cwd_.clear(); return true; }
        std::string actual = walk(writable_, name, false);
        if (!actual.empty()) {
            if (node(actual) != Directory) return false;
            cwd_ = actual.substr(writable_.size() + 1);
            return true;
        }
        actual = walk(assets_, name, false);
        if (actual.empty() || node(actual) != Directory) return false;
        cwd_ = actual.substr(assets_.size() + 1);
        return true;
    }

    std::string current_directory() const {
        return cwd_.empty() ? assets_ : assets_ + '/' + cwd_;
    }

    // Empty means absent. Directories and symlinks are never returned as files.
    std::string read_path(const std::string& relative) const {
        const std::string name = logical_path(relative, true);
        if (name.empty()) return {};
        std::string path = walk(writable_, name, false);
        if (!path.empty()) return node(path) == File ? path : std::string();
        path = walk(assets_, name, false);
        return !path.empty() && node(path) == File ? path : std::string();
    }

    // preserve_existing copies an immutable bundle file before an offset/append
    // update, so untouched bytes have the original Windows file semantics.
    std::string write_path(const std::string& relative, bool preserve_existing = false) const {
        const std::string name = logical_path(relative, false);
        // A bundled file cannot become a parent directory in the merged view.
        for (std::size_t end = name.find('/'); end != std::string::npos; end = name.find('/', end + 1)) {
            std::string parent = walk(assets_, name.substr(0, end), false);
            if (!parent.empty() && node(parent) != Directory)
                throw std::runtime_error("Cannot write beneath an original game file");
        }
        std::string original = walk(assets_, name, false);
        if (!original.empty() && node(original) == Directory)
            throw std::runtime_error("Cannot replace an original game directory with a file");
        std::string path = walk(writable_, name, true);
        Node state = node(path);
        if (state == Directory) throw std::runtime_error("Cannot write a game directory");
        if (state == Missing && preserve_existing) {
            if (!original.empty() && node(original) == File) copy(original, path);
        }
        return path;
    }

    // Match FindFirstFile/HSP's mask semantics. Bit 1 excludes directories,
    // bit 2 excludes hidden files, and bit 4 returns the excluded set instead.
    // Return basenames as dirlist does, with writable entries shadowing assets.
    std::vector<std::string> list(const std::string& pattern, int mode = 0) const {
        if (mode < 0 || (mode & ~7)) throw std::invalid_argument("Unsupported HSP dirlist mode");
        const std::string normalized = logical_path(pattern, false);
        const std::size_t slash = normalized.find_last_of('/');
        const std::string directory = slash == std::string::npos ? "" : normalized.substr(0, slash);
        const std::string wildcard = slash == std::string::npos ? normalized : normalized.substr(slash + 1);
        if (directory.find_first_of("*?") != std::string::npos)
            throw std::invalid_argument("Wildcards are only accepted in the final path component");
        std::map<std::string, Entry> entries;
        const std::string original = directory.empty() ? assets_ : walk(assets_, directory, false);
        const std::string overlay = directory.empty() ? writable_ : walk(writable_, directory, false);
        if (!overlay.empty() && node(overlay) != Directory) return {};
        collect(original, entries, false);
        collect(overlay, entries, true);
        std::vector<std::string> result;
        for (const auto& item : entries) {
            const Entry& entry = item.second;
            if (!glob(wildcard, entry.name)) continue;
            bool masked = ((mode & 1) && entry.kind == Directory) ||
                          ((mode & 2) && entry.hidden);
            if ((mode & 4) ? masked : !masked) result.push_back(entry.name);
        }
        return result;
    }

    const std::string& asset_root() const { return assets_; }
    const std::string& writable_root() const { return writable_; }

private:
    enum Node { Missing, File, Directory };
    struct Entry { std::string name; Node kind; bool hidden; };
    std::string assets_, writable_, cwd_;

    std::string logical_path(const std::string& input, bool allow_root) const {
        if (input.empty()) throw std::invalid_argument("Empty game path");
        std::string portable = input;
        for (char& value : portable) if (value == '\\') value = '/';
        const bool absolute = portable.front() == '/' || (portable.size() > 1 && portable[1] == ':');
        std::string relative;
        if (absolute) {
            bool mapped = false;
            const std::string lower = fold(portable);
            for (const std::string* root : {&assets_, &writable_}) {
                const std::string lower_root = fold(*root);
                if (lower == lower_root) { mapped = true; relative.clear(); break; }
                if (lower.size() > lower_root.size() && lower.compare(0, lower_root.size(), lower_root) == 0 && lower[lower_root.size()] == '/') {
                    mapped = true;
                    relative = portable.substr(root->size() + 1);
                    break;
                }
            }
            if (!mapped) throw std::invalid_argument("Absolute game path is outside trusted roots");
            if (relative.empty()) {
                if (!allow_root) throw std::invalid_argument("Game path names the resource root");
                return {};
            }
        } else relative = cwd_.empty() ? portable : cwd_ + '/' + portable;
        return normalize(relative, allow_root);
    }

    static bool within(const std::string& child, const std::string& parent) {
        return child.size() > parent.size() && child.compare(0, parent.size(), parent) == 0 && child[parent.size()] == '/';
    }

    static Node node(const std::string& path) {
#ifdef _WIN32
        DWORD attributes = GetFileAttributesA(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return Missing;
            throw std::runtime_error("Cannot inspect game path");
        }
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) throw std::runtime_error("Links are not accepted in game paths");
        return attributes & FILE_ATTRIBUTE_DIRECTORY ? Directory : File;
#else
        struct stat status;
        if (lstat(path.c_str(), &status)) {
            if (errno == ENOENT || errno == ENOTDIR) return Missing;
            throw std::runtime_error("Cannot inspect game path");
        }
        if (S_ISLNK(status.st_mode)) throw std::runtime_error("Links are not accepted in game paths");
        if (S_ISDIR(status.st_mode)) return Directory;
        if (S_ISREG(status.st_mode)) return File;
        throw std::runtime_error("Game path is not a regular file/directory");
#endif
    }

    static std::string canonical_root(const std::string& path) {
        // iOS's /var ancestor may be a system symlink. Resolve the trusted root
        // once; every user-provided relative component is inspected via lstat.
#ifdef _WIN32
        if (node(path) != Directory) throw std::invalid_argument("Game root must be a directory");
        char* native = _fullpath(nullptr, path.c_str(), 0);
#else
        char* native = realpath(path.c_str(), nullptr);
#endif
        if (!native) throw std::invalid_argument("Cannot resolve game root");
        std::string result(native);
        std::free(native);
#ifdef _WIN32
        for (char& value : result) if (value == '\\') value = '/';
#endif
        while (result.size() > 1 && result.back() == '/') result.pop_back();
        if (node(result) != Directory) throw std::invalid_argument("Game root must be a directory");
        return result;
    }

    static std::string fold(const std::string& input) {
        std::string result(input);
        for (char& value : result) if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
        return result;
    }

    static std::string matching_component(const std::string& directory, const std::string& requested) {
        DIR* stream = opendir(directory.c_str());
        if (!stream) throw std::runtime_error("Cannot inspect game directory");
        std::string found, wanted = fold(requested);
        bool ambiguous = false;
        while (dirent* entry = readdir(stream)) {
            if (fold(entry->d_name) != wanted) continue;
            if (!found.empty()) ambiguous = true;
            found = entry->d_name;
        }
        closedir(stream);
        if (ambiguous) throw std::runtime_error("Case-ambiguous game path");
        return found;
    }

    static bool glob(std::string pattern, const std::string& text) {
        pattern = fold(pattern);
        // Win32 treats *.* as every name, including names without a suffix.
        if (pattern == "*.*") pattern = "*";
        const std::string name = fold(text);
        std::size_t p = 0, t = 0, star = std::string::npos, retry = 0;
        while (t < name.size()) {
            if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == name[t])) { ++p; ++t; }
            else if (p < pattern.size() && pattern[p] == '*') { star = p++; retry = t; }
            else if (star != std::string::npos) { p = star + 1; t = ++retry; }
            else return false;
        }
        while (p < pattern.size() && pattern[p] == '*') ++p;
        return p == pattern.size();
    }

    static void collect(const std::string& directory, std::map<std::string, Entry>& merged, bool overlay) {
        if (directory.empty() || node(directory) != Directory) return;
        DIR* stream = opendir(directory.c_str());
        if (!stream) throw std::runtime_error("Cannot list game directory");
        std::map<std::string, Entry> local;
        try {
            while (dirent* entry = readdir(stream)) {
                const std::string name = entry->d_name;
                if (name == "." || name == "..") continue;
                const std::string key = fold(name), path = directory + '/' + name;
                if (local.count(key)) throw std::runtime_error("Case-ambiguous game directory listing");
                bool hidden = !name.empty() && name.front() == '.';
#ifdef _WIN32
                DWORD attributes = GetFileAttributesA(path.c_str());
                hidden = hidden || (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
#endif
                local.emplace(key, Entry{name, node(path), hidden});
            }
        } catch (...) { closedir(stream); throw; }
        closedir(stream);
        for (const auto& entry : local) {
            if (overlay) merged.erase(entry.first);
            merged.emplace(entry);
        }
    }

    static std::string walk(const std::string& root, const std::string& name, bool writing) {
        std::string path = root;
        std::size_t start = 0;
        while (start < name.size()) {
            std::size_t end = name.find('/', start);
            bool last = end == std::string::npos;
            std::string component = name.substr(start, last ? std::string::npos : end - start);
            std::string actual = matching_component(path, component);
            if (actual.empty()) {
                if (!writing) return {};
                actual = component;
            }
            path += '/' + actual;
            Node state = node(path);
            if (!last && state == Missing && writing) {
#ifdef _WIN32
                int status = _mkdir(path.c_str());
#else
                int status = mkdir(path.c_str(), 0755);
#endif
                if (status) throw std::runtime_error("Cannot create writable game directory");
                state = node(path);
            }
            if (!last && state != Directory) {
                if (writing) throw std::runtime_error("Cannot write beneath a game file");
                return {};
            }
            if (last) return state == Missing && !writing ? std::string() : path;
            start = end + 1;
        }
        return {};
    }

    static void copy(const std::string& source, const std::string& destination) {
        FILE* input = std::fopen(source.c_str(), "rb");
        if (!input) throw std::runtime_error("Cannot open original game file");
        FILE* output = std::fopen(destination.c_str(), "wb");
        if (!output) {
            std::fclose(input);
            throw std::runtime_error("Cannot create writable game file");
        }
        char buffer[65536];
        bool failed = false;
        while (std::size_t size = std::fread(buffer, 1, sizeof(buffer), input))
            if (std::fwrite(buffer, 1, size, output) != size) { failed = true; break; }
        failed = failed || std::ferror(input);
        std::fclose(input);
        if (std::fclose(output)) failed = true;
        if (failed) {
            std::remove(destination.c_str());
            throw std::runtime_error("Cannot copy original game file to writable overlay");
        }
    }
};

} // namespace cp2
