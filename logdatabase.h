#ifndef LOGDATABASE_H
#define LOGDATABASE_H

#include <QMutex>
#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <QVector>
#include "linerecord.h"

class LogDatabase : public QObject
{
    Q_OBJECT

public:
    static LogDatabase& instance();

    bool createTable(int fileId);
    bool dropTable(int fileId);
    bool insertBatch(int fileId, const QVector<LineRecord>& records);

    bool queryRows(int fileId, int firstLineNumber, int limit,
                   const QString& ftsFilter,
                   QVector<QVector<QString>>& outRows,
                   QStringList& outHeaders);

    int findMatchLine(int fileId,
                      const QString& ftsFilter,
                      const QString& ftsQuery,
                      int fromLineNumber,
                      bool backwards);
    int rowCount(int fileId);
    qint64 totalDbSizeBytes() const;
    QSet<int> activeFileIds() const;

private:
    LogDatabase();
    ~LogDatabase();
    LogDatabase(const LogDatabase&) = delete;
    LogDatabase& operator=(const LogDatabase&) = delete;

    QString tableName(int fileId) const { return QString("logs_%1").arg(fileId); }

    QSqlDatabase m_db;
    mutable QMutex m_mutex;
    QSet<int> m_activeFileIds;
};

#endif // LOGDATABASE_H
