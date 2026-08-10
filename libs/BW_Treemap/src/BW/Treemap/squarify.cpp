#include "BW/Treemap/squarify.h"

#include <algorithm>
#include <limits>

namespace BW::Treemap
{

namespace {

/// Areas below this are not worth a rectangle.
constexpr double kEpsilonArea = 1e-9;

struct Child
{
    const Node *node { nullptr };
    double value { 0.0 };
};

auto orderedChildren(const Node &node, Order order) -> std::vector<Child>
{
    std::vector<Child> children;
    children.reserve(node.children.size());
    for (const auto &child : node.children) {
        children.push_back({ &child, std::max(child.value, 0.0) });
    }

    if (order == Order::ByValueDesc) {
        std::stable_sort(
            children.begin(),
            children.end(),
            [](const Child &a, const Child &b) {
                if (a.value != b.value) {
                    return a.value > b.value;
                }
                return a.node->name < b.node->name;
            });
    }
    else {
        // Node::children already arrive name-ordered from TreeBuilder, but
        // sorting here keeps the guarantee independent of the producer.
        std::stable_sort(
            children.begin(),
            children.end(),
            [](const Child &a, const Child &b) {
                return a.node->name < b.node->name;
            });
    }
    return children;
}

void layoutNode(
    const Node &node,
    const Rect &bounds,
    const LayoutOptions &options,
    int depth,
    std::vector<LayoutItem> &out);

/// Places `children` inside `rect` using the squarify row heuristic.
void squarify(
    const std::vector<Child> &children,
    Rect rect,
    const LayoutOptions &options,
    int depth,
    std::vector<LayoutItem> &out)
{
    std::size_t i = 0;
    const std::size_t n = children.size();

    while (i < n) {
        const double side = std::min(rect.w, rect.h);
        if (side <= 0.0 || rect.w <= 0.0 || rect.h <= 0.0) {
            return;
        }

        double remaining = 0.0;
        for (std::size_t k = i; k < n; ++k) {
            remaining += children[k].value;
        }
        if (remaining <= kEpsilonArea) {
            return;
        }
        const double scale = rect.area() / remaining;

        // Grow the row while the worst aspect ratio keeps improving.
        std::size_t j = i;
        double rowArea = 0.0;
        double rowMin = std::numeric_limits<double>::max();
        double rowMax = 0.0;
        double best = std::numeric_limits<double>::max();
        while (j < n) {
            const double area = children[j].value * scale;
            const double nextArea = rowArea + area;
            const double nextMin = std::min(rowMin, area);
            const double nextMax = std::max(rowMax, area);
            const double ratio
                = worstAspectRatio(nextArea, nextMin, nextMax, side);
            if (j > i && ratio > best) {
                break;
            }
            rowArea = nextArea;
            rowMin = nextMin;
            rowMax = nextMax;
            best = ratio;
            ++j;
        }

        const double thickness = rowArea / side;
        if (thickness <= 0.0) {
            return;
        }

        const bool vertical = rect.w >= rect.h;
        double cursor = vertical ? rect.y : rect.x;
        for (std::size_t k = i; k < j; ++k) {
            const double area = children[k].value * scale;
            const double extent = area / thickness;
            const Rect cell = vertical
                ? Rect { rect.x, cursor, thickness, extent }
                : Rect { cursor, rect.y, extent, thickness };
            cursor += extent;
            layoutNode(*children[k].node, cell, options, depth, out);
        }

        if (vertical) {
            rect.x += thickness;
            rect.w -= thickness;
        }
        else {
            rect.y += thickness;
            rect.h -= thickness;
        }
        i = j;
    }
}

void layoutNode(
    const Node &node,
    const Rect &bounds,
    const LayoutOptions &options,
    int depth,
    std::vector<LayoutItem> &out)
{
    if (bounds.w <= 0.0 || bounds.h <= 0.0) {
        return;
    }

    const bool depthExhausted
        = options.maxDepth >= 0 && depth >= options.maxDepth;
    const bool tooSmall = std::min(bounds.w, bounds.h)
        < options.minRecurseSize;
    const bool asCell = node.isLeaf() || depthExhausted || tooSmall;

    if (asCell) {
        out.push_back({ &node, bounds, depth, true, 0.0 });
        return;
    }

    // All or nothing. A label needs its full height to render without being
    // clipped through the glyphs, so either the box can spare that much (and
    // no more than a quarter of itself, or a short directory would be mostly
    // chrome) or it gets no band at all and its children take the whole box.
    //
    // Scaling the band down instead produced two bugs at once: a renderer
    // drawing a full-height label into a part-height reservation put the name
    // on top of the children, and clamping the label to the reservation left
    // an empty strip that looked like a rendering fault.
    const double header = bounds.h * 0.25 >= options.headerHeight
        ? options.headerHeight
        : 0.0;
    out.push_back({ &node, bounds, depth, false, header });

    Rect inner = bounds;
    inner.y += header;
    inner.h -= header;

    const double pad = std::min(
        options.padding,
        std::max(0.0, std::min(inner.w, inner.h) * 0.2));
    inner.x += pad;
    inner.y += pad;
    inner.w -= 2.0 * pad;
    inner.h -= 2.0 * pad;

    if (inner.w <= 0.0 || inner.h <= 0.0) {
        return;
    }

    const auto children = orderedChildren(node, options.order);
    squarify(children, inner, options, depth + 1, out);
}

}

auto worstAspectRatio(
    double rowArea,
    double minArea,
    double maxArea,
    double side) -> double
{
    if (rowArea <= kEpsilonArea || side <= 0.0 || minArea <= kEpsilonArea) {
        return std::numeric_limits<double>::max();
    }
    const double s2 = side * side;
    const double a2 = rowArea * rowArea;
    return std::max((s2 * maxArea) / a2, a2 / (s2 * minArea));
}

void layout(
    const Node &root,
    const Rect &bounds,
    const LayoutOptions &options,
    std::vector<LayoutItem> &out)
{
    out.clear();
    if (bounds.w <= 0.0 || bounds.h <= 0.0 || root.value <= 0.0) {
        return;
    }
    out.reserve(static_cast<std::size_t>(root.leafCount) * 2 + 16);

    // The root is the canvas, not a drawn directory: it gets no border and no
    // label, so its children take the whole rectangle rather than losing a
    // band to a frame nobody needs. headerHeight stays 0 to say so, which is
    // what stops a renderer from labelling it - drilling into a directory
    // makes that directory the root, and labelling it here would put its name
    // on top of its first child's.
    const auto children = orderedChildren(root, options.order);
    out.push_back({ &root, bounds, 0, false, 0.0 });
    squarify(children, bounds, options, 1, out);
}

auto layout(
    const Node &root,
    const Rect &bounds,
    const LayoutOptions &options) -> std::vector<LayoutItem>
{
    std::vector<LayoutItem> out;
    layout(root, bounds, options, out);
    return out;
}

}
