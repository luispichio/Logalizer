#include "metadatapipeline.h"

#include "logdatabase.h"
#include "loglineparser.h"
#include "loglinestore.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QRunnable>
#include <QtCore/QtLogging>

namespace {
class TaskSlotGuard
{
public:
    explicit TaskSlotGuard(QSharedPointer<QSemaphore> semaphore)
        : m_slots(std::move(semaphore)) {}
    ~TaskSlotGuard() { m_slots->release(); }

private:
    QSharedPointer<QSemaphore> m_slots;
};

class ParseMetadataTask : public QRunnable
{
public:
    ParseMetadataTask(int fileId, QVector<MetadataInputLine>&& lines,
                      const MetadataDetectionConfig& config, quint64 generation,
                      QSharedPointer<QAtomicInt> cancelled, QSharedPointer<QSemaphore> taskSlots)
        : m_fileId(fileId), m_lines(std::move(lines)), m_config(config),
          m_generation(generation), m_cancelled(std::move(cancelled)), m_taskSlots(taskSlots) {}

    void run() override {
        TaskSlotGuard slotGuard(m_taskSlots);
        if (m_cancelled->loadAcquire()) {
            return;
        }

        QVector<LineMetadataRecord> parsed;
        parsed.reserve(m_lines.size());

        int processedLines = 0;
        for (const MetadataInputLine& line : m_lines) {
            if ((processedLines & 63) == 0 && m_cancelled->loadRelaxed()) {
                return;
            }
            const ParsedLineMetadata metadata = parseLineMetadata(QStringView(line.raw), m_config);
            ++processedLines;
            if (metadata.level == LogLevel::Unknown && metadata.timestampText.isEmpty()) {
                continue;
            }
            parsed.append(LineMetadataRecord(line.lineNumber, metadata.timestampText,
                                             metadata.timestampEpochMs, metadata.level));
        }

        MetadataPipeline::instance().enqueueParsedBatch(
            m_fileId, m_generation, processedLines, std::move(parsed));
    }

private:
    int m_fileId;
    QVector<MetadataInputLine> m_lines;
    MetadataDetectionConfig m_config;
    quint64 m_generation;
    QSharedPointer<QAtomicInt> m_cancelled;
    QSharedPointer<QSemaphore> m_taskSlots;
};
}

MetadataPipeline& MetadataPipeline::instance() {
    static MetadataPipeline pipeline;
    return pipeline;
}

MetadataPipeline::MetadataPipeline() {
    m_detectionConfig = loadMetadataDetectionConfig();

    const int parserThreads = qBound(1, QThread::idealThreadCount() - 2, 8);
    m_parserPool.setMaxThreadCount(parserThreads);
    m_parserPool.setThreadPriority(QThread::HighPriority);

    QObject::connect(&m_writerThread, &QThread::started, [this]() { writerLoop(); });
    m_writerThread.start(QThread::HighPriority);

    qInfo() << "MetadataPipeline: Started with" << parserThreads << "parser threads.";
}

MetadataPipeline::~MetadataPipeline() {
    shutdown();
}

void MetadataPipeline::enqueueBatch(int fileId, const QVector<LineRecord>& records) {
    if (records.isEmpty()) {
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        if (m_stopping || m_cancelledFileIds.contains(fileId)) {
            return;
        }
    }

    m_taskSlots->acquire();

    QVector<MetadataInputLine> lines;
    lines.reserve(records.size());
    for (const LineRecord& record : records) {
        lines.append(MetadataInputLine{record.raw, record.lineNumber});
    }

    FileState state;
    {
        QMutexLocker locker(&m_mutex);
        if (m_stopping || m_cancelledFileIds.contains(fileId)) {
            m_taskSlots->release();
            return;
        }
        state = ensureFileStateLocked(fileId);
        m_progressByFile[fileId].queuedLines += lines.size();
    }
    MetadataDetectionConfig config;
    {
        QMutexLocker locker(&m_mutex);
        config = m_detectionConfig;
        const LogFormatDetectionResult formatResult = m_formatByFile.value(fileId);
        if (formatResult.detected) {
            config.hasFormat = true;
            config.format = formatResult.format;
            config.formatPatternName = formatResult.patternName;
        }
        config.referenceDate = m_referenceDateByFile.value(fileId, QDate::currentDate());
    }
    m_parserPool.start(new ParseMetadataTask(fileId, std::move(lines), config,
                                              state.generation, state.cancelled,
                                              m_taskSlots));
}

void MetadataPipeline::cancelFile(int fileId) {
    QMutexLocker locker(&m_mutex);
    m_cancelledFileIds.insert(fileId);
    const auto stateIt = m_fileStates.find(fileId);
    if (stateIt != m_fileStates.end() && stateIt->cancelled) {
        stateIt->cancelled->storeRelease(1);
    }
    m_fileStates.remove(fileId);
    m_formatByFile.remove(fileId);
    m_referenceDateByFile.remove(fileId);
    m_progressByFile.remove(fileId);

    for (int i = m_pendingWrites.size() - 1; i >= 0; --i) {
        if (m_pendingWrites.at(i).fileId == fileId) {
            if (m_pendingWrites.at(i).completion) {
                m_pendingWrites.at(i).completion->release();
            }
            m_pendingWrites.removeAt(i);
        }
    }
}

void MetadataPipeline::forgetFile(int fileId) {
    QMutexLocker locker(&m_mutex);
    m_cancelledFileIds.remove(fileId);
}

MetadataProgress MetadataPipeline::progress(int fileId) const {
    QMutexLocker locker(&m_mutex);
    return m_progressByFile.value(fileId);
}

void MetadataPipeline::reloadConfig() {
    QMutexLocker locker(&m_mutex);
    m_detectionConfig = loadMetadataDetectionConfig();
}

void MetadataPipeline::setDetectedFormat(int fileId, const LogFormatDetectionResult& result) {
    QMutexLocker locker(&m_mutex);
    if (result.detected) {
        m_formatByFile.insert(fileId, result);
    } else {
        m_formatByFile.remove(fileId);
    }
}

LogFormatDetectionResult MetadataPipeline::detectedFormat(int fileId) const {
    QMutexLocker locker(&m_mutex);
    return m_formatByFile.value(fileId);
}

void MetadataPipeline::setReferenceDate(int fileId, const QDate& date) {
    QMutexLocker locker(&m_mutex);
    m_referenceDateByFile.insert(fileId, date.isValid() ? date : QDate::currentDate());
}

void MetadataPipeline::reprocessFile(int fileId) {
    const auto store = LogLineStoreRegistry::instance().store(fileId);
    if (!store) {
        return;
    }

    MetadataDetectionConfig config;
    FileState state;
    const auto clearCompleted = QSharedPointer<QSemaphore>::create(0);
    {
        QMutexLocker locker(&m_mutex);
        if (m_stopping || m_cancelledFileIds.contains(fileId)) {
            return;
        }
        config = m_detectionConfig;
        const LogFormatDetectionResult formatResult = m_formatByFile.value(fileId);
        if (formatResult.detected) {
            config.hasFormat = true;
            config.format = formatResult.format;
            config.formatPatternName = formatResult.patternName;
        }
        config.referenceDate = m_referenceDateByFile.value(fileId, QDate::currentDate());
        const FileState previous = ensureFileStateLocked(fileId);
        previous.cancelled->storeRelease(1);
        state.generation = previous.generation + 1;
        state.cancelled = QSharedPointer<QAtomicInt>::create(0);
        m_fileStates[fileId] = state;
        MetadataProgress& progress = m_progressByFile[fileId];
        progress = MetadataProgress{};
        progress.queuedLines = store->lineCount();

        PendingWrite clear;
        clear.type = PendingWrite::Type::Clear;
        clear.fileId = fileId;
        clear.generation = state.generation;
        clear.completion = clearCompleted;
        m_pendingWrites.enqueue(std::move(clear));
    }

    m_parserPool.start(QRunnable::create([this, fileId, store, config, state, clearCompleted]() {
        clearCompleted->acquire();
        if (state.cancelled->loadAcquire()) {
            return;
        }
        scheduleReprocessRanges(fileId, store, config, state);
    }));
}

void MetadataPipeline::shutdown() {
    {
        QMutexLocker locker(&m_mutex);
        if (m_stopping) {
            return;
        }
        m_stopping = true;
    }

    m_parserPool.waitForDone();
    m_writerThread.quit();
    m_writerThread.wait(3000);
}

void MetadataPipeline::enqueueParsedBatch(int fileId, quint64 generation, int processedLines,
                                          QVector<LineMetadataRecord>&& records) {
    QMutexLocker locker(&m_mutex);
    if (m_stopping || !isCurrentLocked(fileId, generation)) {
        return;
    }
    MetadataProgress& progress = m_progressByFile[fileId];
    progress.processedLines += processedLines;
    progress.taggedLines += records.size();
    if (records.isEmpty()) {
        return;
    }
    PendingWrite write;
    write.fileId = fileId;
    write.generation = generation;
    write.records = std::move(records);
    m_pendingWrites.enqueue(std::move(write));
}

MetadataPipeline::FileState MetadataPipeline::ensureFileStateLocked(int fileId) {
    const auto it = m_fileStates.constFind(fileId);
    if (it != m_fileStates.constEnd()) {
        return it.value();
    }

    FileState state;
    state.generation = 1;
    state.cancelled = QSharedPointer<QAtomicInt>::create(0);
    m_fileStates.insert(fileId, state);
    return state;
}

bool MetadataPipeline::isCurrentLocked(int fileId, quint64 generation) const {
    const auto it = m_fileStates.constFind(fileId);
    return it != m_fileStates.constEnd()
        && it->generation == generation
        && it->cancelled
        && !it->cancelled->loadRelaxed();
}

void MetadataPipeline::scheduleReprocessRanges(int fileId,
                                               const QSharedPointer<LogLineStore>& store,
                                               const MetadataDetectionConfig& config,
                                               const FileState& state) {
    constexpr int BatchSize = 2000;
    const int lineCount = store ? store->lineCount() : 0;
    if (lineCount == 0 || state.cancelled->loadAcquire()) {
        return;
    }

    const auto nextLine = QSharedPointer<QAtomicInt>::create(0);
    const int rangeCount = (lineCount + BatchSize - 1) / BatchSize;
    const int laneCount = qMin(m_parserPool.maxThreadCount(), rangeCount);
    for (int lane = 0; lane < laneCount; ++lane) {
        m_parserPool.start(QRunnable::create([this, fileId, store, config, state, nextLine, lineCount]() {
            while (!state.cancelled->loadAcquire()) {
                const int start = nextLine->fetchAndAddRelaxed(BatchSize);
                if (start >= lineCount) {
                    return;
                }
                const int end = qMin(start + BatchSize, lineCount);
                QVector<LineMetadataRecord> parsed;
                parsed.reserve(end - start);
                for (int lineNumber = start; lineNumber < end; ++lineNumber) {
                    if (((lineNumber - start) & 63) == 0 && state.cancelled->loadRelaxed()) {
                        return;
                    }
                    const QString text = store->lineText(lineNumber);
                    const ParsedLineMetadata metadata = parseLineMetadata(QStringView(text), config);
                    if (metadata.level != LogLevel::Unknown || !metadata.timestampText.isEmpty()) {
                        parsed.append(LineMetadataRecord(lineNumber, metadata.timestampText,
                                                         metadata.timestampEpochMs, metadata.level));
                    }
                }
                enqueueParsedBatch(fileId, state.generation, end - start, std::move(parsed));
            }
        }));
    }
}

void MetadataPipeline::writerLoop() {
    while (true) {
        PendingWrite item;
        bool hasItem = false;

        {
            QMutexLocker locker(&m_mutex);
            if (m_stopping && m_pendingWrites.isEmpty()) {
                break;
            }
            if (!m_pendingWrites.isEmpty()) {
                item = std::move(m_pendingWrites.dequeue());
                hasItem = true;
            }
        }

        if (!hasItem) {
            QThread::msleep(10);
            continue;
        }

        bool current = false;
        {
            QMutexLocker locker(&m_mutex);
            current = isCurrentLocked(item.fileId, item.generation);
        }

        if (current && LogDatabase::instance().isFileActive(item.fileId)) {
            if (item.type == PendingWrite::Type::Clear) {
                LogDatabase::instance().clearMetadata(item.fileId);
            } else {
                LogDatabase::instance().insertMetadataBatch(item.fileId, item.records);
            }
        }
        if (item.completion) {
            item.completion->release();
        }
    }
}
