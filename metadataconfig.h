#ifndef METADATACONFIG_H
#define METADATACONFIG_H

#include <QRegularExpression>
#include <QDate>
#include <QString>
#include <QVector>

#include "logformat.h"

struct MetadataRegexRule {
    QString name;
    QString pattern;
    int captureGroup = 1;
    bool enabled = true;
};

struct CompiledMetadataRegexRule {
    QString name;
    QRegularExpression regex;
    int captureGroup = 1;
    bool enabled = true;
};

struct MetadataDetectionConfig {
    QVector<CompiledMetadataRegexRule> timestampRules;
    QVector<CompiledMetadataRegexRule> levelRules;
    LogFormatDefinition format;
    QString formatPatternName;
    bool hasFormat = false;
    QDate referenceDate;
    int regexScanLimit = 1024;
    bool preferRegexRules = false;
};

MetadataDetectionConfig loadMetadataDetectionConfig();

#endif // METADATACONFIG_H
