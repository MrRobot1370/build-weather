#pragma once

// Parser for the progress lines ninja writes to stdout while building.
//
//     [12/340 8] Building CXX object libs/CMakeFiles/x.dir/src/a.cpp.obj
//
// FORMAT CAVEAT: the prefix is not fixed - it is whatever NINJA_STATUS
// expands to, and the default has changed between ninja versions.
// BuildRunner sets NINJA_STATUS explicitly on the child process (see
// kRequiredStatusFormat) instead of hoping for the default, so the shape is
// known. The bare `[%f/%t]` form is still accepted, for logs pasted in from
// elsewhere.
//
// WHEN THE LINE IS PRINTED: this matters more than the format. Ninja only
// prints a status line as an edge *starts* when it believes stdout is a
// smart terminal. Behind a pipe - which is exactly what a child process
// gets - it prints once per edge *finish* instead, and `%f` is the count of
// finished edges. So these lines are completion events, not start events,
// and no amount of parsing turns them into starts. `%r` is included in the
// status format because it is ninja's own count of edges currently running,
// which is the honest source for the parallelism readout.
//
// The text after the prefix is the rule description, which comes from the
// generator. The mappings below are CMake's Ninja generator wording as of
// CMake 3.2x; an unrecognised description yields an empty `outputPath`
// rather than a guess.

#include "BW/Build/build_types.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BW::Build
{

/// The value BuildRunner puts in NINJA_STATUS so the prefix is known exactly.
/// Ninja's default plus `%r`, the number of edges currently running.
inline constexpr const char *kRequiredStatusFormat = "[%f/%t %r] ";

struct ProgressLine
{
    int finished { 0 }; ///< %f - edges finished so far
    int total { 0 }; ///< %t - edges in the plan
    int running { -1 }; ///< %r - edges in flight, -1 when not in the format
    std::string description; ///< rule description as printed
    std::string outputPath; ///< extracted output, empty when unrecognised
    StepKind kind { StepKind::Other };
};

/// Parses one line of ninja stdout. Returns nullopt for anything that is not
/// a status line (compiler warnings, ninja errors, the final summary).
[[nodiscard]]
auto parseProgressLine(std::string_view line) -> std::optional<ProgressLine>;

/// Best-effort extraction of the produced path from a rule description.
[[nodiscard]]
auto outputPathFromDescription(std::string_view description) -> std::string;

[[nodiscard]]
auto stepKindFromDescription(std::string_view description) -> StepKind;

/// Splits an arbitrarily chunked stdout stream into lines and hands back the
/// status lines. Handles both '\n' and '\r' terminators, since ninja uses
/// carriage returns when it thinks it is talking to a terminal.
class ProgressStream
{
public:
    struct Chunk
    {
        std::vector<ProgressLine> progress;
        std::vector<std::string> other; ///< non-status lines, for the log pane
    };

    [[nodiscard]]
    auto feed(std::string_view bytes) -> Chunk;

    /// Flushes a trailing line that was not terminated.
    [[nodiscard]]
    auto flush() -> Chunk;

    void reset();

private:
    std::string m_pending;
};

}
