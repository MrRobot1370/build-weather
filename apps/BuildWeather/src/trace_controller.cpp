#include "trace_controller.h"

#include "BW/Core/logger.h"

#include <QDir>
#include <QtConcurrent/QtConcurrentRun>

namespace BW::UI
{

TraceController::TraceController(QObject *parent)
    : QObject { parent }
    , m_aggregate { std::make_unique<Build::TraceAggregate>() }
{
    connect(
        &m_watcher,
        &QFutureWatcherBase::finished,
        this,
        &TraceController::onFinished);
    connect(
        &m_watcher,
        &QFutureWatcherBase::progressValueChanged,
        this,
        [this](int value) {
            const int maximum = m_watcher.progressMaximum();
            m_progress = maximum > 0
                ? static_cast<qreal>(value) / maximum
                : 0.0;
            m_status = QStringLiteral("Parsing traces: %1 / %2")
                           .arg(value)
                           .arg(maximum);
            Q_EMIT progressChanged();
            Q_EMIT statusChanged();
        });
}

TraceController::~TraceController()
{
    cancel();
    m_watcher.waitForFinished();
}

void TraceController::loadDirectory(const QString &directory)
{
    if (m_loading) {
        cancel();
        m_watcher.waitForFinished();
    }

    m_directory = QDir::fromNativeSeparators(directory);
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    m_loading = true;
    m_progress = 0.0;
    m_status = QStringLiteral("Scanning %1 ...").arg(m_directory);
    Q_EMIT loadingChanged();
    Q_EMIT progressChanged();
    Q_EMIT statusChanged();

    const std::string root = m_directory.toStdString();
    auto cancelled = m_cancelled;

    m_watcher.setFuture(QtConcurrent::run(
        [root, cancelled](
            QPromise<std::shared_ptr<Build::TraceAggregate>> &promise) {
            auto aggregate = std::make_shared<Build::TraceAggregate>();

            const auto files = Build::findTimeTraceFiles(root);
            promise.setProgressRange(0, static_cast<int>(files.size()));

            for (std::size_t i = 0; i < files.size(); ++i) {
                if (cancelled->load() || promise.isCanceled()) {
                    break;
                }
                std::string error;
                if (auto unit = Build::readTimeTrace(files[i], error)) {
                    if (unit->ok()) {
                        aggregate->add(std::move(*unit));
                    }
                }
                promise.setProgressValue(static_cast<int>(i + 1));
            }

            aggregate->finalize();
            promise.addResult(aggregate);
        }));
}

void TraceController::cancel()
{
    if (m_cancelled) {
        m_cancelled->store(true);
    }
    m_watcher.cancel();
}

void TraceController::clear()
{
    cancel();
    m_watcher.waitForFinished();
    m_aggregate = std::make_unique<Build::TraceAggregate>();
    m_directory.clear();
    m_status = QStringLiteral("No time traces loaded.");
    Q_EMIT statusChanged();
    Q_EMIT loaded();
}

void TraceController::onFinished()
{
    m_loading = false;
    m_progress = 1.0;

    if (!m_watcher.future().isCanceled()
        && m_watcher.future().resultCount() > 0) {
        if (auto result = m_watcher.result()) {
            m_aggregate = std::make_unique<Build::TraceAggregate>(
                std::move(*result));
        }
    }

    const int units = unitCount();
    m_status = units == 0
        ? QStringLiteral(
              "No -ftime-trace documents found in %1. Build with clang-cl "
              "and -ftime-trace to produce them.")
              .arg(m_directory)
        : QStringLiteral("%1 translation units, %2 distinct headers")
              .arg(units)
              .arg(headerCount());

    Core::log("trace")->info(
        "loaded {} translation units from {}",
        units,
        m_directory.toStdString());

    Q_EMIT loadingChanged();
    Q_EMIT progressChanged();
    Q_EMIT statusChanged();
    Q_EMIT loaded();
}

}
