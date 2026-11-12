#include "logwidget.h"
#include "logdatabase.h"
#include "logformat.h"
#include "formatdetector.h"
#include "loglineparser.h"
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
    void refreshPreservesSelection();
    void growingViewPreservesSelection();
    void repeatedNavigation();
    void filteredBatchNavigation();
    void stdinProgress();
    void processProgress();
    void metadataTimestampParsing();
    void metadataCompiledLevels();
    void metadataRegistryCompilesLevels();
    void genericTimestampParsing();
    void timestampSort();
    void javaAndLogfmtDetection();
};

static QString applicationPath;

void LiveTests::refreshPreservesSelection() {
    QTemporaryDir dir;
    QFile file(dir.filePath("selection.log"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    for (int i = 0; i < 100; ++i) file.write(QString("selection line %1\n").arg(i).toUtf8());
    file.close();
    LogWidget widget(file.fileName(), 92);
    widget.resize(800, 400);
    widget.show();
    auto* browser = widget.findChild<QTextBrowser*>();
    QVERIFY(browser);
    QTRY_VERIFY_WITH_TIMEOUT(browser->toPlainText().contains("selection line 0"), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(LogDatabase::instance().rowCount(92), 100, 10000);
    QTest::qWait(200);
    QTextCursor cursor = browser->textCursor();
    cursor.setPosition(0);
    cursor.setPosition(10, QTextCursor::KeepAnchor);
    browser->setTextCursor(cursor);
    const QString selected = browser->textCursor().selectedText();
    QVERIFY(!selected.isEmpty());
    QVERIFY(QMetaObject::invokeMethod(&widget, "onChunkInserted", Qt::DirectConnection,
                                     Q_ARG(int, 92), Q_ARG(qint32, 100)));
    QVERIFY(QMetaObject::invokeMethod(&widget, "onFinished", Qt::DirectConnection, Q_ARG(int, 92)));
    QCOMPARE(browser->textCursor().selectedText(), selected);
    QTest::qWait(1700);
    QCOMPARE(browser->textCursor().selectedText(), selected);
}

void LiveTests::growingViewPreservesSelection() {
    QTemporaryDir dir;
    QFile file(dir.filePath("growing.log"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("first line\n");
    file.close();
    LogWidget widget(file.fileName(), 95);
    widget.resize(800, 400);
    widget.show();
    auto* browser = widget.findChild<QTextBrowser*>();
    QVERIFY(browser);
    QTRY_VERIFY_WITH_TIMEOUT(browser->toPlainText().contains("first line"), 10000);
    QTest::qWait(200);
    QTextCursor cursor = browser->textCursor();
    cursor.setPosition(0);
    cursor.setPosition(5, QTextCursor::KeepAnchor);
    browser->setTextCursor(cursor);
    const QString selected = browser->textCursor().selectedText();
    QVERIFY(!selected.isEmpty());
    QVERIFY(file.open(QIODevice::Append));
    file.write("second line\n");
    file.close();
    QTRY_VERIFY_WITH_TIMEOUT(browser->toPlainText().contains("second line"), 5000);
    QCOMPARE(browser->textCursor().selectedText(), selected);
}

void LiveTests::repeatedNavigation() {
    QTemporaryDir dir;
    QFile file(dir.filePath("navigation.log"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    for (int i = 0; i < 300; ++i) file.write(QString("navigation line %1\n").arg(i).toUtf8());
    file.close();
    LogWidget widget(file.fileName(), 93);
    widget.resize(800, 400);
    widget.show();
    auto* browser = widget.findChild<QTextBrowser*>();
    QVERIFY(browser);
    QTRY_VERIFY_WITH_TIMEOUT(browser->toPlainText().contains("navigation line 0"), 10000);
    for (int i = 0; i < 20; ++i) QTest::keyClick(browser, Qt::Key_Down);
    QTRY_VERIFY_WITH_TIMEOUT(browser->toPlainText().contains("navigation line 20"), 5000);
    for (int i = 0; i < 20; ++i) QTest::keyClick(browser, Qt::Key_Down);
    QTest::keyClick(browser, Qt::Key_Home);
    QTest::qWait(100);
    QVERIFY(browser->toPlainText().contains("navigation line 0"));
}

void LiveTests::filteredBatchNavigation() {
    auto& db = LogDatabase::instance();
    QVERIFY(db.createTable(94));
    QVector<LineRecord> records;
    for (int i = 0; i < 20; ++i)
        records.append(LineRecord(i % 2 == 0 ? "match" : "other", i, i));
    QVERIFY(db.insertBatch(94, records));
    QCOMPARE(db.advanceFilteredLine(94, "match", 0, 3), 6);
    QCOMPARE(db.advanceFilteredLine(94, "match", 6, -2), 2);
    QCOMPARE(db.advanceFilteredLine(94, "match", 2, -50), 0);
    QCOMPARE(db.advanceFilteredLine(94, "match", 18, 1), 18);
    QVERIFY(db.dropTable(94));
}

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

void LiveTests::metadataTimestampParsing() {
    MetadataDetectionConfig config;
    config.hasFormat = true;
    config.referenceDate = QDate(2024, 8, 14);
    config.format.timestampField = "timestamp";
    config.format.timestampFormats = {"%m-%d %H:%M:%S.%L", "%m-%d %H:%M:%S.%f"};
    LogFormatPattern pattern;
    pattern.regex = QRegularExpression("^(?<timestamp>.*)$");
    config.format.patterns.append(pattern);

    const QStringList timestamps = {
        "01-01 00:00:00.000", "02-29 23:59:59.999", "03-10 02:30:00.123",
        "11-03 01:30:00.321", "12-31 23:59:59.999", "02-30 12:00:00.000",
        "08-14 15:12:11.123456", "08-14 15:12:11,123", "invalid"
    };
    for (const QString& timestamp : timestamps) {
        QString normalized = timestamp;
        normalized.replace(',', '.');
        QDateTime expected = QDateTime::fromString("2024 " + normalized, "yyyy MM-dd HH:mm:ss.zzz");
        if (!expected.isValid()) {
            normalized.replace(QRegularExpression("([\\.,:])(\\d{3})\\d+"), "\\1\\2");
            expected = QDateTime::fromString("2024 " + normalized, "yyyy MM-dd HH:mm:ss.zzz");
        }
        const ParsedLineMetadata parsed = parseLineMetadata(QStringView(timestamp), config);
        QCOMPARE(parsed.timestampText, timestamp);
        QCOMPARE(parsed.timestampEpochMs, expected.isValid() ? expected.toMSecsSinceEpoch() : -1);
    }
}

void LiveTests::metadataCompiledLevels() {
    MetadataDetectionConfig config;
    config.hasFormat = true;
    config.format.levelField = "level";
    LogFormatPattern linePattern;
    linePattern.regex = QRegularExpression("^(?<level>.*)$");
    config.format.patterns.append(linePattern);
    config.format.levelPatterns.insert("debug", "^D");
    config.format.levelPatterns.insert("error", "^E");
    config.format.levelPatterns.insert("warning", "["); // Invalid regex, but a literal match remains valid.

    const QStringList levels = {"D", "d", "E", "[", "unknown", "info"};
    QVector<LogLevel> expected;
    for (const QString& level : levels) {
        expected.append(parseLineMetadata(QStringView(level), config).level);
    }
    for (auto it = config.format.levelPatterns.constBegin(); it != config.format.levelPatterns.constEnd(); ++it) {
        LogFormatLevelPattern compiled;
        compiled.level = it.key();
        compiled.pattern = it.value();
        compiled.regex = QRegularExpression(it.value(), QRegularExpression::CaseInsensitiveOption);
        compiled.regex.optimize();
        config.format.compiledLevelPatterns.append(compiled);
    }
    for (int i = 0; i < levels.size(); ++i) {
        QCOMPARE(parseLineMetadata(QStringView(levels[i]), config).level, expected[i]);
    }
    QCOMPARE(expected[0], LogLevel::Debug);
    QCOMPARE(expected[2], LogLevel::Error);
    QCOMPARE(expected[3], LogLevel::Warn);
}

void LiveTests::metadataRegistryCompilesLevels() {
    QSettings settings("Logalizer", "Logalizer");
    const QString key = "formatDetection/customDefinitionsJson";
    const QVariant previous = settings.value(key);
    settings.setValue(key, R"JSON({"test_levels":{"json":true,"level-field":"level","level":{"debug":"^D","error":"^E"}}})JSON");
    const QVector<LogFormatDefinition> formats = LogFormatRegistry::loadFormats();
    if (previous.isValid()) {
        settings.setValue(key, previous);
    } else {
        settings.remove(key);
    }
    for (const LogFormatDefinition& format : formats) {
        if (format.id != QLatin1String("test_levels")) {
            continue;
        }
        QCOMPARE(format.compiledLevelPatterns.size(), 2);
        MetadataDetectionConfig config;
        config.hasFormat = true;
        config.format = format;
        const QString line = "{\"level\":\"D\"}";
        QCOMPARE(parseLineMetadata(QStringView(line), config).level, LogLevel::Debug);
        return;
    }
    QFAIL("Custom format was not loaded");
}

void LiveTests::genericTimestampParsing() {
    const MetadataDetectionConfig config;
    const QStringList timestamps = {
        "2026-03-28 00:00:04,614", "2026-03-28 00:00:04.615",
        "2026-03-28T00:00:04.616", "2026-03-28 00:00:04",
        "2024-03-10 02:30:00.123", "2024-11-03 01:30:00.321",
        "2026-03-28T00:00:04.123456Z", "2026-03-28 00:00:04.123+02:30",
        "2026-03-28T00:00:04Z", "2026-02-30 00:00:04,000"
    };
    for (const QString& timestamp : timestamps) {
        const QString line = timestamp + " [worker] INFO message key=value";
        const ParsedLineMetadata parsed = parseLineMetadata(QStringView(line), config);
        QCOMPARE(parsed.timestampText, timestamp);
        QString normalized = timestamp;
        normalized.replace(',', '.');
        QDateTime expected = QDateTime::fromString(normalized, Qt::ISODateWithMs);
        if (!expected.isValid()) expected = QDateTime::fromString(normalized, Qt::ISODate);
        QCOMPARE(parsed.timestampEpochMs, expected.isValid() ? expected.toMSecsSinceEpoch() : -1);
        QCOMPARE(parsed.level, LogLevel::Info);
    }
    const QString dateOnly = "2026-03-28 INFO message";
    QCOMPARE(parseLineMetadata(QStringView(dateOnly), config).timestampText, QString("2026-03-28"));
    const QString continuation = "    at example.Class.method(Class.java:42)";
    QVERIFY(parseLineMetadata(QStringView(continuation), config).timestampText.isEmpty());
}

void LiveTests::timestampSort() {
    auto& db = LogDatabase::instance();
    constexpr int fileId = 93;
    QVERIFY(db.createTable(fileId));
    const QStringList lines = {"2026-03-28 00:00:04,615 INFO second",
                               "2026-03-28 00:00:04,614 INFO first"};
    QVector<LineRecord> records;
    QVector<LineMetadataRecord> metadata;
    MetadataDetectionConfig config;
    for (int i = 0; i < lines.size(); ++i) {
        records.append(LineRecord(lines[i], i, i));
        const auto parsed = parseLineMetadata(QStringView(lines[i]), config);
        metadata.append(LineMetadataRecord(i, parsed.timestampText, parsed.timestampEpochMs, parsed.level));
    }
    QVERIFY(db.insertBatch(fileId, records));
    QVERIFY(db.insertMetadataBatch(fileId, metadata));
    QVector<QVector<QString>> rows;
    QStringList headers;
    QVERIFY(db.queryRows(fileId, 0, 10, {}, true, true, rows, headers));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0][0], QString("1"));
    QCOMPARE(rows[1][0], QString("0"));
    QVERIFY(db.dropTable(fileId));
}

void LiveTests::javaAndLogfmtDetection() {
    const QStringList javaLines = {
        "2026-03-28 00:00:04,614 [worker-1] INFO service: request id=42",
        "2026-03-28 00:00:04,615 [worker-2] WARN service: request id=43",
        "2026-03-28 00:00:04,616 [worker-3] ERROR service: request id=44"
    };
    const auto java = FormatDetector::detect("app.log", javaLines);
    QVERIFY(java.detected);
    QCOMPARE(java.format.id, QString("java_timestamped"));
    MetadataDetectionConfig config;
    config.hasFormat = true;
    config.format = java.format;
    config.formatPatternName = java.patternName;
    const auto metadata = parseLineMetadata(QStringView(javaLines[0]), config);
    QCOMPARE(metadata.timestampText, QString("2026-03-28 00:00:04,614"));
    QCOMPARE(metadata.level, LogLevel::Info);
    QVERIFY(metadata.timestampEpochMs >= 0);

    const QStringList logfmtLines = {"time=2026-03-28T00:00:04Z level=info msg=hello",
                                    "level=error msg=failed", "event=request status=200"};
    const auto logfmt = FormatDetector::detect("app.log", logfmtLines);
    QVERIFY(logfmt.detected);
    QCOMPARE(logfmt.format.id, QString("logfmt"));

    const QStringList plainLines = {"ordinary message id=1", "ordinary message id=2", "ordinary message id=3"};
    const auto plain = FormatDetector::detect("app.log", plainLines);
    QVERIFY(!plain.detected || plain.format.id != QLatin1String("logfmt"));
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
