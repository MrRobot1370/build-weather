#pragma once

// Hierarchy built from the flat list of tree paths a BuildSnapshot produces.
// Directory nodes carry the sum of their leaves, which is what the treemap
// lays out.

#include <string>
#include <string_view>
#include <vector>

namespace BW::Treemap
{

struct Node
{
    std::string name; ///< one path segment
    std::string path; ///< full tree path down to this node
    double value { 0.0 }; ///< leaf value, or the sum of the subtree
    int leafIndex { -1 }; ///< caller-supplied id; >= 0 only on leaves
    int leafCount { 0 }; ///< leaves in this subtree, 1 for a leaf
    std::vector<Node> children;

    [[nodiscard]]
    auto isLeaf() const -> bool
    {
        return children.empty();
    }
};

/// Accumulates `path -> value` pairs into a tree.
///
/// Values are summed on the way up, children are ordered deterministically by
/// name, and chains of single-child directories are collapsed
/// ("libs/BW_Core/include" instead of three nested one-child boxes) so the
/// borders that survive actually carry information.
class TreeBuilder
{
public:
    void add(std::string_view treePath, double value, int leafIndex);

    void setCollapseSingleChildDirectories(bool collapse)
    {
        m_collapse = collapse;
    }

    /// Builds the tree. `rootName` names the synthetic top node.
    [[nodiscard]]
    auto build(std::string rootName = "") const -> Node;

    [[nodiscard]]
    auto empty() const -> bool
    {
        return m_entries.empty();
    }

    void clear()
    {
        m_entries.clear();
    }

private:
    struct Entry
    {
        std::string path;
        double value { 0.0 };
        int leafIndex { -1 };
    };

    std::vector<Entry> m_entries;
    bool m_collapse { true };
};

/// Depth-first walk. `visit` receives the node and its depth (root = 0).
template <typename Fn>
void walk(const Node &node, Fn &&visit, int depth = 0)
{
    visit(node, depth);
    for (const auto &child : node.children) {
        walk(child, visit, depth + 1);
    }
}

}
