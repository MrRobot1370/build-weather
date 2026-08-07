#pragma once

// Squarified treemap layout (Bruls, Huizing, van Wijk, 2000).
//
// Two properties this implementation is written for:
//
//  * Stability. Children are ordered by name by default, never by value, so a
//    file keeps its place from build to build and only its *area* changes.
//    That is the whole point of comparing two runs. Order::ByValueDesc is
//    available and gives better aspect ratios, but it makes positions move
//    whenever a duration moves, so it is not the default.
//  * Visible structure. Each directory reserves a header band and insets its
//    children, so the nesting is readable rather than implied.

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
    /// Deterministic and independent of the values: a file only moves when
    /// the file set changes.
    ByName,
    /// Classic squarified ordering. Better aspect ratios, unstable positions.
    ByValueDesc
};

struct LayoutOptions
{
    Order order { Order::ByName };
    /// Inset applied inside a directory before laying out its children.
    double padding { 2.0 };
    /// Band reserved at the top of a directory for its label. Dropped
    /// automatically when the box is too small to be worth labelling.
    double headerHeight { 14.0 };
    /// Directories smaller than this are drawn as a single block instead of
    /// being recursed into; this is what keeps several thousand files fast.
    double minRecurseSize { 24.0 };
    /// Hard depth cap; negative means unlimited.
    int maxDepth { -1 };
};

/// LIFETIME: `node` points into the tree that was laid out. Keep the Node
/// alive for as long as the layout is used; passing a temporary tree to
/// layout() leaves every item dangling.
struct LayoutItem
{
    const Node *node { nullptr };
    Rect rect;
    int depth { 0 };
    /// True when the node is drawn as a filled cell (a leaf, or a directory
    /// that was too small to recurse into).
    bool isCell { false };
};

/// Lays `root` out inside `bounds`. Output is in draw order: a directory
/// always precedes its children, so a painter can draw backgrounds then
/// contents in one pass.
void layout(
    const Node &root,
    const Rect &bounds,
    const LayoutOptions &options,
    std::vector<LayoutItem> &out);

/// Convenience overload.
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
