#ifndef LINERECORD_H
#define LINERECORD_H

#include <QString>
#include <cstdint>

enum class LogLevel : qint8 {
    Unknown = 0,
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
};

struct LineRecord {
    QString raw;          // original line
    qint64 filePosition;  // byte offset in file
    qint32 lineNumber;

    LineRecord() : filePosition(0), lineNumber(0) {}

    LineRecord(const QString& raw, qint64 filePosition, qint32 lineNumber)
        : raw(raw), filePosition(filePosition), lineNumber(lineNumber) {}
};

struct LineMetadataRecord {
    QString timestampText;
    qint64 timestampEpochMs;
    qint32 lineNumber;
    LogLevel level;

    LineMetadataRecord()
        : timestampEpochMs(-1), lineNumber(0), level(LogLevel::Unknown) {}

    LineMetadataRecord(qint32 lineNumber, const QString& timestampText,
                       qint64 timestampEpochMs, LogLevel level)
        : timestampText(timestampText), timestampEpochMs(timestampEpochMs),
          lineNumber(lineNumber), level(level) {}
};

#endif // LINERECORD_H
