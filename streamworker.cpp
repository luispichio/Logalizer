#include "streamworker.h"
#include "appsettings.h"
#include "formatdetector.h"
#include "loglinestore.h"
#include "logdatabase.h"
#include "metadatapipeline.h"

#include <QIODevice>
#include <QTextStream>
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

    QTextStream stream(stdin, QIODevice::ReadOnly);
    QVector<LineRecord> batch;
    batch.reserve(m_batchSize);

    qint32 lineNumber = 0;
    qint64 logicalPosition = 0;

    while (!stream.atEnd()) {
        if (m_stopRequested) {
            qInfo() << "StreamWorker: Stop requested, aborting ingestion.";
            break;
        }

        const QString line = stream.readLine();
        const QByteArray lineBytes = line.toUtf8();
        if (!m_formatDetectionEmitted && m_sampleLines.size() < m_sampleLineLimit && !line.trimmed().isEmpty()) {
            m_sampleLines.append(line.trimmed());
        }
        store->appendLine(lineBytes, logicalPosition);
        batch.append(LineRecord(line, logicalPosition, lineNumber));
        logicalPosition += lineBytes.size() + 1;
        ++lineNumber;

        if (batch.size() >= m_batchSize) {
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
    }

    if (!batch.isEmpty()) {
        if (!m_formatDetectionEmitted) {
            emit formatDetected(m_fileId, FormatDetector::detect("stdin", m_sampleLines));
            m_formatDetectionEmitted = true;
        }
        LogDatabase::instance().insertBatch(m_fileId, batch);
        MetadataPipeline::instance().enqueueBatch(m_fileId, batch);
        emit chunkInserted(m_fileId, lineNumber);
    }

    emit progressUpdate(m_fileId, logicalPosition, logicalPosition, lineNumber);
    emit finished(m_fileId);

    qInfo() << "StreamWorker: Ingestion complete." << lineNumber << "lines," << logicalPosition << "bytes.";
}
