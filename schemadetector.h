#ifndef SCHEMADETECTOR_H
#define SCHEMADETECTOR_H

#include <QString>
#include <QVector>
#include <QJsonValue>

struct ColumnDef {
    QString name;
    enum Type { String, Number, Bool, Date } type;

    ColumnDef() : type(String) {}
    ColumnDef(const QString& name, Type type) : name(name), type(type) {}

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
    explicit SchemaDetector(int scanLines = 10000, double threshold = 0.80);

    /// Feed a single raw JSON line to the detector. Returns true if still scanning.
    bool feedLine(const QString& line);

    /// Returns true when scanLines have been consumed.
    bool isComplete() const;

    /// Compute and return the detected columns (call after scanning).
    QVector<ColumnDef> detect() const;

    int linesFed() const { return m_linesFed; }

private:
    int m_scanLines;
    double m_threshold;
    int m_linesFed = 0;
    int m_validJsonLines = 0;

    // field name -> { type -> count }
    QMap<QString, QMap<ColumnDef::Type, int>> m_fieldStats;

    static ColumnDef::Type classifyValue(const QJsonValue& val, const QString& strVal);
    static bool looksLikeDate(const QString& s);
};

#endif // SCHEMADETECTOR_H
