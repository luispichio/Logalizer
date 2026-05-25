#ifndef METADATAPIPELINE_H
#define METADATAPIPELINE_H

#include "linerecord.h"
#include "metadataconfig.h"

#include <QAtomicInt>
#include <QMutex>
#include <QQueue>
#include <QSet>
#include <QThread>
#include <QThreadPool>
#include <QVector>

struct MetadataProgress {
    qint64 queuedLines = 0;
    qint64 processedLines = 0;
    qint64 taggedLines = 0;
    qint64 droppedLines = 0;
};

struct MetadataInputLine {
    QString raw;
    qint32 lineNumber;
};

class MetadataPipeline
{
public:
    static MetadataPipeline& instance();

    void enqueueBatch(int fileId, const QVector<LineRecord>& records);
    void cancelFile(int fileId);
    void shutdown();
    void finishInputBatch(int lineCount);
    void enqueueParsedBatch(int fileId, int processedLines, QVector<LineMetadataRecord>&& records);
    MetadataProgress progress(int fileId) const;
    void reloadConfig();
    void setDetectedFormat(int fileId, const LogFormatDetectionResult& result);
    LogFormatDetectionResult detectedFormat(int fileId) const;
    void setReferenceDate(int fileId, const QDate& date);

private:
    MetadataPipeline();
    ~MetadataPipeline();
    MetadataPipeline(const MetadataPipeline&) = delete;
    MetadataPipeline& operator=(const MetadataPipeline&) = delete;

    void writerLoop();
    bool isCancelledLocked(int fileId) const;

    QThreadPool m_parserPool;
    QThread m_writerThread;
    mutable QMutex m_mutex;
    QQueue<QPair<int, QVector<LineMetadataRecord>>> m_pendingWrites;
    QSet<int> m_cancelledFileIds;
    QHash<int, MetadataProgress> m_progressByFile;
    QHash<int, LogFormatDetectionResult> m_formatByFile;
    QHash<int, QDate> m_referenceDateByFile;
    MetadataDetectionConfig m_detectionConfig;
    QAtomicInt m_pendingInputLines = 0;
    bool m_stopping = false;

    static constexpr int MaxPendingInputLines = 500000;

};

#endif // METADATAPIPELINE_H
