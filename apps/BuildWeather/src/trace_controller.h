#pragma once

// Loads a `-ftime-trace` set off the GUI thread.
//
// A full trace set for a large project is hundreds of megabytes of JSON, so
// parsing runs on a worker, reports progress per file, and is cancellable.
// The UI shows partial state throughout and never blocks: the aggregate is
// swapped in once, at the end, when it is consistent.

#include "BW/Build/time_trace.h"

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>

namespace BW::UI
{

class TraceController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by App")

    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString directory READ directory NOTIFY statusChanged)
    Q_PROPERTY(bool hasData READ hasData NOTIFY loaded)
    Q_PROPERTY(int unitCount READ unitCount NOTIFY loaded)
    Q_PROPERTY(int headerCount READ headerCount NOTIFY loaded)
    Q_PROPERTY(qint64 frontendUs READ frontendUs NOTIFY loaded)
    Q_PROPERTY(qint64 backendUs READ backendUs NOTIFY loaded)

public:
    explicit TraceController(QObject *parent = nullptr);
    ~TraceController() override;

    /// Walks `directory` recursively for trace documents and parses them.
    Q_INVOKABLE void loadDirectory(const QString &directory);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

    [[nodiscard]]
    auto aggregate() const -> const Build::TraceAggregate &
    {
        return *m_aggregate;
    }

    [[nodiscard]]
    auto loading() const -> bool
    {
        return m_loading;
    }

    [[nodiscard]]
    auto progress() const -> qreal
    {
        return m_progress;
    }

    [[nodiscard]]
    auto status() const -> QString
    {
        return m_status;
    }

    [[nodiscard]]
    auto directory() const -> QString
    {
        return m_directory;
    }

    [[nodiscard]]
    auto hasData() const -> bool
    {
        return !m_aggregate->units().empty();
    }

    [[nodiscard]]
    auto unitCount() const -> int
    {
        return static_cast<int>(m_aggregate->units().size());
    }

    [[nodiscard]]
    auto headerCount() const -> int
    {
        return static_cast<int>(m_aggregate->headers().size());
    }

    [[nodiscard]]
    auto frontendUs() const -> qint64
    {
        return m_aggregate->frontendUs();
    }

    [[nodiscard]]
    auto backendUs() const -> qint64
    {
        return m_aggregate->backendUs();
    }

Q_SIGNALS:
    void loadingChanged();
    void progressChanged();
    void statusChanged();
    void loaded();

private:
    void onFinished();

    std::unique_ptr<Build::TraceAggregate> m_aggregate;
    QFutureWatcher<std::shared_ptr<Build::TraceAggregate>> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancelled;

    QString m_status { "No time traces loaded." };
    QString m_directory;
    qreal m_progress { 0.0 };
    bool m_loading { false };
};

}
