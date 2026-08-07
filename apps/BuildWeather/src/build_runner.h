#pragma once

// Live mode: run ninja as a child process and turn it into an event stream.
//
// Two sources, and it is worth being precise about what each one gives:
//
//   stdout status lines   which edge just completed, its description (hence
//                         its output path), the finished/total counters and
//                         %r, ninja's own count of edges still running
//   `.ninja_log` tail     the same completions again, but with the exact
//                         start and end offsets ninja measured
//
// Note what is *not* in that list: start events. Ninja prints a status line
// on edge start only when it thinks stdout is a terminal, and a child
// process never gets one. So the map animates on completion: a cell flashes
// as its status line arrives and settles once the log record confirms the
// duration. The in-flight count comes from %r rather than from counting
// events, which is both simpler and exactly right.
//
// NINJA_STATUS is set explicitly on the child so the prefix format is known
// rather than assumed; see ninja_progress.h.

#include "BW/Build/ninja_log.h"
#include "BW/Build/ninja_progress.h"

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace BW::UI
{

class BuildRunner : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by App")

    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int finishedEdges READ finishedEdges NOTIFY progressChanged)
    Q_PROPERTY(int totalEdges READ totalEdges NOTIFY progressChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(qint64 elapsedMs READ elapsedMs NOTIFY progressChanged)
    Q_PROPERTY(int activeJobs READ activeJobs NOTIFY progressChanged)
    Q_PROPERTY(int peakJobs READ peakJobs NOTIFY progressChanged)
    Q_PROPERTY(QString currentStep READ currentStep NOTIFY progressChanged)
    Q_PROPERTY(QString ninjaProgram READ ninjaProgram WRITE setNinjaProgram
                   NOTIFY ninjaProgramChanged)
    Q_PROPERTY(bool ninjaAvailable READ ninjaAvailable
                   NOTIFY ninjaProgramChanged)
    Q_PROPERTY(QStringList output READ output NOTIFY outputChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY runningChanged)

public:
    explicit BuildRunner(QObject *parent = nullptr);
    ~BuildRunner() override;

    /// Starts `ninja -C <buildDirectory> [targets...]`. Returns false when
    /// ninja cannot be found or a build is already running.
    Q_INVOKABLE bool start(
        const QString &buildDirectory,
        const QStringList &targets = {},
        int jobs = 0);

    /// Asks ninja to stop, then kills it if it does not.
    Q_INVOKABLE void stop();

    /// Best-effort location of a ninja executable. Checks PATH first, then
    /// the copy Visual Studio ships with its CMake integration.
    Q_INVOKABLE static QString findNinja();

    [[nodiscard]]
    auto running() const -> bool
    {
        return m_running;
    }

    [[nodiscard]]
    auto finishedEdges() const -> int
    {
        return m_finished;
    }

    [[nodiscard]]
    auto totalEdges() const -> int
    {
        return m_total;
    }

    [[nodiscard]]
    auto progress() const -> qreal
    {
        return m_total > 0 ? static_cast<qreal>(m_finished) / m_total : 0.0;
    }

    [[nodiscard]]
    auto elapsedMs() const -> qint64
    {
        return m_elapsedMs;
    }

    [[nodiscard]]
    auto activeJobs() const -> int
    {
        return m_activeJobs;
    }

    [[nodiscard]]
    auto peakJobs() const -> int
    {
        return m_peakJobs;
    }

    [[nodiscard]]
    auto currentStep() const -> QString
    {
        return m_currentStep;
    }

    [[nodiscard]]
    auto ninjaProgram() const -> QString
    {
        return m_ninjaProgram;
    }

    void setNinjaProgram(const QString &program);

    [[nodiscard]]
    auto ninjaAvailable() const -> bool
    {
        return !m_ninjaProgram.isEmpty();
    }

    [[nodiscard]]
    auto output() const -> QStringList
    {
        return m_output;
    }

    [[nodiscard]]
    auto lastError() const -> QString
    {
        return m_lastError;
    }

Q_SIGNALS:
    void runningChanged();
    void progressChanged();
    void outputChanged();
    void ninjaProgramChanged();

    void buildStarted();
    void buildFinished(int exitCode, bool success);

    /// Ninja reported a step. Behind a pipe this arrives when the edge
    /// finishes, not when it starts; see ninja_progress.h. `outputPath` may
    /// be empty when the rule description was not one we recognise.
    void stepObserved(const QString &outputPath, const QString &description);

    /// Records appended to `.ninja_log` since the last poll. Direct
    /// connection only: this carries a plain C++ type on purpose so the map
    /// is updated without a QVariant round trip per file.
    void stepsFinished(const std::vector<Build::TargetRecord> &records);

private Q_SLOTS:
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onErrorOccurred(QProcess::ProcessError error);
    void onTick();

private:
    void appendOutput(const QString &line);
    void pollLog();

    QProcess m_process;
    QTimer m_timer;
    Build::ProgressStream m_stream;
    Build::NinjaLogTail m_tail;

    QString m_ninjaProgram;
    QString m_buildDirectory;
    QString m_currentStep;
    QString m_lastError;
    QStringList m_output;

    qint64 m_startedAtMs { 0 };
    qint64 m_elapsedMs { 0 };
    int m_finished { 0 }; ///< ninja's %f
    int m_total { 0 }; ///< ninja's %t
    /// Status lines seen and log records read. Kept for diagnostics only:
    /// the in-flight number comes from ninja's own %r, not from these.
    int m_startedCount { 0 };
    int m_completedCount { 0 };
    int m_activeJobs { 0 };
    int m_peakJobs { 0 };
    bool m_running { false };

    static constexpr int kMaxOutputLines = 400;
};

}
