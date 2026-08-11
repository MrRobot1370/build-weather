#include "BW/Treemap/squarify.h"
#include "BW/Treemap/tree.h"

#include <QElapsedTimer>
#include <QHash>
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
    void childrenStayInsideTheParentContentArea();
    void headerIsReservedInFullOrNotAtAll();
    void mostDirectoriesKeepTheirLabelBand();
    void theCanvasReservesNoHeader();
    void everyRectStaysInsideTheBounds();
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
    // std::map ordering: BW_Build before BW_Core
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
    // the whole one-child chain becomes one box
    QCOMPARE(
        root.children[0].name,
        std::string { "libs/BW_Core/include/BW/Core" });
    QCOMPARE(root.children[0].children.size(), std::size_t { 2 });
    // paths still address the real tree
    QCOMPARE(
        root.children[0].children[0].path,
        std::string { "libs/BW_Core/include/BW/Core/a.h" });
}

void TreemapTests::keepsRootIntact()
{
    // collapsing must not swallow the top-level directory name
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

void TreemapTests::childrenStayInsideTheParentContentArea()
{
    // every child rect lies inside the parent minus its header and padding
    TreeBuilder builder;
    builder.setCollapseSingleChildDirectories(false);
    for (int i = 0; i < 60; ++i) {
        builder.add(
            "root/dir" + std::to_string(i % 6) + "/sub"
                + std::to_string(i % 3) + "/file" + std::to_string(i) + ".cpp",
            5.0 + (i % 23),
            i);
    }

    LayoutOptions options;
    options.padding = 2.0;
    options.headerHeight = 14.0;
    const Node root = builder.build();
    const auto items = layout(root, { 0.0, 0.0, 1200.0, 800.0 }, options);
    QVERIFY(!items.empty());

    // map every node path to its item so parents can be found
    QHash<QString, const LayoutItem *> byPath;
    for (const auto &item : items) {
        byPath.insert(QString::fromStdString(item.node->path), &item);
    }

    int checked = 0;
    for (const auto &item : items) {
        const QString path = QString::fromStdString(item.node->path);
        const int slash = path.lastIndexOf('/');
        if (slash <= 0) {
            continue;
        }
        const LayoutItem *parent = byPath.value(path.left(slash), nullptr);
        if (parent == nullptr || parent->isCell) {
            continue;
        }

        const double top = parent->rect.y + parent->headerHeight;
        constexpr double kSlack = 1e-6;
        QVERIFY2(
            item.rect.y >= top - kSlack,
            qPrintable(QString("%1 starts at y=%2, above its parent's "
                               "content top %3")
                           .arg(path)
                           .arg(item.rect.y)
                           .arg(top)));
        QVERIFY(item.rect.x >= parent->rect.x - kSlack);
        QVERIFY(item.rect.x + item.rect.w
            <= parent->rect.x + parent->rect.w + kSlack);
        QVERIFY(item.rect.y + item.rect.h
            <= parent->rect.y + parent->rect.h + kSlack);
        ++checked;
    }
    QVERIFY2(checked > 20, "the test tree did not produce nested directories");
}

void TreemapTests::headerIsReservedInFullOrNotAtAll()
{
    // Many small directories on a modest canvas, so boxes fall on both sides
    // of the ratio.
    TreeBuilder builder;
    builder.setCollapseSingleChildDirectories(false);
    for (int i = 0; i < 400; ++i) {
        builder.add(
            "root/d" + std::to_string(i / 2) + "/f" + std::to_string(i)
                + ".cpp",
            1.0 + (i % 11),
            i);
    }

    LayoutOptions options;
    options.headerHeight = 14.0;
    options.minRecurseSize = 10.0; // let short directories recurse
    const Node root = builder.build();
    const auto items = layout(root, { 0.0, 0.0, 900.0, 600.0 }, options);

    bool sawDropped = false;
    bool sawReserved = false;
    for (const auto &item : items) {
        if (item.isCell) {
            QCOMPARE(item.headerHeight, 0.0);
            continue;
        }
        // in full or not at all; anything between clips the label
        QVERIFY2(
            item.headerHeight == 0.0
                || qFuzzyCompare(item.headerHeight, options.headerHeight),
            qPrintable(QString("partial band of %1 px reserved")
                           .arg(item.headerHeight)));
        // and never taken from a box too short or too narrow to use it
        if (item.headerHeight > 0.0) {
            QVERIFY(
                item.rect.h
                >= options.headerHeight * options.minHeaderBoxRatio - 1e-9);
            QVERIFY(item.rect.w >= options.minHeaderWidth - 1e-9);
            sawReserved = true;
        }
        else if (item.depth > 0) {
            sawDropped = true;
        }
    }
    QVERIFY2(sawDropped, "no directory was short enough to drop its header");
    QVERIFY2(sawReserved, "no directory was tall enough to keep its header");
}

void TreemapTests::mostDirectoriesKeepTheirLabelBand()
{
    // Tightened far enough, the all-or-nothing rule drops the band on every
    // box and the map loses every directory name.
    TreeBuilder builder;
    for (int i = 0; i < 900; ++i) {
        builder.add(
            "src/mod" + std::to_string(i % 12) + "/part"
                + std::to_string(i % 40) + "/f" + std::to_string(i) + ".cpp",
            20.0 + (i % 400),
            i);
    }

    LayoutOptions options;
    options.headerHeight = 17.0; // what a 10 px DemiBold line measures at
    options.minRecurseSize = 26.0;
    const Node root = builder.build();
    const auto items = layout(root, { 0.0, 0.0, 1600.0, 1000.0 }, options);

    int directories = 0;
    int labelled = 0;
    for (const auto &item : items) {
        if (item.isCell || item.depth == 0) {
            continue;
        }
        ++directories;
        if (item.headerHeight > 0.0) {
            ++labelled;
        }
    }
    QVERIFY(directories > 10);
    QVERIFY2(
        labelled * 4 >= directories * 3,
        qPrintable(QString("only %1 of %2 directories kept a label band")
                       .arg(labelled)
                       .arg(directories)));
}

void TreemapTests::theCanvasReservesNoHeader()
{
    // Drilling in makes that directory the layout root, and the root is the
    // canvas: no band, so its children get the whole rectangle.
    TreeBuilder builder;
    builder.add("libs/a/x.cpp", 10.0, 0);
    builder.add("libs/b/y.cpp", 20.0, 1);
    const Node root = builder.build();

    const Rect bounds { 0.0, 0.0, 600.0, 400.0 };
    const auto items = layout(root, bounds, {});
    QVERIFY(!items.empty());

    const LayoutItem &canvas = items.front();
    QCOMPARE(canvas.depth, 0);
    QCOMPARE(canvas.headerHeight, 0.0);

    // with no band taken the children still cover the full height
    double top = bounds.h;
    for (const auto &item : items) {
        if (item.depth == 1) {
            top = std::min(top, item.rect.y);
        }
    }
    QCOMPARE(top, bounds.y);
}

void TreemapTests::everyRectStaysInsideTheBounds()
{
    // The zoomed view passes a rect offset by the pan, so negative origins
    // and rects larger than the view are the normal case.
    TreeBuilder builder;
    for (int i = 0; i < 200; ++i) {
        builder.add(
            "libs/l" + std::to_string(i % 9) + "/src/f" + std::to_string(i)
                + ".cpp",
            1.0 + (i % 37),
            i);
    }
    const Node root = builder.build();

    const QList<Rect> viewports {
        { 0.0, 0.0, 1200.0, 800.0 },
        { -600.0, -400.0, 2400.0, 1600.0 },   // zoom 2, panned to the middle
        { -7200.0, -4800.0, 9600.0, 6400.0 }, // zoom 8, panned to the corner
        { 0.0, 0.0, 40.0, 4000.0 },           // pathological aspect ratio
    };

    for (const Rect &view : viewports) {
        const auto items = layout(root, view, {});
        QVERIFY(!items.empty());
        constexpr double kSlack = 1e-6;
        for (const auto &item : items) {
            QVERIFY(item.rect.w >= -kSlack);
            QVERIFY(item.rect.h >= -kSlack);
            QVERIFY2(
                item.rect.x >= view.x - kSlack
                    && item.rect.y >= view.y - kSlack
                    && item.rect.x + item.rect.w <= view.x + view.w + kSlack
                    && item.rect.y + item.rect.h <= view.y + view.h + kSlack,
                qPrintable(QString("%1 escaped the canvas")
                               .arg(QString::fromStdString(item.node->path))));
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
    // with Order::ByName a file keeps its position and only its area changes
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

    // the trees must outlive the layouts: LayoutItem::node points at them
    const Node treeBefore = buildTree(0.0);
    const Node treeAfter = buildTree(250.0);
    const auto before = layout(treeBefore, bounds, options);
    const auto after = layout(treeAfter, bounds, options);

    QCOMPARE(before.size(), after.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        QCOMPARE(before[i].node->path, after[i].node->path);
    }

    // a stays left of (or above) b
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
    // one square cell filling the side is the ideal, ratio 1
    QVERIFY(std::abs(worstAspectRatio(100.0, 100.0, 100.0, 10.0) - 1.0)
        < 1e-9);
    // a long thin strip is worse than a square
    QVERIFY(worstAspectRatio(10.0, 10.0, 10.0, 10.0) > 1.0);
    // degenerate input must not score well
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
    // zero width means nothing to draw, not a crash
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
    // a full relayout has to fit inside a frame
    QVERIFY2(
        elapsed < 100,
        qPrintable(QString("layout of %1 leaves took %2 ms")
                       .arg(kFiles)
                       .arg(elapsed)));
}

QTEST_APPLESS_MAIN(TreemapTests)
#include "treemap_tests.moc"
