#pragma once

// The domain model the UI draws: a set of build steps, each already resolved
// to a place in the source tree, plus the event stream needed to replay the
// build.
//
// This is where the three data sources are joined, and every join goes
// through Core::pathKey(). A step's tree path comes from its *source* file
// when one can be resolved, so a translation unit appears where the developer
// expects it, not under CMakeFiles/.

#include "BW/Build/compile_commands.h"
#include "BW/Build/ninja_log.h"

#include <string>
#include <vector>

namespace BW::Build
{

struct TargetInfo
{
    std::string treePath; ///< bucketed display path, the treemap key
    std::string output; ///< ninja output, relative to the build dir
    std::string source; ///< resolved source file, empty when unknown
    Millis startMs { 0 };
    Millis endMs { 0 };
    Millis durationMs { 0 };
    Core::PathBucket bucket { Core::PathBucket::SourceTree };
    StepKind kind { StepKind::Other };
    int rank { 0 }; ///< 1 = slowest entry in the snapshot
    /// Build steps merged into this entry. Greater than one when several
    /// steps land on the same file, most often a generated source that is
    /// first written and then compiled; the durations are summed, which is
    /// what "what does this file cost me" means.
    int stepCount { 1 };
};

struct SnapshotStats
{
    std::size_t targetCount { 0 };
    Millis totalCpuMs { 0 }; ///< sum of every step's duration
    Millis wallMs { 0 }; ///< span from first start to last finish
    Millis maxMs { 0 };
    Millis medianMs { 0 };
    int peakParallelism { 0 };

    [[nodiscard]]
    auto averageParallelism() const -> double
    {
        return wallMs > 0 ? static_cast<double>(totalCpuMs)
                / static_cast<double>(wallMs)
                          : 0.0;
    }
};

/// Which entries of a multi-build log to model.
enum class LogScope
{
    /// Latest entry per output across the whole log. The right choice for the
    /// heat map: it answers "what does a full build of this tree cost".
    LatestPerOutput,
    /// Only the most recent ninja invocation, found by scanning backwards
    /// until an output repeats. The right choice for replay, because
    /// timestamps are only comparable inside one invocation.
    LastInvocation
};

struct SnapshotOptions
{
    Core::PathClassifier classifier;
    const CompileCommands *commands { nullptr };
    LogScope scope { LogScope::LatestPerOutput };
};

class BuildSnapshot
{
public:
    BuildSnapshot() = default;

    [[nodiscard]]
    static auto fromNinjaLog(const NinjaLog &log, const SnapshotOptions &opts)
        -> BuildSnapshot;

    /// Builds a snapshot from records gathered live. Same resolution rules.
    [[nodiscard]]
    static auto fromRecords(
        const std::vector<TargetRecord> &records,
        const SnapshotOptions &opts) -> BuildSnapshot;

    [[nodiscard]]
    auto targets() const -> const std::vector<TargetInfo> &
    {
        return m_targets;
    }

    [[nodiscard]]
    auto stats() const -> const SnapshotStats &
    {
        return m_stats;
    }

    [[nodiscard]]
    auto empty() const -> bool
    {
        return m_targets.empty();
    }

    [[nodiscard]]
    auto label() const -> const std::string &
    {
        return m_label;
    }

    void setLabel(std::string label)
    {
        m_label = std::move(label);
    }

    /// Index of the step with this tree path, or -1.
    [[nodiscard]]
    auto indexOfTreePath(std::string_view treePath) const -> int;

    /// Start and finish moments in chronological order. Only meaningful when
    /// the snapshot came from one invocation (LogScope::LastInvocation)
    /// or from a live capture.
    [[nodiscard]]
    auto timeline() const -> const std::vector<BuildEvent> &
    {
        return m_timeline;
    }

    /// Number of steps in flight at `timeMs`.
    [[nodiscard]]
    auto parallelismAt(Millis timeMs) const -> int;

private:
    void finalize();

    std::vector<TargetInfo> m_targets;
    std::vector<BuildEvent> m_timeline;
    SnapshotStats m_stats;
    std::string m_label;
};

// --- comparison -------------------------------------------------------------

struct TargetDelta
{
    enum class State
    {
        Changed,
        Added, ///< present only in the current build
        Removed ///< present only in the baseline
    };

    std::string treePath;
    Millis baselineMs { 0 };
    Millis currentMs { 0 };
    Millis deltaMs { 0 };
    State state { State::Changed };

    [[nodiscard]]
    auto ratio() const -> double
    {
        return baselineMs > 0 ? static_cast<double>(currentMs)
                / static_cast<double>(baselineMs)
                              : 0.0;
    }
};

/// Per-step differences, sorted by the size of the regression (worst first).
[[nodiscard]]
auto compareSnapshots(
    const BuildSnapshot &baseline,
    const BuildSnapshot &current) -> std::vector<TargetDelta>;

}
