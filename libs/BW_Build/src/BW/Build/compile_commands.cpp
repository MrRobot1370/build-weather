#include "BW/Build/compile_commands.h"

#include "json_reader.h"

#include <fstream>

namespace BW::Build
{

namespace {

auto endsWithAny(std::string_view text, std::initializer_list<const char *> ex)
    -> bool
{
    for (const char *suffix : ex) {
        const std::string_view s { suffix };
        if (text.size() > s.size()
            && text.compare(text.size() - s.size(), s.size(), s) == 0) {
            return true;
        }
    }
    return false;
}

}

auto CompileCommands::parse(std::string_view json, std::string &error) -> bool
{
    m_entries.clear();
    m_byOutputKey.clear();
    m_byOutputName.clear();
    m_hasOutputs = false;

    Json::Reader reader { json };
    if (!reader.beginArray()) {
        error = "compile_commands.json does not start with an array";
        return false;
    }
    if (reader.endArray()) {
        error = "compile_commands.json is empty";
        return true;
    }

    std::string key;
    std::string value;
    do {
        if (!reader.beginObject()) {
            error = "expected an object in compile_commands.json";
            return false;
        }

        CompileEntry entry;
        if (!reader.endObject()) {
            do {
                if (!reader.readString(key) || !reader.consume(':')) {
                    error = "malformed member in compile_commands.json";
                    return false;
                }
                if (key == "directory" || key == "file" || key == "output") {
                    if (!reader.readString(value)) {
                        error = "expected a string for '" + key + "'";
                        return false;
                    }
                    if (key == "directory") {
                        entry.directory = Core::normalizePath(value);
                    }
                    else if (key == "file") {
                        entry.file = Core::normalizePath(value);
                    }
                    else {
                        entry.output = Core::normalizePath(value);
                    }
                }
                else if (!reader.skipValue()) {
                    error = "malformed value in compile_commands.json";
                    return false;
                }
            } while (reader.comma());

            if (!reader.endObject()) {
                error = "unterminated object in compile_commands.json";
                return false;
            }
        }

        if (!entry.file.empty()) {
            if (!entry.directory.empty()) {
                entry.file = Core::joinPath(entry.directory, entry.file);
                if (!entry.output.empty()) {
                    entry.output
                        = Core::joinPath(entry.directory, entry.output);
                }
            }
            if (!entry.output.empty()) {
                m_hasOutputs = true;
                m_byOutputKey.emplace(
                    Core::pathKey(entry.output),
                    m_entries.size());
                m_byOutputName.emplace(
                    Core::pathKey(Core::fileName(entry.output)),
                    m_entries.size());
            }
            m_entries.push_back(std::move(entry));
        }
    } while (reader.comma());

    if (!reader.endArray()) {
        error = "unterminated array in compile_commands.json";
        return false;
    }
    error.clear();
    return true;
}

auto CompileCommands::load(const std::string &buildDir, std::string &error)
    -> bool
{
    const std::string path
        = Core::joinPath(buildDir, "compile_commands.json");
    std::ifstream in { path, std::ios::binary };
    if (!in) {
        error = "no compile_commands.json in " + buildDir;
        return false;
    }
    const std::string content { std::istreambuf_iterator<char> { in },
                                std::istreambuf_iterator<char> {} };
    return parse(content, error);
}

auto CompileCommands::sourceForOutput(std::string_view outputPath) const
    -> std::optional<std::string>
{
    if (outputPath.empty()) {
        return std::nullopt;
    }

    const std::string key = Core::pathKey(outputPath);
    if (const auto it = m_byOutputKey.find(key); it != m_byOutputKey.end()) {
        return m_entries[it->second].file;
    }

    // Ninja records the object relative to the build directory while the
    // database records it relative to each entry's `directory`. Suffix
    // matching covers that without needing to know the build root.
    for (const auto &[outKey, index] : m_byOutputKey) {
        if (outKey.size() > key.size()
            && outKey.compare(outKey.size() - key.size(), key.size(), key) == 0
            && outKey[outKey.size() - key.size() - 1] == '/') {
            return m_entries[index].file;
        }
    }

    const std::string name = Core::pathKey(Core::fileName(outputPath));
    if (const auto it = m_byOutputName.find(name);
        it != m_byOutputName.end()) {
        return m_entries[it->second].file;
    }
    return std::nullopt;
}

auto guessSourceFromObject(std::string_view objectPath) -> std::string
{
    const std::string path = Core::normalizePath(objectPath);
    if (!endsWithAny(path, { ".obj", ".o" })) {
        return {};
    }

    constexpr std::string_view kMarker { "CMakeFiles/" };
    const std::size_t marker = path.find(kMarker);
    if (marker == std::string::npos) {
        return {};
    }
    const std::size_t dirEnd = path.find(".dir/", marker);
    if (dirEnd == std::string::npos) {
        return {};
    }

    const std::string prefix = path.substr(0, marker); // "libs/BW_Build/"
    std::string tail = path.substr(dirEnd + 5); // "src/a.cpp.obj"
    const std::size_t dot = tail.find_last_of('.');
    if (dot == std::string::npos) {
        return {};
    }
    tail.erase(dot); // "src/a.cpp"

    // Objects for sources outside the target's directory get "__/" segments
    // (CMake's encoding of ".."). Translate them back.
    std::string decoded;
    decoded.reserve(tail.size());
    for (std::size_t i = 0; i < tail.size();) {
        if (tail.compare(i, 3, "__/") == 0) {
            decoded += "../";
            i += 3;
        }
        else {
            decoded.push_back(tail[i]);
            ++i;
        }
    }
    return Core::normalizePath(prefix + decoded);
}

auto inferSourceRoot(
    const CompileCommands &commands,
    std::string_view buildRoot) -> std::string
{
    const std::string build = Core::normalizePath(buildRoot);

    std::string root;
    for (const auto &entry : commands.entries()) {
        if (entry.file.empty()) {
            continue;
        }
        if (!build.empty() && Core::relativeTo(entry.file, build)) {
            continue; // generated, so it says nothing about the source root
        }
        if (root.empty()) {
            root = Core::parentPath(entry.file);
            continue;
        }
        // relativeTo, not a string prefix: comparing raw prefixes would let a
        // root of ".../src" claim ".../src2/a.cpp" and stop the walk one
        // directory too deep, which then reads as [external] on the map.
        while (!root.empty() && !Core::relativeTo(entry.file, root)) {
            const std::string parent = Core::parentPath(root);
            if (parent == root) {
                return {};
            }
            root = parent;
        }
        if (root.empty()) {
            return {};
        }
    }
    return root;
}

}
