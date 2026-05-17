#include "streamworker.h"
#include "logdatabase.h"

#include <QIODevice>
#include <QTextStream>
#include <QtCore/QtLogging>

#include <cstdio>

StreamWorker::StreamWorker(int fileId, QObject* parent)
    : QObject(parent), m_fileId(fileId) {}

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

    QTextStream stream(stdin, QIODevice::ReadOnly);
    QVector<LineRecord> batch;
    batch.reserve(CHUNK_SIZE);

    qint32 lineNumber = 0;
    qint64 logicalPosition = 0;

    while (!stream.atEnd()) {
        if (m_stopRequested) {
            qInfo() << "StreamWorker: Stop requested, aborting ingestion.";
            break;
        }

        const QString line = stream.readLine();
        batch.append(LineRecord(line, logicalPosition, lineNumber));
        logicalPosition += line.toUtf8().size() + 1;
        ++lineNumber;

        if (batch.size() >= CHUNK_SIZE) {
            LogDatabase::instance().insertBatch(m_fileId, batch);
            batch.clear();

            emit chunkInserted(m_fileId, lineNumber);
            emit progressUpdate(m_fileId, logicalPosition, 0, lineNumber);
        }
    }

    if (!batch.isEmpty()) {
        LogDatabase::instance().insertBatch(m_fileId, batch);
        emit chunkInserted(m_fileId, lineNumber);
    }

    emit progressUpdate(m_fileId, logicalPosition, logicalPosition, lineNumber);
    emit finished(m_fileId);

    qInfo() << "StreamWorker: Ingestion complete." << lineNumber << "lines," << logicalPosition << "bytes.";
}
