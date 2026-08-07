#pragma once

// The single source of truth the whole UI reads from.
//
// Owns the parsed snapshot, the treemap hierarchy built from it, and the
// per-leaf runtime state that live mode and replay drive. TreemapItem reads
// this through the plain C++ accessors (no QVariant round trips per leaf,
// which matters at several thousand files); QML reads the summary through
// properties.

#include "BW/Build/build_snapshot.h"
#include "BW/Build/compile_commands.h"
#include "BW/Build/ninja_log.h"
#include "BW/Treemap/tree.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace BW::UI
{

/// Where a leaf is in its lifecycle. Post-mortem data starts at Finished;
/// live mode and replay move leaves through Running.
enum class LeafState
{
    Pending, ///< known from a previous build, not touched by this one
    Running,
    Finished
};

struct LeafVisual
{
    double value { 1.0 }; ///< area weight fed to the layout
    Build::Millis durationMs { 0 };
    LeafState state { LeafState::Finished };
    /// Wall clock (ms since app start) of the last state change; drives the
    /// 400 ms settle from the in-flight highlight to the final colour.
    qint64 changedAtMs { 0 };
    /// Signed delta against the baseline build, in ms. Only meaningful when
    /// the model is in comparison mode.
    Build::Millis deltaMs { 0 };
    bool inBaseline { false };
};

class BuildModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by App")

    Q_PROPERTY(QString buildDirectory READ buildDirectory NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceDirectory READ sourceDirectory
                   WRITE setSourceDirectory NOTIFY sourceChanged)
    Q_PROPERTY(QString ninjaLogPath READ ninjaLogPath NOTIFY sourceChanged)
    Q_PROPERTY(bool hasData READ hasData NOTIFY treeChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QStringList diagnostics READ diagnostics NOTIFY statusChanged)

    Q_PROPERTY(int targetCount READ targetCount NOTIFY statsChanged)
    Q_PROPERTY(qint64 totalCpuMs READ totalCpuMs NOTIFY statsChanged)
    Q_PROPERTY(qint64 wallMs READ wallMs NOTIFY statsChanged)
    Q_PROPERTY(qint64 maxMs READ maxMs NOTIFY statsChanged)
    Q_PROPERTY(qint64 medianMs READ medianMs NOTIFY statsChanged)
    Q_PROPERTY(int peakParallelism READ peakParallelism NOTIFY statsChanged)
    Q_PROPERTY(bool usingCompileDatabase READ usingCompileDatabase
                   NOTIFY statusChanged)

    /// 0 = every target's most recent entry, 1 = the last ninja invocation.
    Q_PROPERTY(int scope READ scope WRITE setScope NOTIFY scopeChanged)
    /// True when the log accumulated over more than one build, so its
    /// timestamps come from more than one clock.
    Q_PROPERTY(bool multiBuildLog READ multiBuildLog NOTIFY statsChanged)
    /// Drill-down root; empty means the whole tree.
    Q_PROPERTY(QString focusPath READ focusPath WRITE setFocusPath
                   NOTIFY focusChanged)
    Q_PROPERTY(bool comparing READ comparing NOTIFY comparisonChanged)
    Q_PROPERTY(QString baselineLabel READ baselineLabel
                   NOTIFY comparisonChanged)
    Q_PROPERTY(qint64 baselineTotalMs READ baselineTotalMs
                   NOTIFY comparisonChanged)
    Q_PROPERTY(bool live READ live NOTIFY liveChanged)
    Q_PROPERTY(int runningCount READ runningCount NOTIFY liveChanged)

public:
    explicit BuildModel(QObject *parent = nullptr);

    // --- loading ---------------------------------------------------------

    /// Points the model at a build directory: reads `.ninja_log` and, when
    /// present, `compile_commands.json`. The source root is inferred from
    /// the compile database, or guessed two levels up from the build
    /// directory when there is none.
    Q_INVOKABLE bool loadBuildDirectory(const QString &directory);

    /// Loads a `.ninja_log` directly; the build directory is its parent.
    Q_INVOKABLE bool loadNinjaLog(const QString &path);

    Q_INVOKABLE bool reload();

    /// Loads a second build to compare against. Pass an empty string to drop
    /// the comparison.
    Q_INVOKABLE bool loadBaseline(const QString &ninjaLogPath);

    Q_INVOKABLE void clearBaseline();

    // --- accessors used by TreemapItem -----------------------------------

    [[nodiscard]]
    auto tree() const -> const Treemap::Node &
    {
        return m_tree;
    }

    [[nodiscard]]
    auto leaves() const -> const std::vector<LeafVisual> &
    {
        return m_leaves;
    }

    [[nodiscard]]
    auto targets() const -> const std::vector<Build::TargetInfo> &
    {
        return m_snapshot.targets();
    }

    [[nodiscard]]
    auto snapshot() const -> const Build::BuildSnapshot &
    {
        return m_snapshot;
    }

    [[nodiscard]]
    auto deltas() const -> const std::vector<Build::TargetDelta> &
    {
        return m_deltas;
    }

    /// Bumped when the hierarchy changes and a relayout is required.
    [[nodiscard]]
    auto treeRevision() const -> quint64
    {
        return m_treeRevision;
    }

    /// Bumped when only colours or leaf states changed; a repaint is enough.
    [[nodiscard]]
    auto valueRevision() const -> quint64
    {
        return m_valueRevision;
    }

    /// Reference maximum the heat ramp is normalised against: the 98th
    /// percentile of the step durations, so one pathological file cannot
    /// flatten the whole map. Cached, because TreemapItem asks once per cell
    /// per frame and recomputing a selection each time is O(n) per cell.
    [[nodiscard]]
    auto scaleMaxMs() const -> double
    {
        return m_scaleMaxMs;
    }

    /// Largest absolute delta, used to normalise the comparison ramp.
    [[nodiscard]]
    auto scaleMaxDeltaMs() const -> double
    {
        return m_maxAbsDeltaMs;
    }

    // --- live mode --------------------------------------------------------

    /// Resets every known leaf to Pending and starts accepting events.
    void beginLive();
    /// A target started compiling. `outputPath` is as ninja printed it.
    void noteStarted(const QString &outputPath);
    /// A target finished; the record comes from the tail of `.ninja_log`.
    void noteFinished(const Build::TargetRecord &record);
    void endLive();

    // --- replay -----------------------------------------------------------

    /// Applies the state the build was in at `timeMs` into the leaf visuals.
    Q_INVOKABLE void applyReplayTime(qint64 timeMs);
    Q_INVOKABLE void clearReplay();

    // --- Q_PROPERTY plumbing ---------------------------------------------

    [[nodiscard]]
    auto buildDirectory() const -> QString
    {
        return m_buildDirectory;
    }

    [[nodiscard]]
    auto sourceDirectory() const -> QString
    {
        return m_sourceDirectory;
    }

    void setSourceDirectory(const QString &directory);

    [[nodiscard]]
    auto ninjaLogPath() const -> QString
    {
        return m_ninjaLogPath;
    }

    [[nodiscard]]
    auto hasData() const -> bool
    {
        return !m_snapshot.empty();
    }

    [[nodiscard]]
    auto status() const -> QString
    {
        return m_status;
    }

    [[nodiscard]]
    auto diagnostics() const -> QStringList
    {
        return m_diagnostics;
    }

    [[nodiscard]]
    auto targetCount() const -> int
    {
        return static_cast<int>(m_snapshot.stats().targetCount);
    }

    [[nodiscard]]
    auto totalCpuMs() const -> qint64
    {
        return m_snapshot.stats().totalCpuMs;
    }

    [[nodiscard]]
    auto wallMs() const -> qint64
    {
        return m_snapshot.stats().wallMs;
    }

    [[nodiscard]]
    auto maxMs() const -> qint64
    {
        return m_snapshot.stats().maxMs;
    }

    [[nodiscard]]
    auto medianMs() const -> qint64
    {
        return m_snapshot.stats().medianMs;
    }

    [[nodiscard]]
    auto peakParallelism() const -> int
    {
        return m_snapshot.stats().peakParallelism;
    }

    [[nodiscard]]
    auto multiBuildLog() const -> bool
    {
        return m_multiBuildLog;
    }

    [[nodiscard]]
    auto usingCompileDatabase() const -> bool
    {
        return m_haveCommands;
    }

    [[nodiscard]]
    auto scope() const -> int
    {
        return static_cast<int>(m_scope);
    }

    void setScope(int scope);

    [[nodiscard]]
    auto focusPath() const -> QString
    {
        return m_focusPath;
    }

    void setFocusPath(const QString &path);

    [[nodiscard]]
    auto comparing() const -> bool
    {
        return m_comparing;
    }

    [[nodiscard]]
    auto baselineLabel() const -> QString
    {
        return m_baselineLabel;
    }

    [[nodiscard]]
    auto baselineTotalMs() const -> qint64
    {
        return m_baseline.stats().totalCpuMs;
    }

    [[nodiscard]]
    auto live() const -> bool
    {
        return m_live;
    }

    [[nodiscard]]
    auto runningCount() const -> int
    {
        return m_runningCount;
    }

    /// Leaf index for a tree path, or -1.
    [[nodiscard]]
    auto indexOfTreePath(const QString &treePath) const -> int;

Q_SIGNALS:
    void sourceChanged();
    void statusChanged();
    void statsChanged();
    void scopeChanged();
    void focusChanged();
    void comparisonChanged();
    void liveChanged();
    /// The hierarchy changed: relayout.
    void treeChanged();
    /// Only leaf colours or states changed: repaint.
    void valuesChanged();

private:
    void rebuildFromLog();
    void rebuildTree();
    void recomputeDeltas();
    void recomputeScale();
    void touchValues();

    [[nodiscard]]
    auto leafIndexForOutput(const QString &outputPath) const -> int;

    Build::NinjaLog m_log;
    Build::CompileCommands m_commands;
    Build::BuildSnapshot m_snapshot;
    Build::BuildSnapshot m_baseline;
    std::vector<Build::TargetDelta> m_deltas;

    Treemap::Node m_tree;
    std::vector<LeafVisual> m_leaves;
    /// pathKey(treePath) -> leaf index. The one place leaves are looked up.
    std::unordered_map<std::string, int> m_indexByTreeKey;
    /// pathKey(ninja output) -> leaf index, for live events.
    std::unordered_map<std::string, int> m_indexByOutputKey;

    QString m_buildDirectory;
    QString m_sourceDirectory;
    QString m_ninjaLogPath;
    QString m_status { "No build loaded." };
    QStringList m_diagnostics;
    QString m_focusPath;
    QString m_baselineLabel;

    Build::LogScope m_scope { Build::LogScope::LatestPerOutput };
    bool m_multiBuildLog { false };
    bool m_haveCommands { false };
    bool m_comparing { false };
    bool m_live { false };
    int m_runningCount { 0 };
    double m_maxAbsDeltaMs { 0.0 };
    double m_scaleMaxMs { 1.0 };
    /// Live builds add targets the old log never saw; relayout is deferred
    /// until the frame after, so a burst of new files costs one relayout.
    bool m_treeDirty { false };

    quint64 m_treeRevision { 1 };
    quint64 m_valueRevision { 1 };
};

}
