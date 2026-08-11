#pragma once

#include "BW/Treemap/tree.h"

#include <vector>

namespace BW::Treemap
{

struct Rect
{
    double x { 0.0 };
    double y { 0.0 };
    double w { 0.0 };
    double h { 0.0 };

    [[nodiscard]]
    auto area() const -> double
    {
        return w * h;
    }

    [[nodiscard]]
    auto contains(double px, double py) const -> bool
    {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

enum class Order
{
    /// a file only moves when the file set changes, not when a duration does
    ByName,
    /// classic squarified ordering: better aspect ratios, unstable positions
    ByValueDesc
};

struct LayoutOptions
{
    Order order { Order::ByName };
    /// Inset applied inside a directory before laying out its children.
    double padding { 2.0 };
    /// Band reserved at the top of a directory for its label, in full or not
    /// at all. A caller that draws text in the band must pass the height that
    /// text needs, or the label is clipped or drawn over the children.
    double headerHeight { 14.0 };
    /// A band is only reserved on a box at least this many times as tall as
    /// the band, and at least this wide.
    double minHeaderBoxRatio { 2.5 };
    double minHeaderWidth { 46.0 };
    /// Directories smaller than this are drawn as a single block instead of
    /// being recursed into.
    double minRecurseSize { 24.0 };
    /// Hard depth cap; negative means unlimited.
    int maxDepth { -1 };
};

/// LIFETIME: `node` points into the tree that was laid out; passing a
/// temporary tree to layout() leaves every item dangling.
struct LayoutItem
{
    const Node *node { nullptr };
    Rect rect;
    int depth { 0 };
    /// True for a filled cell: a leaf, or a directory too small to recurse
    /// into.
    bool isCell { false };
    /// Height reserved at the top of `rect` for this directory's label. Zero
    /// for cells, for the canvas at depth 0, and for any box too small to
    /// spare it. A renderer must label a directory if and only if this is
    /// non-zero, and must keep the label inside it.
    double headerHeight { 0.0 };
};

/**
 * @brief Lays `root` out inside `bounds`.
 *
 * Output is in draw order: a directory always precedes its children, so a
 * painter can draw backgrounds then contents in one pass.
 */
void layout(
    const Node &root,
    const Rect &bounds,
    const LayoutOptions &options,
    std::vector<LayoutItem> &out);

[[nodiscard]]
auto layout(
    const Node &root,
    const Rect &bounds,
    const LayoutOptions &options = {}) -> std::vector<LayoutItem>;

/// Worst (largest) aspect ratio produced for a row of the given areas laid
/// along `side`. Exposed for tests.
[[nodiscard]]
auto worstAspectRatio(
    double rowArea,
    double minArea,
    double maxArea,
    double side) -> double;

}
