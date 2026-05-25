#ifndef PROCESSWORKER_H
#define PROCESSWORKER_H

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVector>
#include <atomic>
#include "linerecord.h"
#include "logformat.h"

class ProcessWorker : public QObject {
    Q_OBJECT

public:
    ProcessWorker(const QString& command, int fileId, QObject* parent = nullptr);
    ~ProcessWorker();

public slots:
    void start();
    void stop();

signals:
    void progressUpdate(int fileId, qint64 bytesProcessed, qint64 totalBytes, qint32 linesProcessed);
    void formatDetected(int fileId, LogFormatDetectionResult result);
    void chunkInserted(int fileId, qint32 totalLinesInserted);
    void finished(int fileId);
    void error(int fileId, QString message);

private slots:
    void readProcessOutput();
    void onProcessFinished(int exitCode, int exitStatus);
    void onProcessError(QProcess::ProcessError processError);

private:
    void flushCompleteLines();
    void flushBatch();
    void flushRemainder();

    QString m_command;
    int m_fileId;
    QProcess* m_process = nullptr;
    QByteArray m_pending;
    QVector<LineRecord> m_batch;
    int m_batchSize;
    int m_sampleLineLimit;
    bool m_formatDetectionEmitted = false;
    QStringList m_sampleLines;
    qint32 m_lineNumber = 0;
    qint64 m_logicalPosition = 0;
    bool m_finished = false;
    std::atomic<bool> m_stopRequested{false};

};

#endif // PROCESSWORKER_H
