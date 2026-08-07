#include "BW/Treemap/squarify.h"
#include "BW/Treemap/tree.h"

#include <QElapsedTimer>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace BW::Treemap;

namespace {

auto findNode(const Node &root, std::string_view path) -> const Node *
{
    const Node *found = nullptr;
    walk(root, [&](const Node &node, int) {
        if (found == nullptr && node.path == path) {
            found = &node;
        }
    });
    return found;
}

auto cellsOf(const std::vector<LayoutItem> &items)
    -> std::vector<const LayoutItem *>
{
    std::vector<const LayoutItem *> cells;
    for (const auto &item : items) {
        if (item.isCell) {
            cells.push_back(&item);
        }
    }
    return cells;
}

auto overlaps(const Rect &a, const Rect &b) -> bool
{
    constexpr double kSlack = 1e-6;
    return a.x + kSlack < b.x + b.w && b.x + kSlack < a.x + a.w
        && a.y + kSlack < b.y + b.h && b.y + kSlack < a.y + a.h;
}

}

class TreemapTests : public QObject
{
    Q_OBJECT

private slots:
    void buildsHierarchyAndSumsValues();
    void collapsesSingleChildDirectoryChains();
    void keepsRootIntact();
    void layoutFillsBoundsWithoutOverlap();
    void layoutIsProportionalToValue();
    void layoutIsStableWhenOnlyValuesChange();
    void valueOrderingIsAvailableButNotDefault();
    void worstRatioPrefersSquares();
    void handlesEmptyAndDegenerateInput();
    void scalesToSeveralThousandLeaves();
};

void TreemapTests::buildsHierarchyAndSumsValues()
{
    TreeBuilder builder;
    builder.setCollapseSingleChildDirectories(false);
    builder.add("libs/BW_Core/logger.cpp", 100.0, 0);
    builder.add("libs/BW_Core/path_utils.cpp", 300.0, 1);
    builder.add("libs/BW_Build/ninja_log.cpp", 600.0, 2);

    const Node root = builder.build();
    QCOMPARE(root.value, 1000.0);
    QCOMPARE(root.leafCount, 3);

    const Node *libs = findNode(root, "libs");
    QVERIFY(libs != nullptr);
    QCOMPARE(libs->children.size(), std::size_t { 2 });
    // std::map ordering: BW_Build before BW_Core, deterministically.
    QCOMPARE(libs->children[0].name, std::string { "BW_Build" });

    const Node *core = findNode(root, "libs/BW_Core");
    QVERIFY(core != nullptr);
    QCOMPARE(core->value, 400.0);
    QCOMPARE(core->leafCount, 2);
    QVERIFY(!core->isLeaf());

    const Node *leaf = findNode(root, "libs/BW_Core/logger.cpp");
    QVERIFY(leaf != nullptr);
    QVERIFY(leaf->isLeaf());
    QCOMPARE(leaf->leafIndex, 0);
}

void TreemapTests::collapsesSingleChildDirectoryChains()
{
    TreeBuilder builder;
    builder.add("libs/BW_Core/include/BW/Core/a.h", 10.0, 0);
    builder.add("libs/BW_Core/include/BW/Core/b.h", 20.0, 1);

    const Node root = builder.build();
    QCOMPARE(root.children.size(), std::size_t { 1 });
    // The whole one-child chain becomes one box.
    QCOMPARE(
        root.children[0].name,
        std::string { "libs/BW_Core/include/BW/Core" });
    QCOMPARE(root.children[0].children.size(), std::size_t { 2 });
    // Paths still address the real tree.
    QCOMPARE(
        root.children[0].children[0].path,
        std::string { "libs/BW_Core/include/BW/Core/a.h" });
}

void TreemapTests::keepsRootIntact()
{
    // Collapsing must never swallow the top-level directory name.
    TreeBuilder builder;
    builder.add("libs/a/x.cpp", 1.0, 0);
    builder.add("libs/b/y.cpp", 1.0, 1);

    const Node root = builder.build();
    QVERIFY(root.name.empty());
    QCOMPARE(root.children.size(), std::size_t { 1 });
    QCOMPARE(root.children[0].name, std::string { "libs" });
}

void TreemapTests::layoutFillsBoundsWithoutOverlap()
{
    TreeBuilder builder;
    for (int i = 0; i < 40; ++i) {
        builder.add(
            "src/dir" + std::to_string(i % 5) + "/file"
                + std::to_string(i) + ".cpp",
            10.0 + i,
            i);
    }

    LayoutOptions options;
    options.padding = 0.0;
    options.headerHeight = 0.0;
    const Rect bounds { 0.0, 0.0, 800.0, 600.0 };
    const Node root = builder.build();
    const auto items = layout(root, bounds, options);
    const auto cells = cellsOf(items);
    QCOMPARE(cells.size(), std::size_t { 40 });

    double covered = 0.0;
    for (const LayoutItem *cell : cells) {
        QVERIFY(cell->rect.x >= bounds.x - 1e-6);
        QVERIFY(cell->rect.y >= bounds.y - 1e-6);
        QVERIFY(cell->rect.x + cell->rect.w <= bounds.x + bounds.w + 1e-6);
        QVERIFY(cell->rect.y + cell->rect.h <= bounds.y + bounds.h + 1e-6);
        covered += cell->rect.area();
    }
    QVERIFY(std::abs(covered - bounds.area()) < 1e-3);

    for (std::size_t i = 0; i < cells.size(); ++i) {
        for (std::size_t j = i + 1; j < cells.size(); ++j) {
            QVERIFY2(
                !overlaps(cells[i]->rect, cells[j]->rect),
                qPrintable(
                    QString::fromStdString(
                        cells[i]->node->path + " overlaps "
                        + cells[j]->node->path)));
        }
    }
}

void TreemapTests::layoutIsProportionalToValue()
{
    TreeBuilder builder;
    builder.add("a.cpp", 300.0, 0);
    builder.add("b.cpp", 100.0, 1);

    LayoutOptions options;
    options.padding = 0.0;
    options.headerHeight = 0.0;
    const Node root = builder.build();
    const auto items = layout(root, { 0.0, 0.0, 400.0, 400.0 }, options);
    const auto cells = cellsOf(items);
    QCOMPARE(cells.size(), std::size_t { 2 });

    const LayoutItem *a = cells[0]->node->name == "a.cpp" ? cells[0]
                                                          : cells[1];
    const LayoutItem *b = a == cells[0] ? cells[1] : cells[0];
    QVERIFY(std::abs(a->rect.area() / b->rect.area() - 3.0) < 1e-6);
}

void TreemapTests::layoutIsStableWhenOnlyValuesChange()
{
    // The property the analysis view depends on: with Order::ByName a file
    // keeps its position between builds and only its area changes.
    const auto buildTree = [](double bump) {
        TreeBuilder builder;
        builder.add("src/a.cpp", 100.0 + bump, 0);
        builder.add("src/b.cpp", 500.0, 1);
        builder.add("src/c.cpp", 300.0 - bump, 2);
        builder.add("tests/d.cpp", 200.0, 3);
        return builder.build();
    };

    LayoutOptions options;
    options.order = Order::ByName;
    const Rect bounds { 0.0, 0.0, 500.0, 400.0 };

    // The trees have to outlive the layouts: LayoutItem::node points at them.
    const Node treeBefore = buildTree(0.0);
    const Node treeAfter = buildTree(250.0);
    const auto before = layout(treeBefore, bounds, options);
    const auto after = layout(treeAfter, bounds, options);

    QCOMPARE(before.size(), after.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        QCOMPARE(before[i].node->path, after[i].node->path);
    }

    // Same relative ordering on screen: a stays left of (or above) b.
    const auto positionOf = [](const std::vector<LayoutItem> &items,
                               std::string_view path) {
        for (const auto &item : items) {
            if (item.node->path == path) {
                return std::pair { item.rect.x, item.rect.y };
            }
        }
        return std::pair { -1.0, -1.0 };
    };
    const auto beforeA = positionOf(before, "src/a.cpp");
    const auto beforeB = positionOf(before, "src/b.cpp");
    const auto afterA = positionOf(after, "src/a.cpp");
    const auto afterB = positionOf(after, "src/b.cpp");
    QCOMPARE(beforeA.first <= beforeB.first, afterA.first <= afterB.first);
    QCOMPARE(beforeA.second <= beforeB.second, afterA.second <= afterB.second);
}

void TreemapTests::valueOrderingIsAvailableButNotDefault()
{
    QCOMPARE(LayoutOptions {}.order, Order::ByName);

    TreeBuilder builder;
    builder.add("z.cpp", 900.0, 0);
    builder.add("a.cpp", 100.0, 1);

    LayoutOptions options;
    options.order = Order::ByValueDesc;
    options.padding = 0.0;
    options.headerHeight = 0.0;
    const Node root = builder.build();
    const auto items = layout(root, { 0.0, 0.0, 400.0, 400.0 }, options);
    const auto cells = cellsOf(items);
    QCOMPARE(cells.front()->node->name, std::string { "z.cpp" });
}

void TreemapTests::worstRatioPrefersSquares()
{
    // A single square cell filling the side is the ideal, ratio 1.
    QVERIFY(std::abs(worstAspectRatio(100.0, 100.0, 100.0, 10.0) - 1.0)
        < 1e-9);
    // A long thin strip is worse than a square.
    QVERIFY(worstAspectRatio(10.0, 10.0, 10.0, 10.0) > 1.0);
    // Degenerate input must not produce a "good" score.
    QVERIFY(worstAspectRatio(0.0, 0.0, 0.0, 10.0)
        > std::numeric_limits<double>::max() / 2.0);
}

void TreemapTests::handlesEmptyAndDegenerateInput()
{
    TreeBuilder builder;
    QVERIFY(builder.empty());
    Node root = builder.build();
    QVERIFY(layout(root, { 0.0, 0.0, 100.0, 100.0 }).empty());

    builder.add("a.cpp", 0.0, 0);
    root = builder.build();
    QVERIFY(layout(root, { 0.0, 0.0, 100.0, 100.0 }).empty());

    builder.add("b.cpp", 5.0, 1);
    root = builder.build();
    QVERIFY(!layout(root, { 0.0, 0.0, 100.0, 100.0 }).empty());
    // Zero width means nothing to draw, not a crash.
    QVERIFY(layout(root, { 0.0, 0.0, 0.0, 100.0 }).empty());
}

void TreemapTests::scalesToSeveralThousandLeaves()
{
    constexpr int kFiles = 5000;
    TreeBuilder builder;
    for (int i = 0; i < kFiles; ++i) {
        builder.add(
            "libs/lib" + std::to_string(i % 20) + "/src/group"
                + std::to_string((i / 20) % 12) + "/file" + std::to_string(i)
                + ".cpp",
            1.0 + (i % 97),
            i);
    }
    const Node root = builder.build();
    QCOMPARE(root.leafCount, kFiles);

    QElapsedTimer timer;
    timer.start();
    const auto items = layout(root, { 0.0, 0.0, 1920.0, 1080.0 });
    const qint64 elapsed = timer.elapsed();

    QVERIFY(!items.empty());
    // A full relayout has to fit comfortably inside a frame; the live view
    // relayouts on every resize.
    QVERIFY2(
        elapsed < 100,
        qPrintable(QString("layout of %1 leaves took %2 ms")
                       .arg(kFiles)
                       .arg(elapsed)));
}

QTEST_APPLESS_MAIN(TreemapTests)
#include "treemap_tests.moc"
