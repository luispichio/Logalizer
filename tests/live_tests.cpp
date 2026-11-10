#include "logwidget.h"
#include "logdatabase.h"
#include "logformat.h"
#include "metadatapipeline.h"

#include <QApplication>
#include <QFile>
#include <QComboBox>
#include <QLineEdit>
#include <QProcess>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>

class LiveTests : public QObject {
    Q_OBJECT
private slots:
    void fileTailAndNavigation();
    void stdinProgress();
    void processProgress();
};

static QString applicationPath;

void LiveTests::fileTailAndNavigation() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("test.log");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    for (int i = 0; i < 100; ++i) file.write(QString("original %1\n").arg(i).toUtf8());
    file.close();

    LogWidget widget(path, 91);
    widget.resize(800, 400);
    widget.show();
    auto* browser = widget.findChild<QTextBrowser*>();
    QVERIFY(browser);
    QTRY_COMPARE_WITH_TIMEOUT(LogDatabase::instance().rowCount(91), 100, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(browser->toPlainText().contains("original 0"), 10000);
    QVERIFY(!browser->toPlainText().contains("original 99"));

    QTest::keyClick(browser, Qt::Key_End);
    QTRY_VERIFY(browser->toPlainText().contains("original 99"));
    QVERIFY(file.open(QIODevice::Append));
    file.write("appended 100\n");
    file.close();
    QTRY_COMPARE_WITH_TIMEOUT(LogDatabase::instance().rowCount(91), 101, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(browser->toPlainText().contains("appended 100"), 5000);

    QTest::keyClick(browser, Qt::Key_Home);
    QVERIFY(file.open(QIODevice::Append));
    file.write("appended 101\n");
    file.close();
    QTRY_COMPARE_WITH_TIMEOUT(LogDatabase::instance().rowCount(91), 102, 5000);
    QTest::qWait(2000);
    QVERIFY(browser->toPlainText().contains("original 0"));
    QVERIFY(!browser->toPlainText().contains("appended 101"));

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("replacement\n");
    file.close();
    QTRY_COMPARE_WITH_TIMEOUT(LogDatabase::instance().rowCount(91), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(browser->toPlainText().contains("replacement"), 5000);
    QVERIFY(!browser->toPlainText().contains("original 0"));

    QVERIFY(file.rename(dir.filePath("rotated.log")));
    QFile newFile(path);
    QVERIFY(newFile.open(QIODevice::WriteOnly));
    newFile.write("new generation\nanother line\n");
    newFile.close();
    QTRY_COMPARE_WITH_TIMEOUT(LogDatabase::instance().rowCount(91), 2, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(browser->toPlainText().contains("new generation"), 5000);
    QVERIFY(!browser->toPlainText().contains("replacement"));

    QVERIFY(newFile.open(QIODevice::Append));
    newFile.write("partial");
    newFile.close();
    QTRY_COMPARE_WITH_TIMEOUT(LogDatabase::instance().rowCount(91), 3, 5000);
    QVERIFY(newFile.open(QIODevice::Append));
    newFile.write(" completed\n");
    newFile.close();
    QTRY_VERIFY_WITH_TIMEOUT(browser->toPlainText().contains("partial completed"), 5000);
    QCOMPARE(LogDatabase::instance().rowCount(91), 3);

    QComboBox* filter = nullptr;
    for (auto* combo : widget.findChildren<QComboBox*>()) {
        if (combo->isEditable() && combo->lineEdit()->placeholderText().startsWith("Filter lines")) {
            filter = combo;
            break;
        }
    }
    QVERIFY(filter);
    filter->setCurrentText("unlikelymatch");
    QTest::keyClick(filter->lineEdit(), Qt::Key_Return);
    QVERIFY(newFile.open(QIODevice::Append));
    newFile.write("unlikelymatch arrived\n");
    newFile.close();
    QTRY_VERIFY_WITH_TIMEOUT(browser->toPlainText().contains("unlikelymatch arrived"), 5000);
}

void LiveTests::stdinProgress() {
    QProcess app;
    QTemporaryDir config;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("XDG_CONFIG_HOME", config.path());
    app.setProcessEnvironment(env);
    app.setProcessChannelMode(QProcess::MergedChannels);
    app.start(applicationPath, {"--stdin"});
    QVERIFY(app.waitForStarted());
    app.write("one incoming line\n");
    QVERIFY(app.waitForBytesWritten());
    QByteArray output;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000 && !output.contains("First batch published")) {
        app.waitForReadyRead(100);
        output += app.readAll();
    }
    // Detection is emitted when the first batch is published, before EOF.
    QVERIFY2(output.contains("First batch published"), output.constData());
    QCOMPARE(app.state(), QProcess::Running);
    app.closeWriteChannel();
    app.terminate();
    QVERIFY(app.waitForFinished(5000));
}

void LiveTests::processProgress() {
#ifdef Q_OS_UNIX
    LogWidget widget(LogWidget::SourceType::Command, "printf 'live output\\n'; sleep 2", 92);
    widget.resize(800, 400);
    widget.show();
    auto* browser = widget.findChild<QTextBrowser*>();
    QVERIFY(browser);
    QTRY_COMPARE_WITH_TIMEOUT(LogDatabase::instance().rowCount(92), 1, 1500);
    QTRY_VERIFY_WITH_TIMEOUT(browser->toPlainText().contains("live output"), 1500);
#endif
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    qRegisterMetaType<LogFormatDetectionResult>();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settings;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    if (argc < 2) return 2;
    applicationPath = QString::fromLocal8Bit(argv[1]);
    LiveTests tests;
    char* args[] = {argv[0], nullptr};
    const int result = QTest::qExec(&tests, 1, args);
    MetadataPipeline::instance().shutdown();
    return result;
}

#include "live_tests.moc"
