#include "BW/Build/report.h"

#include <algorithm>
#include <sstream>

namespace BW::Build
{

namespace {

auto jsonEscape(std::string_view text) -> std::string
{
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text) {
        switch (c) {
        case '"' :
            out += "\\\"";
            break;
        case '\\' :
            out += "\\\\";
            break;
        case '\n' :
            out += "\\n";
            break;
        case '\r' :
            out += "\\r";
            break;
        case '\t' :
            out += "\\t";
            break;
        default :
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[7] {};
                std::snprintf(
                    buffer,
                    sizeof buffer,
                    "\\u%04x",
                    static_cast<unsigned>(c));
                out += buffer;
            }
            else {
                out.push_back(c);
            }
        }
    }
    return out;
}

auto csvEscape(std::string_view text) -> std::string
{
    const bool needsQuotes
        = text.find_first_of(",\"\n\r") != std::string_view::npos;
    if (!needsQuotes) {
        return std::string { text };
    }
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"') {
            out += "\"\"";
        }
        else {
            out.push_back(c);
        }
    }
    out += '"';
    return out;
}

auto msFromUs(Micros us) -> double
{
    return static_cast<double>(us) / 1000.0;
}

auto quoted(std::string_view text) -> std::string
{
    return "\"" + jsonEscape(text) + "\"";
}

auto deltaStateName(TargetDelta::State state) -> const char *
{
    switch (state) {
    case TargetDelta::State::Added :
        return "added";
    case TargetDelta::State::Removed :
        return "removed";
    case TargetDelta::State::Changed :
        break;
    }
    return "changed";
}

}

auto exportJson(
    const BuildSnapshot &snapshot,
    const TraceAggregate *traces,
    const ReportOptions &options) -> std::string
{
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);

    const auto &stats = snapshot.stats();
    out << "{\n";
    out << "  \"schema\": \"build-weather/analysis/1\",\n";
    out << "  \"label\": " << quoted(snapshot.label()) << ",\n";
    out << "  \"sourceDirectory\": " << quoted(options.sourceDirectory)
        << ",\n";
    out << "  \"buildDirectory\": " << quoted(options.buildDirectory) << ",\n";
    out << "  \"stats\": {\n";
    out << "    \"targetCount\": " << stats.targetCount << ",\n";
    out << "    \"totalCpuMs\": " << stats.totalCpuMs << ",\n";
    out << "    \"wallMs\": " << stats.wallMs << ",\n";
    out << "    \"maxMs\": " << stats.maxMs << ",\n";
    out << "    \"medianMs\": " << stats.medianMs << ",\n";
    out << "    \"peakParallelism\": " << stats.peakParallelism << ",\n";
    out << "    \"averageParallelism\": " << stats.averageParallelism()
        << "\n";
    out << "  },\n";

    // Slowest translation units / steps.
    std::vector<const TargetInfo *> byRank;
    byRank.reserve(snapshot.targets().size());
    for (const auto &target : snapshot.targets()) {
        byRank.push_back(&target);
    }
    std::sort(
        byRank.begin(),
        byRank.end(),
        [](const TargetInfo *a, const TargetInfo *b) {
            return a->rank < b->rank;
        });

    const std::size_t targetLimit = std::min<std::size_t>(
        byRank.size(),
        static_cast<std::size_t>(std::max(options.topTargets, 0)));
    out << "  \"slowestTargets\": [\n";
    for (std::size_t i = 0; i < targetLimit; ++i) {
        const TargetInfo &target = *byRank[i];
        out << "    { \"rank\": " << target.rank << ", \"path\": "
            << quoted(target.treePath)
            << ", \"durationMs\": " << target.durationMs
            << ", \"kind\": " << quoted(stepKindName(target.kind))
            << ", \"bucket\": " << quoted(Core::bucketName(target.bucket))
            << ", \"output\": " << quoted(target.output) << " }"
            << (i + 1 < targetLimit ? "," : "") << "\n";
    }
    out << "  ]";

    if (traces != nullptr && !traces->units().empty()) {
        out << ",\n";
        out << "  \"timeTrace\": {\n";
        out << "    \"translationUnits\": " << traces->units().size() << ",\n";
        out << "    \"frontendMs\": " << msFromUs(traces->frontendUs())
            << ",\n";
        out << "    \"backendMs\": " << msFromUs(traces->backendUs()) << ",\n";
        out << "    \"totalMs\": " << msFromUs(traces->totalUs()) << ",\n";

        const auto &headers = traces->headers();
        const std::size_t headerLimit = std::min<std::size_t>(
            headers.size(),
            static_cast<std::size_t>(std::max(options.topHeaders, 0)));
        out << "    \"expensiveHeaders\": [\n";
        for (std::size_t i = 0; i < headerLimit; ++i) {
            const auto &header = headers[i];
            out << "      { \"rank\": " << (i + 1)
                << ", \"path\": " << quoted(header.path)
                << ", \"totalMs\": " << msFromUs(header.totalUs)
                << ", \"selfMs\": " << msFromUs(header.selfUs)
                << ", \"translationUnits\": " << header.tuCount
                << ", \"averageMs\": " << msFromUs(header.averageUs()) << " }"
                << (i + 1 < headerLimit ? "," : "") << "\n";
        }
        out << "    ],\n";

        const auto &templates = traces->templates();
        const std::size_t templateLimit = std::min<std::size_t>(
            templates.size(),
            static_cast<std::size_t>(std::max(options.topTemplates, 0)));
        out << "    \"expensiveTemplates\": [\n";
        for (std::size_t i = 0; i < templateLimit; ++i) {
            const auto &tpl = templates[i];
            out << "      { \"rank\": " << (i + 1)
                << ", \"name\": " << quoted(tpl.name)
                << ", \"totalMs\": " << msFromUs(tpl.totalUs)
                << ", \"instantiations\": " << tpl.count
                << ", \"translationUnits\": " << tpl.tuCount
                << ", \"isClass\": " << (tpl.isClass ? "true" : "false")
                << " }" << (i + 1 < templateLimit ? "," : "") << "\n";
        }
        out << "    ]\n";
        out << "  }";
    }

    out << "\n}\n";
    return out.str();
}

auto exportTargetsCsv(const BuildSnapshot &snapshot) -> std::string
{
    std::vector<const TargetInfo *> byRank;
    byRank.reserve(snapshot.targets().size());
    for (const auto &target : snapshot.targets()) {
        byRank.push_back(&target);
    }
    std::sort(
        byRank.begin(),
        byRank.end(),
        [](const TargetInfo *a, const TargetInfo *b) {
            return a->rank < b->rank;
        });

    std::ostringstream out;
    out << "rank,path,duration_ms,kind,bucket,output\r\n";
    for (const TargetInfo *target : byRank) {
        out << target->rank << ',' << csvEscape(target->treePath) << ','
            << target->durationMs << ',' << stepKindName(target->kind) << ','
            << Core::bucketName(target->bucket) << ','
            << csvEscape(target->output) << "\r\n";
    }
    return out.str();
}

auto exportHeadersCsv(const TraceAggregate &traces, int topN) -> std::string
{
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);
    out << "rank,header,total_ms,self_ms,translation_units,average_ms\r\n";

    const auto &headers = traces.headers();
    const std::size_t limit = std::min<std::size_t>(
        headers.size(),
        static_cast<std::size_t>(std::max(topN, 0)));
    for (std::size_t i = 0; i < limit; ++i) {
        const auto &header = headers[i];
        out << (i + 1) << ',' << csvEscape(header.path) << ','
            << msFromUs(header.totalUs) << ',' << msFromUs(header.selfUs)
            << ',' << header.tuCount << ',' << msFromUs(header.averageUs())
            << "\r\n";
    }
    return out.str();
}

auto exportTemplatesCsv(const TraceAggregate &traces, int topN) -> std::string
{
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);
    out << "rank,entity,total_ms,instantiations,translation_units,kind\r\n";

    const auto &templates = traces.templates();
    const std::size_t limit = std::min<std::size_t>(
        templates.size(),
        static_cast<std::size_t>(std::max(topN, 0)));
    for (std::size_t i = 0; i < limit; ++i) {
        const auto &tpl = templates[i];
        out << (i + 1) << ',' << csvEscape(tpl.name) << ','
            << msFromUs(tpl.totalUs) << ',' << tpl.count << ',' << tpl.tuCount
            << ',' << (tpl.isClass ? "class" : "function") << "\r\n";
    }
    return out.str();
}

auto exportDeltaCsv(const std::vector<TargetDelta> &deltas) -> std::string
{
    std::ostringstream out;
    out << "path,baseline_ms,current_ms,delta_ms,state\r\n";
    for (const auto &delta : deltas) {
        out << csvEscape(delta.treePath) << ',' << delta.baselineMs << ','
            << delta.currentMs << ',' << delta.deltaMs << ','
            << deltaStateName(delta.state) << "\r\n";
    }
    return out.str();
}

}
