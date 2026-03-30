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
    QString column;  // original column name (as displayed in UI)
    enum Op { Contains, Equals, NotEquals, GreaterThan, LessThan } op;
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

    /// Create hybrid tables for the given file ID:
    ///   - logs_meta_{id}: regular SQLite table with B-tree indexes on Number/Date columns.
    ///   - logs_fts_{id}: FTS5 virtual table with raw + String/Date columns.
    bool createTable(int fileId, const QVector<ColumnDef>& columns);

    /// Drop both hybrid tables, freeing RAM immediately.
    bool dropTable(int fileId);

    /// Insert a batch of records into both hybrid tables within a single transaction.
    bool insertBatch(int fileId, const QVector<LineRecord>& records,
                     const QVector<ColumnDef>& columns);

    /// Get the column definitions for a file.
    QVector<ColumnDef> getColumns(int fileId) const;

    /// Query rows from a file's hybrid tables with optional FTS query and column filters.
    /// Results are paginated with offset/limit.
    bool queryRows(int fileId, int offset, int limit,
                   const QVector<Filter>& filters,
                   const QString& ftsQuery,
                   QVector<QVector<QString>>& outRows,
                   QStringList& outHeaders,
                   int& totalCount);

    /// Search across ALL active tables (UNION ALL on meta tables).
    bool searchAll(const QString& ftsQuery, const QVector<Filter>& filters,
                   int offset, int limit,
                   QVector<QVector<QString>>& outRows,
                   QStringList& outHeaders,
                   int& totalCount);

    /// Get total row count in the meta table for a file.
    int rowCount(int fileId);

    /// Get list of active file IDs.
    QSet<int> activeFileIds() const;

private:
    LogDatabase();
    ~LogDatabase();
    LogDatabase(const LogDatabase&) = delete;
    LogDatabase& operator=(const LogDatabase&) = delete;

    // Table name helpers
    QString metaTableName(int fileId) const { return QString("logs_meta_%1").arg(fileId); }
    QString ftsTableName(int fileId)  const { return QString("logs_fts_%1").arg(fileId);  }

    // Resolve original column name -> sanitizedName for SQL use
    QString sanitizedName(int fileId, const QString& originalName) const;

    QSqlDatabase m_db;
    mutable QMutex m_mutex;
    QMap<int, QVector<ColumnDef>> m_tableColumns;  // fileId -> columns
};

#endif // LOGDATABASE_H
