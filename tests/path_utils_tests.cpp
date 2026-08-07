#include "BW/Core/path_utils.h"

#include <QTest>

using namespace BW::Core;

class PathUtilsTests : public QObject
{
    Q_OBJECT

private slots:
    void normalizesSeparatorsAndCase();
    void resolvesDotSegments();
    void detectsAbsolutePaths();
    void joinsPaths();
    void relativeToRejectsPrefixCollisions();
    void splitsSegments();
    void extractsNameAndExtension();
    void classifierBucketsBySourceAndBuildRoot();
    void classifierMapsIntoOneTree();
    void keyJoinsAcrossDataSources();
};

void PathUtilsTests::normalizesSeparatorsAndCase()
{
    QCOMPARE(
        normalizePath(R"(c:\proj\src\main.cpp)"),
        std::string { "C:/proj/src/main.cpp" });
    QCOMPARE(
        normalizePath("C:/proj//src///main.cpp"),
        std::string { "C:/proj/src/main.cpp" });
    QCOMPARE(normalizePath("C:"), std::string { "C:/" });
    QCOMPARE(normalizePath(R"(\\server\share\a.h)"),
             std::string { "//server/share/a.h" });
    QCOMPARE(normalizePath(""), std::string {});

    // Case is preserved for display and folded only for the key.
    QCOMPARE(normalizePath("C:/Proj/Main.cpp"),
             std::string { "C:/Proj/Main.cpp" });
#ifdef _WIN32
    QCOMPARE(pathKey("C:/Proj/Main.cpp"), std::string { "c:/proj/main.cpp" });
#endif
}

void PathUtilsTests::resolvesDotSegments()
{
    QCOMPARE(
        normalizePath("C:/proj/src/../include/a.h"),
        std::string { "C:/proj/include/a.h" });
    QCOMPARE(normalizePath("./src/./a.cpp"), std::string { "src/a.cpp" });
    QCOMPARE(normalizePath("../../a.cpp"), std::string { "../../a.cpp" });
    // ".." cannot escape a root.
    QCOMPARE(normalizePath("C:/../a.cpp"), std::string { "C:/a.cpp" });
}

void PathUtilsTests::detectsAbsolutePaths()
{
    QVERIFY(isAbsolutePath("C:/a"));
    QVERIFY(isAbsolutePath(R"(d:\a)"));
    QVERIFY(isAbsolutePath("//server/share"));
    QVERIFY(isAbsolutePath("/usr/include"));
    QVERIFY(!isAbsolutePath("src/a.cpp"));
    QVERIFY(!isAbsolutePath(""));
}

void PathUtilsTests::joinsPaths()
{
    QCOMPARE(
        joinPath("C:/proj/build", "libs/a.obj"),
        std::string { "C:/proj/build/libs/a.obj" });
    QCOMPARE(
        joinPath("C:/proj/build", "../libs/a.cpp"),
        std::string { "C:/proj/libs/a.cpp" });
    // An absolute right-hand side wins.
    QCOMPARE(
        joinPath("C:/proj/build", "D:/other/a.cpp"),
        std::string { "D:/other/a.cpp" });
}

void PathUtilsTests::relativeToRejectsPrefixCollisions()
{
    QCOMPARE(
        relativeTo("C:/proj/src/a.cpp", "C:/proj").value(),
        std::string { "src/a.cpp" });
    QCOMPARE(
        relativeTo("C:/proj/src/a.cpp", "C:/proj/").value(),
        std::string { "src/a.cpp" });
    // Case-insensitive on Windows, where it matters.
#ifdef _WIN32
    QVERIFY(relativeTo("C:/Proj/src/a.cpp", "c:/proj").has_value());
#endif
    // "src2" must not be treated as living under "src".
    QVERIFY(!relativeTo("C:/proj/src2/a.cpp", "C:/proj/src").has_value());
    QVERIFY(!relativeTo("C:/other/a.cpp", "C:/proj").has_value());
    QCOMPARE(relativeTo("C:/proj", "C:/proj").value(), std::string {});
}

void PathUtilsTests::splitsSegments()
{
    const auto segments = splitSegments("C:/proj/src/a.cpp");
    QCOMPARE(segments.size(), std::size_t { 4 });
    QCOMPARE(segments[0], std::string { "C:" });
    QCOMPARE(segments[3], std::string { "a.cpp" });

    const auto relative = splitSegments("libs/BW_Core/a.cpp");
    QCOMPARE(relative.size(), std::size_t { 3 });
    QCOMPARE(relative[0], std::string { "libs" });
}

void PathUtilsTests::extractsNameAndExtension()
{
    QCOMPARE(fileName("C:/proj/src/a.cpp"), std::string { "a.cpp" });
    QCOMPARE(extension("C:/proj/src/a.CPP"), std::string { ".cpp" });
    QCOMPARE(extension("C:/proj/src/Makefile"), std::string {});
    QCOMPARE(parentPath("C:/proj/src/a.cpp"), std::string { "C:/proj/src" });
    QCOMPARE(parentPath("C:/a.cpp"), std::string { "C:/" });
}

void PathUtilsTests::classifierBucketsBySourceAndBuildRoot()
{
    PathClassifier classifier;
    classifier.setSourceRoot("F:/proj");
    classifier.setBuildRoot("F:/proj/build/ninja-x64");

    QCOMPARE(
        classifier.classify("F:/proj/libs/a.cpp"),
        PathBucket::SourceTree);
    // The build root sits inside the source root and must win.
    QCOMPARE(
        classifier.classify("F:/proj/build/ninja-x64/moc_a.cpp"),
        PathBucket::Generated);
    QCOMPARE(
        classifier.classify("D:/vendor/sdk/include/a.h"),
        PathBucket::External);
    QCOMPARE(
        classifier.classify(
            "C:/Program Files/Microsoft Visual Studio/2022/VC/include/vector"),
        PathBucket::System);

    classifier.addSystemPrefix("C:/Qt/6.6.2/msvc2019_64/include");
    QCOMPARE(
        classifier.classify("C:/Qt/6.6.2/msvc2019_64/include/QtCore/qstring.h"),
        PathBucket::System);
}

void PathUtilsTests::classifierMapsIntoOneTree()
{
    PathClassifier classifier;
    classifier.setSourceRoot("F:/proj");
    classifier.setBuildRoot("F:/proj/build/ninja-x64");

    QCOMPARE(
        classifier.toTreePath("F:/proj/libs/BW_Core/src/a.cpp"),
        std::string { "libs/BW_Core/src/a.cpp" });

    // Ninja hands us paths relative to the build directory.
    QCOMPARE(
        classifier.toTreePath("bin/BuildWeather.exe"),
        std::string { "[generated]/bin/BuildWeather.exe" });

    const std::string external
        = classifier.toTreePath("D:/vendor/sdk/include/deep/a.h");
    QVERIFY(external.rfind("[external]/", 0) == 0);
}

void PathUtilsTests::keyJoinsAcrossDataSources()
{
    // The whole point of pathKey: ninja's relative, backslashed, differently
    // cased spelling has to land on the same key as the trace's absolute one.
    const std::string fromNinja = joinPath(
        "F:/proj/build/ninja-x64",
        R"(..\..\libs\BW_Core\SRC\a.cpp)");
    const std::string fromTrace = "F:/proj/libs/BW_Core/src/a.cpp";
#ifdef _WIN32
    QCOMPARE(pathKey(fromNinja), pathKey(fromTrace));
#else
    QCOMPARE(pathKey(fromNinja), pathKey("F:/proj/libs/BW_Core/SRC/a.cpp"));
#endif
}

QTEST_APPLESS_MAIN(PathUtilsTests)
#include "path_utils_tests.moc"
