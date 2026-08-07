#pragma once

// Root object exposed to QML as `App`.
//
// Owns the model, the live runner, the trace loader and the table models,
// and is the only place they are wired to each other. QML asks it for
// actions and reads state off the objects it exposes; it never reaches
// around the back into the libraries.

#include "build_model.h"
#include "build_runner.h"
#include "row_model.h"
#include "trace_controller.h"

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace BW::UI
{

class AppContext : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(BW::UI::BuildModel *build READ build CONSTANT)
    Q_PROPERTY(BW::UI::BuildRunner *runner READ runner CONSTANT)
    Q_PROPERTY(BW::UI::TraceController *traces READ traces CONSTANT)

    Q_PROPERTY(BW::UI::RowModel *targetsModel READ targetsModel CONSTANT)
    Q_PROPERTY(BW::UI::RowModel *headersModel READ headersModel CONSTANT)
    Q_PROPERTY(BW::UI::RowModel *templatesModel READ templatesModel CONSTANT)
    Q_PROPERTY(BW::UI::RowModel *unitsModel READ unitsModel CONSTANT)
    Q_PROPERTY(BW::UI::RowModel *deltaModel READ deltaModel CONSTANT)
    Q_PROPERTY(BW::UI::RowModel *headerUsersModel READ headerUsersModel
                   CONSTANT)

    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)

    // --- replay ---------------------------------------------------------
    Q_PROPERTY(bool replayAvailable READ replayAvailable
                   NOTIFY replayChanged)
    Q_PROPERTY(bool replayActive READ replayActive NOTIFY replayChanged)
    Q_PROPERTY(bool replayPlaying READ replayPlaying NOTIFY replayChanged)
    Q_PROPERTY(qint64 replayTimeMs READ replayTimeMs WRITE setReplayTimeMs
                   NOTIFY replayChanged)
    Q_PROPERTY(qint64 replayDurationMs READ replayDurationMs
                   NOTIFY replayChanged)
    Q_PROPERTY(qreal replaySpeed READ replaySpeed WRITE setReplaySpeed
                   NOTIFY replayChanged)
    Q_PROPERTY(int replayParallelism READ replayParallelism
                   NOTIFY replayChanged)

public:
    explicit AppContext(QObject *parent = nullptr);
    ~AppContext() override;

    [[nodiscard]]
    auto build() const -> BuildModel *
    {
        return m_build;
    }

    [[nodiscard]]
    auto runner() const -> BuildRunner *
    {
        return m_runner;
    }

    [[nodiscard]]
    auto traces() const -> TraceController *
    {
        return m_traces;
    }

    [[nodiscard]]
    auto targetsModel() const -> RowModel *
    {
        return m_targetsModel;
    }

    [[nodiscard]]
    auto headersModel() const -> RowModel *
    {
        return m_headersModel;
    }

    [[nodiscard]]
    auto templatesModel() const -> RowModel *
    {
        return m_templatesModel;
    }

    [[nodiscard]]
    auto unitsModel() const -> RowModel *
    {
        return m_unitsModel;
    }

    [[nodiscard]]
    auto deltaModel() const -> RowModel *
    {
        return m_deltaModel;
    }

    [[nodiscard]]
    auto headerUsersModel() const -> RowModel *
    {
        return m_headerUsersModel;
    }

    [[nodiscard]]
    auto version() const -> QString;

    [[nodiscard]]
    auto message() const -> QString
    {
        return m_message;
    }

    [[nodiscard]]
    auto messageIsError() const -> bool
    {
        return m_messageIsError;
    }

    // --- actions ---------------------------------------------------------

    Q_INVOKABLE bool openBuildDirectory(const QUrl &url);
    Q_INVOKABLE bool openNinjaLog(const QUrl &url);
    Q_INVOKABLE bool openBaseline(const QUrl &url);
    Q_INVOKABLE void clearBaseline();
    Q_INVOKABLE void loadTraces(const QUrl &url);
    Q_INVOKABLE void loadTracesFromBuildDirectory();
    Q_INVOKABLE bool reload();

    Q_INVOKABLE bool startBuild(const QString &targets);
    Q_INVOKABLE void stopBuild();

    Q_INVOKABLE bool exportAnalysisJson(const QUrl &url);
    Q_INVOKABLE bool exportTargetsCsv(const QUrl &url);
    Q_INVOKABLE bool exportHeadersCsv(const QUrl &url);
    Q_INVOKABLE bool exportTemplatesCsv(const QUrl &url);
    Q_INVOKABLE bool exportComparisonCsv(const QUrl &url);

    /// Fills headerUsersModel with the translation units that include the
    /// header ranked at `row` in headersModel.
    Q_INVOKABLE void selectHeader(int row);

    /// Tree path of the target at `row` in targetsModel, for cross-selection
    /// between the table and the map.
    Q_INVOKABLE QString targetPathAt(int row) const;

    Q_INVOKABLE QString formatDuration(qint64 ms) const;
    Q_INVOKABLE QString formatDelta(qint64 ms) const;
    Q_INVOKABLE QString toLocalPath(const QUrl &url) const;
    Q_INVOKABLE QUrl toUrl(const QString &path) const;
    Q_INVOKABLE void showMessage(const QString &text, bool isError = false);

    // --- replay -----------------------------------------------------------

    Q_INVOKABLE void replayPlay();
    Q_INVOKABLE void replayPause();
    Q_INVOKABLE void replayRewind();
    Q_INVOKABLE void replayExit();

    [[nodiscard]]
    auto replayAvailable() const -> bool;

    [[nodiscard]]
    auto replayActive() const -> bool
    {
        return m_replayActive;
    }

    [[nodiscard]]
    auto replayPlaying() const -> bool
    {
        return m_replayTimer.isActive();
    }

    [[nodiscard]]
    auto replayTimeMs() const -> qint64
    {
        return m_replayTimeMs;
    }

    void setReplayTimeMs(qint64 timeMs);

    [[nodiscard]]
    auto replayDurationMs() const -> qint64;

    [[nodiscard]]
    auto replaySpeed() const -> qreal
    {
        return m_replaySpeed;
    }

    void setReplaySpeed(qreal speed);

    [[nodiscard]]
    auto replayParallelism() const -> int;

Q_SIGNALS:
    void messageChanged();
    void replayChanged();
    void analysisChanged();

private Q_SLOTS:
    void refreshTargets();
    void refreshTraceTables();
    void refreshComparison();
    void onReplayTick();

private:
    [[nodiscard]]
    auto writeTextFile(const QUrl &url, const QString &content) -> bool;

    BuildModel *m_build { nullptr };
    BuildRunner *m_runner { nullptr };
    TraceController *m_traces { nullptr };

    RowModel *m_targetsModel { nullptr };
    RowModel *m_headersModel { nullptr };
    RowModel *m_templatesModel { nullptr };
    RowModel *m_unitsModel { nullptr };
    RowModel *m_deltaModel { nullptr };
    RowModel *m_headerUsersModel { nullptr };

    QTimer m_replayTimer;
    QString m_message;
    qint64 m_replayTimeMs { 0 };
    qreal m_replaySpeed { 1.0 };
    bool m_replayActive { false };
    bool m_messageIsError { false };
};

}
