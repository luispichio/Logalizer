#ifndef SCHEMADETECTOR_H
#define SCHEMADETECTOR_H

#include <QMap>
#include <QString>
#include <QVector>
#include <QJsonValue>

struct ColumnDef {
    QString name;           // original JSON field name (used for display / SQL lookups)
    QString sanitizedName;  // SQLite-safe identifier (used in all SQL statements)
    enum Type { String, Number, Bool, Date } type;

    ColumnDef() : type(String) {}
    ColumnDef(const QString& name, Type type)
        : name(name), sanitizedName(sanitize(name)), type(type) {}

    /// Replace any character that is not [a-z0-9_] with '_'.
    /// Prefix with 'col_' if the result starts with a digit.
    /// Collapses consecutive underscores for readability.
    static QString sanitize(const QString& rawName) {
        if (rawName.isEmpty()) return "col";
        QString r;
        r.reserve(rawName.size());
        for (const QChar& ch : rawName) {
            if (ch.isLetter() || ch.isDigit() || ch == '_')
                r += ch.toLower();
            else
                r += '_';
        }
        // Collapse consecutive underscores
        while (r.contains("__"))
            r.replace("__", "_");
        // Strip leading/trailing underscores from collapse (keep single leading _ if original had @, -, etc.)
        if (!r.isEmpty() && r[0].isDigit())
            r.prepend("col_");
        if (r.isEmpty() || r == "_")
            r = "col";
        return r;
    }

    static QString typeToString(Type t) {
        switch (t) {
        case String: return "String";
        case Number: return "Number";
        case Bool:   return "Bool";
        case Date:   return "Date";
        }
        return "String";
    }
};

class SchemaDetector
{
public:
    explicit SchemaDetector(int scanLines = 10000, double threshold = 0.10);

    /// Feed a single raw JSON line to the detector. Returns true if still scanning.
    bool feedLine(const QString& line);

    /// Returns true when scanLines have been consumed.
    bool isComplete() const;

    /// Compute and return the detected columns (call after scanning).
    /// sanitizedName deduplication is applied here.
    QVector<ColumnDef> detect() const;

    int linesFed() const { return m_linesFed; }

private:
    int    m_scanLines;
    double m_threshold;
    int    m_linesFed      = 0;
    int    m_validJsonLines = 0;

    // original field name -> { type -> occurrence count }
    QMap<QString, QMap<ColumnDef::Type, int>> m_fieldStats;

    static ColumnDef::Type classifyValue(const QJsonValue& val, const QString& strVal);
    static bool looksLikeDate(const QString& s);
};

#endif // SCHEMADETECTOR_H
