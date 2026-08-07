#include "BW/Build/build_snapshot.h"
#include "BW/Build/ninja_log.h"
#include "BW/Build/report.h"
#include "BW/Build/time_trace.h"

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

auto findSource(const TimeTraceUnit &unit, std::string_view needle)
    -> const SourceCost *
{
    for (const auto &source : unit.sources) {
        if (source.path.find(needle) != std::string::npos) {
            return &source;
        }
    }
    return nullptr;
}

}

class TraceTests : public QObject
{
    Q_OBJECT

private slots:
    void parsesTheCheckedInTrace();
    void parsesAsyncBeginEndEncoding();
    void selfTimeSubtractsNestedIncludes();
    void repeatedIncludesAccumulate();
    void normalizesMixedSeparators();
    void prefersTotalEventsForFrontendBackend();
    void rejectsNonTraceJson();
    void survivesTruncatedJson();
    void aggregatesAcrossTranslationUnits();
    void exportsJsonAndCsv();
};

void TraceTests::parsesTheCheckedInTrace()
{
    const TimeTraceUnit unit = parseTimeTrace(
        readFixture("sample-time-trace.json"),
        "F:/proj/build/x/time_trace.cpp.json");

    QVERIFY(unit.ok());
    QVERIFY(unit.diagnostics.empty());
    QCOMPARE(unit.totalUs, Micros { 14000 });
    QCOMPARE(unit.objectPath, std::string { "F:/proj/build/x/time_trace.cpp.obj" });
    // process_labels names the real source; it beats the file-name guess.
    QCOMPARE(
        unit.source,
        std::string { "F:/proj/libs/BW_Build/src/BW/Build/time_trace.cpp" });

    QCOMPARE(unit.sources.size(), std::size_t { 3 });
    QCOMPARE(unit.instantiations.size(), std::size_t { 2 });
    // Sorted most expensive first.
    QCOMPARE(unit.instantiations.front().totalUs, Micros { 900 });
    QVERIFY(unit.instantiations.front().isClass);
}

void TraceTests::parsesAsyncBeginEndEncoding()
{
    // clang 15 and later write the include tree as async "b"/"e" pairs
    // instead of complete "X" events. Both fixtures describe the same
    // compilation, so both must produce the same numbers; handling only the
    // "X" form silently yields an empty header ranking against a real
    // clang 19 trace.
    const TimeTraceUnit unit = parseTimeTrace(
        readFixture("sample-time-trace-async.json"),
        "F:/proj/build/x/time_trace.cpp.json");

    QVERIFY(unit.ok());
    QVERIFY(unit.diagnostics.empty());
    QCOMPARE(unit.sources.size(), std::size_t { 3 });
    QCOMPARE(unit.totalUs, Micros { 14000 });
    QCOMPARE(unit.frontendUs, Micros { 9000 });
    QCOMPARE(unit.backendUs, Micros { 5000 });

    const SourceCost *heavy = findSource(unit, "heavy.h");
    QVERIFY(heavy != nullptr);
    QCOMPARE(heavy->totalUs, Micros { 4000 });
    QCOMPARE(heavy->selfUs, Micros { 1300 });

    const SourceCost *qstring = findSource(unit, "qstring.h");
    QVERIFY(qstring != nullptr);
    QCOMPARE(qstring->occurrences, 2);
    QCOMPARE(qstring->totalUs, Micros { 1250 });

    QCOMPARE(unit.instantiations.size(), std::size_t { 2 });
    QCOMPARE(
        unit.source,
        std::string { "F:/proj/libs/BW_Build/src/BW/Build/time_trace.cpp" });
}

void TraceTests::selfTimeSubtractsNestedIncludes()
{
    const TimeTraceUnit unit = parseTimeTrace(
        readFixture("sample-time-trace.json"),
        "heavy.cpp.json");

    // heavy.h costs 4000 us in total but only 1300 us of that is its own
    // text; the rest is inner.h (1500) plus qstring.h (1200).
    const SourceCost *heavy = findSource(unit, "heavy.h");
    QVERIFY(heavy != nullptr);
    QCOMPARE(heavy->totalUs, Micros { 4000 });
    QCOMPARE(heavy->selfUs, Micros { 1300 });

    const SourceCost *inner = findSource(unit, "inner.h");
    QVERIFY(inner != nullptr);
    QCOMPARE(inner->totalUs, Micros { 1500 });
    QCOMPARE(inner->selfUs, Micros { 1500 });
}

void TraceTests::repeatedIncludesAccumulate()
{
    const TimeTraceUnit unit = parseTimeTrace(
        readFixture("sample-time-trace.json"),
        "heavy.cpp.json");

    // qstring.h appears twice: 1200 us nested and 50 us at the top level.
    const SourceCost *qstring = findSource(unit, "qstring.h");
    QVERIFY(qstring != nullptr);
    QCOMPARE(qstring->occurrences, 2);
    QCOMPARE(qstring->totalUs, Micros { 1250 });
    QCOMPARE(qstring->selfUs, Micros { 1250 });
}

void TraceTests::normalizesMixedSeparators()
{
    // The fixture spells qstring.h with forward slashes once and backslashes
    // once; they have to collapse to one entry, which is exactly the failure
    // mode path normalization exists to prevent.
    const TimeTraceUnit unit = parseTimeTrace(
        readFixture("sample-time-trace.json"),
        "heavy.cpp.json");

    int matches = 0;
    for (const auto &source : unit.sources) {
        if (source.path.find("qstring.h") != std::string::npos) {
            ++matches;
        }
    }
    QCOMPARE(matches, 1);
}

void TraceTests::prefersTotalEventsForFrontendBackend()
{
    const TimeTraceUnit unit = parseTimeTrace(
        readFixture("sample-time-trace.json"),
        "heavy.cpp.json");
    QCOMPARE(unit.frontendUs, Micros { 9000 });
    QCOMPARE(unit.backendUs, Micros { 5000 });

    // Without the "Total ..." events the sums have to give the same answer.
    const std::string noTotals
        = R"({"traceEvents":[)"
          R"({"ph":"X","ts":0,"dur":900,"name":"Frontend"},)"
          R"({"ph":"X","ts":900,"dur":400,"name":"Backend"}]})";
    const TimeTraceUnit summed = parseTimeTrace(noTotals, "a.cpp.json");
    QCOMPARE(summed.frontendUs, Micros { 900 });
    QCOMPARE(summed.backendUs, Micros { 400 });
    QCOMPARE(summed.totalUs, Micros { 1300 });
}

void TraceTests::rejectsNonTraceJson()
{
    const TimeTraceUnit unit
        = parseTimeTrace(R"({"hello":"world"})", "a.cpp.json");
    QVERIFY(!unit.ok());
    QCOMPARE(unit.diagnostics.size(), std::size_t { 1 });
    QCOMPARE(
        unit.diagnostics.front().severity,
        ParseDiagnostic::Severity::Error);

    const TimeTraceUnit notJson = parseTimeTrace("nonsense", "a.cpp.json");
    QVERIFY(!notJson.ok());
    QVERIFY(!notJson.diagnostics.empty());
}

void TraceTests::survivesTruncatedJson()
{
    // A trace from a compiler that was killed mid-write must still yield the
    // events that did land, with a warning rather than a crash.
    const std::string truncated
        = R"({"traceEvents":[)"
          R"({"ph":"X","ts":0,"dur":500,"name":"Source","args":{"detail":"a.h"}},)"
          R"({"ph":"X","ts":600,"dur":300,"name":"Sour)";
    const TimeTraceUnit unit = parseTimeTrace(truncated, "a.cpp.json");
    QCOMPARE(unit.sources.size(), std::size_t { 1 });
    QVERIFY(!unit.diagnostics.empty());
}

void TraceTests::aggregatesAcrossTranslationUnits()
{
    const std::string text = readFixture("sample-time-trace.json");

    TraceAggregate aggregate;
    aggregate.add(parseTimeTrace(text, "a.cpp.json"));
    aggregate.add(parseTimeTrace(text, "b.cpp.json"));
    aggregate.add(parseTimeTrace(text, "c.cpp.json"));
    aggregate.finalize();

    QCOMPARE(aggregate.units().size(), std::size_t { 3 });
    QCOMPARE(aggregate.frontendUs(), Micros { 27000 });
    QCOMPARE(aggregate.backendUs(), Micros { 15000 });

    // The ranking is the number the project exists to produce: cost summed
    // over every TU, with the TU count alongside it.
    const auto &headers = aggregate.headers();
    QCOMPARE(headers.size(), std::size_t { 3 });
    QCOMPARE(headers.front().path.find("heavy.h") != std::string::npos, true);
    QCOMPARE(headers.front().totalUs, Micros { 12000 });
    QCOMPARE(headers.front().tuCount, 3);
    QCOMPARE(headers.front().averageUs(), Micros { 4000 });

    const auto including = aggregate.unitsIncluding(headers.front().path);
    QCOMPARE(including.size(), std::size_t { 3 });

    QCOMPARE(aggregate.templates().front().count, 3);
    QCOMPARE(aggregate.templates().front().tuCount, 3);
}

void TraceTests::exportsJsonAndCsv()
{
    const std::string logText = "# ninja log v5\n"
                                "0\t1000\t0\ta.cpp.obj\t1\n"
                                "0\t400\t0\tb.cpp.obj\t2\n";
    SnapshotOptions options;
    options.classifier.setSourceRoot("F:/proj");
    options.classifier.setBuildRoot("F:/proj/build");
    BuildSnapshot snapshot
        = BuildSnapshot::fromNinjaLog(parseNinjaLog(logText), options);
    snapshot.setLabel("unit test");

    TraceAggregate aggregate;
    aggregate.add(parseTimeTrace(
        readFixture("sample-time-trace.json"),
        "a.cpp.json"));
    aggregate.finalize();

    ReportOptions reportOptions;
    reportOptions.buildDirectory = "F:/proj/build";
    reportOptions.sourceDirectory = "F:/proj";
    const std::string json = exportJson(snapshot, &aggregate, reportOptions);

    QVERIFY(json.find("\"schema\": \"build-weather/analysis/1\"")
        != std::string::npos);
    QVERIFY(json.find("\"targetCount\": 2") != std::string::npos);
    QVERIFY(json.find("expensiveHeaders") != std::string::npos);
    QVERIFY(json.find("heavy.h") != std::string::npos);
    // Backslashes in Windows paths have to survive as valid JSON escapes.
    QVERIFY(json.find("\\\\") == std::string::npos);

    const std::string csv = exportTargetsCsv(snapshot);
    QVERIFY(csv.rfind("rank,path,duration_ms,kind,bucket,output", 0) == 0);
    QCOMPARE(std::count(csv.begin(), csv.end(), '\n'), std::ptrdiff_t { 3 });

    const std::string headers = exportHeadersCsv(aggregate, 2);
    QCOMPARE(std::count(headers.begin(), headers.end(), '\n'),
             std::ptrdiff_t { 3 });
}

QTEST_APPLESS_MAIN(TraceTests)
#include "trace_tests.moc"
