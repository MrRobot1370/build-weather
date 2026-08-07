// bw_cli - headless Build Weather.
//
//   bw_cli analyze <build-dir> [--source <dir>] [--traces <dir>]
//                  [--json <file>] [--csv <file>] [--top N]
//   bw_cli compare <baseline .ninja_log> <current .ninja_log>
//                  [--source <dir>] [--build <dir>] [--csv <file>]
//
// Same libraries as the GUI, no Qt, so it doubles as the post-build sanity
// check: if this prints sensible numbers the parsers are wired up correctly.

#include "BW/Build/build_snapshot.h"
#include "BW/Build/compile_commands.h"
#include "BW/Build/ninja_log.h"
#include "BW/Build/report.h"
#include "BW/Build/time_trace.h"
#include "BW/Core/path_utils.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace BW;

struct Args
{
    std::string command;
    std::vector<std::string> positional;
    std::string sourceDir;
    std::string buildDir;
    std::string tracesDir;
    std::string jsonOut;
    std::string csvOut;
    int top { 20 };
    bool help { false };
};

auto parseArgs(int argc, char **argv) -> Args
{
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string arg { argv[i] };
        const auto next = [&]() -> std::string {
            return i + 1 < argc ? std::string { argv[++i] } : std::string {};
        };
        if (arg == "-h" || arg == "--help") {
            args.help = true;
        }
        else if (arg == "--source") {
            args.sourceDir = next();
        }
        else if (arg == "--build") {
            args.buildDir = next();
        }
        else if (arg == "--traces") {
            args.tracesDir = next();
        }
        else if (arg == "--json") {
            args.jsonOut = next();
        }
        else if (arg == "--csv") {
            args.csvOut = next();
        }
        else if (arg == "--top") {
            args.top = std::max(1, std::atoi(next().c_str()));
        }
        else if (args.command.empty()) {
            args.command = arg;
        }
        else {
            args.positional.push_back(arg);
        }
    }
    return args;
}

void usage()
{
    std::cout
        << "bw_cli - headless Build Weather\n\n"
        << "  bw_cli analyze <build-dir> [--source <dir>] [--traces <dir>]\n"
        << "                 [--json <file>] [--csv <file>] [--top N]\n"
        << "  bw_cli compare <baseline.ninja_log> <current.ninja_log>\n"
        << "                 [--source <dir>] [--build <dir>] [--csv <f>]\n";
}

auto writeFile(const std::string &path, const std::string &content) -> bool
{
    std::ofstream out { path, std::ios::binary };
    if (!out) {
        std::cerr << "cannot write " << path << "\n";
        return false;
    }
    out << content;
    return true;
}

auto formatMs(Build::Millis ms) -> std::string
{
    if (ms >= 60000) {
        const long long minutes = ms / 60000;
        const double seconds = static_cast<double>(ms % 60000) / 1000.0;
        char buffer[64] {};
        std::snprintf(
            buffer,
            sizeof buffer,
            "%lldm %.1fs",
            minutes,
            seconds);
        return buffer;
    }
    if (ms >= 1000) {
        char buffer[32] {};
        std::snprintf(
            buffer,
            sizeof buffer,
            "%.2fs",
            static_cast<double>(ms) / 1000.0);
        return buffer;
    }
    return std::to_string(ms) + "ms";
}

auto runAnalyze(const Args &args) -> int
{
    if (args.positional.empty() && args.buildDir.empty()) {
        usage();
        return 2;
    }
    const std::string buildDir = Core::normalizePath(
        args.buildDir.empty() ? args.positional.front() : args.buildDir);
    const std::string sourceDir = args.sourceDir.empty()
        ? Core::normalizePath(buildDir + "/../..")
        : Core::normalizePath(args.sourceDir);

    std::string error;
    const std::string logPath = Core::joinPath(buildDir, ".ninja_log");
    const auto log = Build::readNinjaLog(logPath, error);
    if (!log) {
        std::cerr << error << "\n";
        return 1;
    }
    for (const auto &diagnostic : log->diagnostics) {
        std::cerr << "  note: " << diagnostic.message << "\n";
    }

    Build::CompileCommands commands;
    std::string commandsError;
    const bool haveCommands = commands.load(buildDir, commandsError);

    Build::SnapshotOptions options;
    options.classifier.setSourceRoot(sourceDir);
    options.classifier.setBuildRoot(buildDir);
    options.commands = haveCommands ? &commands : nullptr;
    options.scope = Build::LogScope::LatestPerOutput;

    Build::BuildSnapshot snapshot
        = Build::BuildSnapshot::fromNinjaLog(*log, options);
    snapshot.setLabel(buildDir);

    const auto &stats = snapshot.stats();
    std::cout << "build directory : " << buildDir << "\n"
              << "source directory: " << sourceDir << "\n"
              << "compile database: "
              << (haveCommands
                      ? std::to_string(commands.entries().size()) + " entries"
                      : "not found (" + commandsError + ")")
              << "\n"
              << "log covers      : "
              << (log->spansMultipleBuilds() ? "several builds"
                                             : "a single build")
              << "\n"
              << "steps           : " << stats.targetCount << "\n"
              << "total CPU time  : " << formatMs(stats.totalCpuMs) << "\n"
              << "wall time       : " << formatMs(stats.wallMs) << "\n"
              << "peak parallelism: " << stats.peakParallelism << "\n"
              << "median step     : " << formatMs(stats.medianMs) << "\n\n";

    std::vector<const Build::TargetInfo *> byRank;
    for (const auto &target : snapshot.targets()) {
        byRank.push_back(&target);
    }
    std::sort(
        byRank.begin(),
        byRank.end(),
        [](const Build::TargetInfo *a, const Build::TargetInfo *b) {
            return a->rank < b->rank;
        });

    std::cout << "slowest steps\n";
    const std::size_t limit = std::min<std::size_t>(
        byRank.size(),
        static_cast<std::size_t>(args.top));
    for (std::size_t i = 0; i < limit; ++i) {
        std::cout << "  " << std::setw(3) << (i + 1) << "  " << std::setw(9)
                  << formatMs(byRank[i]->durationMs) << "  "
                  << byRank[i]->treePath << "\n";
    }

    Build::TraceAggregate traces;
    const std::string tracesDir
        = args.tracesDir.empty() ? buildDir : args.tracesDir;
    traces = Build::loadTraceDirectory(tracesDir);
    if (!traces.units().empty()) {
        std::cout << "\ntime-trace: " << traces.units().size()
                  << " translation units, frontend "
                  << formatMs(traces.frontendUs() / 1000) << ", backend "
                  << formatMs(traces.backendUs() / 1000) << "\n\n"
                  << "most expensive headers (aggregate across all TUs)\n";
        const auto &headers = traces.headers();
        const std::size_t headerLimit = std::min<std::size_t>(
            headers.size(),
            static_cast<std::size_t>(args.top));
        for (std::size_t i = 0; i < headerLimit; ++i) {
            std::cout << "  " << std::setw(3) << (i + 1) << "  "
                      << std::setw(9)
                      << formatMs(headers[i].totalUs / 1000) << "  x"
                      << std::setw(5) << headers[i].tuCount << "  "
                      << headers[i].path << "\n";
        }
    }

    Build::ReportOptions reportOptions;
    reportOptions.topTargets = args.top;
    reportOptions.topHeaders = args.top;
    reportOptions.topTemplates = args.top;
    reportOptions.buildDirectory = buildDir;
    reportOptions.sourceDirectory = sourceDir;

    if (!args.jsonOut.empty()
        && !writeFile(
            args.jsonOut,
            Build::exportJson(
                snapshot,
                traces.units().empty() ? nullptr : &traces,
                reportOptions))) {
        return 1;
    }
    if (!args.csvOut.empty()
        && !writeFile(args.csvOut, Build::exportTargetsCsv(snapshot))) {
        return 1;
    }
    return 0;
}

auto runCompare(const Args &args) -> int
{
    if (args.positional.size() < 1) {
        usage();
        return 2;
    }
    const std::string baselinePath
        = args.command == "compare" && !args.positional.empty()
        ? args.positional[0]
        : std::string {};
    const std::string currentPath
        = args.positional.size() > 1 ? args.positional[1] : std::string {};
    if (baselinePath.empty() || currentPath.empty()) {
        usage();
        return 2;
    }

    std::string error;
    const auto baselineLog = Build::readNinjaLog(baselinePath, error);
    if (!baselineLog) {
        std::cerr << error << "\n";
        return 1;
    }
    const auto currentLog = Build::readNinjaLog(currentPath, error);
    if (!currentLog) {
        std::cerr << error << "\n";
        return 1;
    }

    Build::SnapshotOptions options;
    options.classifier.setSourceRoot(args.sourceDir);
    options.classifier.setBuildRoot(
        args.buildDir.empty() ? Core::parentPath(currentPath) : args.buildDir);

    const auto baseline
        = Build::BuildSnapshot::fromNinjaLog(*baselineLog, options);
    const auto current
        = Build::BuildSnapshot::fromNinjaLog(*currentLog, options);
    const auto deltas = Build::compareSnapshots(baseline, current);

    std::cout << "baseline total: " << formatMs(baseline.stats().totalCpuMs)
              << "\ncurrent total : " << formatMs(current.stats().totalCpuMs)
              << "\ndelta         : "
              << formatMs(
                     current.stats().totalCpuMs
                     - baseline.stats().totalCpuMs)
              << "\n\nlargest regressions\n";

    const std::size_t limit = std::min<std::size_t>(
        deltas.size(),
        static_cast<std::size_t>(args.top));
    for (std::size_t i = 0; i < limit; ++i) {
        std::cout << "  " << std::setw(10) << formatMs(deltas[i].deltaMs)
                  << "  " << deltas[i].treePath << "\n";
    }

    if (!args.csvOut.empty()
        && !writeFile(args.csvOut, Build::exportDeltaCsv(deltas))) {
        return 1;
    }
    return 0;
}

}

auto main(int argc, char **argv) -> int
{
    const Args args = parseArgs(argc, argv);
    if (args.help || args.command.empty()) {
        usage();
        return args.help ? 0 : 2;
    }
    if (args.command == "analyze") {
        return runAnalyze(args);
    }
    if (args.command == "compare") {
        return runCompare(args);
    }
    std::cerr << "unknown command: " << args.command << "\n";
    usage();
    return 2;
}
