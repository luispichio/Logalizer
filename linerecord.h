#ifndef LINERECORD_H
#define LINERECORD_H

#include <QMap>
#include <QString>
#include <cstdint>

struct LineRecord {
    QMap<QString, QString> fields;  // column name -> value as string
    QString raw;                     // original line
    qint64 filePosition;            // byte offset in file
    qint32 lineNumber;

    LineRecord() : filePosition(0), lineNumber(0) {}

    LineRecord(const QString& raw, qint64 filePosition, qint32 lineNumber)
        : raw(raw), filePosition(filePosition), lineNumber(lineNumber) {}

    LineRecord(const QMap<QString, QString>& fields, const QString& raw,
               qint64 filePosition, qint32 lineNumber)
        : fields(fields), raw(raw), filePosition(filePosition), lineNumber(lineNumber) {}
};

#endif // LINERECORD_H
