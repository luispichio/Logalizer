#ifndef LINERECORD_H
#define LINERECORD_H

#include <QString>
#include <cstdint>

struct LineRecord {
    QString raw;          // original line
    qint64 filePosition;  // byte offset in file
    qint32 lineNumber;
    QString timestampText;
    qint64 timestampUnixMs;
    QString timestampSource;

    LineRecord() : filePosition(0), lineNumber(0), timestampUnixMs(-1) {}

    LineRecord(const QString& raw, qint64 filePosition, qint32 lineNumber)
        : raw(raw), filePosition(filePosition), lineNumber(lineNumber), timestampUnixMs(-1) {}
};

#endif // LINERECORD_H
