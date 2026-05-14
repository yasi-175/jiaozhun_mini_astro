#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("jiaozhun_miniastro"));
    QApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Read TianShanNode encoder data with QHYCCD SDK and show a live line chart."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption libraryOption(QStringList() << QStringLiteral("l") << QStringLiteral("library"),
                                     QStringLiteral("QHYCCD SDK library path."),
                                     QStringLiteral("path"),
                                     QStringLiteral("/usr/local/lib/libqhyccd.so"));
    QCommandLineOption deviceOption(QStringList() << QStringLiteral("d") << QStringLiteral("device"),
                                    QStringLiteral("Preferred QHYCCD device name prefix."),
                                    QStringLiteral("name"),
                                    QStringLiteral("QHY5III585"));
    QCommandLineOption intervalOption(QStringList() << QStringLiteral("i") << QStringLiteral("interval"),
                                      QStringLiteral("Polling interval in milliseconds."),
                                      QStringLiteral("ms"),
                                      QStringLiteral("200"));

    parser.addOption(libraryOption);
    parser.addOption(deviceOption);
    parser.addOption(intervalOption);
    parser.process(app);

    bool ok = false;
    int intervalMs = parser.value(intervalOption).toInt(&ok);
    if (!ok || intervalMs < 1)
        intervalMs = 200;
    intervalMs = qMin(intervalMs, 2000);

    MainWindow window(parser.value(libraryOption), parser.value(deviceOption), intervalMs);
    window.show();

    return app.exec();
}
