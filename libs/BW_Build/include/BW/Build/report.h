#pragma once

#include "BW/Build/build_snapshot.h"
#include "BW/Build/time_trace.h"

#include <string>

namespace BW::Build
{

struct ReportOptions
{
    int topTargets { 50 };
    int topHeaders { 50 };
    int topTemplates { 50 };
    /// Included verbatim in the JSON so a report can be traced back.
    std::string buildDirectory;
    std::string sourceDirectory;
};

/// One JSON document with the stats, the slowest steps and, when a trace set
/// was loaded, the header and template rankings.
[[nodiscard]]
auto exportJson(
    const BuildSnapshot &snapshot,
    const TraceAggregate *traces,
    const ReportOptions &options) -> std::string;

/// One row per build step: rank, tree path, duration, kind, bucket.
[[nodiscard]]
auto exportTargetsCsv(const BuildSnapshot &snapshot) -> std::string;

/// One row per header: rank, path, total ms, self ms, TU count, average ms.
[[nodiscard]]
auto exportHeadersCsv(const TraceAggregate &traces, int topN) -> std::string;

/// One row per template entity: rank, name, total ms, instantiations, TUs.
[[nodiscard]]
auto exportTemplatesCsv(const TraceAggregate &traces, int topN) -> std::string;

/// One row per changed step: path, baseline ms, current ms, delta ms, state.
[[nodiscard]]
auto exportDeltaCsv(const std::vector<TargetDelta> &deltas) -> std::string;

}
