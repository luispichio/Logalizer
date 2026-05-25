#include "formatdetector.h"

#include "appsettings.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
QString jsonValueForPath(const QJsonObject& object, const QString& path) {
    QJsonValue value = object;
    for (const QString& part : path.split('.', Qt::SkipEmptyParts)) {
        if (!value.isObject()) {
            return QString();
        }
        value = value.toObject().value(part);
    }

    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 15);
    }
    if (value.isBool()) {
        return value.toBool() ? "true" : "false";
    }
    return QString();
}

int jsonScore(const LogFormatDefinition& format, const QStringList& lines, int fileNameScore, int* matchedLines) {
    int validJson = 0;
    int fieldHits = 0;
    for (const QString& line : lines) {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }
        ++validJson;
        const QJsonObject object = doc.object();
        if (!format.timestampField.isEmpty() && !jsonValueForPath(object, format.timestampField).isEmpty()) {
            fieldHits += 2;
        }
        if (!format.levelField.isEmpty() && !jsonValueForPath(object, format.levelField).isEmpty()) {
            fieldHits += 2;
        }
        if (!format.bodyField.isEmpty() && !jsonValueForPath(object, format.bodyField).isEmpty()) {
            fieldHits += 1;
        }
    }

    *matchedLines = validJson;
    if (validJson == 0) {
        return 0;
    }
    return fileNameScore + (validJson * 10) + fieldHits;
}

int regexScore(const LogFormatDefinition& format, const QStringList& lines, int fileNameScore, QString* patternName, int* matchedLines) {
    int bestScore = 0;
    QString bestPattern;
    int bestMatches = 0;

    for (const LogFormatPattern& pattern : format.patterns) {
        int matches = 0;
        int fieldHits = 0;
        for (const QString& line : lines) {
            const QRegularExpressionMatch match = pattern.regex.match(line);
            if (!match.hasMatch()) {
                continue;
            }
            ++matches;
            if (!format.timestampField.isEmpty() && !match.captured(format.timestampField).isEmpty()) {
                fieldHits += 2;
            }
            if (!format.levelField.isEmpty() && !match.captured(format.levelField).isEmpty()) {
                fieldHits += 2;
            }
            if (!format.bodyField.isEmpty() && !match.captured(format.bodyField).isEmpty()) {
                fieldHits += 1;
            }
        }

        const int score = fileNameScore + (matches * 10) + fieldHits;
        if (score > bestScore) {
            bestScore = score;
            bestPattern = pattern.name;
            bestMatches = matches;
        }
    }

    *patternName = bestPattern;
    *matchedLines = bestMatches;
    return bestScore;
}
}

LogFormatDetectionResult FormatDetector::detect(const QString& sourceName, const QStringList& sampleLines) {
    LogFormatDetectionResult result;
    if (!AppSettings::formatDetectionEnabled() || sampleLines.isEmpty()) {
        return result;
    }

    const QVector<LogFormatDefinition> formats = LogFormatRegistry::loadFormats();
    const QString fileName = QFileInfo(sourceName).fileName();
    result.sampledLines = sampleLines.size();

    for (const LogFormatDefinition& format : formats) {
        int fileNameScore = 0;
        if (!format.filePattern.isEmpty()) {
            if (!format.fileRegex.isValid() || !format.fileRegex.match(fileName).hasMatch()) {
                continue;
            }
            fileNameScore = 25;
        }

        int matchedLines = 0;
        QString patternName;
        const int score = format.json
            ? jsonScore(format, sampleLines, fileNameScore, &matchedLines)
            : regexScore(format, sampleLines, fileNameScore, &patternName, &matchedLines);

        if (score > result.score) {
            result.detected = true;
            result.format = format;
            result.patternName = patternName;
            result.score = score;
            result.matchedLines = matchedLines;
        }
    }

    const int minMatches = qMax(1, qMin(3, sampleLines.size()));
    if (!result.detected || result.matchedLines < minMatches) {
        return LogFormatDetectionResult{};
    }
    return result;
}
