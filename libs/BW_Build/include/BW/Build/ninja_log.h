#pragma once

// One tab-separated record per line after the `# ninja log vN` header:
//
//     start_ms <TAB> end_ms <TAB> mtime <TAB> output_path <TAB> command_hash
//
// The file accumulates across invocations, and timestamps are relative to the
// start of their own invocation, so a log spanning several builds has no
// single timeline.

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

    /// One record per output, keeping the last entry, sorted by output. This
    /// is what a full build costs.
    [[nodiscard]]
    auto latestPerOutput() const -> std::vector<TargetRecord>;

    /// The trailing run of entries with no repeated output, that is, the most
    /// recent invocation. Over-reaches into the previous build only when two
    /// consecutive builds touched disjoint file sets.
    [[nodiscard]]
    auto lastInvocationEntries() const -> std::vector<TargetRecord>;

    /// True when some output was recorded more than once, so the log spans
    /// several builds and several clocks.
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
/// edge, so polling the tail is a completion event stream.
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

    /// Skips whatever is already in the file. Call right before launching
    /// ninja.
    void seekToEnd();

    void reset();

    /// Records appended since the previous poll. A shrinking file (ninja
    /// recompacted or removed the log) resets the offset and is reported
    /// through `restarted`.
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
