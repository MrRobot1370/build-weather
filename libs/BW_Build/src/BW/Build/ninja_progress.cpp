#include "BW/Build/ninja_progress.h"

#include <algorithm>
#include <charconv>

namespace BW::Build
{

namespace {

auto trim(std::string_view text) -> std::string_view
{
    while (!text.empty()
        && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty()
        && (text.back() == ' ' || text.back() == '\t'
            || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

auto startsWith(std::string_view text, std::string_view prefix) -> bool
{
    return text.size() >= prefix.size()
        && text.compare(0, prefix.size(), prefix) == 0;
}

auto lastToken(std::string_view text) -> std::string_view
{
    const std::size_t space = text.find_last_of(" \t");
    return space == std::string_view::npos ? text : text.substr(space + 1);
}

/// A description word only counts as a path when it looks like one; this
/// keeps "Automatic MOC for target Foo" from claiming "Foo" is a file.
auto looksLikePath(std::string_view text) -> bool
{
    if (text.size() < 3) {
        return false;
    }
    return text.find('/') != std::string_view::npos
        || text.find('\\') != std::string_view::npos
        || text.find('.') != std::string_view::npos;
}

}

auto stepKindFromDescription(std::string_view description) -> StepKind
{
    const std::string_view text = trim(description);
    if (startsWith(text, "Building ")) {
        return StepKind::Compile;
    }
    if (startsWith(text, "Linking ") || startsWith(text, "Creating library")) {
        return StepKind::Link;
    }
    if (startsWith(text, "Generating ")
        || startsWith(text, "Automatic MOC")
        || startsWith(text, "Automatic RCC")
        || startsWith(text, "Automatic UIC")
        || startsWith(text, "Running ")
        || startsWith(text, "Copying ")) {
        return StepKind::Generate;
    }
    return StepKind::Other;
}

auto outputPathFromDescription(std::string_view description) -> std::string
{
    const std::string_view text = trim(description);

    // "Building CXX object <path>" / "Building CUDA object <path>"
    if (startsWith(text, "Building ")) {
        constexpr std::string_view marker { " object " };
        const std::size_t at = text.find(marker);
        if (at != std::string_view::npos) {
            const std::string_view tail
                = trim(text.substr(at + marker.size()));
            if (looksLikePath(tail)) {
                return Core::normalizePath(tail);
            }
        }
        return {};
    }

    // "Linking CXX executable bin/foo.exe", "Linking CXX static library a.lib"
    if (startsWith(text, "Linking ")) {
        const std::string_view tail = lastToken(text);
        if (looksLikePath(tail)) {
            return Core::normalizePath(tail);
        }
        return {};
    }

    // "Generating moc_foo.cpp", "Generating ui_bar.h"
    if (startsWith(text, "Generating ")) {
        const std::string_view tail = trim(text.substr(11));
        if (looksLikePath(tail) && tail.find(' ') == std::string_view::npos) {
            return Core::normalizePath(tail);
        }
        return {};
    }

    return {};
}

auto parseProgressLine(std::string_view line) -> std::optional<ProgressLine>
{
    std::string_view text = trim(line);
    if (text.size() < 6 || text.front() != '[') {
        return std::nullopt;
    }

    const std::size_t close = text.find(']');
    if (close == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view counters = text.substr(1, close - 1);
    const std::size_t slash = counters.find('/');
    if (slash == std::string_view::npos) {
        return std::nullopt;
    }

    ProgressLine progress;
    const auto parseInt = [](std::string_view field, int &out) {
        field = trim(field);
        const char *first = field.data();
        const char *last = field.data() + field.size();
        const auto result = std::from_chars(first, last, out);
        return result.ec == std::errc {} && result.ptr == last;
    };

    // "[12/340]" or "[12/340 8]": the optional third number is %r.
    std::string_view totalField = counters.substr(slash + 1);
    if (const std::size_t space = totalField.find(' ');
        space != std::string_view::npos) {
        if (!parseInt(totalField.substr(space + 1), progress.running)) {
            progress.running = -1;
        }
        totalField = totalField.substr(0, space);
    }
    if (!parseInt(counters.substr(0, slash), progress.finished)
        || !parseInt(totalField, progress.total)) {
        return std::nullopt;
    }
    if (progress.total <= 0 || progress.finished < 0) {
        return std::nullopt;
    }

    progress.description = std::string { trim(text.substr(close + 1)) };
    progress.kind = stepKindFromDescription(progress.description);
    progress.outputPath = outputPathFromDescription(progress.description);
    return progress;
}

auto ProgressStream::feed(std::string_view bytes) -> Chunk
{
    Chunk chunk;
    m_pending.append(bytes);

    std::size_t start = 0;
    while (true) {
        const std::size_t brk = m_pending.find_first_of("\r\n", start);
        if (brk == std::string::npos) {
            break;
        }
        const std::string_view line { m_pending.data() + start, brk - start };
        if (!trim(line).empty()) {
            if (auto progress = parseProgressLine(line)) {
                chunk.progress.push_back(std::move(*progress));
            }
            else {
                chunk.other.emplace_back(trim(line));
            }
        }
        start = brk + 1;
        // Swallow the '\n' of a CRLF pair.
        if (m_pending[brk] == '\r' && start < m_pending.size()
            && m_pending[start] == '\n') {
            ++start;
        }
    }
    m_pending.erase(0, start);
    return chunk;
}

auto ProgressStream::flush() -> Chunk
{
    Chunk chunk;
    const std::string_view line = trim(m_pending);
    if (!line.empty()) {
        if (auto progress = parseProgressLine(line)) {
            chunk.progress.push_back(std::move(*progress));
        }
        else {
            chunk.other.emplace_back(line);
        }
    }
    m_pending.clear();
    return chunk;
}

void ProgressStream::reset()
{
    m_pending.clear();
}

}
