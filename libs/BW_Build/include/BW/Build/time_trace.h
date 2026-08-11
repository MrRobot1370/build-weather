#pragma once

// clang-cl / clang -ftime-trace reader. The compiler writes a Chrome-tracing
// JSON next to each object file (foo.cpp.obj -> foo.cpp.json), holding events
// with a microsecond dur:
//
//   Source                 one per included header, args.detail = its path
//   InstantiateFunction    args.detail = the instantiated signature
//   InstantiateClass       args.detail = the instantiated class
//   ParseClass / CodeGen Function / OptFunction / RunPass / ...
//   Frontend, Backend      the two halves of the compilation
//   ExecuteCompiler        the whole invocation
//
// clang also emits pre-aggregated "Total <name>" events on a separate thread
// id with args.count. Those are preferred when present and recomputed by
// summation when they are not.
//
// Two encodings occur in the wild: up to clang 14 every event is a complete
// "X" with a dur, since clang 15 the include tree arrives as async "b"/"e"
// pairs (paired LIFO per thread, since every id is 0) while the rest stay
// "X". Parsing only "X" gives an empty header ranking on a current clang.
//
// Events on one thread are properly nested by (ts, dur), so self time is
// dur - sum(direct children).

#include "BW/Build/build_types.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BW::Build
{

/// Cost of one header inside one translation unit.
struct SourceCost
{
    std::string path; ///< normalized absolute path of the header
    Micros totalUs { 0 }; ///< including everything it pulls in
    Micros selfUs { 0 }; ///< excluding nested Source events
    int occurrences { 1 }; ///< times it was parsed in this TU
};

/// Cost of one template entity inside one translation unit.
struct TemplateCost
{
    std::string name; ///< instantiated signature or class name
    Micros totalUs { 0 };
    int count { 0 };
    bool isClass { false };
};

/// One parsed trace file, that is, one translation unit.
struct TimeTraceUnit
{
    std::string tracePath; ///< the .json we read
    std::string objectPath; ///< derived: tracePath with .json -> .obj
    std::string source; ///< main source file, when the trace names it

    Micros totalUs { 0 }; ///< ExecuteCompiler
    Micros frontendUs { 0 };
    Micros backendUs { 0 };

    std::vector<SourceCost> sources;
    std::vector<TemplateCost> instantiations;
    std::vector<ParseDiagnostic> diagnostics;

    [[nodiscard]]
    auto ok() const -> bool
    {
        return totalUs > 0 || !sources.empty();
    }
};

/// Parses one trace document. `tracePath` is only used to fill in
/// objectPath / source; the text alone is enough to parse.
[[nodiscard]]
auto parseTimeTrace(std::string_view json, std::string_view tracePath)
    -> TimeTraceUnit;

[[nodiscard]]
auto readTimeTrace(const std::string &path, std::string &error)
    -> std::optional<TimeTraceUnit>;

/// Recursively finds `*.json` files that look like time traces under `root`.
/// Cheap pre-filter only: the file is confirmed to be a trace when parsed.
[[nodiscard]]
auto findTimeTraceFiles(const std::string &root) -> std::vector<std::string>;

// cross-TU aggregation

/// A header ranked across the whole project. tuCount is the point: a 200 ms
/// header included 400 times costs more than one 8 s file.
struct HeaderAggregate
{
    std::string path;
    Micros totalUs { 0 };
    Micros selfUs { 0 };
    int tuCount { 0 };

    [[nodiscard]]
    auto averageUs() const -> Micros
    {
        return tuCount > 0 ? totalUs / tuCount : 0;
    }
};

struct TemplateAggregate
{
    std::string name;
    Micros totalUs { 0 };
    int count { 0 };
    int tuCount { 0 };
    bool isClass { false };
};

class TraceAggregate
{
public:
    void add(TimeTraceUnit unit);

    /// Sorts the ranked lists. Call once after all units are added.
    void finalize();

    [[nodiscard]]
    auto units() const -> const std::vector<TimeTraceUnit> &
    {
        return m_units;
    }

    /// Headers by aggregate cost across all TUs, most expensive first.
    [[nodiscard]]
    auto headers() const -> const std::vector<HeaderAggregate> &
    {
        return m_headers;
    }

    [[nodiscard]]
    auto templates() const -> const std::vector<TemplateAggregate> &
    {
        return m_templates;
    }

    [[nodiscard]]
    auto frontendUs() const -> Micros
    {
        return m_frontendUs;
    }

    [[nodiscard]]
    auto backendUs() const -> Micros
    {
        return m_backendUs;
    }

    [[nodiscard]]
    auto totalUs() const -> Micros
    {
        return m_totalUs;
    }

    /// Per-TU costs for a single header, keyed by translation unit index.
    [[nodiscard]]
    auto unitsIncluding(std::string_view headerPath) const
        -> std::vector<std::pair<std::size_t, Micros>>;

    void clear();

private:
    std::vector<TimeTraceUnit> m_units;
    std::vector<HeaderAggregate> m_headers;
    std::vector<TemplateAggregate> m_templates;
    Micros m_frontendUs { 0 };
    Micros m_backendUs { 0 };
    Micros m_totalUs { 0 };
    bool m_finalized { false };
};

/// Parses every trace under `root` and aggregates them. `progress` is called
/// with (done, total) and may return false to cancel; parsing happens on the
/// calling thread, so callers put this on a worker.
using TraceProgressFn = std::function<bool(std::size_t, std::size_t)>;

[[nodiscard]]
auto loadTraceDirectory(
    const std::string &root,
    const TraceProgressFn &progress = {}) -> TraceAggregate;

}
