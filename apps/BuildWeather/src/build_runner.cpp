#include "build_runner.h"

#include "BW/Core/logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTextStream>

namespace BW::UI
{

namespace {

/// The log is polled rather than watched: QFileSystemWatcher coalesces
/// rapid appends and would drop completions during a parallel build.
constexpr int kPollIntervalMs = 60;

/// cl.exe finds neither the CRT headers nor the import libraries unless
/// INCLUDE and LIB are set, and those come from vcvars64.bat. A GUI app
/// started from Explorer or the Start menu does not have them, so every
/// compile fails with C1083 and the map just goes red. Catching it up front
/// with an actionable message beats letting the user read 40 identical
/// compiler errors.
auto looksLikeMsvcBuild(const QString &buildDirectory) -> bool
{
    QFile cache { buildDirectory + "/CMakeCache.txt" };
    if (!cache.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream stream { &cache };
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.startsWith("CMAKE_CXX_COMPILER:")) {
            return line.contains("cl.exe", Qt::CaseInsensitive)
                && !line.contains("clang", Qt::CaseInsensitive);
        }
    }
    return false;
}

}

BuildRunner::BuildRunner(QObject *parent)
    : QObject { parent }
{
    m_ninjaProgram = findNinja();

    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(
        &m_process,
        &QProcess::readyReadStandardOutput,
        this,
        &BuildRunner::onReadyRead);
    connect(
        &m_process,
        &QProcess::finished,
        this,
        &BuildRunner::onFinished);
    connect(
        &m_process,
        &QProcess::errorOccurred,
        this,
        &BuildRunner::onErrorOccurred);

    m_timer.setInterval(kPollIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &BuildRunner::onTick);
}

BuildRunner::~BuildRunner()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(2000);
    }
}

auto BuildRunner::findNinja() -> QString
{
    const QString onPath = QStandardPaths::findExecutable("ninja");
    if (!onPath.isEmpty()) {
        return QDir::fromNativeSeparators(onPath);
    }

    // Visual Studio ships ninja with its CMake integration; on a machine
    // with only the VS build tools installed this is the only copy present.
    static const QStringList kCandidates {
        "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/Common7/IDE/"
        "CommonExtensions/Microsoft/CMake/Ninja/ninja.exe",
        "C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/"
        "IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe",
        "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/"
        "CommonExtensions/Microsoft/CMake/Ninja/ninja.exe",
        "C:/Program Files/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/"
        "CommonExtensions/Microsoft/CMake/Ninja/ninja.exe",
    };
    for (const QString &candidate : kCandidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

void BuildRunner::setNinjaProgram(const QString &program)
{
    const QString clean = QDir::fromNativeSeparators(program);
    if (clean == m_ninjaProgram) {
        return;
    }
    m_ninjaProgram = clean;
    Q_EMIT ninjaProgramChanged();
}

auto BuildRunner::start(
    const QString &buildDirectory,
    const QStringList &targets,
    int jobs) -> bool
{
    if (m_running) {
        m_lastError = QStringLiteral("A build is already running.");
        Q_EMIT runningChanged();
        return false;
    }
    if (m_ninjaProgram.isEmpty()) {
        m_lastError = QStringLiteral(
            "ninja was not found on PATH. Set the path to ninja.exe in "
            "Settings.");
        Q_EMIT runningChanged();
        return false;
    }

    const QString directory = QDir::fromNativeSeparators(buildDirectory);
    if (!QFileInfo::exists(directory + "/build.ninja")) {
        m_lastError = QStringLiteral(
                          "%1 does not contain a build.ninja. Live mode "
                          "needs a directory configured with the Ninja "
                          "generator.")
                          .arg(directory);
        Q_EMIT runningChanged();
        return false;
    }

    m_buildDirectory = directory;
    m_lastError.clear();
    m_output.clear();
    m_stream.reset();
    m_finished = 0;
    m_total = 0;
    m_startedCount = 0;
    m_completedCount = 0;
    m_activeJobs = 0;
    m_peakJobs = 0;
    m_elapsedMs = 0;
    m_currentStep.clear();
    m_startedAtMs = QDateTime::currentMSecsSinceEpoch();

    // Only entries written from here on are ours.
    m_tail.setPath((directory + "/.ninja_log").toStdString());
    m_tail.seekToEnd();

#ifdef Q_OS_WIN
    if (!qEnvironmentVariableIsSet("INCLUDE")
        && looksLikeMsvcBuild(directory)) {
        appendOutput(
            QStringLiteral(
                "> warning: INCLUDE is not set, so cl.exe will not find the "
                "standard headers. Start Build Weather from a Developer "
                "Command Prompt (or run vcvars64.bat first) for live builds "
                "against an MSVC build directory."));
    }
#endif

    QStringList arguments { "-C", QDir::toNativeSeparators(directory) };
    if (jobs > 0) {
        arguments << "-j" << QString::number(jobs);
    }
    arguments << targets;

    QProcessEnvironment environment
        = QProcessEnvironment::systemEnvironment();
    // Pin the status prefix so the progress parser is not guessing at a
    // format that varies by ninja version and by the user's environment.
    environment.insert(
        "NINJA_STATUS",
        QString::fromLatin1(Build::kRequiredStatusFormat));
    m_process.setProcessEnvironment(environment);
    m_process.setWorkingDirectory(directory);

    Core::log("live")->info(
        "starting {} {}",
        m_ninjaProgram.toStdString(),
        arguments.join(' ').toStdString());
    appendOutput(
        QStringLiteral("> %1 %2")
            .arg(m_ninjaProgram, arguments.join(' ')));

    m_process.start(m_ninjaProgram, arguments);
    if (!m_process.waitForStarted(5000)) {
        m_lastError = QStringLiteral("Could not start %1").arg(m_ninjaProgram);
        Q_EMIT runningChanged();
        return false;
    }

    m_running = true;
    m_timer.start();
    Q_EMIT runningChanged();
    Q_EMIT progressChanged();
    Q_EMIT buildStarted();
    return true;
}

void BuildRunner::stop()
{
    if (!m_running) {
        return;
    }
    appendOutput(QStringLiteral("> stopping"));
    m_process.terminate();
    if (!m_process.waitForFinished(1500)) {
        m_process.kill();
    }
}

void BuildRunner::onReadyRead()
{
    const QByteArray chunk = m_process.readAllStandardOutput();
    const auto parsed = m_stream.feed(
        std::string_view { chunk.constData(),
                           static_cast<std::size_t>(chunk.size()) });

    bool progressed = false;
    for (const auto &line : parsed.progress) {
        m_finished = line.finished;
        m_total = line.total;
        m_currentStep = QString::fromStdString(line.description);
        ++m_startedCount;
        // %r is ninja's own count of edges currently running. Deriving it
        // from our own bookkeeping does not work: behind a pipe ninja emits
        // one status line per *finished* edge, so started and finished
        // counts move together and their difference is always zero.
        if (line.running >= 0) {
            m_activeJobs = line.running;
            m_peakJobs = std::max(m_peakJobs, m_activeJobs);
        }
        progressed = true;

        appendOutput(
            QStringLiteral("[%1/%2] %3")
                .arg(line.finished)
                .arg(line.total)
                .arg(QString::fromStdString(line.description)));

        // Behind a pipe this line means "this edge just finished". The map
        // still flashes the cell as in flight for the moment between this
        // and the matching .ninja_log record landing, which is what makes a
        // parallel build read as a shimmer rather than a step change.
        Q_EMIT stepObserved(
            QString::fromStdString(line.outputPath),
            QString::fromStdString(line.description));
    }
    for (const auto &line : parsed.other) {
        appendOutput(QString::fromStdString(line));
    }

    if (progressed) {
        Q_EMIT progressChanged();
    }
    // Compiler diagnostics arrive interleaved with progress; pick up any
    // completions they raced ahead of.
    pollLog();
}

void BuildRunner::onTick()
{
    m_elapsedMs = QDateTime::currentMSecsSinceEpoch() - m_startedAtMs;
    pollLog();
    Q_EMIT progressChanged();
}

void BuildRunner::pollLog()
{
    bool restarted = false;
    auto records = m_tail.poll(&restarted);
    if (restarted) {
        appendOutput(
            QStringLiteral("> .ninja_log was rewritten; resynchronised"));
    }
    if (records.empty()) {
        return;
    }

    m_completedCount += static_cast<int>(records.size());
    Q_EMIT stepsFinished(records);
}

void BuildRunner::onFinished(int exitCode, QProcess::ExitStatus status)
{
    m_timer.stop();
    // Drain whatever ninja wrote between the last poll and exiting.
    onReadyRead();
    const auto trailing = m_stream.flush();
    for (const auto &line : trailing.other) {
        appendOutput(QString::fromStdString(line));
    }
    pollLog();

    m_running = false;
    m_activeJobs = 0;
    m_elapsedMs = QDateTime::currentMSecsSinceEpoch() - m_startedAtMs;

    const bool success
        = status == QProcess::NormalExit && exitCode == 0;
    appendOutput(
        success ? QStringLiteral("> build succeeded")
                : QStringLiteral("> build failed (exit %1)").arg(exitCode));

    Q_EMIT runningChanged();
    Q_EMIT progressChanged();
    Q_EMIT buildFinished(exitCode, success);
}

void BuildRunner::onErrorOccurred(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        m_lastError
            = QStringLiteral("Could not start %1").arg(m_ninjaProgram);
        m_running = false;
        m_timer.stop();
        Q_EMIT runningChanged();
    }
}

void BuildRunner::appendOutput(const QString &line)
{
    m_output.append(line);
    while (m_output.size() > kMaxOutputLines) {
        m_output.removeFirst();
    }
    Q_EMIT outputChanged();
}

}
