#include "fileworker.h"
#include "loglinestore.h"
#include "logdatabase.h"
#include "metadatapipeline.h"

#include <QFile>
#include <QFileInfo>
#include <QtCore/QtLogging>

FileWorker::FileWorker(const QString& fileName, int fileId, QObject* parent)
    : QObject(parent), m_fileName(fileName), m_fileId(fileId) {}

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
    batch.reserve(CHUNK_SIZE);

    qint32 lineNumber = 0;
    qint64 bytesProcessed = 0;

    while (!file.atEnd()) {
        if (m_stopRequested) {
            qInfo() << "FileWorker: Stop requested, aborting ingestion.";
            break;
        }

        qint64 posBefore = file.pos();
        QByteArray lineBytes = file.readLine();
        qint64 posAfter = file.pos();
        bytesProcessed = posAfter;

        while (lineBytes.endsWith('\n') || lineBytes.endsWith('\r')) {
            lineBytes.chop(1);
        }
        const qint32 lineLength = lineBytes.size();
        store->appendLine(posBefore, lineLength);
        const QString line = QString::fromUtf8(lineBytes);

        batch.append(LineRecord(line, posBefore, lineNumber));
        ++lineNumber;

        if (batch.size() >= CHUNK_SIZE) {
            LogDatabase::instance().insertBatch(m_fileId, batch);
            MetadataPipeline::instance().enqueueBatch(m_fileId, batch);
            batch.clear();

            emit chunkInserted(m_fileId, lineNumber);
            emit progressUpdate(m_fileId, bytesProcessed, totalBytes, lineNumber);
        }
    }

    if (!m_stopRequested && !batch.isEmpty()) {
        LogDatabase::instance().insertBatch(m_fileId, batch);
        MetadataPipeline::instance().enqueueBatch(m_fileId, batch);
        emit chunkInserted(m_fileId, lineNumber);
    }

    emit progressUpdate(m_fileId, totalBytes, totalBytes, lineNumber);
    emit finished(m_fileId);

    qInfo() << "FileWorker: Ingestion complete." << lineNumber << "lines,"
            << totalBytes << "bytes.";
}
