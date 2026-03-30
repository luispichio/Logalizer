#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>
#include "schemadetector.h"

SchemaDetector::SchemaDetector(int scanLines, double threshold)
    : m_scanLines(scanLines), m_threshold(threshold) {}

bool SchemaDetector::feedLine(const QString& line) {
    if (m_linesFed >= m_scanLines)
        return false;

    m_linesFed++;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return m_linesFed < m_scanLines;

    m_validJsonLines++;
    QJsonObject obj = doc.object();

    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QString& key = it.key();
        const QJsonValue& val = it.value();

        // Skip objects and arrays — they go into 'raw' only
        if (val.isObject() || val.isArray())
            continue;

        ColumnDef::Type type = classifyValue(val, val.isString() ? val.toString() : QString());
        m_fieldStats[key][type]++;
    }

    return m_linesFed < m_scanLines;
}

bool SchemaDetector::isComplete() const {
    return m_linesFed >= m_scanLines;
}

QVector<ColumnDef> SchemaDetector::detect() const {
    QVector<ColumnDef> columns;

    if (m_validJsonLines == 0)
        return columns;

    int minCount = static_cast<int>(m_validJsonLines * m_threshold);

    for (auto it = m_fieldStats.begin(); it != m_fieldStats.end(); ++it) {
        const QString& fieldName = it.key();
        const QMap<ColumnDef::Type, int>& typeCounts = it.value();

        // Total occurrences of this field
        int totalCount = 0;
        for (int c : typeCounts)
            totalCount += c;

        if (totalCount < minCount)
            continue;

        // Determine dominant type (most frequent)
        ColumnDef::Type dominantType = ColumnDef::String;
        int maxTypeCount = 0;
        for (auto tit = typeCounts.begin(); tit != typeCounts.end(); ++tit) {
            if (tit.value() > maxTypeCount) {
                maxTypeCount = tit.value();
                dominantType = tit.key();
            }
        }

        columns.append(ColumnDef(fieldName, dominantType));
    }

    // Sort alphabetically for deterministic column order
    std::sort(columns.begin(), columns.end(),
              [](const ColumnDef& a, const ColumnDef& b) { return a.name < b.name; });

    // Deduplicate sanitizedNames (e.g. "@foo" and "_foo" both -> "_foo")
    QSet<QString> usedNames;
    for (auto& col : columns) {
        QString base = col.sanitizedName;
        int suffix = 2;
        while (usedNames.contains(col.sanitizedName))
            col.sanitizedName = base + "_" + QString::number(suffix++);
        usedNames.insert(col.sanitizedName);
    }

    return columns;
}

ColumnDef::Type SchemaDetector::classifyValue(const QJsonValue& val, const QString& strVal) {
    if (val.isBool())
        return ColumnDef::Bool;
    if (val.isDouble())
        return ColumnDef::Number;
    if (val.isString()) {
        if (looksLikeDate(strVal))
            return ColumnDef::Date;
        return ColumnDef::String;
    }
    return ColumnDef::String;
}

bool SchemaDetector::looksLikeDate(const QString& s) {
    // Match ISO-8601 patterns: 2024-01-15, 2024-01-15T10:30:00, 2024-01-15T10:30:00Z, etc.
    static QRegularExpression iso8601(
        R"(^\d{4}-\d{2}-\d{2}(T\d{2}:\d{2}(:\d{2})?(\.\d+)?(Z|[+-]\d{2}:?\d{2})?)?$)");
    return iso8601.match(s).hasMatch();
}
