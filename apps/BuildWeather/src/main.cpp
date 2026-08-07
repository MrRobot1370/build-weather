#include "app_context.h"
#include "BW/Core/logger.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QSurfaceFormat>

auto main(int argc, char *argv[]) -> int
{
    QGuiApplication app { argc, argv };
    app.setApplicationName(QStringLiteral(BW_NAME));
    app.setOrganizationName(QStringLiteral("BuildWeather"));
    app.setApplicationVersion(QStringLiteral(BW_VERSION));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    BW::Core::initLogger("BuildWeather");
    BW::Core::log("ui")->info("{} {} starting", BW_NAME, BW_VERSION);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Live and post-mortem visualization of a C++ build.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        "build-dir",
        "Build directory to open on start (must contain .ninja_log).");
    const QCommandLineOption tracesOption {
        QStringList { "traces" },
        "Directory to scan for -ftime-trace documents on start.",
        "dir"
    };
    const QCommandLineOption sourceOption {
        QStringList { "source" },
        "Source root, when it cannot be inferred from the build directory.",
        "dir"
    };
    const QCommandLineOption baselineOption {
        QStringList { "baseline" },
        "Baseline .ninja_log to compare the loaded build against.",
        "file"
    };
    parser.addOption(tracesOption);
    parser.addOption(sourceOption);
    parser.addOption(baselineOption);
    parser.process(app);

    QQmlApplicationEngine engine;
    // Our own QML modules (BW.UICore) live next to the executable in a
    // deployed build and under the build tree during development.
    engine.addImportPath(QCoreApplication::applicationDirPath() + "/qml");
#ifdef BW_QML_IMPORT_PATH
    engine.addImportPath(QStringLiteral(BW_QML_IMPORT_PATH));
#endif

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [] { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("BuildWeather", "Main");
    if (engine.rootObjects().isEmpty()) {
        BW::Core::log("ui")->error("QML root object failed to load");
        return -1;
    }

    // The singleton is created by the engine; reach it to apply command line
    // arguments now that QML exists and its bindings are live.
    auto *context = engine.singletonInstance<BW::UI::AppContext *>(
        "BuildWeather",
        "AppContext");
    if (context == nullptr) {
        BW::Core::log("ui")->warn(
            "AppContext singleton not reachable; command line arguments "
            "ignored");
    }
    else {
        if (!parser.value(sourceOption).isEmpty()) {
            context->build()->setSourceDirectory(parser.value(sourceOption));
        }
        const QStringList positional = parser.positionalArguments();
        if (!positional.isEmpty()) {
            context->openBuildDirectory(
                QUrl::fromLocalFile(positional.front()));
        }
        if (!parser.value(tracesOption).isEmpty()) {
            context->loadTraces(
                QUrl::fromLocalFile(parser.value(tracesOption)));
        }
        if (!parser.value(baselineOption).isEmpty()) {
            context->openBaseline(
                QUrl::fromLocalFile(parser.value(baselineOption)));
        }
    }

    return app.exec();
}
