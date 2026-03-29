#include "logdatabase.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QtLogging>

LogDatabase& LogDatabase::instance() {
    static LogDatabase inst;
    return inst;
}

LogDatabase::LogDatabase() {
    m_db = QSqlDatabase::addDatabase("QSQLITE", "logalizer_memdb");
    m_db.setDatabaseName(":memory:");
    if (!m_db.open()) {
        qCritical() << "LogDatabase: Failed to open in-memory database:" << m_db.lastError().text();
    } else {
        qInfo() << "LogDatabase: In-memory SQLite database opened.";
    }
}

LogDatabase::~LogDatabase() {
    if (m_db.isOpen())
        m_db.close();
}

QString LogDatabase::tableName(int fileId) const {
    return QString("logs_%1").arg(fileId);
}

bool LogDatabase::createTable(int fileId, const QVector<ColumnDef>& columns) {
    QMutexLocker locker(&m_mutex);

    QString tbl = tableName(fileId);

    // Build column list for FTS5: raw, file_position (UNINDEXED), line_number (UNINDEXED), + dynamic cols
    QStringList colDefs;
    colDefs << "raw";
    colDefs << "file_position UNINDEXED";
    colDefs << "line_number UNINDEXED";

    for (const auto& col : columns) {
        // FTS5 columns are all text, but we track types in m_tableColumns
        colDefs << col.name;
    }

    QString sql = QString("CREATE VIRTUAL TABLE IF NOT EXISTS %1 USING fts5(%2)")
                      .arg(tbl, colDefs.join(", "));

    QSqlQuery query(m_db);
    if (!query.exec(sql)) {
        qCritical() << "LogDatabase::createTable: Failed to create" << tbl << ":" << query.lastError().text();
        qCritical() << "SQL:" << sql;
        return false;
    }

    m_tableColumns[fileId] = columns;
    qInfo() << "LogDatabase: Created table" << tbl << "with" << (columns.size() + 3) << "columns";
    return true;
}

bool LogDatabase::dropTable(int fileId) {
    QMutexLocker locker(&m_mutex);

    QString sql = QString("DROP TABLE IF EXISTS %1").arg(tableName(fileId));
    QSqlQuery query(m_db);
    if (!query.exec(sql)) {
        qCritical() << "LogDatabase::dropTable: Failed:" << query.lastError().text();
        return false;
    }

    m_tableColumns.remove(fileId);
    qInfo() << "LogDatabase: Dropped table" << tableName(fileId);
    return true;
}

bool LogDatabase::insertBatch(int fileId, const QVector<LineRecord>& records,
                               const QVector<ColumnDef>& columns) {
    QMutexLocker locker(&m_mutex);

    if (records.isEmpty()) return true;

    QString tbl = tableName(fileId);

    // Build: INSERT INTO logs_N(raw, file_position, line_number, col1, col2, ...) VALUES(?, ?, ?, ?, ?, ...)
    QStringList colNames;
    colNames << "raw" << "file_position" << "line_number";
    for (const auto& col : columns)
        colNames << col.name;

    QStringList placeholders;
    for (int i = 0; i < colNames.size(); i++)
        placeholders << "?";

    QString sql = QString("INSERT INTO %1(%2) VALUES(%3)")
                      .arg(tbl, colNames.join(", "), placeholders.join(", "));

    if (!m_db.transaction()) {
        qCritical() << "LogDatabase::insertBatch: Failed to begin transaction:" << m_db.lastError().text();
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(sql);

    for (const auto& rec : records) {
        query.addBindValue(rec.raw);
        query.addBindValue(rec.filePosition);
        query.addBindValue(rec.lineNumber);

        for (const auto& col : columns) {
            query.addBindValue(rec.fields.value(col.name, ""));
        }

        if (!query.exec()) {
            qWarning() << "LogDatabase::insertBatch: Insert failed:" << query.lastError().text();
        }
    }

    if (!m_db.commit()) {
        qCritical() << "LogDatabase::insertBatch: Commit failed:" << m_db.lastError().text();
        m_db.rollback();
        return false;
    }

    return true;
}

QVector<ColumnDef> LogDatabase::getColumns(int fileId) const {
    QMutexLocker locker(&m_mutex);
    return m_tableColumns.value(fileId);
}

int LogDatabase::rowCount(int fileId) {
    QMutexLocker locker(&m_mutex);

    // FTS5 tables: use a simple count query
    QString sql = QString("SELECT COUNT(*) FROM %1").arg(tableName(fileId));
    QSqlQuery query(m_db);
    if (query.exec(sql) && query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

QSet<int> LogDatabase::activeFileIds() const {
    QMutexLocker locker(&m_mutex);
    QSet<int> ids;
    for (auto it = m_tableColumns.keyBegin(); it != m_tableColumns.keyEnd(); ++it)
        ids.insert(*it);
    return ids;
}

QString LogDatabase::buildWhereClause(int fileId, const QVector<Filter>& filters,
                                       const QString& ftsQuery,
                                       QStringList& bindValues) const {
    Q_UNUSED(fileId)

    QStringList conditions;

    // FTS5 full-text match on the whole table
    if (!ftsQuery.isEmpty()) {
        conditions << QString("%1 MATCH ?").arg(tableName(fileId));
        bindValues << ftsQuery;
    }

    for (const auto& f : filters) {
        QString cond;
        switch (f.op) {
        case Filter::Contains:
            cond = QString("%1 LIKE ?").arg(f.column);
            bindValues << QString("%%%1%%").arg(f.value);
            break;
        case Filter::Equals:
            cond = QString("%1 = ?").arg(f.column);
            bindValues << f.value;
            break;
        case Filter::NotEquals:
            cond = QString("%1 != ?").arg(f.column);
            bindValues << f.value;
            break;
        case Filter::GreaterThan:
            cond = QString("CAST(%1 AS REAL) > ?").arg(f.column);
            bindValues << f.value;
            break;
        case Filter::LessThan:
            cond = QString("CAST(%1 AS REAL) < ?").arg(f.column);
            bindValues << f.value;
            break;
        case Filter::MatchFts:
            cond = QString("%1 MATCH ?").arg(f.column);
            bindValues << f.value;
            break;
        }

        if (f.logic == Filter::Not) {
            cond = "NOT (" + cond + ")";
        }

        conditions << cond;
    }

    if (conditions.isEmpty())
        return QString();

    // Join with AND/OR based on filter logic (simplified: AND between all for now)
    return "WHERE " + conditions.join(" AND ");
}

bool LogDatabase::queryRows(int fileId, int offset, int limit,
                             const QVector<Filter>& filters,
                             const QString& ftsQuery,
                             QVector<QVector<QString>>& outRows,
                             QStringList& outHeaders,
                             int& totalCount) {
    QMutexLocker locker(&m_mutex);

    QString tbl = tableName(fileId);
    outRows.clear();

    // Build headers
    outHeaders.clear();
    outHeaders << "line_number" << "raw";
    auto cols = m_tableColumns.value(fileId);
    for (const auto& col : cols)
        outHeaders << col.name;

    // Build WHERE
    QStringList bindValues;
    QString whereClause = buildWhereClause(fileId, filters, ftsQuery, bindValues);

    // Count query
    {
        QString countSql = QString("SELECT COUNT(*) FROM %1 %2").arg(tbl, whereClause);
        QSqlQuery cq(m_db);
        cq.prepare(countSql);
        for (const auto& v : bindValues)
            cq.addBindValue(v);
        if (cq.exec() && cq.next())
            totalCount = cq.value(0).toInt();
        else
            totalCount = 0;
    }

    // Select columns
    QStringList selectCols;
    selectCols << "line_number" << "raw";
    for (const auto& col : cols)
        selectCols << col.name;

    QString sql = QString("SELECT %1 FROM %2 %3 ORDER BY CAST(line_number AS INTEGER) LIMIT %4 OFFSET %5")
                      .arg(selectCols.join(", "), tbl, whereClause)
                      .arg(limit).arg(offset);

    QSqlQuery query(m_db);
    query.prepare(sql);
    for (const auto& v : bindValues)
        query.addBindValue(v);

    if (!query.exec()) {
        qWarning() << "LogDatabase::queryRows: Query failed:" << query.lastError().text();
        qWarning() << "SQL:" << sql;
        return false;
    }

    while (query.next()) {
        QVector<QString> row;
        for (int i = 0; i < selectCols.size(); i++)
            row << query.value(i).toString();
        outRows << row;
    }

    return true;
}

bool LogDatabase::searchAll(const QString& ftsQuery, const QVector<Filter>& filters,
                             int offset, int limit,
                             QVector<QVector<QString>>& outRows,
                             QStringList& outHeaders,
                             int& totalCount) {
    QMutexLocker locker(&m_mutex);

    outRows.clear();
    outHeaders.clear();
    outHeaders << "file_id" << "line_number" << "raw";

    if (m_tableColumns.isEmpty()) {
        totalCount = 0;
        return true;
    }

    // Build UNION ALL across all active tables
    QStringList unionParts;
    QStringList allBindValues;

    for (auto it = m_tableColumns.begin(); it != m_tableColumns.end(); ++it) {
        int fid = it.key();
        QString tbl = tableName(fid);

        QStringList bindValues;
        QString whereClause = buildWhereClause(fid, filters, ftsQuery, bindValues);

        QString part = QString("SELECT %1 AS file_id, line_number, raw FROM %2 %3")
                           .arg(fid).arg(tbl).arg(whereClause);
        unionParts << part;
        allBindValues << bindValues;
    }

    QString unionSql = unionParts.join(" UNION ALL ");

    // Count
    {
        QString countSql = QString("SELECT COUNT(*) FROM (%1)").arg(unionSql);
        QSqlQuery cq(m_db);
        cq.prepare(countSql);
        for (const auto& v : allBindValues)
            cq.addBindValue(v);
        if (cq.exec() && cq.next())
            totalCount = cq.value(0).toInt();
        else
            totalCount = 0;
    }

    // Query
    QString sql = QString("SELECT * FROM (%1) ORDER BY line_number LIMIT %2 OFFSET %3")
                      .arg(unionSql).arg(limit).arg(offset);

    QSqlQuery query(m_db);
    query.prepare(sql);
    for (const auto& v : allBindValues)
        query.addBindValue(v);

    if (!query.exec()) {
        qWarning() << "LogDatabase::searchAll: Query failed:" << query.lastError().text();
        return false;
    }

    while (query.next()) {
        QVector<QString> row;
        row << query.value(0).toString(); // file_id
        row << query.value(1).toString(); // line_number
        row << query.value(2).toString(); // raw
        outRows << row;
    }

    return true;
}
