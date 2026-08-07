#include "BW/Treemap/tree.h"

#include <algorithm>
#include <map>

namespace BW::Treemap
{

namespace {

/// Mutable scratch tree; Node is the immutable result. std::map keeps the
/// children in a deterministic order without a sort pass.
struct BuildNode
{
    std::string name;
    double value { 0.0 };
    int leafIndex { -1 };
    std::map<std::string, BuildNode> children;
};

void insert(
    BuildNode &root,
    std::string_view path,
    double value,
    int leafIndex)
{
    BuildNode *node = &root;
    std::size_t i = 0;
    while (i < path.size()) {
        const std::size_t slash = std::min(path.find('/', i), path.size());
        const std::string segment { path.substr(i, slash - i) };
        i = slash + 1;
        if (segment.empty()) {
            continue;
        }
        auto [it, inserted] = node->children.try_emplace(segment);
        if (inserted) {
            it->second.name = segment;
        }
        node = &it->second;
    }
    // BuildSnapshot::finalize() de-duplicates tree paths before we see them,
    // so this only ever adds to a fresh node; summing is the safe fallback.
    node->value += value;
    if (node->leafIndex < 0) {
        node->leafIndex = leafIndex;
    }
}

/// `collapseThis` folds a chain of single-child directories starting at
/// `source` into one display name. It is off for the synthetic root, which
/// would otherwise swallow the name of the only top-level directory.
auto convert(
    const BuildNode &source,
    const std::string &parentPath,
    bool collapseThis,
    bool collapseChildren) -> Node
{
    const BuildNode *current = &source;
    std::string name = source.name;
    while (collapseThis && current->children.size() == 1
        && current->value == 0.0 && current->leafIndex < 0
        && !current->children.begin()->second.children.empty()) {
        current = &current->children.begin()->second;
        name = name.empty() ? current->name : name + "/" + current->name;
    }

    Node node;
    node.name = name;
    node.path = parentPath.empty() ? name : parentPath + "/" + name;
    node.leafIndex = current->leafIndex;

    if (current->children.empty()) {
        node.value = current->value;
        node.leafCount = 1;
        return node;
    }

    node.children.reserve(current->children.size());
    for (const auto &[childName, child] : current->children) {
        Node converted = convert(
            child,
            node.path,
            collapseChildren,
            collapseChildren);
        node.value += converted.value;
        node.leafCount += converted.leafCount;
        node.children.push_back(std::move(converted));
    }
    node.value += current->value;
    return node;
}

}

void TreeBuilder::add(std::string_view treePath, double value, int leafIndex)
{
    if (treePath.empty()) {
        return;
    }
    m_entries.push_back({ std::string { treePath }, value, leafIndex });
}

auto TreeBuilder::build(std::string rootName) const -> Node
{
    BuildNode root;
    root.name = std::move(rootName);
    for (const auto &entry : m_entries) {
        insert(root, entry.path, entry.value, entry.leafIndex);
    }
    return convert(root, {}, false, m_collapse);
}

}
