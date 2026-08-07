#include "BW/Build/build_snapshot.h"
#include "BW/Build/compile_commands.h"
#include "BW/Build/ninja_log.h"
#include "BW/Build/ninja_progress.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <fstream>
#include <string>

using namespace BW::Build;

namespace {

auto fixture(const char *name) -> std::string
{
    return std::string { BW_TEST_DATA_DIR } + "/" + name;
}

auto readFixture(const char *name) -> std::string
{
    std::ifstream in { fixture(name), std::ios::binary };
    return std::string { std::istreambuf_iterator<char> { in },
                         std::istreambuf_iterator<char> {} };
}

}

class NinjaTests : public QObject
{
    Q_OBJECT

private slots:
    void parsesTheCheckedInLog();
    void durationsAreExact();
    void latestEntryPerOutputWins();
    void findsTheLastInvocation();
    void outOfOrderEndTimesDoNotSplitAnInvocation();
    void skipsMalformedLinesWithoutFailing();
    void warnsOnUnexpectedVersion();
    void tailYieldsOnlyAppendedRecords();

    void parsesProgressLines();
    void extractsOutputFromDescriptions();
    void rejectsNonStatusLines();
    void progressStreamHandlesSplitChunks();

    void compileCommandsJoinObjectsToSources();
    void objectPathGuessFallback();

    void snapshotResolvesSourcesAndRanks();
    void snapshotDropsDuplicateOutputSpellings();
    void snapshotMergesStepsSharingAFile();
    void snapshotTimelineIsOrdered();
    void comparisonReportsDeltas();
};

// --- .ninja_log --------------------------------------------------------------

void NinjaTests::parsesTheCheckedInLog()
{
    const NinjaLog log = parseNinjaLog(readFixture("sample.ninja_log"));
    QCOMPARE(log.version, 5);
    QCOMPARE(log.entries.size(), std::size_t { 11 });
    QVERIFY(!log.hasError());

    const TargetRecord &first = log.entries.front();
    QCOMPARE(first.startMs, Millis { 0 });
    QCOMPARE(first.endMs, Millis { 142 });
    QCOMPARE(
        first.output,
        std::string {
            "libs/BW_Core/CMakeFiles/BW_Core.dir/src/BW/Core/logger.cpp.obj" });
    QCOMPARE(first.commandHash, std::uint64_t { 0x9a3f1c22b7de4401ull });
}

void NinjaTests::durationsAreExact()
{
    const auto record
        = parseNinjaLogLine("7\t2410\t1737000000000000000\ta.obj\tdeadbeef");
    QVERIFY(record.has_value());
    QCOMPARE(record->durationMs(), Millis { 2403 });

    // An end before the start is nonsense, not a negative duration.
    const auto reversed = parseNinjaLogLine("500\t100\t0\ta.obj\t0");
    QVERIFY(reversed.has_value());
    QCOMPARE(reversed->durationMs(), Millis { 0 });
}

void NinjaTests::latestEntryPerOutputWins()
{
    const NinjaLog log = parseNinjaLog(readFixture("sample.ninja_log"));
    const auto latest = log.latestPerOutput();

    // 11 entries, two outputs rebuilt in the second session.
    QCOMPARE(latest.size(), std::size_t { 9 });

    const auto it = std::find_if(
        latest.begin(),
        latest.end(),
        [](const TargetRecord &r) {
            return r.output.find("time_trace.cpp.obj") != std::string::npos;
        });
    QVERIFY(it != latest.end());
    // The rebuild took 2455 ms, not the original 2403 ms.
    QCOMPARE(it->durationMs(), Millis { 2455 });
}

void NinjaTests::findsTheLastInvocation()
{
    const NinjaLog log = parseNinjaLog(readFixture("sample.ninja_log"));
    QVERIFY(log.spansMultipleBuilds());

    // The fixture's second build rebuilt time_trace.cpp.obj and relinked the
    // executable. Scanning backwards stops as soon as an output repeats, so
    // exactly those two entries come back.
    const auto last = log.lastInvocationEntries();
    QCOMPARE(last.size(), std::size_t { 2 });
    QCOMPARE(last.front().durationMs(), Millis { 2455 });
    QVERIFY(last.back().output.find("BuildWeather.exe") != std::string::npos);

    // A log from a single build has nothing repeated, so the whole file is
    // the last invocation.
    const NinjaLog single = parseNinjaLog(
        "# ninja log v6\n"
        "0\t100\t0\ta.obj\t1\n"
        "0\t200\t0\tb.obj\t2\n");
    QVERIFY(!single.spansMultipleBuilds());
    QCOMPARE(single.lastInvocationEntries().size(), std::size_t { 2 });
}

void NinjaTests::outOfOrderEndTimesDoNotSplitAnInvocation()
{
    // Measured against ninja 1.12: end_ms goes backwards inside a single
    // run (a long edge is recorded before short ones). An earlier version of
    // this parser split on that and reported 68 sessions for four builds.
    const NinjaLog log = parseNinjaLog(
        "# ninja log v6\n"
        "0\t7102\t0\tslow.cpp.obj\t1\n"
        "0\t350\t0\tfast_a.cpp.obj\t2\n"
        "0\t190\t0\tfast_b.cpp.obj\t3\n"
        "0\t228\t0\tfast_c.cpp.obj\t4\n");
    QVERIFY(!log.spansMultipleBuilds());
    QCOMPARE(log.lastInvocationEntries().size(), std::size_t { 4 });
}

void NinjaTests::skipsMalformedLinesWithoutFailing()
{
    const std::string text = "# ninja log v5\n"
                             "0\t100\t0\tgood.obj\tabc\n"
                             "not a record at all\n"
                             "\n"
                             "x\ty\t0\tbad.obj\tabc\n"
                             "200\t400\t0\tsecond.obj\tdef\n";
    const NinjaLog log = parseNinjaLog(text);
    QCOMPARE(log.entries.size(), std::size_t { 2 });
    QCOMPARE(log.diagnostics.size(), std::size_t { 2 });
    QVERIFY(!log.hasError());
}

void NinjaTests::warnsOnUnexpectedVersion()
{
    // v5 and v6 share the five-field layout, so neither should complain.
    for (const int version : { 5, 6 }) {
        const NinjaLog log = parseNinjaLog(
            "# ninja log v" + std::to_string(version)
            + "\n0\t100\t0\ta.obj\tabc\n");
        QCOMPARE(log.version, version);
        QVERIFY2(
            log.diagnostics.empty(),
            qPrintable(QString("v%1 produced a diagnostic").arg(version)));
    }

    // Anything newer is flagged rather than silently misread.
    const NinjaLog future
        = parseNinjaLog("# ninja log v9\n0\t100\t0\ta.obj\tabc\n");
    QCOMPARE(future.version, 9);
    QVERIFY(!future.diagnostics.empty());
    QVERIFY(
        future.diagnostics.front().message.find("newer")
        != std::string::npos);
}

void NinjaTests::tailYieldsOnlyAppendedRecords()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const std::string path
        = (dir.path() + "/.ninja_log").toStdString();

    {
        std::ofstream out { path, std::ios::binary };
        out << "# ninja log v5\n0\t100\t0\tfirst.obj\tabc\n";
    }

    NinjaLogTail tail { path };
    tail.seekToEnd();
    QVERIFY(tail.poll().empty());

    {
        std::ofstream out { path, std::ios::binary | std::ios::app };
        out << "10\t250\t0\tsecond.obj\tdef\n";
    }
    auto fresh = tail.poll();
    QCOMPARE(fresh.size(), std::size_t { 1 });
    QCOMPARE(fresh.front().output, std::string { "second.obj" });
    QVERIFY(tail.poll().empty());

    // A half-written line must not be reported until it is complete.
    {
        std::ofstream out { path, std::ios::binary | std::ios::app };
        out << "20\t300\t0\tthi";
    }
    QVERIFY(tail.poll().empty());
    {
        std::ofstream out { path, std::ios::binary | std::ios::app };
        out << "rd.obj\t123\n";
    }
    fresh = tail.poll();
    QCOMPARE(fresh.size(), std::size_t { 1 });
    QCOMPARE(fresh.front().output, std::string { "third.obj" });

    // Recompaction shrinks the file; the tail restarts instead of desyncing.
    {
        std::ofstream out { path, std::ios::binary | std::ios::trunc };
        out << "# ninja log v5\n0\t50\t0\tonly.obj\tabc\n";
    }
    bool restarted = false;
    fresh = tail.poll(&restarted);
    QVERIFY(restarted);
    QCOMPARE(fresh.size(), std::size_t { 1 });
    QCOMPARE(fresh.front().output, std::string { "only.obj" });
}

// --- progress lines ----------------------------------------------------------

void NinjaTests::parsesProgressLines()
{
    const auto line = parseProgressLine(
        "[12/340] Building CXX object libs/CMakeFiles/a.dir/src/b.cpp.obj");
    QVERIFY(line.has_value());
    QCOMPARE(line->finished, 12);
    QCOMPARE(line->total, 340);
    QCOMPARE(line->running, -1); // no %r in the bare format
    QCOMPARE(line->kind, StepKind::Compile);
    QCOMPARE(
        line->outputPath,
        std::string { "libs/CMakeFiles/a.dir/src/b.cpp.obj" });

    // The format BuildRunner actually asks for carries %r, which is the only
    // trustworthy source for the in-flight count.
    const auto withRunning = parseProgressLine(
        "[12/340 8] Building CXX object libs/CMakeFiles/a.dir/src/b.cpp.obj");
    QVERIFY(withRunning.has_value());
    QCOMPARE(withRunning->finished, 12);
    QCOMPARE(withRunning->total, 340);
    QCOMPARE(withRunning->running, 8);
    QCOMPARE(
        withRunning->outputPath,
        std::string { "libs/CMakeFiles/a.dir/src/b.cpp.obj" });
}

void NinjaTests::extractsOutputFromDescriptions()
{
    QCOMPARE(
        outputPathFromDescription(
            "Linking CXX executable bin/BuildWeather.exe"),
        std::string { "bin/BuildWeather.exe" });
    QCOMPARE(
        outputPathFromDescription("Generating moc_app_context.cpp"),
        std::string { "moc_app_context.cpp" });
    // Not a path: do not guess.
    QCOMPARE(
        outputPathFromDescription("Automatic MOC for target BuildWeather"),
        std::string {});
    QCOMPARE(
        stepKindFromDescription("Automatic MOC for target BuildWeather"),
        StepKind::Generate);
}

void NinjaTests::rejectsNonStatusLines()
{
    QVERIFY(!parseProgressLine("ninja: build stopped: subcommand failed.")
                 .has_value());
    QVERIFY(!parseProgressLine("a.cpp(12): warning C4100: unused").has_value());
    QVERIFY(!parseProgressLine("[not/numbers] Building").has_value());
    QVERIFY(!parseProgressLine("").has_value());
}

void NinjaTests::progressStreamHandlesSplitChunks()
{
    ProgressStream stream;
    auto chunk = stream.feed("[1/2] Building CXX object a.cpp.o");
    QVERIFY(chunk.progress.empty());

    chunk = stream.feed("bj\n[2/2] Linking CXX executable bin/x.exe\r\n");
    QCOMPARE(chunk.progress.size(), std::size_t { 2 });
    QCOMPARE(chunk.progress[0].finished, 1);
    QCOMPARE(chunk.progress[1].outputPath, std::string { "bin/x.exe" });

    chunk = stream.feed("ninja: build stopped.\n");
    QVERIFY(chunk.progress.empty());
    QCOMPARE(chunk.other.size(), std::size_t { 1 });
}

// --- compile_commands.json ---------------------------------------------------

void NinjaTests::compileCommandsJoinObjectsToSources()
{
    CompileCommands commands;
    std::string error;
    QVERIFY(commands.parse(
        readFixture("sample-compile-commands.json"),
        error));
    QVERIFY2(error.empty(), error.c_str());
    QCOMPARE(commands.entries().size(), std::size_t { 3 });
    QVERIFY(commands.hasOutputs());

    const auto source = commands.sourceForOutput(
        "libs/BW_Core/CMakeFiles/BW_Core.dir/src/BW/Core/logger.cpp.obj");
    QVERIFY(source.has_value());
    QCOMPARE(
        *source,
        std::string { "F:/proj/libs/BW_Core/src/BW/Core/logger.cpp" });

    QVERIFY(!commands.sourceForOutput("libs/nothing.obj").has_value());
}

void NinjaTests::objectPathGuessFallback()
{
    QCOMPARE(
        guessSourceFromObject(
            "libs/BW_Core/CMakeFiles/BW_Core.dir/src/BW/Core/logger.cpp.obj"),
        std::string { "libs/BW_Core/src/BW/Core/logger.cpp" });

    // "__" is CMake's encoding of a ".." segment, relative to the directory
    // that declared the target: apps/../shared/a.cpp is shared/a.cpp.
    QCOMPARE(
        guessSourceFromObject("apps/CMakeFiles/x.dir/__/shared/a.cpp.obj"),
        std::string { "shared/a.cpp" });

    // Anything that is not shaped like a CMake object is not guessed at.
    QCOMPARE(guessSourceFromObject("bin/BuildWeather.exe"), std::string {});
    QCOMPARE(guessSourceFromObject("some/random.obj"), std::string {});
}

// --- snapshot ----------------------------------------------------------------

namespace {

auto makeSnapshot(LogScope scope) -> BuildSnapshot
{
    const NinjaLog log = parseNinjaLog(readFixture("sample.ninja_log"));
    SnapshotOptions options;
    options.classifier.setSourceRoot("F:/proj");
    options.classifier.setBuildRoot("F:/proj/build/ninja-x64");
    options.scope = scope;
    return BuildSnapshot::fromNinjaLog(log, options);
}

}

void NinjaTests::snapshotResolvesSourcesAndRanks()
{
    const BuildSnapshot snapshot = makeSnapshot(LogScope::LatestPerOutput);
    QCOMPARE(snapshot.targets().size(), std::size_t { 9 });

    // Compile steps land on their source file, not under CMakeFiles/.
    const auto it = std::find_if(
        snapshot.targets().begin(),
        snapshot.targets().end(),
        [](const TargetInfo &t) {
            return t.treePath == "libs/BW_Build/src/BW/Build/time_trace.cpp";
        });
    QVERIFY(it != snapshot.targets().end());
    QCOMPARE(it->kind, StepKind::Compile);
    QCOMPARE(it->bucket, BW::Core::PathBucket::SourceTree);
    QCOMPARE(it->durationMs, Millis { 2455 });

    // The link step has no source and stays in the generated bucket.
    const auto link = std::find_if(
        snapshot.targets().begin(),
        snapshot.targets().end(),
        [](const TargetInfo &t) { return t.kind == StepKind::Link; });
    QVERIFY(link != snapshot.targets().end());
    QCOMPARE(link->bucket, BW::Core::PathBucket::Generated);

    // Ranks: 1 is the slowest step in the snapshot.
    const auto slowest = std::find_if(
        snapshot.targets().begin(),
        snapshot.targets().end(),
        [](const TargetInfo &t) { return t.rank == 1; });
    QVERIFY(slowest != snapshot.targets().end());
    QCOMPARE(slowest->durationMs, snapshot.stats().maxMs);
    QCOMPARE(slowest->durationMs, Millis { 3971 });
}

void NinjaTests::snapshotDropsDuplicateOutputSpellings()
{
    // Ninja logs an edge once per output name, and CMake gives some outputs
    // both a build-relative and an absolute name. They are the same file and
    // the same work, so counting both would inflate the total.
    const std::string text = "# ninja log v6\n"
                             "0\t100\t0\tqml/a.qml\tabc\n"
                             "0\t100\t0\tF:/proj/build/qml/a.qml\tabc\n";
    SnapshotOptions options;
    options.classifier.setSourceRoot("F:/proj");
    options.classifier.setBuildRoot("F:/proj/build");

    const auto snapshot
        = BuildSnapshot::fromNinjaLog(parseNinjaLog(text), options);
    QCOMPARE(snapshot.targets().size(), std::size_t { 1 });
    QCOMPARE(snapshot.targets().front().stepCount, 1);
    QCOMPARE(snapshot.stats().totalCpuMs, Millis { 100 });
}

void NinjaTests::snapshotMergesStepsSharingAFile()
{
    // One source compiled into two targets is two build steps at one place
    // in the tree. The map needs one leaf, and the cost of that file is the
    // sum, so they merge and report how many steps went into them.
    const std::string text
        = "# ninja log v6\n"
          "0\t500\t0\tlibs/a/CMakeFiles/t1.dir/src/shared.cpp.obj\t1\n"
          "0\t700\t0\tlibs/a/CMakeFiles/t2.dir/src/shared.cpp.obj\t2\n";
    SnapshotOptions options;
    options.classifier.setSourceRoot("F:/proj");
    options.classifier.setBuildRoot("F:/proj/build");

    const auto snapshot
        = BuildSnapshot::fromNinjaLog(parseNinjaLog(text), options);
    QCOMPARE(snapshot.targets().size(), std::size_t { 1 });

    const auto &target = snapshot.targets().front();
    QCOMPARE(target.treePath, std::string { "libs/a/src/shared.cpp" });
    QCOMPARE(target.stepCount, 2);
    QCOMPARE(target.durationMs, Millis { 1200 });
    QCOMPARE(target.kind, StepKind::Compile);
    // The longer step decides the label.
    QVERIFY(target.output.find("t2.dir") != std::string::npos);
}

void NinjaTests::snapshotTimelineIsOrdered()
{
    const BuildSnapshot snapshot = makeSnapshot(LogScope::LastInvocation);
    QCOMPARE(snapshot.targets().size(), std::size_t { 2 });

    const auto &timeline = snapshot.timeline();
    QCOMPARE(timeline.size(), std::size_t { 4 });
    for (std::size_t i = 1; i < timeline.size(); ++i) {
        QVERIFY(timeline[i - 1].timeMs <= timeline[i].timeMs);
    }

    // The two steps in the last session do not overlap.
    QCOMPARE(snapshot.stats().peakParallelism, 1);
    QCOMPARE(snapshot.parallelismAt(1000), 1);
    QCOMPARE(snapshot.parallelismAt(2457), 0);
}

void NinjaTests::comparisonReportsDeltas()
{
    const std::string before = "# ninja log v5\n"
                               "0\t1000\t0\ta.cpp.obj\t1\n"
                               "0\t500\t0\tb.cpp.obj\t2\n";
    const std::string after = "# ninja log v5\n"
                              "0\t1400\t0\ta.cpp.obj\t1\n"
                              "0\t300\t0\tc.cpp.obj\t3\n";

    SnapshotOptions options;
    options.classifier.setSourceRoot("F:/proj");
    options.classifier.setBuildRoot("F:/proj/build");

    const auto baseline
        = BuildSnapshot::fromNinjaLog(parseNinjaLog(before), options);
    const auto current
        = BuildSnapshot::fromNinjaLog(parseNinjaLog(after), options);
    const auto deltas = compareSnapshots(baseline, current);

    QCOMPARE(deltas.size(), std::size_t { 3 });
    // Sorted worst regression first.
    QCOMPARE(deltas.front().deltaMs, Millis { 400 });
    QCOMPARE(deltas.front().state, TargetDelta::State::Changed);
    QCOMPARE(deltas.back().state, TargetDelta::State::Removed);
    QCOMPARE(deltas.back().deltaMs, Millis { -500 });

    const auto added = std::find_if(
        deltas.begin(),
        deltas.end(),
        [](const TargetDelta &d) {
            return d.state == TargetDelta::State::Added;
        });
    QVERIFY(added != deltas.end());
    QCOMPARE(added->currentMs, Millis { 300 });
}

QTEST_APPLESS_MAIN(NinjaTests)
#include "ninja_tests.moc"
