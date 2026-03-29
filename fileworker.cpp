#include "fileworker.h"
#include "logdatabase.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtLogging>

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

    qint64 totalBytes = fileInfo.size();

    // ─── Phase 1: Schema Detection ───────────────────────────────────
    qInfo() << "FileWorker: Phase 1 — Schema detection for" << m_fileName;

    SchemaDetector detector(SCAN_LINES);
    {
        QFile file(m_fileName);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            emit error(m_fileId, QString("Cannot open file: %1").arg(m_fileName));
            return;
        }
        QTextStream stream(&file);
        while (!stream.atEnd() && !detector.isComplete()) {
            if (m_stopRequested) return;
            QString line = stream.readLine();
            detector.feedLine(line);
        }
    }

    QVector<ColumnDef> columns = detector.detect();
    qInfo() << "FileWorker: Detected" << columns.size() << "columns from"
            << detector.linesFed() << "lines";

    // Create table in DB
    if (!LogDatabase::instance().createTable(m_fileId, columns)) {
        emit error(m_fileId, "Failed to create database table");
        return;
    }

    emit schemaReady(m_fileId, columns);

    // ─── Phase 2: Full Ingestion ─────────────────────────────────────
    qInfo() << "FileWorker: Phase 2 — Ingesting" << m_fileName;

    QFile file(m_fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit error(m_fileId, QString("Cannot reopen file: %1").arg(m_fileName));
        return;
    }

    QTextStream stream(&file);
    QVector<LineRecord> batch;
    batch.reserve(CHUNK_SIZE);

    qint32 lineNumber = 0;
    qint64 bytesProcessed = 0;

    while (!stream.atEnd()) {
        if (m_stopRequested) {
            qInfo() << "FileWorker: Stop requested, aborting ingestion.";
            break;
        }

        qint64 posBefore = file.pos();
        QString line = stream.readLine();
        qint64 posAfter = file.pos();
        bytesProcessed = posAfter;

        LineRecord record = parseLine(line, posBefore, lineNumber, columns);
        batch.append(record);
        lineNumber++;

        if (batch.size() >= CHUNK_SIZE) {
            LogDatabase::instance().insertBatch(m_fileId, batch, columns);
            batch.clear();

            emit chunkInserted(m_fileId, lineNumber);
            emit progressUpdate(m_fileId, bytesProcessed, totalBytes, lineNumber);
        }
    }

    // Insert remaining
    if (!batch.isEmpty()) {
        LogDatabase::instance().insertBatch(m_fileId, batch, columns);
        emit chunkInserted(m_fileId, lineNumber);
    }

    emit progressUpdate(m_fileId, totalBytes, totalBytes, lineNumber);
    emit finished(m_fileId);

    qInfo() << "FileWorker: Ingestion complete." << lineNumber << "lines,"
            << totalBytes << "bytes.";
}

LineRecord FileWorker::parseLine(const QString& line, qint64 offset, qint32 lineNum,
                                  const QVector<ColumnDef>& columns) {
    LineRecord record(line, offset, lineNum);

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return record;  // Non-JSON line: raw only

    QJsonObject obj = doc.object();
    for (const auto& col : columns) {
        if (obj.contains(col.name)) {
            QJsonValue val = obj.value(col.name);
            if (val.isString())
                record.fields[col.name] = val.toString();
            else if (val.isBool())
                record.fields[col.name] = val.toBool() ? "true" : "false";
            else if (val.isDouble())
                record.fields[col.name] = QString::number(val.toDouble(), 'g', 15);
            else
                record.fields[col.name] = "";
        }
    }

    return record;
}
