#ifndef STREAMWORKER_H
#define STREAMWORKER_H

#include <QObject>
#include <QVector>
#include <atomic>
#include "linerecord.h"

class StreamWorker : public QObject {
    Q_OBJECT

public:
    explicit StreamWorker(int fileId, QObject* parent = nullptr);
    ~StreamWorker();

public slots:
    void start();
    void stop();

signals:
    void progressUpdate(int fileId, qint64 bytesProcessed, qint64 totalBytes, qint32 linesProcessed);
    void chunkInserted(int fileId, qint32 totalLinesInserted);
    void finished(int fileId);
    void error(int fileId, QString message);

private:
    void doWork();

    int m_fileId;
    std::atomic<bool> m_stopRequested{false};

    static constexpr int CHUNK_SIZE = 10000;
};

#endif // STREAMWORKER_H
