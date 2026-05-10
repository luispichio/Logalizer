#include "fileworker.h"
#include "logdatabase.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonValue>
#include <QTextStream>
#include <QtLogging>

namespace {
const QStringList kPriorityTimestampFields = {
    "@timestamp",
    "timestamp",
    "time",
    "ts",
    "datetime",
    "date",
};

const QRegularExpression kRawTimestampRegex(
    R"((\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:?\d{2})?|\d{4}-\d{2}-\d{2}))"
);
}

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

    QFile file(m_fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit error(m_fileId, QString("Cannot open file: %1").arg(m_fileName));
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

        batch.append(parseLine(line, posBefore, lineNumber));
        ++lineNumber;

        if (batch.size() >= CHUNK_SIZE) {
            LogDatabase::instance().insertBatch(m_fileId, batch);
            batch.clear();

            emit chunkInserted(m_fileId, lineNumber);
            emit progressUpdate(m_fileId, bytesProcessed, totalBytes, lineNumber);
        }
    }

    if (!batch.isEmpty()) {
        LogDatabase::instance().insertBatch(m_fileId, batch);
        emit chunkInserted(m_fileId, lineNumber);
    }

    emit progressUpdate(m_fileId, totalBytes, totalBytes, lineNumber);
    emit finished(m_fileId);

    qInfo() << "FileWorker: Ingestion complete." << lineNumber << "lines,"
            << totalBytes << "bytes.";
}

LineRecord FileWorker::parseLine(const QString& line, qint64 offset, qint32 lineNum) const {
    LineRecord record(line, offset, lineNum);

    QString timestampText;
    qint64 timestampUnixMs = -1;
    QString timestampSource;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && doc.isObject()) {
        if (extractTimestampFromJson(doc.object(), timestampText, timestampUnixMs, timestampSource)) {
            record.timestampText = timestampText;
            record.timestampUnixMs = timestampUnixMs;
            record.timestampSource = timestampSource;
            return record;
        }
    }

    if (extractTimestampFromRaw(line, timestampText, timestampUnixMs, timestampSource)) {
        record.timestampText = timestampText;
        record.timestampUnixMs = timestampUnixMs;
        record.timestampSource = timestampSource;
    }

    return record;
}

bool FileWorker::extractTimestampFromJson(const QJsonObject& obj,
                                          QString& timestampText,
                                          qint64& timestampUnixMs,
                                          QString& timestampSource) const {
    QString normalizedText;

    for (const QString& key : kPriorityTimestampFields) {
        auto it = obj.find(key);
        if (it == obj.end() || !it->isString()) {
            continue;
        }
        if (tryParseTimestampText(it->toString(), normalizedText, timestampUnixMs)) {
            timestampText = normalizedText;
            timestampSource = key;
            return true;
        }
    }

    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (!it->isString()) {
            continue;
        }
        if (tryParseTimestampText(it->toString(), normalizedText, timestampUnixMs)) {
            timestampText = normalizedText;
            timestampSource = QString("json:%1").arg(it.key());
            return true;
        }
    }

    return false;
}

bool FileWorker::extractTimestampFromRaw(const QString& line,
                                         QString& timestampText,
                                         qint64& timestampUnixMs,
                                         QString& timestampSource) const {
    const QRegularExpressionMatch match = kRawTimestampRegex.match(line);
    if (!match.hasMatch()) {
        return false;
    }

    QString normalizedText;
    if (!tryParseTimestampText(match.captured(1), normalizedText, timestampUnixMs)) {
        return false;
    }

    timestampText = normalizedText;
    timestampSource = "raw_regex";
    return true;
}

bool FileWorker::tryParseTimestampText(const QString& input, QString& normalizedText, qint64& unixMs) const {
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    QDateTime dt = QDateTime::fromString(trimmed, Qt::ISODateWithMs);
    if (!dt.isValid()) {
        dt = QDateTime::fromString(trimmed, Qt::ISODate);
    }
    if (!dt.isValid()) {
        dt = QDateTime::fromString(trimmed, "yyyy-MM-dd HH:mm:ss.zzz");
    }
    if (!dt.isValid()) {
        dt = QDateTime::fromString(trimmed, "yyyy-MM-dd HH:mm:ss");
    }
    if (!dt.isValid()) {
        dt = QDateTime::fromString(trimmed, "yyyy-MM-dd");
    }
    if (!dt.isValid()) {
        return false;
    }

    if (dt.timeSpec() == Qt::LocalTime || dt.timeSpec() == Qt::TimeZone) {
        dt = dt.toUTC();
    } else if (dt.timeSpec() == Qt::OffsetFromUTC) {
        dt = dt.toUTC();
    } else if (dt.timeSpec() == Qt::UTC) {
        dt = dt.toUTC();
    }

    normalizedText = dt.toString(Qt::ISODateWithMs);
    unixMs = dt.toMSecsSinceEpoch();
    return true;
}
