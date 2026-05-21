#ifndef METADATACONFIG_H
#define METADATACONFIG_H

#include <QRegularExpression>
#include <QString>
#include <QVector>

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
    int regexScanLimit = 1024;
    bool preferRegexRules = false;
};

MetadataDetectionConfig loadMetadataDetectionConfig();

#endif // METADATACONFIG_H
