#include "logdatabase.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QtLogging>

LogDatabase& LogDatabase::instance() {
    static LogDatabase inst;
    return inst;
}

LogDatabase::LogDatabase() {
    m_db = QSqlDatabase::addDatabase("QSQLITE", "logalizer_memdb");
    m_db.setDatabaseName(":memory:");
    if (!m_db.open()) {
        qCritical() << "LogDatabase: Failed to open in-memory database:"
                    << m_db.lastError().text();
    } else {
        qInfo() << "LogDatabase: In-memory SQLite opened.";
    }
}

LogDatabase::~LogDatabase() {
    if (m_db.isOpen()) {
        m_db.close();
    }
}

bool LogDatabase::createTable(int fileId) {
    QMutexLocker locker(&m_mutex);

    QString metaT = metaTableName(fileId);
    QString ftsT = ftsTableName(fileId);
    QSqlQuery q(m_db);

    QString createMeta = QString(
        "CREATE TABLE IF NOT EXISTS %1 ("
        "line_number INTEGER PRIMARY KEY, "
        "file_position INTEGER NOT NULL, "
        "raw TEXT NOT NULL, "
        "timestamp_text TEXT, "
        "timestamp_unix_ms INTEGER, "
        "timestamp_source TEXT)"
    ).arg(metaT);

    if (!q.exec(createMeta)) {
        qCritical() << "LogDatabase::createTable: meta failed:" << q.lastError().text()
                    << "\nSQL:" << createMeta;
        return false;
    }

    const QStringList indexSqls = {
        QString("CREATE INDEX IF NOT EXISTS idx_meta_%1_fp ON %2(file_position)").arg(fileId).arg(metaT),
        QString("CREATE INDEX IF NOT EXISTS idx_meta_%1_ts ON %2(timestamp_unix_ms)").arg(fileId).arg(metaT),
    };
    for (const QString& idxSql : indexSqls) {
        if (!q.exec(idxSql)) {
            qWarning() << "LogDatabase::createTable: index warning:" << q.lastError().text();
        }
    }

    QString createFts = QString("CREATE VIRTUAL TABLE IF NOT EXISTS %1 USING fts5(raw)").arg(ftsT);
    if (!q.exec(createFts)) {
        qCritical() << "LogDatabase::createTable: FTS5 failed:" << q.lastError().text()
                    << "\nSQL:" << createFts;
        return false;
    }

    m_activeFileIds.insert(fileId);
    qInfo() << "LogDatabase: Created fixed tables for fileId" << fileId;
    return true;
}

bool LogDatabase::dropTable(int fileId) {
    QMutexLocker locker(&m_mutex);

    QSqlQuery q(m_db);
    bool ok = true;

    if (!q.exec(QString("DROP TABLE IF EXISTS %1").arg(metaTableName(fileId)))) {
        qCritical() << "LogDatabase::dropTable: meta drop failed:" << q.lastError().text();
        ok = false;
    }
    if (!q.exec(QString("DROP TABLE IF EXISTS %1").arg(ftsTableName(fileId)))) {
        qCritical() << "LogDatabase::dropTable: fts drop failed:" << q.lastError().text();
        ok = false;
    }

    q.exec("PRAGMA shrink_memory");

    m_activeFileIds.remove(fileId);
    qInfo() << "LogDatabase: Dropped tables for fileId" << fileId;
    return ok;
}

bool LogDatabase::insertBatch(int fileId, const QVector<LineRecord>& records) {
    QMutexLocker locker(&m_mutex);

    if (records.isEmpty()) {
        return true;
    }

    QString metaT = metaTableName(fileId);
    QString ftsT = ftsTableName(fileId);

    QString metaSql = QString(
        "INSERT OR IGNORE INTO %1 "
        "(line_number, file_position, raw, timestamp_text, timestamp_unix_ms, timestamp_source) "
        "VALUES (?, ?, ?, ?, ?, ?)"
    ).arg(metaT);

    QString ftsSql = QString("INSERT INTO %1 (rowid, raw) VALUES (?, ?)").arg(ftsT);

    if (!m_db.transaction()) {
        qCritical() << "LogDatabase::insertBatch: transaction start failed:"
                    << m_db.lastError().text();
        return false;
    }

    QSqlQuery metaQ(m_db);
    QSqlQuery ftsQ(m_db);
    metaQ.prepare(metaSql);
    ftsQ.prepare(ftsSql);

    for (const LineRecord& rec : records) {
        metaQ.addBindValue(rec.lineNumber);
        metaQ.addBindValue(rec.filePosition);
        metaQ.addBindValue(rec.raw);
        metaQ.addBindValue(rec.timestampText);
        metaQ.addBindValue(rec.timestampUnixMs >= 0 ? QVariant(rec.timestampUnixMs) : QVariant());
        metaQ.addBindValue(rec.timestampSource);
        if (!metaQ.exec()) {
            qWarning() << "LogDatabase: meta insert failed:" << metaQ.lastError().text();
        }

        ftsQ.addBindValue(rec.lineNumber);
        ftsQ.addBindValue(rec.raw);
        if (!ftsQ.exec()) {
            qWarning() << "LogDatabase: fts insert failed:" << ftsQ.lastError().text();
        }
    }

    if (!m_db.commit()) {
        qCritical() << "LogDatabase::insertBatch: commit failed:" << m_db.lastError().text();
        m_db.rollback();
        return false;
    }

    return true;
}

int LogDatabase::rowCount(int fileId) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    if (q.exec(QString("SELECT COUNT(*) FROM %1").arg(metaTableName(fileId))) && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

qint64 LogDatabase::totalDbSizeBytes() const {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    qint64 pageCount = 0;
    qint64 pageSize = 4096;
    if (q.exec("PRAGMA page_count") && q.next()) {
        pageCount = q.value(0).toLongLong();
    }
    if (q.exec("PRAGMA page_size") && q.next()) {
        pageSize = q.value(0).toLongLong();
    }
    return pageCount * pageSize;
}

QSet<int> LogDatabase::activeFileIds() const {
    QMutexLocker locker(&m_mutex);
    return m_activeFileIds;
}

QString LogDatabase::buildOrderByClause(SortMode sortMode, SortOrder sortOrder) const {
    if (sortMode == SortMode::Timestamp) {
        if (sortOrder == SortOrder::Descending) {
            return "ORDER BY m.timestamp_unix_ms IS NULL, m.timestamp_unix_ms DESC, m.line_number DESC";
        }
        return "ORDER BY m.timestamp_unix_ms IS NULL, m.timestamp_unix_ms ASC, m.line_number ASC";
    }

    if (sortOrder == SortOrder::Descending) {
        return "ORDER BY m.line_number DESC";
    }
    return "ORDER BY m.line_number ASC";
}

bool LogDatabase::queryRows(int fileId, int offset, int limit,
                            const QString& ftsQuery,
                            qint64 fromTimestampMs,
                            qint64 toTimestampMs,
                            bool onlyWithTimestamp,
                            SortMode sortMode,
                            SortOrder sortOrder,
                            QVector<QVector<QString>>& outRows,
                            QStringList& outHeaders,
                            int& totalCount) {
    QMutexLocker locker(&m_mutex);

    outRows.clear();
    outHeaders = {"line_number", "raw", "timestamp_text", "timestamp_source"};

    QString metaT = metaTableName(fileId);
    QString ftsT = ftsTableName(fileId);
    const bool needsFts = !ftsQuery.trimmed().isEmpty();

    QString fromClause = needsFts
        ? QString("FROM %1 m INNER JOIN %2 f ON f.rowid = m.line_number").arg(metaT, ftsT)
        : QString("FROM %1 m").arg(metaT);

    QStringList conditions;
    QList<QVariant> bindValues;

    if (needsFts) {
        conditions << QString("%1 MATCH ?").arg(ftsT);
        bindValues << ftsQuery;
    }
    if (onlyWithTimestamp || fromTimestampMs >= 0 || toTimestampMs >= 0) {
        conditions << "m.timestamp_unix_ms IS NOT NULL";
    }
    if (fromTimestampMs >= 0) {
        conditions << "m.timestamp_unix_ms >= ?";
        bindValues << fromTimestampMs;
    }
    if (toTimestampMs >= 0) {
        conditions << "m.timestamp_unix_ms <= ?";
        bindValues << toTimestampMs;
    }

    QString whereClause = conditions.isEmpty() ? QString() : "WHERE " + conditions.join(" AND ");

    {
        QString countSql = QString("SELECT COUNT(*) %1 %2").arg(fromClause, whereClause);
        QSqlQuery cq(m_db);
        cq.prepare(countSql);
        for (const QVariant& value : bindValues) {
            cq.addBindValue(value);
        }
        if (cq.exec() && cq.next()) {
            totalCount = cq.value(0).toInt();
        } else {
            totalCount = 0;
            qWarning() << "LogDatabase::queryRows: count failed:" << cq.lastError().text()
                       << "\nSQL:" << countSql;
        }
    }

    QString sql = QString(
        "SELECT m.line_number, m.raw, m.timestamp_text, m.timestamp_source %1 %2 %3 LIMIT ? OFFSET ?"
    ).arg(fromClause, whereClause, buildOrderByClause(sortMode, sortOrder));

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const QVariant& value : bindValues) {
        q.addBindValue(value);
    }
    q.addBindValue(limit);
    q.addBindValue(offset);

    if (!q.exec()) {
        qWarning() << "LogDatabase::queryRows: failed:" << q.lastError().text()
                   << "\nSQL:" << sql;
        return false;
    }

    while (q.next()) {
        QVector<QString> row;
        row.reserve(outHeaders.size());
        for (int i = 0; i < outHeaders.size(); ++i) {
            row << q.value(i).toString();
        }
        outRows << row;
    }

    return true;
}
