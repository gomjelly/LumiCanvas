#include "core/ImageLoader.h"
#include "core/StepGenerator.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QScreen>
#include <QSettings>
#include <QStyleHints>
#include <QTranslator>

namespace {

// Installs the UI language chosen in View > Language ("system", "ko" or "en").
void installTranslators(QApplication& app)
{
    const QString language = QSettings().value(QStringLiteral("language"), QStringLiteral("system")).toString();
    const QLocale locale = language == QLatin1String("system") ? QLocale::system() : QLocale(language);
    QLocale::setDefault(locale);

    // English is the source language, so it needs no catalog.
    auto* appTranslator = new QTranslator(&app);
    if (appTranslator->load(locale, QStringLiteral("lumicanvas"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QApplication::installTranslator(appTranslator);

    // Qt's own strings: standard buttons, file dialogs, context menus.
    auto* qtTranslator = new QTranslator(&app);
    if (qtTranslator->load(locale, QStringLiteral("qt"), QStringLiteral("_"),
                           QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(qtTranslator);
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("LumiCanvas"));
    QApplication::setOrganizationName(QStringLiteral("LumiCanvas"));
    QApplication::setApplicationVersion(QStringLiteral(LUMI_VERSION));
    QApplication::setStyle(QStringLiteral("Fusion"));
    // Title bars of the editor and the output windows; the .exe icon comes from lumicanvas.rc.
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/lumicanvas.ico")));
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif
    installTranslators(app);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Projection guide for drawing"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption windowedOutput(QStringLiteral("windowed-output"),
                                            QStringLiteral("Open a windowed output on startup."));
    parser.addOption(windowedOutput);
    const QCommandLineOption generateOption(QStringLiteral("generate-steps"),
                                            QStringLiteral("Split the image at <path> into steps and exit, without opening a window."),
                                            QStringLiteral("output folder"));
    parser.addOption(generateOption);
    parser.addPositionalArgument(QStringLiteral("path"),
                                 QStringLiteral("A .lumi project or a folder of step images."),
                                 QStringLiteral("[path]"));
    parser.process(app);

    if (parser.isSet(generateOption)) {
        const QStringList paths = parser.positionalArguments();
        const QImage source = paths.isEmpty() ? QImage() : lumi::loadImage(paths.constFirst());
        if (source.isNull())
            return 2;
        return lumi::saveSteps(source, lumi::StepOptions(), parser.value(generateOption), {}).isEmpty() ? 0 : 3;
    }

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
