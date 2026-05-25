#include "mainwindow.h"
#include "logformat.h"

#include <QApplication>
#include <QLocale>
#include <QMetaType>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    qRegisterMetaType<LogFormatDetectionResult>("LogFormatDetectionResult");
    a.setApplicationName("Logalizer");
    a.setApplicationVersion("0.1");

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "Logalizer_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }
    MainWindow w;
    const QStringList args = a.arguments().mid(1);
    if (args.contains("-") || args.contains("--stdin")) {
        w.openStdin();
    }
    w.show();

    return a.exec();
}
