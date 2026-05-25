#ifndef FILEWORKER_H
#define FILEWORKER_H

#include <QObject>
#include <QStringList>
#include <QVector>
#include <atomic>
#include "linerecord.h"
#include "logformat.h"

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
    void progressUpdate(int fileId, qint64 bytesProcessed, qint64 totalBytes, qint32 linesProcessed);
    void formatDetected(int fileId, LogFormatDetectionResult result);
    void chunkInserted(int fileId, qint32 totalLinesInserted);
    void finished(int fileId);
    void error(int fileId, QString message);

private:
    void doWork();

    QString m_fileName;
    int m_fileId;
    int m_batchSize;
    int m_sampleLineLimit;
    std::atomic<bool> m_stopRequested{false};
};

#endif // FILEWORKER_H
