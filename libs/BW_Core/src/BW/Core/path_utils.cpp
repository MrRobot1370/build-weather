#include "BW/Core/path_utils.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace BW::Core
{

namespace {

auto toLowerAscii(char c) -> char
{
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(c)));
}

auto toUpperAscii(char c) -> char
{
    return static_cast<char>(
        std::toupper(static_cast<unsigned char>(c)));
}

auto isSeparator(char c) -> bool
{
    return c == '/' || c == '\\';
}

/// Splits off the root prefix. Returns the prefix in canonical form and
/// advances `rest` past it. Recognises "C:/", "C:", "//server/share" and "/".
auto takeRoot(std::string_view path, std::string_view &rest) -> std::string
{
    rest = path;

    // UNC: \\server\share
    if (path.size() >= 2 && isSeparator(path[0]) && isSeparator(path[1])) {
        rest = path.substr(2);
        return "//";
    }

    // Drive letter
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0]))
        && path[1] == ':') {
        std::string root { toUpperAscii(path[0]), ':' };
        if (path.size() >= 3 && isSeparator(path[2])) {
            rest = path.substr(3);
            root += '/';
        }
        else {
            rest = path.substr(2);
        }
        return root;
    }

    // POSIX absolute
    if (!path.empty() && isSeparator(path[0])) {
        rest = path.substr(1);
        return "/";
    }

    return {};
}

auto collapse(std::string_view body, bool absolute)
    -> std::vector<std::string_view>
{
    std::vector<std::string_view> out;
    out.reserve(8);

    std::size_t i = 0;
    while (i <= body.size()) {
        const std::size_t j = std::min(
            body.find_first_of("/\\", i),
            body.size());
        const std::string_view seg = body.substr(i, j - i);
        i = j + 1;

        if (seg.empty() || seg == ".") {
            continue;
        }
        if (seg == "..") {
            if (!out.empty() && out.back() != "..") {
                out.pop_back();
                continue;
            }
            if (absolute) {
                // ".." above the root is meaningless; drop it.
                continue;
            }
        }
        out.push_back(seg);
    }
    return out;
}

}

auto normalizePath(std::string_view path) -> std::string
{
    if (path.empty()) {
        return {};
    }

    std::string_view body;
    const std::string root = takeRoot(path, body);
    const bool absolute = !root.empty();

    const auto segments = collapse(body, absolute);

    std::string out = root;
    for (const auto &seg : segments) {
        if (!out.empty() && out.back() != '/') {
            out += '/';
        }
        out.append(seg);
    }

    // A bare drive normalizes to "C:/", not "C:".
    if (out.size() == 2 && out[1] == ':') {
        out += '/';
    }
    return out;
}

auto pathKey(std::string_view path) -> std::string
{
    std::string key = normalizePath(path);
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), toLowerAscii);
#endif
    return key;
}

auto isAbsolutePath(std::string_view path) -> bool
{
    std::string_view rest;
    return !takeRoot(path, rest).empty();
}

auto joinPath(std::string_view base, std::string_view relative) -> std::string
{
    if (relative.empty()) {
        return normalizePath(base);
    }
    if (isAbsolutePath(relative) || base.empty()) {
        return normalizePath(relative);
    }

    std::string combined { base };
    if (!combined.empty() && !isSeparator(combined.back())) {
        combined += '/';
    }
    combined.append(relative);
    return normalizePath(combined);
}

auto relativeTo(std::string_view path, std::string_view base)
    -> std::optional<std::string>
{
    const std::string pathNorm = normalizePath(path);
    const std::string baseNorm = normalizePath(base);
    if (baseNorm.empty()) {
        return pathNorm;
    }

    std::string pathK = pathKey(pathNorm);
    std::string baseK = pathKey(baseNorm);
    while (!baseK.empty() && baseK.back() == '/') {
        baseK.pop_back();
    }

    if (pathK.size() < baseK.size()
        || pathK.compare(0, baseK.size(), baseK) != 0) {
        return std::nullopt;
    }
    if (pathK.size() == baseK.size()) {
        return std::string {};
    }
    if (pathK[baseK.size()] != '/') {
        return std::nullopt; // "src2" must not match base "src"
    }
    return pathNorm.substr(baseK.size() + 1);
}

auto parentPath(std::string_view path) -> std::string
{
    const std::string norm = normalizePath(path);
    const std::size_t slash = norm.find_last_of('/');
    if (slash == std::string::npos) {
        return {};
    }
    if (slash == 0) {
        return "/";
    }
    // Keep the slash on a drive root: "C:/foo" -> "C:/"
    if (slash == 2 && norm.size() > 2 && norm[1] == ':') {
        return norm.substr(0, 3);
    }
    return norm.substr(0, slash);
}

auto fileName(std::string_view path) -> std::string
{
    const std::string norm = normalizePath(path);
    const std::size_t slash = norm.find_last_of('/');
    return slash == std::string::npos ? norm : norm.substr(slash + 1);
}

auto extension(std::string_view path) -> std::string
{
    const std::string name = fileName(path);
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) {
        return {};
    }
    std::string ext = name.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), toLowerAscii);
    return ext;
}

auto splitSegments(std::string_view path) -> std::vector<std::string>
{
    const std::string norm = normalizePath(path);
    std::vector<std::string> out;

    std::size_t i = 0;
    // Keep the root prefix ("C:", "//", "/") as one leading segment.
    std::string_view rest;
    const std::string root = takeRoot(norm, rest);
    if (!root.empty()) {
        std::string head = root;
        if (head.size() > 1 && head.back() == '/' && head != "//") {
            head.pop_back();
        }
        out.push_back(head);
        i = root.size();
    }

    while (i < norm.size()) {
        const std::size_t j = std::min(norm.find('/', i), norm.size());
        if (j > i) {
            out.emplace_back(norm.substr(i, j - i));
        }
        i = j + 1;
    }
    return out;
}

auto bucketName(PathBucket bucket) -> const char *
{
    switch (bucket) {
    case PathBucket::SourceTree :
        return "source";
    case PathBucket::Generated :
        return "generated";
    case PathBucket::External :
        return "external";
    case PathBucket::System :
        return "system";
    }
    return "unknown";
}

namespace {

auto withTrailingSlash(std::string s) -> std::string
{
    if (!s.empty() && s.back() != '/') {
        s += '/';
    }
    return s;
}

/// Heuristics for toolchain headers, used when the exact compiler include
/// directories are not known. Matched against a pathKey, so lower case.
constexpr std::array<std::string_view, 7> kSystemHints {
    "/microsoft visual studio/",
    "/windows kits/",
    "/msvc/",
    "/mingw",
    "/usr/include/",
    "/usr/lib/gcc/",
    "/llvm/lib/clang/",
};

}

void PathClassifier::setSourceRoot(std::string_view root)
{
    m_sourceRoot = normalizePath(root);
    m_sourceRootKey = withTrailingSlash(pathKey(m_sourceRoot));
}

void PathClassifier::setBuildRoot(std::string_view root)
{
    m_buildRoot = normalizePath(root);
    m_buildRootKey = withTrailingSlash(pathKey(m_buildRoot));
}

void PathClassifier::addSystemPrefix(std::string_view prefix)
{
    std::string key = withTrailingSlash(pathKey(prefix));
    if (key.size() > 1
        && std::find(m_systemPrefixes.begin(), m_systemPrefixes.end(), key)
            == m_systemPrefixes.end()) {
        m_systemPrefixes.push_back(std::move(key));
    }
}

auto PathClassifier::classify(std::string_view path) const -> PathBucket
{
    const std::string key = pathKey(path);
    if (key.empty()) {
        return PathBucket::SourceTree;
    }

    for (const auto &prefix : m_systemPrefixes) {
        if (key.compare(0, prefix.size(), prefix) == 0) {
            return PathBucket::System;
        }
    }

    // The build root is usually inside the source root, so test it first.
    if (!m_buildRootKey.empty()
        && key.size() > m_buildRootKey.size()
        && key.compare(0, m_buildRootKey.size(), m_buildRootKey) == 0) {
        return PathBucket::Generated;
    }
    if (!m_sourceRootKey.empty()
        && key.size() > m_sourceRootKey.size()
        && key.compare(0, m_sourceRootKey.size(), m_sourceRootKey) == 0) {
        return PathBucket::SourceTree;
    }

    if (!isAbsolutePath(key)) {
        // Relative and not resolvable: treat as build-directory output.
        return PathBucket::Generated;
    }

    for (const auto &hint : kSystemHints) {
        if (key.find(hint) != std::string::npos) {
            return PathBucket::System;
        }
    }
    return PathBucket::External;
}

auto PathClassifier::toTreePath(std::string_view path) const -> std::string
{
    if (path.empty()) {
        return {};
    }

    // Relative inputs come from ninja, which writes them relative to the
    // build directory.
    const std::string absolute = isAbsolutePath(path)
        ? normalizePath(path)
        : joinPath(m_buildRoot, path);

    switch (classify(absolute)) {
    case PathBucket::SourceTree : {
        if (auto rel = relativeTo(absolute, m_sourceRoot)) {
            return *rel;
        }
        return absolute;
    }
    case PathBucket::Generated : {
        auto rel = relativeTo(absolute, m_buildRoot);
        return std::string { kGeneratedRoot } + "/"
            + (rel ? *rel : fileName(absolute));
    }
    case PathBucket::External : {
        // Keep the last few segments; the full absolute path buries the map
        // under "C:/Program Files/..." style nesting.
        const auto segs = splitSegments(absolute);
        std::string tail;
        const std::size_t keep = std::min<std::size_t>(segs.size(), 4);
        for (std::size_t i = segs.size() - keep; i < segs.size(); ++i) {
            if (!tail.empty()) {
                tail += '/';
            }
            tail += segs[i];
        }
        return std::string { kExternalRoot } + "/" + tail;
    }
    case PathBucket::System : {
        const auto segs = splitSegments(absolute);
        std::string tail;
        const std::size_t keep = std::min<std::size_t>(segs.size(), 3);
        for (std::size_t i = segs.size() - keep; i < segs.size(); ++i) {
            if (!tail.empty()) {
                tail += '/';
            }
            tail += segs[i];
        }
        return std::string { kSystemRoot } + "/" + tail;
    }
    }
    return absolute;
}

}
