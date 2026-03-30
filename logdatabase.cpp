#include "logdatabase.h"
#include <QSqlQuery>
#include <QSqlError>
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
    if (m_db.isOpen()) m_db.close();
}

// ─── Private helpers ─────────────────────────────────────────────────────────

QString LogDatabase::sanitizedName(int fileId, const QString& originalName) const {
    // Built-in columns use their names directly
    if (originalName == "line_number" || originalName == "file_position"
        || originalName == "raw")
        return originalName;

    const auto& cols = m_tableColumns.value(fileId);
    for (const auto& col : cols) {
        if (col.name == originalName)
            return col.sanitizedName;
    }
    // Fallback: sanitize on the fly
    return ColumnDef::sanitize(originalName);
}

// ─── createTable ─────────────────────────────────────────────────────────────

bool LogDatabase::createTable(int fileId, const QVector<ColumnDef>& columns) {
    QMutexLocker locker(&m_mutex);

    QString metaT = metaTableName(fileId);
    QString ftsT  = ftsTableName(fileId);
    QSqlQuery q(m_db);

    // ── Meta table (regular SQLite, B-tree indexed) ───────────────────
    // line_number INTEGER PRIMARY KEY → it's the rowid alias → zero overhead
    QStringList metaCols;
    metaCols << "line_number INTEGER PRIMARY KEY"
             << "file_position INTEGER NOT NULL"
             << "raw TEXT";

    QStringList indexSqls;
    // Always index file_position for future file-offset navigation
    indexSqls << QString("CREATE INDEX IF NOT EXISTS idx_meta_%1_fp ON %2(file_position)")
                     .arg(fileId).arg(metaT);

    for (const auto& col : columns) {
        switch (col.type) {
        case ColumnDef::Number:
            metaCols << QString("%1 REAL").arg(col.sanitizedName);
            indexSqls << QString("CREATE INDEX IF NOT EXISTS idx_meta_%1_%2 ON %3(%2)")
                             .arg(fileId).arg(col.sanitizedName).arg(metaT);
            break;
        case ColumnDef::Bool:
            metaCols << QString("%1 INTEGER").arg(col.sanitizedName);
            break;
        case ColumnDef::Date:
            metaCols << QString("%1 TEXT").arg(col.sanitizedName);
            indexSqls << QString("CREATE INDEX IF NOT EXISTS idx_meta_%1_%2 ON %3(%2)")
                             .arg(fileId).arg(col.sanitizedName).arg(metaT);
            break;
        default: // String
            metaCols << QString("%1 TEXT").arg(col.sanitizedName);
            break;
        }
    }

    QString createMeta = QString("CREATE TABLE IF NOT EXISTS %1 (%2)")
                             .arg(metaT, metaCols.join(", "));

    if (!q.exec(createMeta)) {
        qCritical() << "LogDatabase::createTable: meta failed:" << q.lastError().text()
                    << "\nSQL:" << createMeta;
        return false;
    }
    for (const auto& idxSql : indexSqls) {
        if (!q.exec(idxSql))
            qWarning() << "LogDatabase::createTable: index warning:" << q.lastError().text();
    }

    // ── FTS5 table (raw + String/Date columns for full-text search) ───
    // Only text-searchable types go here. rowid = line_number via explicit insert.
    QStringList ftsCols;
    ftsCols << "raw";
    for (const auto& col : columns) {
        if (col.type == ColumnDef::String || col.type == ColumnDef::Date)
            ftsCols << col.sanitizedName;
    }

    QString createFts = QString("CREATE VIRTUAL TABLE IF NOT EXISTS %1 USING fts5(%2)")
                            .arg(ftsT, ftsCols.join(", "));

    if (!q.exec(createFts)) {
        qCritical() << "LogDatabase::createTable: FTS5 failed:" << q.lastError().text()
                    << "\nSQL:" << createFts;
        return false;
    }

    m_tableColumns[fileId] = columns;

    // Log which columns go in FTS
    QStringList ftsNames;
    for (const auto& col : columns)
        if (col.type == ColumnDef::String || col.type == ColumnDef::Date)
            ftsNames << col.sanitizedName;
    qInfo() << "LogDatabase: Created hybrid tables for fileId" << fileId
            << "| meta cols:" << (3 + columns.size())
            << "| fts cols:" << ftsCols.size()
            << "| indexed:" << ftsNames;
    return true;
}

// ─── dropTable ───────────────────────────────────────────────────────────────

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

    m_tableColumns.remove(fileId);
    qInfo() << "LogDatabase: Dropped tables for fileId" << fileId;
    return ok;
}

// ─── insertBatch ─────────────────────────────────────────────────────────────

bool LogDatabase::insertBatch(int fileId, const QVector<LineRecord>& records,
                               const QVector<ColumnDef>& columns) {
    QMutexLocker locker(&m_mutex);

    if (records.isEmpty()) return true;

    QString metaT = metaTableName(fileId);
    QString ftsT  = ftsTableName(fileId);

    // ── Build meta INSERT ─────────────────────────────────────────────
    QStringList metaCols = {"line_number", "file_position", "raw"};
    for (const auto& col : columns)
        metaCols << col.sanitizedName;

    QStringList metaPH;
    for (int i = 0; i < metaCols.size(); i++) metaPH << "?";

    QString metaSql = QString("INSERT OR IGNORE INTO %1 (%2) VALUES (%3)")
                          .arg(metaT, metaCols.join(","), metaPH.join(","));

    // ── Build FTS INSERT (rowid + raw + string/date cols) ────────────
    // FTS columns order must match what was used in CREATE VIRTUAL TABLE
    QStringList ftsCols = {"rowid", "raw"};
    QVector<QString> ftsDataCols; // sanitizedNames of columns that go into FTS data
    for (const auto& col : columns) {
        if (col.type == ColumnDef::String || col.type == ColumnDef::Date) {
            ftsCols << col.sanitizedName;
            ftsDataCols << col.sanitizedName;
        }
    }
    QStringList ftsPH;
    for (int i = 0; i < ftsCols.size(); i++) ftsPH << "?";

    QString ftsSql = QString("INSERT INTO %1 (%2) VALUES (%3)")
                         .arg(ftsT, ftsCols.join(","), ftsPH.join(","));

    if (!m_db.transaction()) {
        qCritical() << "LogDatabase::insertBatch: transaction start failed:"
                    << m_db.lastError().text();
        return false;
    }

    QSqlQuery metaQ(m_db), ftsQ(m_db);
    metaQ.prepare(metaSql);
    ftsQ.prepare(ftsSql);

    for (const auto& rec : records) {
        // ── Meta ───────────────────────────────────────────────────────
        metaQ.addBindValue(rec.lineNumber);
        metaQ.addBindValue(rec.filePosition);
        metaQ.addBindValue(rec.raw);

        for (const auto& col : columns) {
            QString val = rec.fields.value(col.sanitizedName, "");
            if (col.type == ColumnDef::Number) {
                metaQ.addBindValue(val.isEmpty() ? QVariant() : QVariant(val.toDouble()));
            } else if (col.type == ColumnDef::Bool) {
                if (val == "true")       metaQ.addBindValue(1);
                else if (val == "false") metaQ.addBindValue(0);
                else                     metaQ.addBindValue(QVariant());
            } else {
                metaQ.addBindValue(val);
            }
        }

        if (!metaQ.exec())
            qWarning() << "LogDatabase: meta insert failed:" << metaQ.lastError().text();

        // ── FTS (rowid = line_number for JOIN by rowid) ────────────────
        ftsQ.addBindValue(rec.lineNumber); // explicit rowid
        ftsQ.addBindValue(rec.raw);
        for (const auto& sanName : ftsDataCols)
            ftsQ.addBindValue(rec.fields.value(sanName, ""));

        if (!ftsQ.exec())
            qWarning() << "LogDatabase: fts insert failed:" << ftsQ.lastError().text();
    }

    if (!m_db.commit()) {
        qCritical() << "LogDatabase::insertBatch: commit failed:" << m_db.lastError().text();
        m_db.rollback();
        return false;
    }

    return true;
}

// ─── getColumns / rowCount / activeFileIds ────────────────────────────────────

QVector<ColumnDef> LogDatabase::getColumns(int fileId) const {
    QMutexLocker locker(&m_mutex);
    return m_tableColumns.value(fileId);
}

int LogDatabase::rowCount(int fileId) {
    QMutexLocker locker(&m_mutex);
    QSqlQuery q(m_db);
    if (q.exec(QString("SELECT COUNT(*) FROM %1").arg(metaTableName(fileId))) && q.next())
        return q.value(0).toInt();
    return 0;
}

QSet<int> LogDatabase::activeFileIds() const {
    QMutexLocker locker(&m_mutex);
    QSet<int> ids;
    for (auto it = m_tableColumns.keyBegin(); it != m_tableColumns.keyEnd(); ++it)
        ids.insert(*it);
    return ids;
}

// ─── queryRows ───────────────────────────────────────────────────────────────

bool LogDatabase::queryRows(int fileId, int offset, int limit,
                             const QVector<Filter>& filters,
                             const QString& ftsQuery,
                             QVector<QVector<QString>>& outRows,
                             QStringList& outHeaders,
                             int& totalCount) {
    QMutexLocker locker(&m_mutex);

    outRows.clear();
    outHeaders.clear();

    QString metaT = metaTableName(fileId);
    QString ftsT  = ftsTableName(fileId);
    auto cols = m_tableColumns.value(fileId);

    // ── Build SELECT column list (all come from meta table alias 'm') ─
    QStringList selectParts;
    selectParts << "m.line_number" << "m.raw";
    for (const auto& col : cols)
        selectParts << QString("m.%1").arg(col.sanitizedName);

    // Display headers use original names
    outHeaders << "line_number" << "raw";
    for (const auto& col : cols)
        outHeaders << col.name;

    // ── Determine if FTS join is needed ───────────────────────────────
    bool needsFts = !ftsQuery.trimmed().isEmpty();

    QString fromClause = needsFts
        ? QString("FROM %1 m INNER JOIN %2 f ON f.rowid = m.line_number")
              .arg(metaT, ftsT)
        : QString("FROM %1 m").arg(metaT);

    // ── Build WHERE clause ────────────────────────────────────────────
    QStringList conditions;
    QStringList bindVals;

    if (needsFts) {
        // FTS5 table-level MATCH: "fts_table_name MATCH ?"
        conditions << QString("%1 MATCH ?").arg(ftsT);
        bindVals << ftsQuery;
    }

    for (const auto& f : filters) {
        // Resolve to sanitized column name for SQL
        QString sanCol = sanitizedName(fileId, f.column);
        QString colRef = QString("m.%1").arg(sanCol);

        QString cond;
        switch (f.op) {
        case Filter::Contains:
            cond = QString("%1 LIKE ?").arg(colRef);
            bindVals << "%" + f.value + "%";
            break;
        case Filter::Equals:
            cond = QString("%1 = ?").arg(colRef);
            bindVals << f.value;
            break;
        case Filter::NotEquals:
            cond = QString("%1 != ?").arg(colRef);
            bindVals << f.value;
            break;
        case Filter::GreaterThan:
            cond = QString("CAST(%1 AS REAL) > ?").arg(colRef);
            bindVals << f.value;
            break;
        case Filter::LessThan:
            cond = QString("CAST(%1 AS REAL) < ?").arg(colRef);
            bindVals << f.value;
            break;
        }

        if (f.logic == Filter::Not)
            cond = "NOT (" + cond + ")";
        conditions << cond;
    }

    QString whereClause = conditions.isEmpty()
        ? QString()
        : "WHERE " + conditions.join(" AND ");

    // ── Count total matching rows ─────────────────────────────────────
    {
        QString countSql = QString("SELECT COUNT(*) %1 %2")
                               .arg(fromClause, whereClause);
        QSqlQuery cq(m_db);
        cq.prepare(countSql);
        for (const auto& v : bindVals) cq.addBindValue(v);
        if (cq.exec() && cq.next())
            totalCount = cq.value(0).toInt();
        else
            totalCount = 0;
    }

    // ── Fetch rows ────────────────────────────────────────────────────
    QString sql = QString("SELECT %1 %2 %3 ORDER BY m.line_number LIMIT ? OFFSET ?")
                      .arg(selectParts.join(", "), fromClause, whereClause);

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const auto& v : bindVals) q.addBindValue(v);
    q.addBindValue(limit);
    q.addBindValue(offset);

    if (!q.exec()) {
        qWarning() << "LogDatabase::queryRows: failed:" << q.lastError().text()
                   << "\nSQL:" << sql;
        return false;
    }

    while (q.next()) {
        QVector<QString> row;
        row.reserve(selectParts.size());
        for (int i = 0; i < selectParts.size(); i++)
            row << q.value(i).toString();
        outRows << row;
    }

    return true;
}

// ─── searchAll (UNION ALL across meta tables) ─────────────────────────────────

bool LogDatabase::searchAll(const QString& ftsQuery, const QVector<Filter>& filters,
                             int offset, int limit,
                             QVector<QVector<QString>>& outRows,
                             QStringList& outHeaders,
                             int& totalCount) {
    QMutexLocker locker(&m_mutex);

    outRows.clear();
    outHeaders.clear();
    outHeaders << "file_id" << "line_number" << "raw";

    if (m_tableColumns.isEmpty()) { totalCount = 0; return true; }

    QStringList unionParts;
    QStringList allBindVals;

    for (auto it = m_tableColumns.begin(); it != m_tableColumns.end(); ++it) {
        int fid      = it.key();
        QString metaT = metaTableName(fid);
        QString ftsT  = ftsTableName(fid);
        bool needsFts = !ftsQuery.trimmed().isEmpty();

        QString from = needsFts
            ? QString("FROM %1 m INNER JOIN %2 f ON f.rowid = m.line_number")
                  .arg(metaT, ftsT)
            : QString("FROM %1 m").arg(metaT);

        QStringList conds;
        QStringList bv;

        if (needsFts) {
            conds << QString("%1 MATCH ?").arg(ftsT);
            bv << ftsQuery;
        }

        for (const auto& f : filters) {
            QString sanCol = sanitizedName(fid, f.column);
            QString colRef = "m." + sanCol;
            QString cond;
            switch (f.op) {
            case Filter::Contains:    cond = colRef + " LIKE ?"; bv << "%" + f.value + "%"; break;
            case Filter::Equals:      cond = colRef + " = ?";    bv << f.value; break;
            case Filter::NotEquals:   cond = colRef + " != ?";   bv << f.value; break;
            case Filter::GreaterThan: cond = "CAST(" + colRef + " AS REAL) > ?"; bv << f.value; break;
            case Filter::LessThan:    cond = "CAST(" + colRef + " AS REAL) < ?"; bv << f.value; break;
            }
            if (f.logic == Filter::Not) cond = "NOT (" + cond + ")";
            conds << cond;
        }

        QString where = conds.isEmpty() ? "" : "WHERE " + conds.join(" AND ");

        unionParts << QString("SELECT %1 AS file_id, m.line_number, m.raw %2 %3")
                          .arg(fid).arg(from).arg(where);
        allBindVals << bv;
    }

    QString unionSql = unionParts.join(" UNION ALL ");

    // Count
    {
        QString countSql = QString("SELECT COUNT(*) FROM (%1)").arg(unionSql);
        QSqlQuery cq(m_db);
        cq.prepare(countSql);
        for (const auto& v : allBindVals) cq.addBindValue(v);
        if (cq.exec() && cq.next())
            totalCount = cq.value(0).toInt();
        else
            totalCount = 0;
    }

    QString sql = QString("SELECT * FROM (%1) ORDER BY line_number LIMIT ? OFFSET ?")
                      .arg(unionSql);
    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const auto& v : allBindVals) q.addBindValue(v);
    q.addBindValue(limit);
    q.addBindValue(offset);

    if (!q.exec()) {
        qWarning() << "LogDatabase::searchAll: failed:" << q.lastError().text();
        return false;
    }

    while (q.next()) {
        QVector<QString> row;
        row << q.value(0).toString() << q.value(1).toString() << q.value(2).toString();
        outRows << row;
    }

    return true;
}
