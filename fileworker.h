#ifndef FILEWORKER_H
#define FILEWORKER_H

#include <QObject>
#include <QVector>
#include "linerecord.h"
#include "schemadetector.h"

class LogDatabase;

class FileWorker : public QObject {
    Q_OBJECT

public:
    FileWorker(const QString& fileName, int fileId, QObject* parent = nullptr);
    ~FileWorker();

    int fileId() const { return m_fileId; }

public slots:
    void start();
    void stop();

signals:
    void schemaReady(int fileId, QVector<ColumnDef> columns);
    void progressUpdate(int fileId, qint64 bytesProcessed, qint64 totalBytes, qint32 linesProcessed);
    void chunkInserted(int fileId, qint32 totalLinesInserted);
    void finished(int fileId);
    void error(int fileId, QString message);

private:
    void doWork();
    LineRecord parseLine(const QString& line, qint64 offset, qint32 lineNum,
                         const QVector<ColumnDef>& columns);

    QString m_fileName;
    int m_fileId;
    bool m_stopRequested = false;

    static constexpr int SCAN_LINES = 10000;
    static constexpr int CHUNK_SIZE = 5000;
};

#endif // FILEWORKER_H
