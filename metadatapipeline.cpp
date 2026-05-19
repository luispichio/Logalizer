#include "metadatapipeline.h"

#include "logdatabase.h"
#include "loglineparser.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QRunnable>
#include <QtCore/QtLogging>

namespace {
class ParseMetadataTask : public QRunnable
{
public:
    ParseMetadataTask(int fileId, QVector<MetadataInputLine>&& lines)
        : m_fileId(fileId), m_lines(std::move(lines)) {}

    void run() override {
        QVector<LineMetadataRecord> parsed;
        parsed.reserve(m_lines.size());

        for (const MetadataInputLine& line : m_lines) {
            const ParsedLineMetadata metadata = parseLineMetadata(QStringView(line.raw));
            if (metadata.level == LogLevel::Unknown && metadata.timestampText.isEmpty()) {
                continue;
            }
            parsed.append(LineMetadataRecord(line.lineNumber, metadata.timestampText,
                                             metadata.timestampEpochMs, metadata.level));
        }

        MetadataPipeline::instance().enqueueParsedBatch(m_fileId, std::move(parsed));
        MetadataPipeline::instance().finishInputBatch(m_lines.size());
    }

private:
    int m_fileId;
    QVector<MetadataInputLine> m_lines;
};
}

MetadataPipeline& MetadataPipeline::instance() {
    static MetadataPipeline pipeline;
    return pipeline;
}

MetadataPipeline::MetadataPipeline() {
    const int parserThreads = qBound(1, QThread::idealThreadCount() - 2, 4);
    m_parserPool.setMaxThreadCount(parserThreads);
    m_parserPool.setThreadPriority(QThread::LowPriority);

    QObject::connect(&m_writerThread, &QThread::started, [this]() { writerLoop(); });
    m_writerThread.start(QThread::LowPriority);

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

    const int currentPending = m_pendingInputLines.loadRelaxed();
    if (currentPending + records.size() > MaxPendingInputLines) {
        qWarning() << "MetadataPipeline: Dropping metadata batch due to backlog for fileId" << fileId;
        return;
    }

    QVector<MetadataInputLine> lines;
    lines.reserve(records.size());
    for (const LineRecord& record : records) {
        lines.append(MetadataInputLine{record.raw, record.lineNumber});
    }

    m_pendingInputLines.fetchAndAddRelaxed(lines.size());
    m_parserPool.start(new ParseMetadataTask(fileId, std::move(lines)));
}

void MetadataPipeline::cancelFile(int fileId) {
    QMutexLocker locker(&m_mutex);
    m_cancelledFileIds.insert(fileId);

    for (int i = m_pendingWrites.size() - 1; i >= 0; --i) {
        if (m_pendingWrites.at(i).first == fileId) {
            m_pendingWrites.removeAt(i);
        }
    }
}

void MetadataPipeline::finishInputBatch(int lineCount) {
    m_pendingInputLines.fetchAndSubRelaxed(lineCount);
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

void MetadataPipeline::enqueueParsedBatch(int fileId, QVector<LineMetadataRecord>&& records) {
    if (records.isEmpty()) {
        return;
    }

    QMutexLocker locker(&m_mutex);
    if (m_stopping || m_cancelledFileIds.contains(fileId)) {
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
