#ifndef LOGDATABASE_H
#define LOGDATABASE_H

#include <QMutex>
#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <QVector>
#include "linerecord.h"

enum class SortMode {
    LineNumber,
    Timestamp,
};

enum class SortOrder {
    Ascending,
    Descending,
};

class LogDatabase : public QObject
{
    Q_OBJECT

public:
    static LogDatabase& instance();

    bool createTable(int fileId);
    bool dropTable(int fileId);
    bool insertBatch(int fileId, const QVector<LineRecord>& records);

    bool queryRows(int fileId, int offset, int limit,
                   const QString& ftsQuery,
                   qint64 fromTimestampMs,
                   qint64 toTimestampMs,
                   bool onlyWithTimestamp,
                   SortMode sortMode,
                   SortOrder sortOrder,
                   QVector<QVector<QString>>& outRows,
                   QStringList& outHeaders,
                   int& totalCount);

    int rowCount(int fileId);
    qint64 totalDbSizeBytes() const;
    QSet<int> activeFileIds() const;

private:
    LogDatabase();
    ~LogDatabase();
    LogDatabase(const LogDatabase&) = delete;
    LogDatabase& operator=(const LogDatabase&) = delete;

    QString metaTableName(int fileId) const { return QString("logs_meta_%1").arg(fileId); }
    QString ftsTableName(int fileId)  const { return QString("logs_fts_%1").arg(fileId); }
    QString buildOrderByClause(SortMode sortMode, SortOrder sortOrder) const;

    QSqlDatabase m_db;
    mutable QMutex m_mutex;
    QSet<int> m_activeFileIds;
};

#endif // LOGDATABASE_H
