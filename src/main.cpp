#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QScreen>
#include <QStyleHints>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("LumiCanvas"));
    QApplication::setOrganizationName(QStringLiteral("LumiCanvas"));
    QApplication::setApplicationVersion(QStringLiteral(LUMI_VERSION));
    QApplication::setStyle(QStringLiteral("Fusion"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Projection mapping for Windows"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption windowedOutput(QStringLiteral("windowed-output"),
                                            QStringLiteral("Open a windowed output on startup."));
    parser.addOption(windowedOutput);
    parser.addPositionalArgument(QStringLiteral("path"),
                                 QStringLiteral("A .lumi project or a folder of step images."),
                                 QStringLiteral("[path]"));
    parser.process(app);

    lumi::MainWindow window;
    if (!window.initialize())
        return 1;
    window.show();
    if (!parser.positionalArguments().isEmpty())
        window.openPath(parser.positionalArguments().constFirst());
    if (parser.isSet(windowedOutput))
        window.openOutput(QGuiApplication::primaryScreen(), false);

    return app.exec();
}
