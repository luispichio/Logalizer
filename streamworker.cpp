#include "streamworker.h"
#include "appsettings.h"
#include "formatdetector.h"
#include "loglinestore.h"
#include "logdatabase.h"
#include "metadatapipeline.h"

#include <QByteArray>
#include <QIODevice>
#include <QDate>
#include <QtCore/QtLogging>

#include <cstdio>

StreamWorker::StreamWorker(int fileId, QObject* parent)
    : QObject(parent), m_fileId(fileId), m_batchSize(AppSettings::streamBatchSize()), m_sampleLineLimit(AppSettings::formatDetectionSampleLines()) {}

StreamWorker::~StreamWorker() {}

void StreamWorker::start() {
    doWork();
}

void StreamWorker::stop() {
    m_stopRequested = true;
}

void StreamWorker::doWork() {
    if (!LogDatabase::instance().createTable(m_fileId)) {
        emit error(m_fileId, "Failed to create database table");
        return;
    }

    qInfo() << "StreamWorker: Ingesting stdin";

    auto store = QSharedPointer<SpillLineStore>::create();
    QString storeError;
    if (!store->open(&storeError)) {
        emit error(m_fileId, storeError);
        return;
    }
    LogLineStoreRegistry::instance().registerStore(m_fileId, store);

    QVector<LineRecord> batch;
    batch.reserve(m_batchSize);

    QByteArray pending;
    char readBuffer[64 * 1024];
    qint32 lineNumber = 0;
    qint64 logicalPosition = 0;

    while (!m_stopRequested) {
        if (m_stopRequested) {
            qInfo() << "StreamWorker: Stop requested, aborting ingestion.";
            break;
        }

        const size_t bytesRead = std::fread(readBuffer, 1, sizeof(readBuffer), stdin);
        if (bytesRead == 0) {
            if (std::feof(stdin)) {
                break;
            }
            if (std::ferror(stdin)) {
                emit error(m_fileId, "Failed to read stdin");
                return;
            }
            continue;
        }

        pending.append(readBuffer, static_cast<qsizetype>(bytesRead));
        int newlineIndex = -1;
        while ((newlineIndex = pending.indexOf('\n')) >= 0) {
            QByteArray lineBytes = pending.left(newlineIndex);
            if (lineBytes.endsWith('\r')) {
                lineBytes.chop(1);
            }
            pending.remove(0, newlineIndex + 1);
            appendLine(lineBytes, store, batch, lineNumber, logicalPosition);
            if (batch.size() >= m_batchSize) {
                flushBatch(batch, lineNumber, logicalPosition);
            }
        }
    }

    if (!pending.isEmpty()) {
        if (pending.endsWith('\r')) {
            pending.chop(1);
        }
        appendLine(pending, store, batch, lineNumber, logicalPosition);
    }

    if (!batch.isEmpty()) {
        flushBatch(batch, lineNumber, logicalPosition);
    }

    if (!m_formatDetectionEmitted) {
        emit formatDetected(m_fileId, FormatDetector::detect("stdin", m_sampleLines));
        m_formatDetectionEmitted = true;
    }

    emit progressUpdate(m_fileId, logicalPosition, logicalPosition, lineNumber);
    emit finished(m_fileId);

    qInfo() << "StreamWorker: Ingestion complete." << lineNumber << "lines," << logicalPosition << "bytes.";
}

void StreamWorker::appendLine(const QByteArray& lineBytes, QSharedPointer<SpillLineStore> store,
                              QVector<LineRecord>& batch, qint32& lineNumber, qint64& logicalPosition) {
    const QString line = QString::fromUtf8(lineBytes);
    if (!m_formatDetectionEmitted && m_sampleLines.size() < m_sampleLineLimit && !line.trimmed().isEmpty()) {
        m_sampleLines.append(line.trimmed());
    }

    store->appendLine(lineBytes, logicalPosition);
    batch.append(LineRecord(line, logicalPosition, lineNumber));
    logicalPosition += lineBytes.size() + 1;
    ++lineNumber;
}

void StreamWorker::flushBatch(QVector<LineRecord>& batch, qint32 lineNumber, qint64 logicalPosition) {
    if (batch.isEmpty()) {
        return;
    }
    if (!m_formatDetectionEmitted) {
        emit formatDetected(m_fileId, FormatDetector::detect("stdin", m_sampleLines));
        m_formatDetectionEmitted = true;
    }
    LogDatabase::instance().insertBatch(m_fileId, batch);
    MetadataPipeline::instance().enqueueBatch(m_fileId, batch);
    batch.clear();

    emit chunkInserted(m_fileId, lineNumber);
    emit progressUpdate(m_fileId, logicalPosition, 0, lineNumber);
}
