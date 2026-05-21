#include "logdatabase.h"

#include <QVariant>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QtCore/QtLogging>

namespace {
int logLevelValue(LogLevel level) {
    return static_cast<int>(level);
}
}

LogDatabase& LogDatabase::instance() {
    static LogDatabase inst;
    return inst;
}

LogDatabase::LogDatabase() {
    openDatabase();
}

bool LogDatabase::openDatabase() {
    m_db = QSqlDatabase::addDatabase("QSQLITE", "logalizer_memdb");
    m_db.setDatabaseName(":memory:");
    if (!m_db.open()) {
        qCritical() << "LogDatabase: Failed to open in-memory database:"
                    << m_db.lastError().text();
        return false;
    } else {
        qInfo() << "LogDatabase: In-memory SQLite opened.";
    }
    return true;
}

LogDatabase::~LogDatabase() {
    if (m_db.isOpen()) {
        m_db.close();
    }
}

void LogDatabase::resetDatabase() {
    if (m_db.isOpen()) {
        m_db.close();
    }
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase("logalizer_memdb");
    openDatabase();
}

bool LogDatabase::createTable(int fileId) {
    QMutexLocker locker(&m_mutex);

    QString table = tableName(fileId);
    QString indexTable = indexTableName(fileId);
    QSqlQuery q(m_db);

    QString createFts = QString("CREATE VIRTUAL TABLE IF NOT EXISTS %1 USING fts5(file_position UNINDEXED, raw, content='', columnsize=0)").arg(table);
    if (!q.exec(createFts)) {
        qCritical() << "LogDatabase::createTable: FTS5 failed:" << q.lastError().text()
                    << "\nSQL:" << createFts;
        return false;
    }

    const QString createIndex = QString(
        "CREATE TABLE IF NOT EXISTS %1 ("
        "rowid INTEGER PRIMARY KEY, "
        "file_position INTEGER NOT NULL)"
    ).arg(indexTable);
    if (!q.exec(createIndex)) {
        qCritical() << "LogDatabase::createTable: index table failed:" << q.lastError().text()
                    << "\nSQL:" << createIndex;
        return false;
    }

    const QString createMeta = QString(
        "CREATE TABLE IF NOT EXISTS %1 ("
        "rowid INTEGER PRIMARY KEY, "
        "timestamp_text TEXT, "
        "timestamp_epoch_ms INTEGER, "
        "level INTEGER NOT NULL DEFAULT 0)"
    ).arg(metadataTableName(fileId));
    if (!q.exec(createMeta)) {
        qCritical() << "LogDatabase::createTable: metadata table failed:" << q.lastError().text()
                    << "\nSQL:" << createMeta;
        return false;
    }

    m_activeFileIds.insert(fileId);
    qInfo() << "LogDatabase: Created FTS table for fileId" << fileId;
    return true;
}

bool LogDatabase::dropTable(int fileId) {
    QMutexLocker locker(&m_mutex);

    bool ok = true;

    {
        QSqlQuery q(m_db);
        if (!q.exec(QString("DROP TABLE IF EXISTS %1").arg(tableName(fileId)))) {
            qCritical() << "LogDatabase::dropTable: fts drop failed:" << q.lastError().text();
            ok = false;
        }
    }

    {
        QSqlQuery q(m_db);
        if (!q.exec(QString("DROP TABLE IF EXISTS %1").arg(indexTableName(fileId)))) {
            qCritical() << "LogDatabase::dropTable: index drop failed:" << q.lastError().text();
            ok = false;
        }
    }

    {
        QSqlQuery q(m_db);
        if (!q.exec(QString("DROP TABLE IF EXISTS %1").arg(metadataTableName(fileId)))) {
            qCritical() << "LogDatabase::dropTable: metadata drop failed:" << q.lastError().text();
            ok = false;
        }
    }

    m_activeFileIds.remove(fileId);
    if (m_activeFileIds.isEmpty()) {
        resetDatabase();
    } else {
        QSqlQuery q(m_db);
        if (!q.exec("PRAGMA shrink_memory")) {
            qWarning() << "LogDatabase::dropTable: shrink_memory failed:" << q.lastError().text();
        }
    }
    qInfo() << "LogDatabase: Dropped table for fileId" << fileId;
    return ok;
}

bool LogDatabase::insertBatch(int fileId, const QVector<LineRecord>& records) {
    QMutexLocker locker(&m_mutex);

    if (records.isEmpty()) {
        return true;
    }

    const QString ftsSql = QString("INSERT OR IGNORE INTO %1 (rowid, file_position, raw) VALUES (?, ?, ?)").arg(tableName(fileId));
    const QString indexSql = QString("INSERT OR IGNORE INTO %1 (rowid, file_position) VALUES (?, ?)").arg(indexTableName(fileId));

    if (!m_db.transaction()) {
        qCritical() << "LogDatabase::insertBatch: transaction start failed:"
                    << m_db.lastError().text();
        return false;
    }

    QSqlQuery insertQ(m_db);
    insertQ.prepare(ftsSql);
    QSqlQuery indexQ(m_db);
    indexQ.prepare(indexSql);

    for (const LineRecord& rec : records) {
        const int rowid = rec.lineNumber + 1;
        insertQ.bindValue(0, rowid);
        insertQ.bindValue(1, rec.filePosition);
        insertQ.bindValue(2, rec.raw);
        if (!insertQ.exec()) {
            qWarning() << "LogDatabase: fts insert failed:" << insertQ.lastError().text();
        }

        indexQ.bindValue(0, rowid);
        indexQ.bindValue(1, rec.filePosition);
        if (!indexQ.exec()) {
            qWarning() << "LogDatabase: index insert failed:" << indexQ.lastError().text();
        }
    }

    if (!m_db.commit()) {
        qCritical() << "LogDatabase::insertBatch: commit failed:" << m_db.lastError().text();
        m_db.rollback();
        return false;
    }

    return true;
}

bool LogDatabase::insertMetadataBatch(int fileId, const QVector<LineMetadataRecord>& records) {
    QMutexLocker locker(&m_mutex);

    if (records.isEmpty() || !m_activeFileIds.contains(fileId)) {
        return true;
    }

    QString sql = QString("INSERT OR IGNORE INTO %1 (rowid, timestamp_text, timestamp_epoch_ms, level) VALUES (?, ?, ?, ?)")
        .arg(metadataTableName(fileId));

    if (!m_db.transaction()) {
        qCritical() << "LogDatabase::insertMetadataBatch: transaction start failed:"
                    << m_db.lastError().text();
        return false;
    }

    QSqlQuery insertQ(m_db);
    insertQ.prepare(sql);

    for (const LineMetadataRecord& rec : records) {
        insertQ.bindValue(0, rec.lineNumber + 1);
        insertQ.bindValue(1, rec.timestampText);
        insertQ.bindValue(2, rec.timestampEpochMs >= 0 ? QVariant(rec.timestampEpochMs) : QVariant());
        insertQ.bindValue(3, logLevelValue(rec.level));
        if (!insertQ.exec()) {
            qWarning() << "LogDatabase: metadata insert failed:" << insertQ.lastError().text();
        }
    }

    if (!m_db.commit()) {
        qCritical() << "LogDatabase::insertMetadataBatch: commit failed:" << m_db.lastError().text();
        m_db.rollback();
        return false;
    }

    return true;
}

int LogDatabase::rowCount(int fileId) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    if (q.exec(QString("SELECT COUNT(*) FROM %1").arg(indexTableName(fileId))) && q.next()) {
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

qint64 LogDatabase::totalDbUsedBytes() const {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    qint64 pageCount = 0;
    qint64 freeCount = 0;
    qint64 pageSize = 4096;
    if (q.exec("PRAGMA page_count") && q.next()) {
        pageCount = q.value(0).toLongLong();
    }
    if (q.exec("PRAGMA freelist_count") && q.next()) {
        freeCount = q.value(0).toLongLong();
    }
    if (q.exec("PRAGMA page_size") && q.next()) {
        pageSize = q.value(0).toLongLong();
    }
    return qMax<qint64>(0, pageCount - freeCount) * pageSize;
}

QSet<int> LogDatabase::activeFileIds() const {
    QMutexLocker locker(&m_mutex);
    return m_activeFileIds;
}

bool LogDatabase::isFileActive(int fileId) const {
    QMutexLocker locker(&m_mutex);
    return m_activeFileIds.contains(fileId);
}

bool LogDatabase::queryRows(int fileId, int firstLineNumber, int limit,
                            const QString& ftsFilter,
                            bool includeMetadata,
                            bool sortByTimestamp,
                            QVector<QVector<QString>>& outRows,
                            QStringList& outHeaders) {
    QMutexLocker locker(&m_mutex);

    outRows.clear();
    outHeaders = includeMetadata || sortByTimestamp
        ? QStringList{"line_number", "file_position", "timestamp_text", "timestamp_epoch_ms", "level"}
        : QStringList{"line_number", "file_position"};

    QString table = tableName(fileId);
    QString indexTable = indexTableName(fileId);
    QString metaTable = metadataTableName(fileId);
    const QString filter = ftsFilter.trimmed();
    const bool hasFilter = !filter.isEmpty();
    const int start = qMax(0, firstLineNumber);

    if (sortByTimestamp) {
        QString sql = hasFilter
            ? QString(
                  "SELECT i.rowid - 1 AS line_number, i.file_position, m.timestamp_text, "
                  "m.timestamp_epoch_ms, m.level FROM %1 m "
                  "JOIN %2 i ON i.rowid = m.rowid "
                  "JOIN %3 f ON f.rowid = m.rowid "
                  "WHERE m.timestamp_epoch_ms IS NOT NULL AND %3 MATCH ? "
                  "ORDER BY m.timestamp_epoch_ms ASC, m.rowid ASC LIMIT ? OFFSET ?"
              ).arg(metaTable, indexTable, table)
            : QString(
                  "SELECT i.rowid - 1 AS line_number, i.file_position, m.timestamp_text, "
                  "m.timestamp_epoch_ms, m.level FROM %1 m "
                  "JOIN %2 i ON i.rowid = m.rowid "
                  "WHERE m.timestamp_epoch_ms IS NOT NULL "
                  "ORDER BY m.timestamp_epoch_ms ASC, m.rowid ASC LIMIT ? OFFSET ?"
              ).arg(metaTable, indexTable);

        QSqlQuery q(m_db);
        q.prepare(sql);
        if (hasFilter) {
            q.addBindValue(filter);
        }
        q.addBindValue(limit);
        q.addBindValue(start);

        if (!q.exec()) {
            qWarning() << "LogDatabase::queryRows: timestamp sort failed:" << q.lastError().text()
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

    QString sql = hasFilter
        ? (includeMetadata
            ? QString(
                  "SELECT i.rowid - 1 AS line_number, i.file_position, m.timestamp_text, "
                  "m.timestamp_epoch_ms, m.level FROM %1 f "
                  "JOIN %2 i ON i.rowid = f.rowid "
                  "LEFT JOIN %3 m ON m.rowid = i.rowid "
                  "WHERE %1 MATCH ? AND i.rowid >= ? ORDER BY i.rowid ASC LIMIT ?"
              ).arg(table, indexTable, metaTable)
            : QString(
                  "SELECT i.rowid - 1 AS line_number, i.file_position FROM %1 f "
                  "JOIN %2 i ON i.rowid = f.rowid "
                  "WHERE %1 MATCH ? AND i.rowid >= ? ORDER BY i.rowid ASC LIMIT ?"
              ).arg(table, indexTable))
        : (includeMetadata
            ? QString(
                  "SELECT i.rowid - 1 AS line_number, i.file_position, m.timestamp_text, "
                  "m.timestamp_epoch_ms, m.level FROM %1 i "
                  "LEFT JOIN %2 m ON m.rowid = i.rowid "
                  "WHERE i.rowid >= ? ORDER BY i.rowid ASC LIMIT ?"
              ).arg(indexTable, metaTable)
            : QString(
                  "SELECT rowid - 1 AS line_number, file_position FROM %1 "
                  "WHERE rowid >= ? ORDER BY rowid ASC LIMIT ?"
              ).arg(indexTable));

    QSqlQuery q(m_db);
    q.prepare(sql);
    if (hasFilter) {
        q.addBindValue(filter);
    }
    q.addBindValue(qMax(1, firstLineNumber + 1));
    q.addBindValue(limit);

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

int LogDatabase::timestampRowCount(int fileId, const QString& ftsFilter) {
    QMutexLocker locker(&m_mutex);

    const QString table = tableName(fileId);
    const QString metaTable = metadataTableName(fileId);
    const QString filter = ftsFilter.trimmed();
    const bool hasFilter = !filter.isEmpty();
    const QString sql = hasFilter
        ? QString(
              "SELECT COUNT(*) FROM %1 m JOIN %2 f ON f.rowid = m.rowid "
              "WHERE m.timestamp_epoch_ms IS NOT NULL AND %2 MATCH ?"
          ).arg(metaTable, table)
        : QString("SELECT COUNT(*) FROM %1 WHERE timestamp_epoch_ms IS NOT NULL").arg(metaTable);

    QSqlQuery q(m_db);
    q.prepare(sql);
    if (hasFilter) {
        q.addBindValue(filter);
    }

    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }

    qWarning() << "LogDatabase::timestampRowCount: failed:" << q.lastError().text()
               << "\nSQL:" << sql;
    return 0;
}

int LogDatabase::findMatchLine(int fileId,
                               const QString& ftsFilter,
                               const QString& ftsQuery,
                               int fromLineNumber,
                               bool backwards) {
    QMutexLocker locker(&m_mutex);

    const QString filter = ftsFilter.trimmed();
    const QString queryText = ftsQuery.trimmed();
    if (queryText.isEmpty()) {
        return -1;
    }

    const QString matchQuery = filter.isEmpty()
        ? queryText
        : QString("(%1) AND (%2)").arg(filter, queryText);

    const QString table = tableName(fileId);
    const QString sql = backwards
        ? QString("SELECT rowid - 1 FROM %1 WHERE %1 MATCH ? AND rowid <= ? ORDER BY rowid DESC LIMIT 1").arg(table)
        : QString("SELECT rowid - 1 FROM %1 WHERE %1 MATCH ? AND rowid >= ? ORDER BY rowid ASC LIMIT 1").arg(table);

    QSqlQuery q(m_db);
    q.prepare(sql);
    q.addBindValue(matchQuery);
    q.addBindValue(qMax(1, fromLineNumber + 1));

    if (!q.exec()) {
        qWarning() << "LogDatabase::findMatchLine: failed:" << q.lastError().text()
                   << "\nSQL:" << sql;
        return -1;
    }

    if (!q.next()) {
        return -1;
    }

    return q.value(0).toInt();
}

int LogDatabase::findFilteredLine(int fileId,
                                  const QString& ftsFilter,
                                  int fromLineNumber,
                                  bool backwards) {
    QMutexLocker locker(&m_mutex);

    const QString filter = ftsFilter.trimmed();
    if (filter.isEmpty()) {
        return qMax(0, fromLineNumber);
    }

    const QString table = tableName(fileId);
    const QString sql = backwards
        ? QString("SELECT rowid - 1 FROM %1 WHERE %1 MATCH ? AND rowid <= ? ORDER BY rowid DESC LIMIT 1").arg(table)
        : QString("SELECT rowid - 1 FROM %1 WHERE %1 MATCH ? AND rowid >= ? ORDER BY rowid ASC LIMIT 1").arg(table);

    QSqlQuery q(m_db);
    q.prepare(sql);
    q.addBindValue(filter);
    q.addBindValue(qMax(1, fromLineNumber + 1));

    if (!q.exec()) {
        qWarning() << "LogDatabase::findFilteredLine: failed:" << q.lastError().text()
                   << "\nSQL:" << sql;
        return -1;
    }

    if (!q.next()) {
        return -1;
    }

    return q.value(0).toInt();
}

int LogDatabase::findTextLine(int fileId,
                              const QString& ftsFilter,
                              const QStringList& words,
                              int fromLineNumber,
                              bool backwards) {
    QMutexLocker locker(&m_mutex);

    QStringList cleanWords;
    for (const QString& word : words) {
        const QString trimmed = word.trimmed();
        if (!trimmed.isEmpty()) {
            cleanWords << trimmed;
        }
    }
    if (cleanWords.isEmpty()) {
        return -1;
    }

    const QString filter = ftsFilter.trimmed();
    const bool hasFilter = !filter.isEmpty();
    const QString table = tableName(fileId);
    QStringList matchWords;
    matchWords.reserve(cleanWords.size());
    for (const QString& word : cleanWords) {
        QString escaped = word;
        escaped.replace('"', "\"\"");
        matchWords << QString("\"%1\"").arg(escaped);
    }

    const QString directionPredicate = backwards ? "rowid <= ?" : "rowid >= ?";
    const QString order = backwards ? "DESC" : "ASC";
    QString sql = QString("SELECT rowid - 1 FROM %1 WHERE %1 MATCH ? AND ").arg(table);
    sql += directionPredicate;
    sql += QString(" ORDER BY rowid %1 LIMIT 1").arg(order);

    QSqlQuery q(m_db);
    q.prepare(sql);
    const QString matchQuery = hasFilter
        ? QString("(%1) AND (%2)").arg(filter, matchWords.join(" OR "))
        : matchWords.join(" OR ");
    q.addBindValue(matchQuery);
    q.addBindValue(qMax(1, fromLineNumber + 1));

    if (!q.exec()) {
        qWarning() << "LogDatabase::findTextLine: failed:" << q.lastError().text()
                   << "\nSQL:" << sql;
        return -1;
    }

    if (!q.next()) {
        return -1;
    }

    return q.value(0).toInt();
}
