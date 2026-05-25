#include "loglineparser.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpressionMatch>
#include <QtGlobal>

namespace {
constexpr int TimestampScanLimit = 96;
constexpr int LevelScanLimit = 256;

bool isDigit(QChar ch) {
    return ch >= QLatin1Char('0') && ch <= QLatin1Char('9');
}

bool isAlpha(QChar ch) {
    const ushort c = ch.unicode();
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool isTokenBoundary(QChar ch) {
    return ch.isNull()
        || ch.isSpace()
        || ch == QLatin1Char('[')
        || ch == QLatin1Char(']')
        || ch == QLatin1Char('(')
        || ch == QLatin1Char(')')
        || ch == QLatin1Char('{')
        || ch == QLatin1Char('}')
        || ch == QLatin1Char('<')
        || ch == QLatin1Char('>')
        || ch == QLatin1Char(':')
        || ch == QLatin1Char('=')
        || ch == QLatin1Char(',')
        || ch == QLatin1Char('|')
        || ch == QLatin1Char('-')
        || ch == QLatin1Char('/');
}

QChar upperAscii(QChar ch) {
    const ushort c = ch.unicode();
    if (c >= 'a' && c <= 'z') {
        return QChar(c - 32);
    }
    return ch;
}

bool matchesAscii(QStringView line, int offset, const char* token, int tokenSize) {
    if (offset < 0 || offset + tokenSize > line.size()) {
        return false;
    }

    const QChar before = offset > 0 ? line[offset - 1] : QChar();
    const QChar after = offset + tokenSize < line.size() ? line[offset + tokenSize] : QChar();
    if (!isTokenBoundary(before) || !isTokenBoundary(after)) {
        return false;
    }

    for (int i = 0; i < tokenSize; ++i) {
        if (upperAscii(line[offset + i]) != QLatin1Char(token[i])) {
            return false;
        }
    }
    return true;
}

LogLevel detectLevel(QStringView line) {
    struct LevelToken {
        const char* text;
        int size;
        LogLevel level;
    };

    static constexpr LevelToken tokens[] = {
        {"TRACE", 5, LogLevel::Trace},
        {"DEBUG", 5, LogLevel::Debug},
        {"INFO", 4, LogLevel::Info},
        {"WARN", 4, LogLevel::Warn},
        {"WARNING", 7, LogLevel::Warn},
        {"ERROR", 5, LogLevel::Error},
        {"ERR", 3, LogLevel::Error},
        {"FATAL", 5, LogLevel::Fatal},
        {"CRITICAL", 8, LogLevel::Fatal},
    };

    const int limit = qMin(line.size(), LevelScanLimit);
    for (int i = 0; i < limit; ++i) {
        if (!isAlpha(line[i])) {
            continue;
        }

        for (const LevelToken& token : tokens) {
            if (i + token.size <= limit && matchesAscii(line, i, token.text, token.size)) {
                return token.level;
            }
        }
    }

    return LogLevel::Unknown;
}

LogLevel levelFromText(const QString& text) {
    const QString value = text.trimmed().toLower();
    if (value == QLatin1String("trace") || value == QLatin1String("trc")) {
        return LogLevel::Trace;
    }
    if (value == QLatin1String("debug") || value == QLatin1String("dbg")) {
        return LogLevel::Debug;
    }
    if (value == QLatin1String("info") || value == QLatin1String("information") || value == QLatin1String("notice")) {
        return LogLevel::Info;
    }
    if (value == QLatin1String("warn") || value == QLatin1String("warning")) {
        return LogLevel::Warn;
    }
    if (value == QLatin1String("error") || value == QLatin1String("err")) {
        return LogLevel::Error;
    }
    if (value == QLatin1String("fatal") || value == QLatin1String("critical")
        || value == QLatin1String("panic") || value == QLatin1String("alert")
        || value == QLatin1String("emergency")) {
        return LogLevel::Fatal;
    }
    return LogLevel::Unknown;
}

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

LogLevel levelFromFormat(const QString& value, const LogFormatDefinition& format) {
    const LogLevel direct = levelFromText(value);
    if (direct != LogLevel::Unknown) {
        return direct;
    }

    for (auto it = format.levelPatterns.constBegin(); it != format.levelPatterns.constEnd(); ++it) {
        const QString pattern = it.value();
        if (pattern.isEmpty()) {
            continue;
        }
        const QRegularExpression regex(pattern, QRegularExpression::CaseInsensitiveOption);
        if ((regex.isValid() && regex.match(value).hasMatch()) || value.compare(pattern, Qt::CaseInsensitive) == 0) {
            return levelFromText(it.key());
        }
    }
    return LogLevel::Unknown;
}

ParsedLineMetadata parseFormatJson(QStringView line, const LogFormatDefinition& format) {
    ParsedLineMetadata metadata;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(line.toString().toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return metadata;
    }

    const QJsonObject object = doc.object();
    if (!format.timestampField.isEmpty()) {
        metadata.timestampText = jsonValueForPath(object, format.timestampField);
    }
    if (!format.levelField.isEmpty()) {
        metadata.level = levelFromFormat(jsonValueForPath(object, format.levelField), format);
    }
    return metadata;
}

ParsedLineMetadata parseFormatRegex(QStringView line, const LogFormatDefinition& format, const QString& patternName) {
    ParsedLineMetadata metadata;
    for (const LogFormatPattern& pattern : format.patterns) {
        if (!patternName.isEmpty() && pattern.name != patternName) {
            continue;
        }

        const QRegularExpressionMatch match = pattern.regex.match(line.toString());
        if (!match.hasMatch()) {
            continue;
        }
        if (!format.timestampField.isEmpty()) {
            metadata.timestampText = match.captured(format.timestampField).trimmed();
        }
        if (!format.levelField.isEmpty()) {
            metadata.level = levelFromFormat(match.captured(format.levelField), format);
        }
        return metadata;
    }
    return metadata;
}

ParsedLineMetadata parseFormatMetadata(QStringView line, const MetadataDetectionConfig& config) {
    if (!config.hasFormat) {
        return ParsedLineMetadata{};
    }
    return config.format.json
        ? parseFormatJson(line, config.format)
        : parseFormatRegex(line, config.format, config.formatPatternName);
}

QString detectTimestampByRegex(QStringView line, const MetadataDetectionConfig& config) {
    if (config.timestampRules.isEmpty()) {
        return QString();
    }

    const QString subject = line.left(qMin(line.size(), config.regexScanLimit)).toString();
    for (const CompiledMetadataRegexRule& rule : config.timestampRules) {
        const QRegularExpressionMatch match = rule.regex.match(subject);
        if (!match.hasMatch()) {
            continue;
        }
        const QString captured = match.captured(rule.captureGroup).trimmed();
        if (!captured.isEmpty()) {
            return captured;
        }
    }
    return QString();
}

LogLevel detectLevelByRegex(QStringView line, const MetadataDetectionConfig& config) {
    if (config.levelRules.isEmpty()) {
        return LogLevel::Unknown;
    }

    const QString subject = line.left(qMin(line.size(), config.regexScanLimit)).toString();
    for (const CompiledMetadataRegexRule& rule : config.levelRules) {
        const QRegularExpressionMatch match = rule.regex.match(subject);
        if (!match.hasMatch()) {
            continue;
        }
        const LogLevel level = levelFromText(match.captured(rule.captureGroup));
        if (level != LogLevel::Unknown) {
            return level;
        }
    }
    return LogLevel::Unknown;
}

bool matchesIsoDate(QStringView line, int offset) {
    return offset + 10 <= line.size()
        && isDigit(line[offset])
        && isDigit(line[offset + 1])
        && isDigit(line[offset + 2])
        && isDigit(line[offset + 3])
        && line[offset + 4] == QLatin1Char('-')
        && isDigit(line[offset + 5])
        && isDigit(line[offset + 6])
        && line[offset + 7] == QLatin1Char('-')
        && isDigit(line[offset + 8])
        && isDigit(line[offset + 9]);
}

bool isTimestampChar(QChar ch) {
    return isDigit(ch)
        || isAlpha(ch)
        || ch == QLatin1Char('-')
        || ch == QLatin1Char(':')
        || ch == QLatin1Char('.')
        || ch == QLatin1Char(',')
        || ch == QLatin1Char('T')
        || ch == QLatin1Char('Z')
        || ch == QLatin1Char('+');
}

QString extractIsoTimestamp(QStringView line, int offset) {
    if (!matchesIsoDate(line, offset)) {
        return QString();
    }

    int end = offset + 10;
    const int limit = qMin(line.size(), offset + TimestampScanLimit);
    while (end < limit && isTimestampChar(line[end])) {
        ++end;
    }
    return line.mid(offset, end - offset).toString();
}

bool matchesSyslogMonth(QStringView line, int offset) {
    static constexpr const char* months[] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
    };

    if (offset + 3 > line.size()) {
        return false;
    }
    for (const char* month : months) {
        if (upperAscii(line[offset]) == QLatin1Char(month[0])
            && upperAscii(line[offset + 1]) == QLatin1Char(month[1])
            && upperAscii(line[offset + 2]) == QLatin1Char(month[2])) {
            return true;
        }
    }
    return false;
}

QString extractSyslogTimestamp(QStringView line, int offset) {
    if (!matchesSyslogMonth(line, offset)) {
        return QString();
    }

    int end = offset + 3;
    const int limit = qMin(line.size(), offset + 16);
    while (end < limit && (line[end].isSpace() || isDigit(line[end]) || line[end] == QLatin1Char(':'))) {
        ++end;
    }

    const QStringView candidate = line.mid(offset, end - offset);
    return candidate.size() >= 15 ? candidate.toString() : QString();
}

QString extractEpochTimestamp(QStringView line, int offset) {
    int end = offset;
    while (end < line.size() && isDigit(line[end])) {
        ++end;
    }

    const int digits = end - offset;
    if ((digits == 10 || digits == 13) && (end == line.size() || isTokenBoundary(line[end]))) {
        return line.mid(offset, digits).toString();
    }
    return QString();
}

QString detectTimestamp(QStringView line) {
    int offset = 0;
    const int maxPrefix = qMin(line.size(), 4);
    while (offset < maxPrefix && (line[offset].isSpace()
                                  || line[offset] == QLatin1Char('[')
                                  || line[offset] == QLatin1Char('(')
                                  || line[offset] == QLatin1Char('{'))) {
        ++offset;
    }

    if (const QString iso = extractIsoTimestamp(line, offset); !iso.isEmpty()) {
        return iso;
    }
    if (const QString syslog = extractSyslogTimestamp(line, offset); !syslog.isEmpty()) {
        return syslog;
    }
    if (const QString epoch = extractEpochTimestamp(line, offset); !epoch.isEmpty()) {
        return epoch;
    }

    return QString();
}

qint64 epochFromText(const QString& timestampText) {
    if (timestampText.isEmpty()) {
        return -1;
    }

    bool ok = false;
    const qint64 numeric = timestampText.toLongLong(&ok);
    if (ok) {
        if (timestampText.size() == 10) {
            return numeric * 1000;
        }
        if (timestampText.size() == 13) {
            return numeric;
        }
    }

    const QString normalized = QString(timestampText).replace(QLatin1Char(','), QLatin1Char('.'));
    const QDateTime dateTime = QDateTime::fromString(normalized, Qt::ISODateWithMs);
    if (dateTime.isValid()) {
        return dateTime.toMSecsSinceEpoch();
    }

    const QDateTime dateTimeNoMs = QDateTime::fromString(normalized, Qt::ISODate);
    if (dateTimeNoMs.isValid()) {
        return dateTimeNoMs.toMSecsSinceEpoch();
    }

    return -1;
}
}

ParsedLineMetadata parseLineMetadata(QStringView line) {
    static const MetadataDetectionConfig config = loadMetadataDetectionConfig();
    return parseLineMetadata(line, config);
}

ParsedLineMetadata parseLineMetadata(QStringView line, const MetadataDetectionConfig& config) {
    ParsedLineMetadata metadata = parseFormatMetadata(line, config);
    if (config.preferRegexRules) {
        if (metadata.timestampText.isEmpty()) {
            metadata.timestampText = detectTimestampByRegex(line, config);
        }
        if (metadata.level == LogLevel::Unknown) {
            metadata.level = detectLevelByRegex(line, config);
        }
    }

    if (metadata.timestampText.isEmpty()) {
        metadata.timestampText = detectTimestamp(line);
    }
    if (metadata.level == LogLevel::Unknown) {
        metadata.level = detectLevel(line);
    }
    if (!config.preferRegexRules) {
        if (metadata.timestampText.isEmpty()) {
            metadata.timestampText = detectTimestampByRegex(line, config);
        }
        if (metadata.level == LogLevel::Unknown) {
            metadata.level = detectLevelByRegex(line, config);
        }
    }

    metadata.timestampEpochMs = epochFromText(metadata.timestampText);
    return metadata;
}

QString logLevelToString(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
        return QStringLiteral("trace");
    case LogLevel::Debug:
        return QStringLiteral("debug");
    case LogLevel::Info:
        return QStringLiteral("info");
    case LogLevel::Warn:
        return QStringLiteral("warn");
    case LogLevel::Error:
        return QStringLiteral("error");
    case LogLevel::Fatal:
        return QStringLiteral("fatal");
    case LogLevel::Unknown:
    default:
        return QString();
    }
}
