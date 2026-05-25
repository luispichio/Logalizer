#include "fileworker.h"
#include "appsettings.h"
#include "formatdetector.h"
#include "loglinestore.h"
#include "logdatabase.h"
#include "metadatapipeline.h"

#include <QFile>
#include <QFileInfo>
#include <QtCore/QtLogging>

FileWorker::FileWorker(const QString& fileName, int fileId, QObject* parent)
    : QObject(parent), m_fileName(fileName), m_fileId(fileId), m_batchSize(AppSettings::fileBatchSize()), m_sampleLineLimit(AppSettings::formatDetectionSampleLines()) {}

FileWorker::~FileWorker() {}

void FileWorker::start() {
    doWork();
}

void FileWorker::stop() {
    m_stopRequested = true;
}

void FileWorker::doWork() {
    QFileInfo fileInfo(m_fileName);
    if (!fileInfo.exists()) {
        emit error(m_fileId, QString("File does not exist: %1").arg(m_fileName));
        return;
    }

    if (!LogDatabase::instance().createTable(m_fileId)) {
        emit error(m_fileId, "Failed to create database table");
        return;
    }

    qint64 totalBytes = fileInfo.size();
    qInfo() << "FileWorker: Ingesting" << m_fileName;

    auto store = QSharedPointer<MmapLineStore>::create(m_fileName);
    QString storeError;
    if (!store->open(&storeError)) {
        emit error(m_fileId, storeError);
        return;
    }
    LogLineStoreRegistry::instance().registerStore(m_fileId, store);

    QFile file(m_fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit error(m_fileId, QString("Cannot open file: %1").arg(m_fileName));
        return;
    }

    QVector<LineRecord> batch;
    batch.reserve(m_batchSize);

    const qint32 totalLines = store->lineCount();
    QStringList sampleLines;
    sampleLines.reserve(qMin<int>(m_sampleLineLimit, totalLines));
    for (qint32 lineNumber = 0; lineNumber < totalLines && sampleLines.size() < m_sampleLineLimit; ++lineNumber) {
        const QString line = QString::fromUtf8(store->lineBytes(lineNumber)).trimmed();
        if (!line.isEmpty()) {
            sampleLines.append(line);
        }
    }
    emit formatDetected(m_fileId, FormatDetector::detect(m_fileName, sampleLines));

    qint64 bytesProcessed = 0;

    for (qint32 lineNumber = 0; lineNumber < totalLines; ++lineNumber) {
        if (m_stopRequested) {
            qInfo() << "FileWorker: Stop requested, aborting ingestion.";
            break;
        }

        const qint64 posBefore = store->filePosition(lineNumber);
        const QByteArray lineBytes = store->lineBytes(lineNumber);
        bytesProcessed = lineNumber + 1 < totalLines
            ? store->filePosition(lineNumber + 1)
            : totalBytes;
        const QString line = QString::fromUtf8(lineBytes);

        batch.append(LineRecord(line, posBefore, lineNumber));

        if (batch.size() >= m_batchSize) {
            LogDatabase::instance().insertBatch(m_fileId, batch);
            MetadataPipeline::instance().enqueueBatch(m_fileId, batch);
            batch.clear();

            emit chunkInserted(m_fileId, lineNumber + 1);
            emit progressUpdate(m_fileId, bytesProcessed, totalBytes, lineNumber + 1);
        }
    }

    if (!m_stopRequested && !batch.isEmpty()) {
        LogDatabase::instance().insertBatch(m_fileId, batch);
        MetadataPipeline::instance().enqueueBatch(m_fileId, batch);
        emit chunkInserted(m_fileId, totalLines);
    }

    emit progressUpdate(m_fileId, totalBytes, totalBytes, totalLines);
    emit finished(m_fileId);

    qInfo() << "FileWorker: Ingestion complete." << totalLines << "lines,"
            << totalBytes << "bytes.";
}
