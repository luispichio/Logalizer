#include "loglineparser.h"

#include <QDateTime>
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
    ParsedLineMetadata metadata;
    metadata.timestampText = detectTimestamp(line);
    metadata.timestampEpochMs = epochFromText(metadata.timestampText);
    metadata.level = detectLevel(line);
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
