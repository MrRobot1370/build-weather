#pragma once

// Parser for the progress lines ninja writes to stdout while building:
//
//     [12/340 8] Building CXX object libs/CMakeFiles/x.dir/src/a.cpp.obj
//
// Behind a pipe ninja prints a status line once per edge *finish*, not per
// start, so these are completion events. %r is ninja's own count of edges
// currently running, which is what the parallelism readout uses.
//
// The prefix is whatever NINJA_STATUS expands to, so BuildRunner sets it
// explicitly (kRequiredStatusFormat). The bare [%f/%t] form is still
// accepted for logs pasted in from elsewhere.

#include "BW/Build/build_types.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BW::Build
{

/// Ninja's default plus %r, the number of edges currently running.
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

/// Returns nullopt for anything that is not a status line: compiler
/// warnings, ninja errors, the final summary.
[[nodiscard]]
auto parseProgressLine(std::string_view line) -> std::optional<ProgressLine>;

/// Best-effort extraction of the produced path from a rule description.
/// Descriptions are the generator's wording; unrecognised ones give "".
[[nodiscard]]
auto outputPathFromDescription(std::string_view description) -> std::string;

[[nodiscard]]
auto stepKindFromDescription(std::string_view description) -> StepKind;

/// Splits an arbitrarily chunked stdout stream into lines. Handles both '\n'
/// and '\r' terminators, since ninja uses carriage returns when it thinks it
/// is talking to a terminal.
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
