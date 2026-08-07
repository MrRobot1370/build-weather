#include "BW/Build/build_snapshot.h"

#include <algorithm>
#include <unordered_map>

namespace BW::Build
{

namespace {

auto stepKindFromOutput(std::string_view output) -> StepKind
{
    const std::string ext = Core::extension(output);
    if (ext == ".obj" || ext == ".o") {
        return StepKind::Compile;
    }
    if (ext == ".exe" || ext == ".dll" || ext == ".lib" || ext == ".so"
        || ext == ".a" || ext == ".pdb" || ext == ".dylib") {
        return StepKind::Link;
    }
    if (ext.empty()) {
        return StepKind::Other;
    }
    return StepKind::Generate;
}

auto resolveSource(
    const TargetRecord &record,
    const SnapshotOptions &opts) -> std::string
{
    if (opts.commands != nullptr) {
        if (auto source = opts.commands->sourceForOutput(record.output)) {
            return *source;
        }
    }
    const std::string guess = guessSourceFromObject(record.output);
    if (guess.empty()) {
        return {};
    }
    // The guess strips the CMakeFiles infix out of a build-relative object
    // path, and what is left mirrors the *source* tree, so it is resolved
    // against the source root.
    return Core::joinPath(opts.classifier.sourceRoot(), guess);
}

}

auto BuildSnapshot::fromNinjaLog(
    const NinjaLog &log,
    const SnapshotOptions &opts) -> BuildSnapshot
{
    if (opts.scope == LogScope::LastInvocation) {
        return fromRecords(log.lastInvocationEntries(), opts);
    }
    return fromRecords(log.latestPerOutput(), opts);
}

auto BuildSnapshot::fromRecords(
    const std::vector<TargetRecord> &records,
    const SnapshotOptions &opts) -> BuildSnapshot
{
    BuildSnapshot snapshot;
    snapshot.m_targets.reserve(records.size());

    // An edge with several output names is logged once per name, so the same
    // file appears twice: `qml/a.qml` and `<build>/qml/a.qml`. They are only
    // the same string after normalization against the build root, which is
    // why the de-duplication happens here and not in the log parser.
    std::unordered_map<std::string, std::size_t> seenOutputs;
    seenOutputs.reserve(records.size());

    for (const auto &record : records) {
        const std::string outputKey = Core::pathKey(
            Core::isAbsolutePath(record.output)
                ? record.output
                : Core::joinPath(opts.classifier.buildRoot(), record.output));
        if (const auto it = seenOutputs.find(outputKey);
            it != seenOutputs.end()) {
            // Same edge under another name: keep the longer measurement
            // rather than counting the work twice.
            TargetInfo &existing = snapshot.m_targets[it->second];
            if (record.durationMs() > existing.durationMs) {
                existing.startMs = record.startMs;
                existing.endMs = record.endMs;
                existing.durationMs = record.durationMs();
            }
            continue;
        }
        seenOutputs.emplace(outputKey, snapshot.m_targets.size());

        TargetInfo info;
        info.output = record.output;
        info.startMs = record.startMs;
        info.endMs = record.endMs;
        info.durationMs = record.durationMs();
        info.kind = stepKindFromOutput(record.output);
        info.source = resolveSource(record, opts);

        const std::string &anchor
            = info.source.empty() ? info.output : info.source;
        info.treePath = opts.classifier.toTreePath(anchor);
        info.bucket = opts.classifier.classify(
            Core::isAbsolutePath(anchor)
                ? anchor
                : Core::joinPath(opts.classifier.buildRoot(), anchor));

        if (info.treePath.empty()) {
            info.treePath = record.output;
        }
        snapshot.m_targets.push_back(std::move(info));
    }

    snapshot.finalize();
    return snapshot;
}

void BuildSnapshot::finalize()
{
    // Several steps can land on one file: a generated source is written by
    // one edge and compiled by another, and both belong at that leaf. The
    // treemap needs exactly one leaf per path, and summing is also the
    // honest answer to "what does this file cost me", so they merge.
    {
        std::unordered_map<std::string, std::size_t> byTreePath;
        byTreePath.reserve(m_targets.size());
        std::vector<TargetInfo> merged;
        merged.reserve(m_targets.size());

        for (auto &target : m_targets) {
            const std::string key = Core::pathKey(target.treePath);
            const auto it = byTreePath.find(key);
            if (it == byTreePath.end()) {
                byTreePath.emplace(key, merged.size());
                merged.push_back(std::move(target));
                continue;
            }

            TargetInfo &existing = merged[it->second];
            // The dominant step decides how the leaf is labelled and
            // coloured; a 2 second compile is what the file is, not the
            // 30 ms moc run that produced it.
            if (target.durationMs > existing.durationMs) {
                existing.kind = target.kind;
                existing.output = target.output;
                if (!target.source.empty()) {
                    existing.source = target.source;
                }
            }
            existing.durationMs += target.durationMs;
            existing.startMs = std::min(existing.startMs, target.startMs);
            existing.endMs = std::max(existing.endMs, target.endMs);
            existing.stepCount += target.stepCount;
        }
        m_targets = std::move(merged);
    }

    // Deterministic order first, so rank ties break the same way every run.
    std::sort(
        m_targets.begin(),
        m_targets.end(),
        [](const TargetInfo &a, const TargetInfo &b) {
            return a.treePath < b.treePath;
        });

    std::vector<std::size_t> order(m_targets.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::sort(
        order.begin(),
        order.end(),
        [this](std::size_t a, std::size_t b) {
            if (m_targets[a].durationMs != m_targets[b].durationMs) {
                return m_targets[a].durationMs > m_targets[b].durationMs;
            }
            return m_targets[a].treePath < m_targets[b].treePath;
        });
    for (std::size_t i = 0; i < order.size(); ++i) {
        m_targets[order[i]].rank = static_cast<int>(i) + 1;
    }

    m_stats = SnapshotStats {};
    m_stats.targetCount = m_targets.size();
    if (m_targets.empty()) {
        return;
    }

    Millis lo = m_targets.front().startMs;
    Millis hi = m_targets.front().endMs;
    for (const auto &target : m_targets) {
        m_stats.totalCpuMs += target.durationMs;
        m_stats.maxMs = std::max(m_stats.maxMs, target.durationMs);
        lo = std::min(lo, target.startMs);
        hi = std::max(hi, target.endMs);
    }
    m_stats.wallMs = hi > lo ? hi - lo : 0;

    std::vector<Millis> durations;
    durations.reserve(m_targets.size());
    for (const auto &target : m_targets) {
        durations.push_back(target.durationMs);
    }
    const std::size_t mid = durations.size() / 2;
    std::nth_element(
        durations.begin(),
        durations.begin() + static_cast<std::ptrdiff_t>(mid),
        durations.end());
    m_stats.medianMs = durations[mid];

    // Event stream, sorted so a scrub can binary-search it.
    m_timeline.clear();
    m_timeline.reserve(m_targets.size() * 2);
    for (std::size_t i = 0; i < m_targets.size(); ++i) {
        m_timeline.push_back({ m_targets[i].startMs,
                               static_cast<int>(i),
                               BuildEvent::Type::Start });
        m_timeline.push_back({ m_targets[i].endMs,
                               static_cast<int>(i),
                               BuildEvent::Type::Finish });
    }
    std::sort(
        m_timeline.begin(),
        m_timeline.end(),
        [](const BuildEvent &a, const BuildEvent &b) {
            if (a.timeMs != b.timeMs) {
                return a.timeMs < b.timeMs;
            }
            // Finish before start at the same millisecond, so parallelism
            // never reads high because of a handover.
            if (a.type != b.type) {
                return a.type == BuildEvent::Type::Finish;
            }
            return a.targetIndex < b.targetIndex;
        });

    int active = 0;
    for (const auto &event : m_timeline) {
        active += event.type == BuildEvent::Type::Start ? 1 : -1;
        m_stats.peakParallelism = std::max(m_stats.peakParallelism, active);
    }
}

auto BuildSnapshot::indexOfTreePath(std::string_view treePath) const -> int
{
    const std::string key = Core::pathKey(treePath);
    for (std::size_t i = 0; i < m_targets.size(); ++i) {
        if (Core::pathKey(m_targets[i].treePath) == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

auto BuildSnapshot::parallelismAt(Millis timeMs) const -> int
{
    int active = 0;
    for (const auto &event : m_timeline) {
        if (event.timeMs > timeMs) {
            break;
        }
        active += event.type == BuildEvent::Type::Start ? 1 : -1;
    }
    return active;
}

auto compareSnapshots(
    const BuildSnapshot &baseline,
    const BuildSnapshot &current) -> std::vector<TargetDelta>
{
    std::unordered_map<std::string, Millis> before;
    before.reserve(baseline.targets().size());
    for (const auto &target : baseline.targets()) {
        before[Core::pathKey(target.treePath)] += target.durationMs;
    }

    std::vector<TargetDelta> deltas;
    deltas.reserve(current.targets().size() + before.size());

    for (const auto &target : current.targets()) {
        const std::string key = Core::pathKey(target.treePath);
        const auto it = before.find(key);
        TargetDelta delta;
        delta.treePath = target.treePath;
        delta.currentMs = target.durationMs;
        if (it == before.end()) {
            delta.state = TargetDelta::State::Added;
            delta.deltaMs = target.durationMs;
        }
        else {
            delta.baselineMs = it->second;
            delta.deltaMs = target.durationMs - it->second;
            before.erase(it);
        }
        deltas.push_back(std::move(delta));
    }

    for (const auto &target : baseline.targets()) {
        const std::string key = Core::pathKey(target.treePath);
        if (before.find(key) == before.end()) {
            continue; // already matched
        }
        TargetDelta delta;
        delta.treePath = target.treePath;
        delta.baselineMs = target.durationMs;
        delta.deltaMs = -target.durationMs;
        delta.state = TargetDelta::State::Removed;
        deltas.push_back(std::move(delta));
        before.erase(key);
    }

    std::sort(
        deltas.begin(),
        deltas.end(),
        [](const TargetDelta &a, const TargetDelta &b) {
            if (a.deltaMs != b.deltaMs) {
                return a.deltaMs > b.deltaMs;
            }
            return a.treePath < b.treePath;
        });
    return deltas;
}

}
