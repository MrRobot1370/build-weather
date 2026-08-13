#include "build_model.h"

#include "BW/Core/logger.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

namespace BW::UI
{

namespace {

/// One clock for every animation, so the settle is identical whether it came
/// from a live event or a replay scrub.
auto nowMs() -> qint64
{
    static QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return timer.elapsed();
}

auto toStd(const QString &text) -> std::string
{
    return text.toStdString();
}

/// Area floor: a zero-duration leaf still has to be visible, or the tree
/// stops matching the disk.
constexpr double kMinLeafValue = 0.35;

/// Backdated past the settle, so a leaf reads as long since finished.
/// Loading a log is not a build: without this the whole map renders in the
/// completion-flash colour.
constexpr qint64 kAlreadySettledMs = 10000;

auto areaValue(Build::Millis durationMs) -> double
{
    return std::max(kMinLeafValue, static_cast<double>(durationMs));
}

}

BuildModel::BuildModel(QObject *parent)
    : QObject { parent }
{
}

// --- loading -----------------------------------------------------------------

auto BuildModel::loadBuildDirectory(const QString &directory) -> bool
{
    const QString clean = QDir::fromNativeSeparators(directory);
    return loadNinjaLog(clean + "/.ninja_log");
}

auto BuildModel::loadNinjaLog(const QString &path) -> bool
{
    const QString clean = QDir::fromNativeSeparators(path);
    const QFileInfo info { clean };
    if (!info.exists()) {
        m_status = QStringLiteral("Not found: %1").arg(clean);
        Q_EMIT statusChanged();
        return false;
    }

    m_ninjaLogPath = info.absoluteFilePath();
    m_buildDirectory = info.absolutePath();

    std::string error;
    auto log = Build::readNinjaLog(toStd(m_ninjaLogPath), error);
    if (!log) {
        m_status = QString::fromStdString(error);
        Q_EMIT statusChanged();
        return false;
    }
    m_log = std::move(*log);

    // the reliable object-to-source join; without it the fallback is a guess
    std::string commandsError;
    m_haveCommands
        = m_commands.load(toStd(m_buildDirectory), commandsError);
    if (m_haveCommands && m_sourceDirectory.isEmpty()) {
        const std::string root
            = Build::inferSourceRoot(m_commands, toStd(m_buildDirectory));
        if (!root.empty()) {
            m_sourceDirectory = QString::fromStdString(root);
        }
    }
    if (m_sourceDirectory.isEmpty()) {
        // GUESS: the usual in-project layout is <source>/build/<preset>.
        m_sourceDirectory = QString::fromStdString(
            Core::normalizePath(toStd(m_buildDirectory) + "/../.."));
    }

    rebuildFromLog();
    Q_EMIT sourceChanged();
    return true;
}

auto BuildModel::reload() -> bool
{
    return m_ninjaLogPath.isEmpty() ? false : loadNinjaLog(m_ninjaLogPath);
}

void BuildModel::rebuildFromLog()
{
    Build::SnapshotOptions options;
    options.classifier.setSourceRoot(toStd(m_sourceDirectory));
    options.classifier.setBuildRoot(toStd(m_buildDirectory));
    options.commands = m_haveCommands ? &m_commands : nullptr;
    options.scope = m_scope;

    m_snapshot = Build::BuildSnapshot::fromNinjaLog(m_log, options);
    m_snapshot.setLabel(m_buildDirectory.toStdString());
    m_multiBuildLog = m_log.spansMultipleBuilds();

    m_diagnostics.clear();
    for (const auto &diagnostic : m_log.diagnostics) {
        m_diagnostics << QString::fromStdString(diagnostic.message);
    }
    if (!m_haveCommands) {
        m_diagnostics << QStringLiteral(
            "No compile_commands.json in the build directory; source files "
            "are inferred from the CMake object path convention.");
    }

    m_status = m_multiBuildLog
        ? QStringLiteral("%1 steps, log covers more than one build")
              .arg(m_snapshot.stats().targetCount)
        : QStringLiteral("%1 steps from a single build")
              .arg(m_snapshot.stats().targetCount);

    m_live = false;
    m_runningCount = 0;
    rebuildTree();
    recomputeDeltas();

    Q_EMIT statusChanged();
    Q_EMIT statsChanged();
    Q_EMIT liveChanged();
}

void BuildModel::rebuildTree()
{
    const auto &targets = m_snapshot.targets();

    Treemap::TreeBuilder builder;
    m_leaves.assign(targets.size(), LeafVisual {});
    m_indexByTreeKey.clear();
    m_indexByOutputKey.clear();
    m_indexByTreeKey.reserve(targets.size());
    m_indexByOutputKey.reserve(targets.size());

    for (std::size_t i = 0; i < targets.size(); ++i) {
        const auto &target = targets[i];
        LeafVisual &leaf = m_leaves[i];
        leaf.durationMs = target.durationMs;
        leaf.value = areaValue(target.durationMs);
        leaf.state = m_live ? LeafState::Pending : LeafState::Finished;
        leaf.changedAtMs = m_live ? nowMs() : nowMs() - kAlreadySettledMs;

        builder.add(target.treePath, leaf.value, static_cast<int>(i));
        m_indexByTreeKey.emplace(
            Core::pathKey(target.treePath),
            static_cast<int>(i));
        m_indexByOutputKey.emplace(
            Core::pathKey(target.output),
            static_cast<int>(i));
    }

    recomputeScale();
    m_tree = builder.build();
    m_treeDirty = false;
    ++m_treeRevision;
    ++m_valueRevision;
    Q_EMIT treeChanged();
}

void BuildModel::touchValues()
{
    ++m_valueRevision;
    Q_EMIT valuesChanged();
}

void BuildModel::recomputeScale()
{
    // the slowest step alone would flatten the scale, so it tops out at p98
    if (m_leaves.empty()) {
        m_scaleMaxMs = 1.0;
        return;
    }
    std::vector<Build::Millis> durations;
    durations.reserve(m_leaves.size());
    for (const auto &leaf : m_leaves) {
        durations.push_back(leaf.durationMs);
    }
    const std::size_t index = static_cast<std::size_t>(
        std::llround(0.98 * static_cast<double>(durations.size() - 1)));
    std::nth_element(
        durations.begin(),
        durations.begin() + static_cast<std::ptrdiff_t>(index),
        durations.end());
    m_scaleMaxMs = std::max(1.0, static_cast<double>(durations[index]));
}

void BuildModel::setSourceDirectory(const QString &directory)
{
    const QString clean = QString::fromStdString(
        Core::normalizePath(toStd(QDir::fromNativeSeparators(directory))));
    if (clean == m_sourceDirectory) {
        return;
    }
    m_sourceDirectory = clean;
    if (!m_ninjaLogPath.isEmpty()) {
        rebuildFromLog();
    }
    Q_EMIT sourceChanged();
}

void BuildModel::setScope(int scope)
{
    const auto value = scope == 1 ? Build::LogScope::LastInvocation
                                  : Build::LogScope::LatestPerOutput;
    if (value == m_scope) {
        return;
    }
    m_scope = value;
    if (!m_ninjaLogPath.isEmpty()) {
        rebuildFromLog();
    }
    Q_EMIT scopeChanged();
}

void BuildModel::setFocusPath(const QString &path)
{
    if (path == m_focusPath) {
        return;
    }
    m_focusPath = path;
    Q_EMIT focusChanged();
}

auto BuildModel::indexOfTreePath(const QString &treePath) const -> int
{
    const auto it = m_indexByTreeKey.find(Core::pathKey(toStd(treePath)));
    return it == m_indexByTreeKey.end() ? -1 : it->second;
}

auto BuildModel::leafIndexForOutput(const QString &outputPath) const -> int
{
    const std::string key = Core::pathKey(toStd(outputPath));
    if (const auto it = m_indexByOutputKey.find(key);
        it != m_indexByOutputKey.end()) {
        return it->second;
    }
    // the description's path can differ in leading segments from the log's
    // output path, so fall back to the file name
    const std::string name
        = Core::pathKey(Core::fileName(toStd(outputPath)));
    for (const auto &[outputKey, index] : m_indexByOutputKey) {
        if (Core::pathKey(Core::fileName(outputKey)) == name) {
            return index;
        }
    }
    return -1;
}

// --- comparison ---------------------------------------------------------------

auto BuildModel::loadBaseline(const QString &ninjaLogPath) -> bool
{
    if (ninjaLogPath.isEmpty()) {
        clearBaseline();
        return true;
    }

    const QString clean = QDir::fromNativeSeparators(ninjaLogPath);
    std::string error;
    const auto log = Build::readNinjaLog(toStd(clean), error);
    if (!log) {
        m_status = QString::fromStdString(error);
        Q_EMIT statusChanged();
        return false;
    }

    Build::SnapshotOptions options;
    options.classifier.setSourceRoot(toStd(m_sourceDirectory));
    // resolve the baseline against its own build directory, so its tree
    // paths line up with ours
    options.classifier.setBuildRoot(
        Core::parentPath(toStd(QFileInfo { clean }.absoluteFilePath())));
    options.commands = m_haveCommands ? &m_commands : nullptr;
    options.scope = m_scope;

    m_baseline = Build::BuildSnapshot::fromNinjaLog(*log, options);
    m_baselineLabel = QFileInfo { clean }.absoluteFilePath();
    m_comparing = true;
    recomputeDeltas();
    Q_EMIT comparisonChanged();
    touchValues();
    return true;
}

void BuildModel::clearBaseline()
{
    if (!m_comparing) {
        return;
    }
    m_baseline = Build::BuildSnapshot {};
    m_baselineLabel.clear();
    m_comparing = false;
    recomputeDeltas();
    Q_EMIT comparisonChanged();
    touchValues();
}

void BuildModel::recomputeDeltas()
{
    m_deltas.clear();
    m_maxAbsDeltaMs = 0.0;
    for (auto &leaf : m_leaves) {
        leaf.deltaMs = 0;
        leaf.inBaseline = false;
    }
    if (!m_comparing) {
        return;
    }

    m_deltas = Build::compareSnapshots(m_baseline, m_snapshot);
    for (const auto &delta : m_deltas) {
        m_maxAbsDeltaMs = std::max(
            m_maxAbsDeltaMs,
            static_cast<double>(std::abs(delta.deltaMs)));
        const auto it = m_indexByTreeKey.find(Core::pathKey(delta.treePath));
        if (it == m_indexByTreeKey.end()) {
            continue; // removed in the current build: no leaf to colour
        }
        LeafVisual &leaf = m_leaves[static_cast<std::size_t>(it->second)];
        leaf.deltaMs = delta.deltaMs;
        leaf.inBaseline = delta.state != Build::TargetDelta::State::Added;
    }
}

// --- live mode ----------------------------------------------------------------

void BuildModel::beginLive()
{
    m_live = true;
    m_runningCount = 0;
    const qint64 now = nowMs();
    for (auto &leaf : m_leaves) {
        leaf.state = LeafState::Pending;
        leaf.changedAtMs = now;
    }
    Q_EMIT liveChanged();
    touchValues();
}

void BuildModel::noteStarted(const QString &outputPath)
{
    const int index = leafIndexForOutput(outputPath);
    if (index < 0) {
        // new target: it gets a leaf on the next finish event, which carries
        // a real duration to size it with
        return;
    }
    LeafVisual &leaf = m_leaves[static_cast<std::size_t>(index)];
    if (leaf.state != LeafState::Running) {
        leaf.state = LeafState::Running;
        leaf.changedAtMs = nowMs();
        ++m_runningCount;
        Q_EMIT liveChanged();
    }
    touchValues();
}

void BuildModel::noteFinished(const Build::TargetRecord &record)
{
    const int index
        = leafIndexForOutput(QString::fromStdString(record.output));
    if (index < 0) {
        // New target: remember it and relayout once the burst settles.
        m_treeDirty = true;
        return;
    }

    LeafVisual &leaf = m_leaves[static_cast<std::size_t>(index)];
    if (leaf.state == LeafState::Running) {
        m_runningCount = std::max(0, m_runningCount - 1);
    }
    leaf.state = LeafState::Finished;
    leaf.durationMs = record.durationMs();
    leaf.changedAtMs = nowMs();
    recomputeScale();
    // The area does not change mid-build: a box resizing under the cursor is
    // unreadable. The new value lands at the next relayout.
    Q_EMIT liveChanged();
    touchValues();
}

void BuildModel::endLive()
{
    m_live = false;
    m_runningCount = 0;
    const qint64 now = nowMs();
    for (auto &leaf : m_leaves) {
        if (leaf.state != LeafState::Finished) {
            leaf.state = LeafState::Finished;
            leaf.changedAtMs = now;
        }
    }
    // re-read the log so durations, ranks and areas share one source
    if (!m_ninjaLogPath.isEmpty()) {
        std::string error;
        if (auto log = Build::readNinjaLog(toStd(m_ninjaLogPath), error)) {
            m_log = std::move(*log);
            rebuildFromLog();
            return;
        }
    }
    if (m_treeDirty) {
        rebuildTree();
    }
    Q_EMIT liveChanged();
    touchValues();
}

// --- replay -------------------------------------------------------------------

void BuildModel::applyReplayTime(qint64 timeMs)
{
    const auto &targets = m_snapshot.targets();
    if (targets.size() != m_leaves.size()) {
        return;
    }

    const qint64 now = nowMs();
    int running = 0;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const auto &target = targets[i];
        LeafVisual &leaf = m_leaves[i];

        LeafState state = LeafState::Pending;
        if (timeMs >= target.endMs) {
            state = LeafState::Finished;
        }
        else if (timeMs >= target.startMs) {
            state = LeafState::Running;
            ++running;
        }

        if (state != leaf.state) {
            leaf.state = state;
            // anchored to the replayed finish, so scrubbing back does not
            // re-trigger the fade
            leaf.changedAtMs = state == LeafState::Finished
                ? now - std::min<qint64>(timeMs - target.endMs, 10000)
                : now;
        }
    }

    if (running != m_runningCount) {
        m_runningCount = running;
        Q_EMIT liveChanged();
    }
    touchValues();
}

void BuildModel::clearReplay()
{
    const qint64 now = nowMs();
    for (auto &leaf : m_leaves) {
        leaf.state = LeafState::Finished;
        leaf.changedAtMs = now - kAlreadySettledMs;
    }
    m_runningCount = 0;
    Q_EMIT liveChanged();
    touchValues();
}

}
