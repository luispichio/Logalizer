#ifndef METADATAPIPELINE_H
#define METADATAPIPELINE_H

#include "linerecord.h"

#include <QAtomicInt>
#include <QMutex>
#include <QQueue>
#include <QSet>
#include <QThread>
#include <QThreadPool>
#include <QVector>

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
    void enqueueParsedBatch(int fileId, QVector<LineMetadataRecord>&& records);

private:
    MetadataPipeline();
    ~MetadataPipeline();
    MetadataPipeline(const MetadataPipeline&) = delete;
    MetadataPipeline& operator=(const MetadataPipeline&) = delete;

    void writerLoop();
    bool isCancelledLocked(int fileId) const;

    QThreadPool m_parserPool;
    QThread m_writerThread;
    QMutex m_mutex;
    QQueue<QPair<int, QVector<LineMetadataRecord>>> m_pendingWrites;
    QSet<int> m_cancelledFileIds;
    QAtomicInt m_pendingInputLines = 0;
    bool m_stopping = false;

    static constexpr int MaxPendingInputLines = 500000;

};

#endif // METADATAPIPELINE_H
