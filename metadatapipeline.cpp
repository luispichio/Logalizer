#include "metadatapipeline.h"

#include "logdatabase.h"
#include "loglineparser.h"
#include "loglinestore.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QRunnable>
#include <QtCore/QtLogging>

namespace {
class ParseMetadataTask : public QRunnable
{
public:
    ParseMetadataTask(int fileId, QVector<MetadataInputLine>&& lines, const MetadataDetectionConfig& config)
        : m_fileId(fileId), m_lines(std::move(lines)), m_config(config) {}

    void run() override {
        QVector<LineMetadataRecord> parsed;
        parsed.reserve(m_lines.size());

        for (const MetadataInputLine& line : m_lines) {
            const ParsedLineMetadata metadata = parseLineMetadata(QStringView(line.raw), m_config);
            if (metadata.level == LogLevel::Unknown && metadata.timestampText.isEmpty()) {
                continue;
            }
            //qDebug() << line.lineNumber << " " << metadata.timestampText << " " << QVariant::fromValue(metadata.level).toString();
            parsed.append(LineMetadataRecord(line.lineNumber, metadata.timestampText,
                                             metadata.timestampEpochMs, metadata.level));
        }

        MetadataPipeline::instance().enqueueParsedBatch(m_fileId, m_lines.size(), std::move(parsed));
        MetadataPipeline::instance().finishInputBatch(m_lines.size());
    }

private:
    int m_fileId;
    QVector<MetadataInputLine> m_lines;
    MetadataDetectionConfig m_config;
};

class ReprocessMetadataTask : public QRunnable
{
public:
    ReprocessMetadataTask(int fileId, QSharedPointer<LogLineStore> store, const MetadataDetectionConfig& config)
        : m_fileId(fileId), m_store(std::move(store)), m_config(config) {}

    void run() override {
        if (!m_store || !LogDatabase::instance().clearMetadata(m_fileId)) {
            return;
        }

        constexpr int BatchSize = 2000;
        QVector<LineMetadataRecord> parsed;
        parsed.reserve(BatchSize);
        const int lineCount = m_store->lineCount();
        int batchStart = 0;
        for (int lineNumber = 0; lineNumber < lineCount; ++lineNumber) {
            const ParsedLineMetadata metadata = parseLineMetadata(QStringView(m_store->lineText(lineNumber)), m_config);
            if (metadata.level != LogLevel::Unknown || !metadata.timestampText.isEmpty()) {
                parsed.append(LineMetadataRecord(lineNumber, metadata.timestampText, metadata.timestampEpochMs, metadata.level));
            }
            if ((lineNumber + 1) % BatchSize == 0 || lineNumber + 1 == lineCount) {
                LogDatabase::instance().insertMetadataBatch(m_fileId, parsed);
                MetadataPipeline::instance().enqueueParsedBatch(m_fileId, lineNumber + 1 - batchStart, {});
                parsed.clear();
                batchStart = lineNumber + 1;
            }
        }
    }

private:
    int m_fileId;
    QSharedPointer<LogLineStore> m_store;
    MetadataDetectionConfig m_config;
};
}

MetadataPipeline& MetadataPipeline::instance() {
    static MetadataPipeline pipeline;
    return pipeline;
}

MetadataPipeline::MetadataPipeline() {
    m_detectionConfig = loadMetadataDetectionConfig();

    const int parserThreads = qBound(1, QThread::idealThreadCount() - 2, 8);
    m_parserPool.setMaxThreadCount(parserThreads);
    m_parserPool.setThreadPriority(QThread::HighPriority);

    QObject::connect(&m_writerThread, &QThread::started, [this]() { writerLoop(); });
    m_writerThread.start(QThread::HighPriority);

    qInfo() << "MetadataPipeline: Started with" << parserThreads << "parser threads.";
}

MetadataPipeline::~MetadataPipeline() {
    shutdown();
}

void MetadataPipeline::enqueueBatch(int fileId, const QVector<LineRecord>& records) {
    if (records.isEmpty()) {
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        if (m_stopping || m_cancelledFileIds.contains(fileId)) {
            return;
        }
    }

/*
    const int currentPending = m_pendingInputLines.loadRelaxed();
    if (currentPending + records.size() > MaxPendingInputLines) {
        QMutexLocker locker(&m_mutex);
        m_progressByFile[fileId].droppedLines += records.size();
        qWarning() << "MetadataPipeline: Dropping metadata batch due to backlog for fileId" << fileId;
        return;
    }
*/

    QVector<MetadataInputLine> lines;
    lines.reserve(records.size());
    for (const LineRecord& record : records) {
        lines.append(MetadataInputLine{record.raw, record.lineNumber});
    }

    m_pendingInputLines.fetchAndAddRelaxed(lines.size());
    {
        QMutexLocker locker(&m_mutex);
        m_progressByFile[fileId].queuedLines += lines.size();
    }
    MetadataDetectionConfig config;
    {
        QMutexLocker locker(&m_mutex);
        config = m_detectionConfig;
        const LogFormatDetectionResult formatResult = m_formatByFile.value(fileId);
        if (formatResult.detected) {
            config.hasFormat = true;
            config.format = formatResult.format;
            config.formatPatternName = formatResult.patternName;
        }
        config.referenceDate = m_referenceDateByFile.value(fileId, QDate::currentDate());
    }
    m_parserPool.start(new ParseMetadataTask(fileId, std::move(lines), config));
}

void MetadataPipeline::cancelFile(int fileId) {
    QMutexLocker locker(&m_mutex);
    m_cancelledFileIds.insert(fileId);
    m_formatByFile.remove(fileId);
    m_referenceDateByFile.remove(fileId);

    for (int i = m_pendingWrites.size() - 1; i >= 0; --i) {
        if (m_pendingWrites.at(i).first == fileId) {
            m_pendingWrites.removeAt(i);
        }
    }
}

void MetadataPipeline::finishInputBatch(int lineCount) {
    m_pendingInputLines.fetchAndSubRelaxed(lineCount);
}

MetadataProgress MetadataPipeline::progress(int fileId) const {
    QMutexLocker locker(&m_mutex);
    return m_progressByFile.value(fileId);
}

void MetadataPipeline::reloadConfig() {
    QMutexLocker locker(&m_mutex);
    m_detectionConfig = loadMetadataDetectionConfig();
}

void MetadataPipeline::setDetectedFormat(int fileId, const LogFormatDetectionResult& result) {
    QMutexLocker locker(&m_mutex);
    if (result.detected) {
        m_formatByFile.insert(fileId, result);
    } else {
        m_formatByFile.remove(fileId);
    }
}

LogFormatDetectionResult MetadataPipeline::detectedFormat(int fileId) const {
    QMutexLocker locker(&m_mutex);
    return m_formatByFile.value(fileId);
}

void MetadataPipeline::setReferenceDate(int fileId, const QDate& date) {
    QMutexLocker locker(&m_mutex);
    m_referenceDateByFile.insert(fileId, date.isValid() ? date : QDate::currentDate());
}

void MetadataPipeline::reprocessFile(int fileId) {
    const auto store = LogLineStoreRegistry::instance().store(fileId);
    if (!store) {
        return;
    }

    MetadataDetectionConfig config;
    {
        QMutexLocker locker(&m_mutex);
        config = m_detectionConfig;
        const LogFormatDetectionResult formatResult = m_formatByFile.value(fileId);
        if (formatResult.detected) {
            config.hasFormat = true;
            config.format = formatResult.format;
            config.formatPatternName = formatResult.patternName;
        }
        config.referenceDate = m_referenceDateByFile.value(fileId, QDate::currentDate());
        MetadataProgress& progress = m_progressByFile[fileId];
        progress = MetadataProgress{};
        progress.queuedLines = store->lineCount();
    }
    m_parserPool.start(new ReprocessMetadataTask(fileId, store, config));
}

void MetadataPipeline::shutdown() {
    {
        QMutexLocker locker(&m_mutex);
        if (m_stopping) {
            return;
        }
        m_stopping = true;
    }

    m_parserPool.waitForDone();
    m_writerThread.quit();
    m_writerThread.wait(3000);
}

void MetadataPipeline::enqueueParsedBatch(int fileId, int processedLines, QVector<LineMetadataRecord>&& records) {
    QMutexLocker locker(&m_mutex);
    if (m_stopping || m_cancelledFileIds.contains(fileId)) {
        return;
    }
    MetadataProgress& progress = m_progressByFile[fileId];
    progress.processedLines += processedLines;
    progress.taggedLines += records.size();
    if (records.isEmpty()) {
        return;
    }
    m_pendingWrites.enqueue(qMakePair(fileId, std::move(records)));
}

void MetadataPipeline::writerLoop() {
    while (true) {
        QPair<int, QVector<LineMetadataRecord>> item;
        bool hasItem = false;

        {
            QMutexLocker locker(&m_mutex);
            if (m_stopping && m_pendingWrites.isEmpty()) {
                break;
            }
            if (!m_pendingWrites.isEmpty()) {
                item = std::move(m_pendingWrites.dequeue());
                hasItem = true;
            }
        }

        if (!hasItem) {
            QThread::msleep(10);
            continue;
        }

        {
            QMutexLocker locker(&m_mutex);
            if (m_cancelledFileIds.contains(item.first)) {
                continue;
            }
        }

        if (LogDatabase::instance().isFileActive(item.first)) {
            LogDatabase::instance().insertMetadataBatch(item.first, item.second);
        }
    }
}
