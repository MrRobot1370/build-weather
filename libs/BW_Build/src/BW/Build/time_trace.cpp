#include "BW/Build/time_trace.h"

#include "json_reader.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace BW::Build
{

namespace {

/// One complete event, reduced to the fields we use.
struct TraceEvent
{
    std::string name;
    std::string detail;
    std::string phase; ///< "X" complete, "b"/"e" async pair, "M" metadata
    std::int64_t tid { 0 };
    Micros ts { 0 };
    Micros dur { 0 };
    std::int64_t count { 0 }; ///< only on "Total ..." events
    bool isTotal { false };
};

/// An async event that has begun but not yet ended.
struct OpenEvent
{
    std::string name;
    std::string detail;
    Micros ts { 0 };
};

auto startsWith(std::string_view text, std::string_view prefix) -> bool
{
    return text.size() >= prefix.size()
        && text.compare(0, prefix.size(), prefix) == 0;
}

/// Reads the `args` object, keeping only `detail`, `count` and `name`.
auto readArgs(Json::Reader &reader, TraceEvent &event, std::string &scratch)
    -> bool
{
    if (!reader.beginObject()) {
        return reader.skipValue();
    }
    if (reader.endObject()) {
        return true;
    }
    do {
        if (!reader.readString(scratch) || !reader.consume(':')) {
            return false;
        }
        if (scratch == "detail") {
            if (!reader.readString(event.detail)) {
                return false;
            }
        }
        else if (scratch == "count") {
            std::int64_t value = 0;
            if (!reader.readInteger(value)) {
                return false;
            }
            event.count = value;
        }
        else if (scratch == "name" && event.detail.empty()) {
            // Metadata events (process_labels) carry the source file here.
            if (!reader.readString(event.detail)) {
                return false;
            }
        }
        else if (!reader.skipValue()) {
            return false;
        }
    } while (reader.comma());
    return reader.endObject();
}

/// Reads one event object. Returns false on malformed input.
auto readEvent(Json::Reader &reader, TraceEvent &event, std::string &scratch)
    -> bool
{
    event = TraceEvent {};
    if (!reader.beginObject()) {
        return false;
    }
    if (reader.endObject()) {
        return true;
    }
    do {
        if (!reader.readString(scratch) || !reader.consume(':')) {
            return false;
        }
        if (scratch == "name") {
            if (!reader.readString(event.name)) {
                return false;
            }
        }
        else if (scratch == "ts") {
            if (!reader.readInteger(event.ts)) {
                return false;
            }
        }
        else if (scratch == "dur") {
            if (!reader.readInteger(event.dur)) {
                return false;
            }
        }
        else if (scratch == "tid") {
            if (!reader.readInteger(event.tid)) {
                return false;
            }
        }
        else if (scratch == "ph") {
            if (!reader.readString(event.phase)) {
                return false;
            }
        }
        else if (scratch == "args") {
            if (!readArgs(reader, event, scratch)) {
                return false;
            }
        }
        else if (!reader.skipValue()) {
            return false;
        }
    } while (reader.comma());

    if (!reader.endObject()) {
        return false;
    }
    event.isTotal = startsWith(event.name, "Total ");
    return true;
}

/// Strips the quotes clang wraps around the source file in `process_labels`.
auto unquote(std::string text) -> std::string
{
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

struct SourceAccumulator
{
    Micros totalUs { 0 };
    Micros selfUs { 0 };
    int occurrences { 0 };
};

struct TemplateAccumulator
{
    Micros totalUs { 0 };
    int count { 0 };
    bool isClass { false };
};

}

auto parseTimeTrace(std::string_view json, std::string_view tracePath)
    -> TimeTraceUnit
{
    TimeTraceUnit unit;
    unit.tracePath = Core::normalizePath(tracePath);
    if (!unit.tracePath.empty()) {
        const std::string ext = Core::extension(unit.tracePath);
        if (ext == ".json") {
            const std::string stem
                = unit.tracePath.substr(0, unit.tracePath.size() - 5);
            unit.objectPath = stem + ".obj";
            // GUESS: `-ftime-trace` names the file after the object, which
            // for CMake is "<source name>.obj", so the stem is the source
            // file name. Only used when the trace does not name the source.
            unit.source = Core::fileName(stem);
        }
    }

    Json::Reader reader { json };
    if (!reader.beginObject()) {
        unit.diagnostics.push_back(
            { ParseDiagnostic::Severity::Error, 0, "not a JSON object" });
        return unit;
    }

    bool sawTraceEvents = false;
    std::string key;
    std::string scratch;

    // clang appends each event when its scope ends, so a child is written
    // before its parent and the document is not in pre-order. Self time
    // cannot be computed in one pass: the Source events are retained and
    // sorted into pre-order afterwards, everything else accumulates directly.
    struct SourceEvent
    {
        Micros ts { 0 };
        Micros dur { 0 };
        std::int64_t tid { 0 };
        std::size_t pathIndex { 0 };
    };
    std::vector<SourceEvent> sourceEvents;
    std::vector<std::string> pathArena;
    std::unordered_map<std::string, std::size_t> pathIds;

    std::unordered_map<std::string, TemplateAccumulator> templates;

    Micros summedFrontend = 0;
    Micros summedBackend = 0;
    Micros totalFrontend = -1;
    Micros totalBackend = -1;

    if (!reader.endObject()) {
        do {
            if (!reader.readString(key) || !reader.consume(':')) {
                unit.diagnostics.push_back(
                    { ParseDiagnostic::Severity::Error,
                      0,
                      "malformed top-level member" });
                return unit;
            }
            if (key != "traceEvents") {
                if (!reader.skipValue()) {
                    return unit;
                }
                continue;
            }

            sawTraceEvents = true;
            if (!reader.beginArray()) {
                unit.diagnostics.push_back(
                    { ParseDiagnostic::Severity::Error,
                      0,
                      "traceEvents is not an array" });
                return unit;
            }
            if (reader.endArray()) {
                continue;
            }

            // Everything that has a name, a start and a duration ends up
            // here, whichever encoding it arrived in.
            const auto handleComplete = [&](const std::string &name,
                                            const std::string &detail,
                                            std::int64_t tid,
                                            Micros ts,
                                            Micros dur) {
                if (dur <= 0 && name != "ExecuteCompiler") {
                    return;
                }
                if (name == "Source") {
                    if (detail.empty()) {
                        return;
                    }
                    auto [it, inserted] = pathIds.emplace(
                        Core::normalizePath(detail),
                        pathArena.size());
                    if (inserted) {
                        pathArena.push_back(it->first);
                    }
                    sourceEvents.push_back({ ts, dur, tid, it->second });
                }
                else if (
                    name == "InstantiateFunction"
                    || name == "InstantiateClass") {
                    if (!detail.empty()) {
                        auto &entry = templates[detail];
                        entry.totalUs += dur;
                        entry.count += 1;
                        entry.isClass = name == "InstantiateClass";
                    }
                }
                else if (name == "Frontend") {
                    summedFrontend += dur;
                }
                else if (name == "Backend") {
                    summedBackend += dur;
                }
                else if (name == "ExecuteCompiler") {
                    unit.totalUs = std::max(unit.totalUs, dur);
                }
            };

            // Both encodings are handled: up to clang 14 every event is a
            // complete "X" with a dur, clang 15 and later emit the include
            // tree as async "b"/"e" pairs (all id 0, so they match LIFO per
            // thread) and keep "X" for the rest.
            std::unordered_map<std::int64_t, std::vector<OpenEvent>> open;

            TraceEvent event;
            do {
                if (!readEvent(reader, event, scratch)) {
                    unit.diagnostics.push_back(
                        { ParseDiagnostic::Severity::Warning,
                          0,
                          "stopped at a malformed trace event" });
                    break;
                }

                if (event.phase == "M" || event.name == "process_labels"
                    || event.name == "process_name") {
                    if (event.name == "process_labels"
                        && !event.detail.empty()) {
                        unit.source
                            = Core::normalizePath(unquote(event.detail));
                    }
                    continue;
                }

                if (event.isTotal) {
                    const std::string_view what
                        = std::string_view { event.name }.substr(6);
                    if (what == "Frontend") {
                        totalFrontend = event.dur;
                    }
                    else if (what == "Backend") {
                        totalBackend = event.dur;
                    }
                    else if (what == "ExecuteCompiler" && unit.totalUs == 0) {
                        unit.totalUs = event.dur;
                    }
                    continue;
                }

                if (event.phase == "b") {
                    open[event.tid].push_back(
                        { event.name, event.detail, event.ts });
                    continue;
                }
                if (event.phase == "e") {
                    auto &stack = open[event.tid];
                    // Ids are all zero, so pair by name, innermost first.
                    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                        if (it->name != event.name) {
                            continue;
                        }
                        handleComplete(
                            it->name,
                            it->detail,
                            event.tid,
                            it->ts,
                            event.ts - it->ts);
                        stack.erase(std::next(it).base());
                        break;
                    }
                    continue;
                }

                handleComplete(
                    event.name,
                    event.detail,
                    event.tid,
                    event.ts,
                    event.dur);
            } while (reader.comma());

            if (!reader.endArray()) {
                unit.diagnostics.push_back(
                    { ParseDiagnostic::Severity::Warning,
                      0,
                      "traceEvents array was truncated" });
            }
        } while (reader.comma());
    }

    if (!sawTraceEvents) {
        unit.diagnostics.push_back(
            { ParseDiagnostic::Severity::Error,
              0,
              "no traceEvents member; not a -ftime-trace document" });
        return unit;
    }

    unit.frontendUs = totalFrontend >= 0 ? totalFrontend : summedFrontend;
    unit.backendUs = totalBackend >= 0 ? totalBackend : summedBackend;
    if (unit.totalUs == 0) {
        unit.totalUs = unit.frontendUs + unit.backendUs;
    }

    // Pre-order: outermost include first, so a stack walk gives the parent of
    // every nested Source event and self time is total minus direct children.
    std::sort(
        sourceEvents.begin(),
        sourceEvents.end(),
        [](const SourceEvent &a, const SourceEvent &b) {
            if (a.tid != b.tid) {
                return a.tid < b.tid;
            }
            if (a.ts != b.ts) {
                return a.ts < b.ts;
            }
            return a.dur > b.dur;
        });

    std::vector<SourceAccumulator> accumulators(pathArena.size());
    struct Frame
    {
        Micros endTs { 0 };
        std::size_t pathIndex { 0 };
    };
    std::vector<Frame> stack;
    std::int64_t currentTid = sourceEvents.empty() ? 0 : sourceEvents[0].tid;

    for (const auto &event : sourceEvents) {
        if (event.tid != currentTid) {
            stack.clear();
            currentTid = event.tid;
        }
        while (!stack.empty() && stack.back().endTs <= event.ts) {
            stack.pop_back();
        }
        if (!stack.empty()) {
            accumulators[stack.back().pathIndex].selfUs -= event.dur;
        }
        auto &entry = accumulators[event.pathIndex];
        entry.totalUs += event.dur;
        entry.selfUs += event.dur;
        entry.occurrences += 1;
        stack.push_back({ event.ts + event.dur, event.pathIndex });
    }

    unit.sources.reserve(pathArena.size());
    for (std::size_t i = 0; i < pathArena.size(); ++i) {
        const auto &entry = accumulators[i];
        unit.sources.push_back(
            { pathArena[i],
              entry.totalUs,
              std::max<Micros>(entry.selfUs, 0),
              entry.occurrences });
    }
    std::sort(
        unit.sources.begin(),
        unit.sources.end(),
        [](const SourceCost &a, const SourceCost &b) {
            return a.totalUs != b.totalUs ? a.totalUs > b.totalUs
                                          : a.path < b.path;
        });

    unit.instantiations.reserve(templates.size());
    for (auto &[name, entry] : templates) {
        unit.instantiations.push_back(
            { name, entry.totalUs, entry.count, entry.isClass });
    }
    std::sort(
        unit.instantiations.begin(),
        unit.instantiations.end(),
        [](const TemplateCost &a, const TemplateCost &b) {
            return a.totalUs != b.totalUs ? a.totalUs > b.totalUs
                                          : a.name < b.name;
        });

    return unit;
}

auto readTimeTrace(const std::string &path, std::string &error)
    -> std::optional<TimeTraceUnit>
{
    std::ifstream in { path, std::ios::binary };
    if (!in) {
        error = "cannot open " + path;
        return std::nullopt;
    }
    const std::string content { std::istreambuf_iterator<char> { in },
                                std::istreambuf_iterator<char> {} };
    if (content.empty()) {
        error = path + " is empty";
        return std::nullopt;
    }
    error.clear();
    return parseTimeTrace(content, path);
}

auto findTimeTraceFiles(const std::string &root) -> std::vector<std::string>
{
    namespace fs = std::filesystem;
    std::vector<std::string> out;

    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        return out;
    }

    fs::recursive_directory_iterator it {
        root,
        fs::directory_options::skip_permission_denied,
        ec
    };
    const fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec)) {
            continue;
        }
        const fs::path &p = it->path();
        if (p.extension() != ".json") {
            continue;
        }
        // Skip the obvious non-traces sitting in a build tree.
        const std::string name = p.filename().string();
        if (name == "compile_commands.json"
            || name == "CMakePresets.json"
            || name == "CMakeUserPresets.json") {
            continue;
        }
        out.push_back(Core::normalizePath(p.string()));
    }

    std::sort(out.begin(), out.end());
    return out;
}

void TraceAggregate::clear()
{
    m_units.clear();
    m_headers.clear();
    m_templates.clear();
    m_frontendUs = 0;
    m_backendUs = 0;
    m_totalUs = 0;
    m_finalized = false;
}

void TraceAggregate::add(TimeTraceUnit unit)
{
    m_frontendUs += unit.frontendUs;
    m_backendUs += unit.backendUs;
    m_totalUs += unit.totalUs;
    m_units.push_back(std::move(unit));
    m_finalized = false;
}

void TraceAggregate::finalize()
{
    if (m_finalized) {
        return;
    }

    std::unordered_map<std::string, std::size_t> headerIndex;
    std::unordered_map<std::string, std::size_t> templateIndex;
    m_headers.clear();
    m_templates.clear();

    for (const auto &unit : m_units) {
        for (const auto &source : unit.sources) {
            const std::string key = Core::pathKey(source.path);
            auto [it, inserted]
                = headerIndex.emplace(key, m_headers.size());
            if (inserted) {
                m_headers.push_back({ source.path, 0, 0, 0 });
            }
            auto &agg = m_headers[it->second];
            agg.totalUs += source.totalUs;
            agg.selfUs += source.selfUs;
            agg.tuCount += 1;
        }
        for (const auto &tpl : unit.instantiations) {
            auto [it, inserted]
                = templateIndex.emplace(tpl.name, m_templates.size());
            if (inserted) {
                m_templates.push_back({ tpl.name, 0, 0, 0, tpl.isClass });
            }
            auto &agg = m_templates[it->second];
            agg.totalUs += tpl.totalUs;
            agg.count += tpl.count;
            agg.tuCount += 1;
        }
    }

    std::sort(
        m_headers.begin(),
        m_headers.end(),
        [](const HeaderAggregate &a, const HeaderAggregate &b) {
            return a.totalUs != b.totalUs ? a.totalUs > b.totalUs
                                          : a.path < b.path;
        });
    std::sort(
        m_templates.begin(),
        m_templates.end(),
        [](const TemplateAggregate &a, const TemplateAggregate &b) {
            return a.totalUs != b.totalUs ? a.totalUs > b.totalUs
                                          : a.name < b.name;
        });
    m_finalized = true;
}

auto TraceAggregate::unitsIncluding(std::string_view headerPath) const
    -> std::vector<std::pair<std::size_t, Micros>>
{
    const std::string key = Core::pathKey(headerPath);
    std::vector<std::pair<std::size_t, Micros>> out;
    for (std::size_t i = 0; i < m_units.size(); ++i) {
        for (const auto &source : m_units[i].sources) {
            if (Core::pathKey(source.path) == key) {
                out.emplace_back(i, source.totalUs);
                break;
            }
        }
    }
    std::sort(
        out.begin(),
        out.end(),
        [](const auto &a, const auto &b) { return a.second > b.second; });
    return out;
}

auto loadTraceDirectory(
    const std::string &root,
    const TraceProgressFn &progress) -> TraceAggregate
{
    TraceAggregate aggregate;
    const auto files = findTimeTraceFiles(root);

    for (std::size_t i = 0; i < files.size(); ++i) {
        if (progress && !progress(i, files.size())) {
            break;
        }
        std::string error;
        if (auto unit = readTimeTrace(files[i], error)) {
            if (unit->ok()) {
                aggregate.add(std::move(*unit));
            }
        }
    }
    if (progress) {
        progress(files.size(), files.size());
    }
    aggregate.finalize();
    return aggregate;
}

}
