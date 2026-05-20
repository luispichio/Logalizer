#ifndef LOGLINEPARSER_H
#define LOGLINEPARSER_H

#include "linerecord.h"
#include "metadataconfig.h"

#include <QString>
#include <QStringView>

struct ParsedLineMetadata {
    QString timestampText;
    qint64 timestampEpochMs = -1;
    LogLevel level = LogLevel::Unknown;
};

ParsedLineMetadata parseLineMetadata(QStringView line);
ParsedLineMetadata parseLineMetadata(QStringView line, const MetadataDetectionConfig& config);
QString logLevelToString(LogLevel level);

#endif // LOGLINEPARSER_H
