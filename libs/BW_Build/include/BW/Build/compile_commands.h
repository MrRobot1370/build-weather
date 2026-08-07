#pragma once

// `compile_commands.json` reader.
//
// This is the reliable way to join a ninja output path back to the source
// file that produced it. Both reference projects enable
// CMAKE_EXPORT_COMPILE_COMMANDS, so the database is usually sitting in the
// build directory next to `.ninja_log`. When it is missing we fall back to
// the CMake object-path convention, which is a guess (see
// guessSourceFromObject).

#include "BW/Build/build_types.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace BW::Build
{

struct CompileEntry
{
    std::string directory; ///< absolute, normalized
    std::string file; ///< absolute source path, normalized
    std::string output; ///< absolute object path, normalized (may be empty)
};

class CompileCommands
{
public:
    /// Parses the database text. `error` is set and false returned when the
    /// text is not a JSON array of objects.
    auto parse(std::string_view json, std::string &error) -> bool;

    /// Reads `<buildDir>/compile_commands.json` if it exists.
    auto load(const std::string &buildDir, std::string &error) -> bool;

    [[nodiscard]]
    auto empty() const -> bool
    {
        return m_entries.empty();
    }

    [[nodiscard]]
    auto entries() const -> const std::vector<CompileEntry> &
    {
        return m_entries;
    }

    /// Source file for an object path, absolute or relative to the build
    /// directory. Returns nullopt when the object is not in the database.
    [[nodiscard]]
    auto sourceForOutput(std::string_view outputPath) const
        -> std::optional<std::string>;

    /// True when the database contained "output" fields. Older CMake versions
    /// omit them, in which case only sourceForObjectName() can work.
    [[nodiscard]]
    auto hasOutputs() const -> bool
    {
        return m_hasOutputs;
    }

private:
    std::vector<CompileEntry> m_entries;
    std::unordered_map<std::string, std::size_t> m_byOutputKey;
    std::unordered_map<std::string, std::size_t> m_byOutputName;
    bool m_hasOutputs { false };
};

/// Fallback for when there is no compile database.
///
/// CMake's Ninja generator writes object files as
///   <target binary dir>/CMakeFiles/<target>.dir/<source path>.obj
/// so stripping the `CMakeFiles/<target>.dir/` infix and the object suffix
/// recovers a path relative to the source root - provided the build tree
/// mirrors the source tree, which is the normal in-project `build/` layout.
/// GUESS: returns an empty string when the shape does not match rather than
/// inventing something.
[[nodiscard]]
auto guessSourceFromObject(std::string_view objectPath) -> std::string;

}
