// bw_cli - headless Build Weather.
//
//   bw_cli analyze <build-dir> [--source <dir>] [--traces <dir>]
//                  [--json <file>] [--csv <file>] [--top N]
//   bw_cli compare <baseline .ninja_log> <current .ninja_log>
//                  [--source <dir>] [--build <dir>] [--csv <file>]
//
// Same libraries as the GUI, no Qt, so it doubles as the post-build sanity
// check: if this prints sensible numbers the parsers are wired up correctly.
//
// Exit codes: 0 success, 1 an input could not be read or an output written,
// 2 the command line itself was wrong. CI scripts branch on these.

#include "BW/Build/build_snapshot.h"
#include "BW/Build/compile_commands.h"
#include "BW/Build/ninja_log.h"
#include "BW/Build/report.h"
#include "BW/Build/time_trace.h"
#include "BW/Core/path_utils.h"

#include <algorithm>
#include <charconv>
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
    bool version { false };
    /// Set when a flag was misused; the message is already on stderr.
    bool invalid { false };
};

auto parseArgs(int argc, char **argv) -> Args
{
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string arg { argv[i] };
        // A flag whose value is missing must not silently become an empty
        // string: --csv with nothing after it would then write no file and
        // still exit 0, which a CI script reads as success.
        const auto next = [&](std::string &out) -> bool {
            if (i + 1 >= argc) {
                std::cerr << arg << " needs a value\n";
                args.invalid = true;
                return false;
            }
            out = argv[++i];
            return true;
        };
        if (arg == "-h" || arg == "--help") {
            args.help = true;
        }
        else if (arg == "-v" || arg == "--version") {
            args.version = true;
        }
        else if (arg == "--source") {
            next(args.sourceDir);
        }
        else if (arg == "--build") {
            next(args.buildDir);
        }
        else if (arg == "--traces") {
            next(args.tracesDir);
        }
        else if (arg == "--json") {
            next(args.jsonOut);
        }
        else if (arg == "--csv") {
            next(args.csvOut);
        }
        else if (arg == "--top") {
            std::string value;
            if (next(value)) {
                const char *first = value.data();
                const char *last = value.data() + value.size();
                int parsed = 0;
                if (std::from_chars(first, last, parsed).ptr != last
                    || parsed < 1) {
                    std::cerr << "--top needs a positive whole number, got '"
                              << value << "'\n";
                    args.invalid = true;
                }
                else {
                    args.top = parsed;
                }
            }
        }
        else if (!arg.empty() && arg.front() == '-') {
            std::cerr << "unknown option: " << arg << "\n";
            args.invalid = true;
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

// Requested help goes to stdout, usage printed because the command line was
// wrong goes to stderr, so `bw_cli --help > usage.txt` works and a failing CI
// step does not have its diagnostic swallowed by a redirect.
void usage(std::ostream &to)
{
    to
        << "bw_cli " << BW_VERSION << " - headless Build Weather\n\n"
        << "  bw_cli analyze <build-dir> [--source <dir>] [--traces <dir>]\n"
        << "                 [--json <file>] [--csv <file>] [--top N]\n"
        << "  bw_cli compare <baseline.ninja_log> <current.ninja_log>\n"
        << "                 [--source <dir>] [--build <dir>] [--csv <f>]\n\n"
        << "  --source <dir>  source root, when it cannot be inferred\n"
        << "  --build <dir>   build root, for resolving relative log paths\n"
        << "  --traces <dir>  directory to scan for -ftime-trace documents\n"
        << "  --json <file>   write the full analysis as JSON\n"
        << "  --csv <file>    write every step (analyze) or delta (compare)\n"
        << "  --top N         rows to print and to put in --json (default 20;\n"
        << "                  --csv is never truncated)\n"
        << "  -v, --version   print the version and exit\n";
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

// Deltas are negative, so the magnitude picks the unit and the sign is put
// back afterwards; comparing on the raw value left every regression that got
// faster than a minute printed as a raw millisecond count.
auto formatMs(Build::Millis ms) -> std::string
{
    const char *sign = ms < 0 ? "-" : "";
    const auto magnitude = ms < 0 ? -ms : ms;

    if (magnitude >= 60000) {
        const long long minutes = magnitude / 60000;
        const double seconds = static_cast<double>(magnitude % 60000) / 1000.0;
        char buffer[64] {};
        std::snprintf(
            buffer,
            sizeof buffer,
            "%s%lldm %.1fs",
            sign,
            minutes,
            seconds);
        return buffer;
    }
    if (magnitude >= 1000) {
        char buffer[32] {};
        std::snprintf(
            buffer,
            sizeof buffer,
            "%s%.2fs",
            sign,
            static_cast<double>(magnitude) / 1000.0);
        return buffer;
    }
    return std::to_string(ms) + "ms";
}

auto runAnalyze(const Args &args) -> int
{
    if (args.positional.empty() && args.buildDir.empty()) {
        usage(std::cerr);
        return 2;
    }
    const std::string buildDir = Core::normalizePath(
        args.buildDir.empty() ? args.positional.front() : args.buildDir);

    std::string error;
    const std::string logPath = Core::joinPath(buildDir, ".ninja_log");
    const auto log = Build::readNinjaLog(logPath, error);
    if (!log) {
        std::cerr << error << "\n";
        // A log copied aside keeps the extension but not the exact name, so
        // both spellings are worth catching; this is the likeliest mistake.
        if (Core::fileName(buildDir) == ".ninja_log"
            || Core::extension(buildDir) == ".ninja_log") {
            std::cerr << "analyze takes the build directory, not the log "
                         "itself. Pass "
                      << Core::parentPath(buildDir)
                      << ", or use 'compare' if you meant to diff two logs.\n";
        }
        return 1;
    }
    for (const auto &diagnostic : log->diagnostics) {
        std::cerr << "  note: " << diagnostic.message << "\n";
    }

    Build::CompileCommands commands;
    std::string commandsError;
    const bool haveCommands = commands.load(buildDir, commandsError);

    // The compile database names every source file, so their deepest common
    // directory is the source root. Only when there is no database does this
    // fall back to the <source>/build/<preset> convention, which is wrong for
    // a build directory that sits directly under the source root.
    std::string sourceDir = Core::normalizePath(args.sourceDir);
    if (sourceDir.empty() && haveCommands) {
        sourceDir = Build::inferSourceRoot(commands, buildDir);
    }
    if (sourceDir.empty()) {
        sourceDir = Core::normalizePath(buildDir + "/../..");
    }

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
              << "median step     : " << formatMs(stats.medianMs) << "\n";

    // Timestamps are relative to the start of their own ninja invocation, so
    // across several of them the durations stay exact but the wall time and
    // the parallelism come from overlaying separate clocks.
    if (log->spansMultipleBuilds()) {
        std::cout << "\nnote: this log covers several ninja invocations. Each "
                     "duration is still\n      exact, but wall time and peak "
                     "parallelism mix clocks and mean little.\n";
    }
    std::cout << "\n";

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
    if (traces.units().empty() && !args.tracesDir.empty()) {
        std::cerr << "\nno -ftime-trace documents under " << tracesDir
                  << "; the header and template rankings need a clang-cl "
                     "build compiled with /clang:-ftime-trace\n";
    }
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
        usage(std::cerr);
        return 2;
    }
    const std::string baselinePath
        = args.command == "compare" && !args.positional.empty()
        ? args.positional[0]
        : std::string {};
    const std::string currentPath
        = args.positional.size() > 1 ? args.positional[1] : std::string {};
    if (baselinePath.empty() || currentPath.empty()) {
        usage(std::cerr);
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
    if (args.version) {
        std::cout << "bw_cli " << BW_VERSION << "\n";
        return 0;
    }
    if (args.help) {
        usage(std::cout);
        return 0;
    }
    if (args.invalid || args.command.empty()) {
        usage(std::cerr);
        return 2;
    }
    if (args.command == "analyze") {
        return runAnalyze(args);
    }
    if (args.command == "compare") {
        return runCompare(args);
    }
    std::cerr << "unknown command: " << args.command << "\n";
    usage(std::cerr);
    return 2;
}
