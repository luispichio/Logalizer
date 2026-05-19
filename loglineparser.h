#ifndef LOGLINEPARSER_H
#define LOGLINEPARSER_H

#include "linerecord.h"

#include <QString>
#include <QStringView>

struct ParsedLineMetadata {
    QString timestampText;
    qint64 timestampEpochMs = -1;
    LogLevel level = LogLevel::Unknown;
};

ParsedLineMetadata parseLineMetadata(QStringView line);
QString logLevelToString(LogLevel level);

#endif // LOGLINEPARSER_H
