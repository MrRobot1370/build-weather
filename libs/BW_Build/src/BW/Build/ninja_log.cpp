#include "BW/Build/ninja_log.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace BW::Build
{

namespace {

/// Newest header version whose layout has actually been checked. v5 is the
/// documented one; v6 (ninja 1.12) was verified byte for byte against the
/// ninja that ships with Visual Studio 2022 and keeps the same five
/// tab-separated fields.
constexpr int kNewestKnownVersion = 6;

template <typename T>
auto toInteger(std::string_view text, T &out) -> bool
{
    if (text.empty()) {
        return false;
    }
    const char *first = text.data();
    const char *last = text.data() + text.size();
    const auto result = std::from_chars(first, last, out);
    return result.ec == std::errc {} && result.ptr == last;
}

auto splitTabs(std::string_view line, std::string_view (&fields)[5])
    -> std::size_t
{
    std::size_t count = 0;
    std::size_t start = 0;
    while (count < 5) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string_view::npos) {
            fields[count++] = line.substr(start);
            break;
        }
        fields[count++] = line.substr(start, tab - start);
        start = tab + 1;
    }
    return count;
}

}

auto stepKindName(StepKind kind) -> const char *
{
    switch (kind) {
    case StepKind::Compile :
        return "compile";
    case StepKind::Link :
        return "link";
    case StepKind::Generate :
        return "generate";
    case StepKind::Other :
        break;
    }
    return "other";
}

auto parseNinjaLogLine(std::string_view line) -> std::optional<TargetRecord>
{
    std::string_view fields[5] {};
    const std::size_t count = splitTabs(line, fields);
    if (count < 4) {
        return std::nullopt;
    }

    TargetRecord record;
    if (!toInteger(fields[0], record.startMs)
        || !toInteger(fields[1], record.endMs)) {
        return std::nullopt;
    }
    // mtime is a signed 64-bit value in some ninja versions and unsigned in
    // others; we never do arithmetic on it, so a failed parse is not fatal.
    std::int64_t mtime = 0;
    if (toInteger(fields[2], mtime)) {
        record.mtime = static_cast<std::uint64_t>(mtime);
    }
    if (fields[3].empty()) {
        return std::nullopt;
    }
    record.output = Core::normalizePath(fields[3]);

    if (count >= 5 && !fields[4].empty()) {
        // The hash is printed in hexadecimal without a 0x prefix.
        const char *first = fields[4].data();
        const char *last = fields[4].data() + fields[4].size();
        std::uint64_t hash = 0;
        if (std::from_chars(first, last, hash, 16).ec == std::errc {}) {
            record.commandHash = hash;
        }
    }
    return record;
}

auto parseNinjaLog(std::string_view content) -> NinjaLog
{
    NinjaLog log;
    log.entries.reserve(content.size() / 64 + 8);

    std::size_t lineNumber = 0;
    std::size_t pos = 0;
    std::size_t malformed = 0;

    while (pos <= content.size()) {
        const std::size_t nl = std::min(content.find('\n', pos), content.size());
        std::string_view line = content.substr(pos, nl - pos);
        pos = nl + 1;
        ++lineNumber;

        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty()) {
            continue;
        }

        if (line.front() == '#') {
            // "# ninja log v5"
            const std::size_t v = line.rfind(" v");
            if (v != std::string_view::npos) {
                int version = 0;
                if (toInteger(line.substr(v + 2), version)) {
                    log.version = version;
                }
            }
            continue;
        }

        if (auto record = parseNinjaLogLine(line)) {
            log.entries.push_back(std::move(*record));
        }
        else if (++malformed <= 5) {
            log.diagnostics.push_back(
                { ParseDiagnostic::Severity::Warning,
                  lineNumber,
                  "skipped malformed entry" });
        }
    }

    if (log.version == 0 && !log.entries.empty()) {
        log.diagnostics.push_back(
            { ParseDiagnostic::Severity::Warning,
              0,
              "no '# ninja log vN' header; assuming v5 layout" });
        log.version = 5;
    }
    else if (log.version > kNewestKnownVersion) {
        log.diagnostics.push_back(
            { ParseDiagnostic::Severity::Warning,
              0,
              "log version " + std::to_string(log.version)
                  + " is newer than v"
                  + std::to_string(kNewestKnownVersion)
                  + ", the newest layout this parser has been checked "
                    "against; fields may be misread" });
    }
    if (log.entries.empty()) {
        log.diagnostics.push_back(
            { ParseDiagnostic::Severity::Error,
              0,
              "no usable entries in the log" });
    }

    return log;
}

auto readNinjaLog(const std::string &path, std::string &error)
    -> std::optional<NinjaLog>
{
    std::ifstream in { path, std::ios::binary };
    if (!in) {
        error = "cannot open " + path;
        return std::nullopt;
    }
    std::string content { std::istreambuf_iterator<char> { in },
                          std::istreambuf_iterator<char> {} };
    if (content.empty()) {
        error = path + " is empty";
        return std::nullopt;
    }
    error.clear();
    return parseNinjaLog(content);
}

auto NinjaLog::hasError() const -> bool
{
    return std::any_of(
        diagnostics.begin(),
        diagnostics.end(),
        [](const ParseDiagnostic &d) {
            return d.severity == ParseDiagnostic::Severity::Error;
        });
}

auto NinjaLog::latestPerOutput() const -> std::vector<TargetRecord>
{
    std::unordered_map<std::string, std::size_t> byKey;
    byKey.reserve(entries.size());
    std::vector<TargetRecord> out;
    out.reserve(entries.size());

    for (const auto &entry : entries) {
        const std::string key = Core::pathKey(entry.output);
        const auto it = byKey.find(key);
        if (it == byKey.end()) {
            byKey.emplace(key, out.size());
            out.push_back(entry);
        }
        else {
            out[it->second] = entry; // later entry wins
        }
    }

    std::sort(
        out.begin(),
        out.end(),
        [](const TargetRecord &a, const TargetRecord &b) {
            return a.output < b.output;
        });
    return out;
}

auto NinjaLog::lastInvocationEntries() const -> std::vector<TargetRecord>
{
    // Ninja builds an output at most once per invocation, so walking
    // backwards until an output repeats lands on the boundary of the most
    // recent run without relying on any timestamp ordering.
    std::unordered_set<std::string> seen;
    seen.reserve(entries.size());

    std::size_t first = entries.size();
    for (std::size_t i = entries.size(); i-- > 0;) {
        const std::string key = Core::pathKey(entries[i].output);
        if (!seen.insert(key).second) {
            break;
        }
        first = i;
    }

    return { entries.begin() + static_cast<std::ptrdiff_t>(first),
             entries.end() };
}

auto NinjaLog::spansMultipleBuilds() const -> bool
{
    std::unordered_set<std::string> seen;
    seen.reserve(entries.size());
    for (const auto &entry : entries) {
        if (!seen.insert(Core::pathKey(entry.output)).second) {
            return true;
        }
    }
    return false;
}

NinjaLogTail::NinjaLogTail(std::string path)
    : m_path { std::move(path) }
{
}

void NinjaLogTail::setPath(std::string path)
{
    m_path = std::move(path);
    reset();
}

void NinjaLogTail::reset()
{
    m_offset = 0;
    m_pending.clear();
}

void NinjaLogTail::seekToEnd()
{
    m_pending.clear();
    std::ifstream in { m_path, std::ios::binary | std::ios::ate };
    m_offset = in ? static_cast<std::uint64_t>(in.tellg()) : 0;
}

auto NinjaLogTail::poll(bool *restarted) -> std::vector<TargetRecord>
{
    if (restarted != nullptr) {
        *restarted = false;
    }
    std::vector<TargetRecord> fresh;
    if (m_path.empty()) {
        return fresh;
    }

    std::ifstream in { m_path, std::ios::binary | std::ios::ate };
    if (!in) {
        return fresh;
    }
    const auto size = static_cast<std::uint64_t>(in.tellg());
    if (size < m_offset) {
        // Truncated or recompacted underneath us: start over.
        m_offset = 0;
        m_pending.clear();
        if (restarted != nullptr) {
            *restarted = true;
        }
    }
    if (size == m_offset) {
        return fresh;
    }

    in.seekg(static_cast<std::streamoff>(m_offset), std::ios::beg);
    std::string chunk;
    chunk.resize(static_cast<std::size_t>(size - m_offset));
    in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    chunk.resize(static_cast<std::size_t>(in.gcount()));
    m_offset += chunk.size();

    m_pending += chunk;
    std::size_t start = 0;
    while (true) {
        const std::size_t nl = m_pending.find('\n', start);
        if (nl == std::string::npos) {
            break;
        }
        std::string_view line { m_pending.data() + start, nl - start };
        start = nl + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (auto record = parseNinjaLogLine(line)) {
            fresh.push_back(std::move(*record));
        }
    }
    m_pending.erase(0, start);
    return fresh;
}

}
