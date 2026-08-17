#ifndef METADATAPIPELINE_H
#define METADATAPIPELINE_H

#include "linerecord.h"
#include "metadataconfig.h"

#include <QAtomicInt>
#include <QHash>
#include <QMutex>
#include <QQueue>
#include <QSemaphore>
#include <QSharedPointer>
#include <QSet>
#include <QThread>
#include <QThreadPool>
#include <QVector>

class LogLineStore;

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
    void forgetFile(int fileId);
    void shutdown();
    void enqueueParsedBatch(int fileId, quint64 generation, int processedLines,
                            QVector<LineMetadataRecord>&& records);
    MetadataProgress progress(int fileId) const;
    void reloadConfig();
    void setDetectedFormat(int fileId, const LogFormatDetectionResult& result);
    LogFormatDetectionResult detectedFormat(int fileId) const;
    void setReferenceDate(int fileId, const QDate& date);
    void reprocessFile(int fileId);

private:
    MetadataPipeline();
    ~MetadataPipeline();
    MetadataPipeline(const MetadataPipeline&) = delete;
    MetadataPipeline& operator=(const MetadataPipeline&) = delete;

    struct FileState {
        quint64 generation = 0;
        QSharedPointer<QAtomicInt> cancelled;
    };

    struct PendingWrite {
        enum class Type { Batch, Clear };
        Type type = Type::Batch;
        int fileId = 0;
        quint64 generation = 0;
        QVector<LineMetadataRecord> records;
        QSharedPointer<QSemaphore> completion;
    };

    void scheduleReprocessRanges(int fileId, const QSharedPointer<LogLineStore>& store,
                                 const MetadataDetectionConfig& config,
                                 const FileState& state);
    FileState ensureFileStateLocked(int fileId);
    bool isCurrentLocked(int fileId, quint64 generation) const;
    void writerLoop();

    QThreadPool m_parserPool;
    QThread m_writerThread;
    mutable QMutex m_mutex;
    QQueue<PendingWrite> m_pendingWrites;
    QSet<int> m_cancelledFileIds;
    QHash<int, FileState> m_fileStates;
    QHash<int, MetadataProgress> m_progressByFile;
    QHash<int, LogFormatDetectionResult> m_formatByFile;
    QHash<int, QDate> m_referenceDateByFile;
    MetadataDetectionConfig m_detectionConfig;
    QSharedPointer<QSemaphore> m_taskSlots = QSharedPointer<QSemaphore>::create(256);
    bool m_stopping = false;
};

#endif // METADATAPIPELINE_H
