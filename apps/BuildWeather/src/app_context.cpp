#include "app_context.h"

#include "BW/Build/report.h"
#include "BW/Core/logger.h"
#include "format.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>

namespace BW::UI
{

namespace {

constexpr int kTopRows = 500; ///< enough to scroll, cheap enough to format
constexpr int kReplayIntervalMs = 33;

auto bucketLabel(Core::PathBucket bucket) -> QString
{
    return QString::fromLatin1(Core::bucketName(bucket));
}

}

AppContext::AppContext(QObject *parent)
    : QObject { parent }
    , m_build { new BuildModel { this } }
    , m_runner { new BuildRunner { this } }
    , m_traces { new TraceController { this } }
    , m_targetsModel { new RowModel {
          { "rank", "path", "duration", "kind", "bucket" },
          this } }
    , m_headersModel { new RowModel {
          { "rank", "path", "total", "self", "tus", "average" },
          this } }
    , m_templatesModel { new RowModel {
          { "rank", "name", "total", "count", "tus", "kind" },
          this } }
    , m_unitsModel { new RowModel {
          { "rank", "source", "total", "frontend", "backend", "headers" },
          this } }
    , m_deltaModel { new RowModel {
          { "path", "baseline", "current", "delta", "state", "rowColor" },
          this } }
    , m_headerUsersModel { new RowModel { { "rank", "source", "cost" },
                                          this } }
{
    connect(
        m_build,
        &BuildModel::treeChanged,
        this,
        &AppContext::refreshTargets);
    connect(
        m_build,
        &BuildModel::comparisonChanged,
        this,
        &AppContext::refreshComparison);
    connect(
        m_traces,
        &TraceController::loaded,
        this,
        &AppContext::refreshTraceTables);

    // Live mode: ninja's stdout gives us the starts, the log tail gives us
    // the finishes, and the model turns both into leaf states.
    connect(
        m_runner,
        &BuildRunner::buildStarted,
        this,
        [this] {
            replayExit();
            m_build->beginLive();
        });
    connect(
        m_runner,
        &BuildRunner::stepObserved,
        this,
        [this](const QString &outputPath, const QString &description) {
            Q_UNUSED(description)
            if (!outputPath.isEmpty()) {
                m_build->noteStarted(outputPath);
            }
        });
    connect(
        m_runner,
        &BuildRunner::stepsFinished,
        this,
        [this](const std::vector<Build::TargetRecord> &records) {
            for (const auto &record : records) {
                m_build->noteFinished(record);
            }
        },
        Qt::DirectConnection);
    connect(
        m_runner,
        &BuildRunner::buildFinished,
        this,
        [this](int exitCode, bool success) {
            m_build->endLive();
            showMessage(
                success
                    ? QStringLiteral("Build finished in %1.")
                          .arg(formatDuration(m_runner->elapsedMs()))
                    : QStringLiteral("Build failed (exit %1).").arg(exitCode),
                !success);
        });

    m_replayTimer.setInterval(kReplayIntervalMs);
    connect(
        &m_replayTimer,
        &QTimer::timeout,
        this,
        &AppContext::onReplayTick);
}

AppContext::~AppContext() = default;

auto AppContext::version() const -> QString
{
    return QStringLiteral(BW_VERSION);
}

void AppContext::showMessage(const QString &text, bool isError)
{
    m_message = text;
    m_messageIsError = isError;
    Q_EMIT messageChanged();
}

auto AppContext::toLocalPath(const QUrl &url) const -> QString
{
    return url.isLocalFile() ? QDir::fromNativeSeparators(url.toLocalFile())
                             : url.toString();
}

auto AppContext::toUrl(const QString &path) const -> QUrl
{
    return path.isEmpty() ? QUrl {} : QUrl::fromLocalFile(path);
}

auto AppContext::formatDuration(qint64 ms) const -> QString
{
    return formatMs(ms);
}

auto AppContext::formatDelta(qint64 ms) const -> QString
{
    return formatDeltaMs(ms);
}

// --- loading -------------------------------------------------------------------

auto AppContext::openBuildDirectory(const QUrl &url) -> bool
{
    const QString path = toLocalPath(url);
    if (!m_build->loadBuildDirectory(path)) {
        showMessage(m_build->status(), true);
        return false;
    }
    showMessage(
        QStringLiteral("Loaded %1 (%2).")
            .arg(path, m_build->status()));
    return true;
}

auto AppContext::openNinjaLog(const QUrl &url) -> bool
{
    if (!m_build->loadNinjaLog(toLocalPath(url))) {
        showMessage(m_build->status(), true);
        return false;
    }
    showMessage(m_build->status());
    return true;
}

auto AppContext::openBaseline(const QUrl &url) -> bool
{
    if (!m_build->loadBaseline(toLocalPath(url))) {
        showMessage(m_build->status(), true);
        return false;
    }
    showMessage(
        QStringLiteral("Comparing against %1.")
            .arg(QFileInfo { toLocalPath(url) }.fileName()));
    return true;
}

void AppContext::clearBaseline()
{
    m_build->clearBaseline();
}

void AppContext::loadTraces(const QUrl &url)
{
    m_traces->loadDirectory(toLocalPath(url));
}

void AppContext::loadTracesFromBuildDirectory()
{
    if (m_build->buildDirectory().isEmpty()) {
        showMessage(QStringLiteral("Load a build directory first."), true);
        return;
    }
    m_traces->loadDirectory(m_build->buildDirectory());
}

auto AppContext::reload() -> bool
{
    const bool ok = m_build->reload();
    showMessage(ok ? m_build->status() : QStringLiteral("Nothing to reload."),
                !ok);
    return ok;
}

auto AppContext::startBuild(const QString &targets) -> bool
{
    const QStringList list = targets.split(' ', Qt::SkipEmptyParts);
    if (!m_runner->start(m_build->buildDirectory(), list)) {
        showMessage(m_runner->lastError(), true);
        return false;
    }
    return true;
}

void AppContext::stopBuild()
{
    m_runner->stop();
}

// --- tables ---------------------------------------------------------------------

void AppContext::refreshTargets()
{
    const auto &targets = m_build->targets();
    std::vector<const Build::TargetInfo *> byRank;
    byRank.reserve(targets.size());
    for (const auto &target : targets) {
        byRank.push_back(&target);
    }
    std::sort(
        byRank.begin(),
        byRank.end(),
        [](const Build::TargetInfo *a, const Build::TargetInfo *b) {
            return a->rank < b->rank;
        });

    QList<QVariantList> rows;
    const std::size_t limit
        = std::min<std::size_t>(byRank.size(), kTopRows);
    rows.reserve(static_cast<int>(limit));
    for (std::size_t i = 0; i < limit; ++i) {
        const Build::TargetInfo &target = *byRank[i];
        rows.append(QVariantList {
            target.rank,
            QString::fromStdString(target.treePath),
            formatMs(target.durationMs),
            QString::fromLatin1(Build::stepKindName(target.kind)),
            bucketLabel(target.bucket) });
    }
    m_targetsModel->setRows(std::move(rows));

    refreshComparison();
    Q_EMIT analysisChanged();
    Q_EMIT replayChanged();
}

void AppContext::refreshTraceTables()
{
    const Build::TraceAggregate &aggregate = m_traces->aggregate();

    QList<QVariantList> headerRows;
    const auto &headers = aggregate.headers();
    const std::size_t headerLimit
        = std::min<std::size_t>(headers.size(), kTopRows);
    headerRows.reserve(static_cast<int>(headerLimit));
    for (std::size_t i = 0; i < headerLimit; ++i) {
        const auto &header = headers[i];
        headerRows.append(QVariantList {
            static_cast<int>(i + 1),
            QString::fromStdString(header.path),
            formatUs(header.totalUs),
            formatUs(header.selfUs),
            header.tuCount,
            formatUs(header.averageUs()) });
    }
    m_headersModel->setRows(std::move(headerRows));

    QList<QVariantList> templateRows;
    const auto &templates = aggregate.templates();
    const std::size_t templateLimit
        = std::min<std::size_t>(templates.size(), kTopRows);
    templateRows.reserve(static_cast<int>(templateLimit));
    for (std::size_t i = 0; i < templateLimit; ++i) {
        const auto &entry = templates[i];
        templateRows.append(QVariantList {
            static_cast<int>(i + 1),
            QString::fromStdString(entry.name),
            formatUs(entry.totalUs),
            entry.count,
            entry.tuCount,
            entry.isClass ? QStringLiteral("class")
                          : QStringLiteral("function") });
    }
    m_templatesModel->setRows(std::move(templateRows));

    std::vector<const Build::TimeTraceUnit *> units;
    units.reserve(aggregate.units().size());
    for (const auto &unit : aggregate.units()) {
        units.push_back(&unit);
    }
    std::sort(
        units.begin(),
        units.end(),
        [](const Build::TimeTraceUnit *a, const Build::TimeTraceUnit *b) {
            return a->totalUs > b->totalUs;
        });

    QList<QVariantList> unitRows;
    const std::size_t unitLimit = std::min<std::size_t>(units.size(), kTopRows);
    unitRows.reserve(static_cast<int>(unitLimit));
    for (std::size_t i = 0; i < unitLimit; ++i) {
        const Build::TimeTraceUnit &unit = *units[i];
        unitRows.append(QVariantList {
            static_cast<int>(i + 1),
            QString::fromStdString(unit.source),
            formatUs(unit.totalUs),
            formatUs(unit.frontendUs),
            formatUs(unit.backendUs),
            static_cast<int>(unit.sources.size()) });
    }
    m_unitsModel->setRows(std::move(unitRows));

    m_headerUsersModel->clear();
    Q_EMIT analysisChanged();
}

void AppContext::refreshComparison()
{
    if (!m_build->comparing()) {
        m_deltaModel->clear();
        return;
    }

    QList<QVariantList> rows;
    const auto &deltas = m_build->deltas();
    const std::size_t limit = std::min<std::size_t>(deltas.size(), kTopRows);
    rows.reserve(static_cast<int>(limit));
    for (std::size_t i = 0; i < limit; ++i) {
        const auto &delta = deltas[i];
        QString state;
        switch (delta.state) {
        case Build::TargetDelta::State::Added :
            state = QStringLiteral("added");
            break;
        case Build::TargetDelta::State::Removed :
            state = QStringLiteral("removed");
            break;
        case Build::TargetDelta::State::Changed :
            state = QStringLiteral("changed");
            break;
        }
        rows.append(QVariantList {
            QString::fromStdString(delta.treePath),
            formatMs(delta.baselineMs),
            formatMs(delta.currentMs),
            formatDeltaMs(delta.deltaMs),
            state,
            delta.deltaMs > 0 ? QStringLiteral("#E2545B")
                              : (delta.deltaMs < 0
                                     ? QStringLiteral("#3FB97F")
                                     : QStringLiteral("#98A3B2")) });
    }
    m_deltaModel->setRows(std::move(rows));
}

void AppContext::selectHeader(int row)
{
    const auto &headers = m_traces->aggregate().headers();
    if (row < 0 || static_cast<std::size_t>(row) >= headers.size()) {
        m_headerUsersModel->clear();
        return;
    }

    const auto &aggregate = m_traces->aggregate();
    const auto users = aggregate.unitsIncluding(headers[row].path);

    QList<QVariantList> rows;
    rows.reserve(static_cast<int>(users.size()));
    for (std::size_t i = 0; i < users.size(); ++i) {
        const auto &[unitIndex, cost] = users[i];
        rows.append(QVariantList {
            static_cast<int>(i + 1),
            QString::fromStdString(aggregate.units()[unitIndex].source),
            formatUs(cost) });
    }
    m_headerUsersModel->setRows(std::move(rows));
}

auto AppContext::targetPathAt(int row) const -> QString
{
    return m_targetsModel->value(row, QStringLiteral("path")).toString();
}

// --- export ---------------------------------------------------------------------

auto AppContext::writeTextFile(const QUrl &url, const QString &content)
    -> bool
{
    const QString path = toLocalPath(url);
    if (path.isEmpty()) {
        showMessage(QStringLiteral("No destination selected."), true);
        return false;
    }

    QSaveFile file { path };
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        showMessage(
            QStringLiteral("Cannot write %1: %2")
                .arg(path, file.errorString()),
            true);
        return false;
    }
    QTextStream stream { &file };
    stream.setEncoding(QStringConverter::Utf8);
    stream << content;
    if (!file.commit()) {
        showMessage(
            QStringLiteral("Cannot write %1: %2")
                .arg(path, file.errorString()),
            true);
        return false;
    }
    showMessage(QStringLiteral("Wrote %1.").arg(path));
    return true;
}

auto AppContext::exportAnalysisJson(const QUrl &url) -> bool
{
    Build::ReportOptions options;
    options.buildDirectory = m_build->buildDirectory().toStdString();
    options.sourceDirectory = m_build->sourceDirectory().toStdString();
    options.topTargets = kTopRows;
    options.topHeaders = kTopRows;
    options.topTemplates = kTopRows;

    const Build::TraceAggregate *aggregate
        = m_traces->hasData() ? &m_traces->aggregate() : nullptr;
    return writeTextFile(
        url,
        QString::fromStdString(
            Build::exportJson(m_build->snapshot(), aggregate, options)));
}

auto AppContext::exportTargetsCsv(const QUrl &url) -> bool
{
    return writeTextFile(
        url,
        QString::fromStdString(
            Build::exportTargetsCsv(m_build->snapshot())));
}

auto AppContext::exportHeadersCsv(const QUrl &url) -> bool
{
    if (!m_traces->hasData()) {
        showMessage(QStringLiteral("No time traces loaded."), true);
        return false;
    }
    return writeTextFile(
        url,
        QString::fromStdString(
            Build::exportHeadersCsv(m_traces->aggregate(), kTopRows)));
}

auto AppContext::exportTemplatesCsv(const QUrl &url) -> bool
{
    if (!m_traces->hasData()) {
        showMessage(QStringLiteral("No time traces loaded."), true);
        return false;
    }
    return writeTextFile(
        url,
        QString::fromStdString(
            Build::exportTemplatesCsv(m_traces->aggregate(), kTopRows)));
}

auto AppContext::exportComparisonCsv(const QUrl &url) -> bool
{
    if (!m_build->comparing()) {
        showMessage(QStringLiteral("No baseline loaded."), true);
        return false;
    }
    return writeTextFile(
        url,
        QString::fromStdString(Build::exportDeltaCsv(m_build->deltas())));
}

// --- replay -----------------------------------------------------------------------

auto AppContext::replayAvailable() const -> bool
{
    return m_build->hasData() && replayDurationMs() > 0;
}

auto AppContext::replayDurationMs() const -> qint64
{
    return m_build->snapshot().stats().wallMs;
}

auto AppContext::replayParallelism() const -> int
{
    return m_build->runningCount();
}

void AppContext::setReplayTimeMs(qint64 timeMs)
{
    const qint64 clamped
        = std::clamp<qint64>(timeMs, 0, replayDurationMs());
    if (clamped == m_replayTimeMs && m_replayActive) {
        return;
    }
    m_replayTimeMs = clamped;
    m_replayActive = true;
    m_build->applyReplayTime(m_replayTimeMs);
    Q_EMIT replayChanged();
}

void AppContext::setReplaySpeed(qreal speed)
{
    const qreal clamped = std::clamp<qreal>(speed, 0.25, 64.0);
    if (qFuzzyCompare(clamped, m_replaySpeed)) {
        return;
    }
    m_replaySpeed = clamped;
    Q_EMIT replayChanged();
}

void AppContext::replayPlay()
{
    if (!replayAvailable() || m_runner->running()) {
        return;
    }
    if (m_replayTimeMs >= replayDurationMs()) {
        m_replayTimeMs = 0;
    }
    m_replayActive = true;
    m_replayTimer.start();
    Q_EMIT replayChanged();
}

void AppContext::replayPause()
{
    m_replayTimer.stop();
    Q_EMIT replayChanged();
}

void AppContext::replayRewind()
{
    setReplayTimeMs(0);
}

void AppContext::replayExit()
{
    m_replayTimer.stop();
    if (m_replayActive) {
        m_replayActive = false;
        m_replayTimeMs = 0;
        m_build->clearReplay();
    }
    Q_EMIT replayChanged();
}

void AppContext::onReplayTick()
{
    const qint64 duration = replayDurationMs();
    const auto step = static_cast<qint64>(
        kReplayIntervalMs * m_replaySpeed);
    const qint64 next = m_replayTimeMs + step;
    if (next >= duration) {
        setReplayTimeMs(duration);
        m_replayTimer.stop();
        Q_EMIT replayChanged();
        return;
    }
    setReplayTimeMs(next);
}

}
