#ifndef LOGDATABASE_H
#define LOGDATABASE_H

#include <QMutex>
#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <QVector>
#include "linerecord.h"
#include "schemadetector.h"

struct Filter {
    QString column;
    enum Op { Contains, Equals, NotEquals, GreaterThan, LessThan, MatchFts } op;
    QString value;
    enum Logic { And, Or, Not } logic = And;

    Filter() : op(Contains), logic(And) {}
    Filter(const QString& column, Op op, const QString& value, Logic logic = And)
        : column(column), op(op), value(value), logic(logic) {}
};

class LogDatabase : public QObject
{
    Q_OBJECT

public:
    static LogDatabase& instance();

    /// Create an FTS5 table for the given file ID with dynamic columns.
    bool createTable(int fileId, const QVector<ColumnDef>& columns);

    /// Drop the table for the given file ID (frees RAM).
    bool dropTable(int fileId);

    /// Insert a batch of records into the file's table within a single transaction.
    bool insertBatch(int fileId, const QVector<LineRecord>& records,
                     const QVector<ColumnDef>& columns);

    /// Get the column definitions for a file.
    QVector<ColumnDef> getColumns(int fileId) const;

    /// Query rows from a file's table with optional filters, pagination.
    /// Returns the query result. Caller must iterate.
    bool queryRows(int fileId, int offset, int limit,
                   const QVector<Filter>& filters,
                   const QString& ftsQuery,
                   QVector<QVector<QString>>& outRows,
                   QStringList& outHeaders,
                   int& totalCount);

    /// Search across ALL active tables (UNION ALL).
    bool searchAll(const QString& ftsQuery, const QVector<Filter>& filters,
                   int offset, int limit,
                   QVector<QVector<QString>>& outRows,
                   QStringList& outHeaders,
                   int& totalCount);

    /// Get table row count.
    int rowCount(int fileId);

    /// Get list of active file IDs.
    QSet<int> activeFileIds() const;

private:
    LogDatabase();
    ~LogDatabase();
    LogDatabase(const LogDatabase&) = delete;
    LogDatabase& operator=(const LogDatabase&) = delete;

    QString tableName(int fileId) const;
    QString buildWhereClause(int fileId, const QVector<Filter>& filters,
                             const QString& ftsQuery,
                             QStringList& bindValues) const;

    QSqlDatabase m_db;
    mutable QMutex m_mutex;
    QMap<int, QVector<ColumnDef>> m_tableColumns;  // fileId -> columns
};

#endif // LOGDATABASE_H
