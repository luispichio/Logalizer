#ifndef STREAMWORKER_H
#define STREAMWORKER_H

#include <QObject>
#include <QStringList>
#include <QVector>
#include <atomic>
#include "linerecord.h"
#include "logformat.h"

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
    void formatDetected(int fileId, LogFormatDetectionResult result);
    void chunkInserted(int fileId, qint32 totalLinesInserted);
    void finished(int fileId);
    void error(int fileId, QString message);

private:
    void doWork();

    int m_fileId;
    int m_batchSize;
    int m_sampleLineLimit;
    bool m_formatDetectionEmitted = false;
    QStringList m_sampleLines;
    std::atomic<bool> m_stopRequested{false};
};

#endif // STREAMWORKER_H
