#ifndef LOGFORMAT_H
#define LOGFORMAT_H

#include <QHash>
#include <QMetaType>
#include <QRegularExpression>
#include <QString>
#include <QVector>

struct LogFormatPattern {
    QString name;
    QString pattern;
    QRegularExpression regex;
};

struct LogFormatDefinition {
    QString id;
    QString title;
    QString description;
    QString source;
    QString filePattern;
    QRegularExpression fileRegex;
    bool json = false;
    QString timestampField;
    QString levelField;
    QString bodyField;
    QHash<QString, QString> levelPatterns;
    QVector<LogFormatPattern> patterns;
    QStringList valueNames;

    QString displayName() const;
};

struct LogFormatDetectionResult {
    bool detected = false;
    LogFormatDefinition format;
    QString patternName;
    int score = 0;
    int matchedLines = 0;
    int sampledLines = 0;
};

class LogFormatRegistry
{
public:
    static QVector<LogFormatDefinition> loadFormats();
};

Q_DECLARE_METATYPE(LogFormatDetectionResult)

#endif // LOGFORMAT_H
