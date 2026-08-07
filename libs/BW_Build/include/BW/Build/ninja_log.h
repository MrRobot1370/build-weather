#pragma once

// `.ninja_log` reader.
//
// Format (after the `# ninja log vN` header), one tab-separated record per
// line, appended by ninja as each edge finishes:
//
//     start_ms <TAB> end_ms <TAB> mtime <TAB> output_path <TAB> command_hash
//
// Two properties matter and are handled here:
//
//  * The file accumulates across invocations. A later entry for the same
//    output supersedes the earlier one, so post-mortem durations take the
//    last entry per output.
//  * Timestamps are relative to the start of *their own* ninja invocation, so
//    a log spanning several builds has no single timeline.
//
// Splitting the file back into invocations is the awkward part, and the
// obvious approach does not work: `end_ms` is NOT monotonic within a run.
// Measured against ninja 1.12 it goes backwards constantly (a 7 s edge is
// recorded before 350 ms ones), and `mtime` is no better because copy edges
// record the *source* file's timestamp. Both were tried and both reported
// dozens of phantom sessions in a log containing four real builds.
//
// What ninja does guarantee is that an output is built at most once per
// invocation. So the last invocation is the trailing run of entries with no
// repeated output, found by scanning backwards until an output repeats. That
// is exact unless two consecutive builds touched disjoint file sets, in which
// case it over-reaches into the earlier one; lastInvocationEntries() says so
// rather than pretending otherwise.

#include "BW/Build/build_types.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BW::Build
{

struct NinjaLog
{
    int version { 0 };
    /// Every parsed record in file order, duplicates included.
    std::vector<TargetRecord> entries;
    std::vector<ParseDiagnostic> diagnostics;

    [[nodiscard]]
    auto hasError() const -> bool;

    /// One record per output, keeping the last (most recent) entry. Sorted by
    /// output for determinism. Exact: this is what a full build costs.
    [[nodiscard]]
    auto latestPerOutput() const -> std::vector<TargetRecord>;

    /// The trailing run of entries with no repeated output, that is, the most
    /// recent ninja invocation. See the note at the top of this file for what
    /// this can and cannot promise.
    [[nodiscard]]
    auto lastInvocationEntries() const -> std::vector<TargetRecord>;

    /// True when some output was recorded more than once, which means the log
    /// accumulated over several builds and its timestamps span several
    /// clocks. The one thing about sessions that *can* be answered exactly.
    [[nodiscard]]
    auto spansMultipleBuilds() const -> bool;
};

/// Parses the contents of a `.ninja_log`. Never throws; malformed lines are
/// skipped and reported through NinjaLog::diagnostics.
[[nodiscard]]
auto parseNinjaLog(std::string_view content) -> NinjaLog;

/// Reads and parses a `.ninja_log` from disk. Returns nullopt and fills
/// `error` when the file cannot be read.
[[nodiscard]]
auto readNinjaLog(const std::string &path, std::string &error)
    -> std::optional<NinjaLog>;

/// Incremental reader for live mode: ninja appends one line per finished
/// edge, so polling the tail turns the log into a completion event stream.
class NinjaLogTail
{
public:
    NinjaLogTail() = default;
    explicit NinjaLogTail(std::string path);

    void setPath(std::string path);

    [[nodiscard]]
    auto path() const -> const std::string &
    {
        return m_path;
    }

    /// Skips whatever is already in the file, so only entries written after
    /// this call are reported. Call right before launching ninja.
    void seekToEnd();

    /// Rewinds to the beginning of the file.
    void reset();

    /// Returns records appended since the previous poll. A shrinking file
    /// (ninja recompacted or removed the log) resets the offset and is
    /// reported through `restarted`.
    [[nodiscard]]
    auto poll(bool *restarted = nullptr) -> std::vector<TargetRecord>;

private:
    std::string m_path;
    std::uint64_t m_offset { 0 };
    std::string m_pending; ///< partial trailing line
};

/// Parses a single record line. Exposed for the tail reader and for tests.
[[nodiscard]]
auto parseNinjaLogLine(std::string_view line) -> std::optional<TargetRecord>;

}
