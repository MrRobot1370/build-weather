#pragma once

// The data sources disagree about separators, casing and whether a path is
// absolute, so every path entering the application goes through
// normalizePath() for display and pathKey() for joining. Nothing joins on a
// raw string.
//
// Pure string transforms, free of Qt and std::filesystem: they never touch
// the disk, because a header recorded in a trace may no longer exist.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BW::Core
{

/// Canonical display form: forward slashes, `.` segments dropped, `..`
/// segments resolved where possible, drive letters upper-cased, no trailing
/// slash. Relative inputs stay relative.
[[nodiscard]]
auto normalizePath(std::string_view path) -> std::string;

/// Case-folded key derived from normalizePath(). This, never the display
/// string, is what two data sources are joined on.
[[nodiscard]]
auto pathKey(std::string_view path) -> std::string;

[[nodiscard]]
auto isAbsolutePath(std::string_view path) -> bool;

/// Normalized `base/relative`. If `relative` is already absolute it wins.
[[nodiscard]]
auto joinPath(std::string_view base, std::string_view relative) -> std::string;

/// `path` expressed relative to `base`, or nullopt when `path` is not
/// underneath `base`. Comparison is case-insensitive on Windows.
[[nodiscard]]
auto relativeTo(std::string_view path, std::string_view base)
    -> std::optional<std::string>;

[[nodiscard]]
auto parentPath(std::string_view path) -> std::string;

[[nodiscard]]
auto fileName(std::string_view path) -> std::string;

/// Lower-cased extension including the leading dot, or an empty string.
[[nodiscard]]
auto extension(std::string_view path) -> std::string;

/// Segments of a normalized path, root prefix included as the first segment
/// for absolute inputs ("C:", "//server").
[[nodiscard]]
auto splitSegments(std::string_view path) -> std::vector<std::string>;

/// Where a file sits relative to the project being measured. Each bucket is
/// kept apart so it does not distort the source-tree map.
enum class PathBucket
{
    SourceTree, ///< under the source root, authored by the user
    Generated, ///< under the build root (moc_*, ui_*, protobuf output)
    External, ///< absolute, outside both roots (vendored SDKs, 3rdparty)
    System ///< toolchain / SDK headers
};

[[nodiscard]]
auto bucketName(PathBucket bucket) -> const char *;

/**
 * @brief Maps any incoming path onto a single tree of display paths.
 *
 * Everything the UI shows is a tree path: a normalized relative path whose
 * first segment is either a real source directory or one of the synthetic
 * bucket roots below, so a system header cannot appear to live in src/.
 */
class PathClassifier
{
public:
    static constexpr const char *kGeneratedRoot = "[generated]";
    static constexpr const char *kExternalRoot = "[external]";
    static constexpr const char *kSystemRoot = "[system]";

    PathClassifier() = default;

    void setSourceRoot(std::string_view root);
    void setBuildRoot(std::string_view root);

    /// Extra absolute prefixes to treat as toolchain headers. The built-in
    /// heuristics cover the MSVC toolset, the Windows SDK, MinGW and the usual
    /// Unix locations; anything else, Qt included, lands in External.
    void addSystemPrefix(std::string_view prefix);

    [[nodiscard]]
    auto sourceRoot() const -> const std::string &
    {
        return m_sourceRoot;
    }

    [[nodiscard]]
    auto buildRoot() const -> const std::string &
    {
        return m_buildRoot;
    }

    [[nodiscard]]
    auto classify(std::string_view path) const -> PathBucket;

    /// Normalized path plus bucket prefix, ready to be used as a treemap key.
    /// `path` may be absolute or relative to the build root.
    [[nodiscard]]
    auto toTreePath(std::string_view path) const -> std::string;

private:
    std::string m_sourceRoot; ///< normalized, display form
    std::string m_buildRoot;
    std::string m_sourceRootKey; ///< pathKey form, with trailing '/'
    std::string m_buildRootKey;
    std::vector<std::string> m_systemPrefixes; ///< pathKey form
};

}
