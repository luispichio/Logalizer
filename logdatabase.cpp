#include "logdatabase.h"

#include <QVariant>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QtLogging>

namespace {
QString escapedLikePattern(const QString& word) {
    QString pattern;
    pattern.reserve(word.size() + 2);
    pattern += '%';
    for (const QChar ch : word) {
        if (ch == '\\' || ch == '%' || ch == '_') {
            pattern += '\\';
        }
        pattern += ch;
    }
    pattern += '%';
    return pattern;
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
    QSqlQuery q(m_db);

    QString createFts = QString("CREATE VIRTUAL TABLE IF NOT EXISTS %1 USING fts5(file_position UNINDEXED, raw)").arg(table);
    if (!q.exec(createFts)) {
        qCritical() << "LogDatabase::createTable: FTS5 failed:" << q.lastError().text()
                    << "\nSQL:" << createFts;
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

    QString sql = QString("INSERT OR IGNORE INTO %1 (rowid, file_position, raw) VALUES (?, ?, ?)").arg(tableName(fileId));

    if (!m_db.transaction()) {
        qCritical() << "LogDatabase::insertBatch: transaction start failed:"
                    << m_db.lastError().text();
        return false;
    }

    QSqlQuery insertQ(m_db);
    insertQ.prepare(sql);

    for (const LineRecord& rec : records) {
        insertQ.bindValue(0, rec.lineNumber + 1);
        insertQ.bindValue(1, rec.filePosition);
        insertQ.bindValue(2, rec.raw);
        if (!insertQ.exec()) {
            qWarning() << "LogDatabase: fts insert failed:" << insertQ.lastError().text();
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
    if (q.exec(QString("SELECT COUNT(*) FROM %1").arg(tableName(fileId))) && q.next()) {
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

bool LogDatabase::queryRows(int fileId, int firstLineNumber, int limit,
                            const QString& ftsFilter,
                            QVector<QVector<QString>>& outRows,
                            QStringList& outHeaders) {
    QMutexLocker locker(&m_mutex);

    outRows.clear();
    outHeaders = {"line_number", "file_position", "raw"};

    QString table = tableName(fileId);
    const QString filter = ftsFilter.trimmed();
    const bool hasFilter = !filter.isEmpty();

    QString sql = hasFilter
        ? QString(
              "SELECT rowid - 1 AS line_number, file_position, raw FROM %1 "
              "WHERE %1 MATCH ? AND rowid >= ? ORDER BY rowid ASC LIMIT ?"
          ).arg(table)
        : QString(
              "SELECT rowid - 1 AS line_number, file_position, raw FROM %1 "
              "WHERE rowid >= ? ORDER BY rowid ASC LIMIT ?"
          ).arg(table);

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
    QStringList predicates;
    predicates.reserve(cleanWords.size());
    for (int i = 0; i < cleanWords.size(); ++i) {
        predicates << "raw LIKE ? ESCAPE '\\'";
    }

    const QString directionPredicate = backwards ? "rowid <= ?" : "rowid >= ?";
    const QString order = backwards ? "DESC" : "ASC";
    QString sql = QString("SELECT rowid - 1 FROM %1 WHERE ").arg(table);
    if (hasFilter) {
        sql += QString("%1 MATCH ? AND ").arg(table);
    }
    sql += directionPredicate;
    sql += " AND (" + predicates.join(" OR ") + ")";
    sql += QString(" ORDER BY rowid %1 LIMIT 1").arg(order);

    QSqlQuery q(m_db);
    q.prepare(sql);
    if (hasFilter) {
        q.addBindValue(filter);
    }
    q.addBindValue(qMax(1, fromLineNumber + 1));
    for (const QString& word : cleanWords) {
        q.addBindValue(escapedLikePattern(word));
    }

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
