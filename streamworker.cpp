#include "streamworker.h"
#include "appsettings.h"
#include "formatdetector.h"
#include "loglinestore.h"
#include "logdatabase.h"
#include "metadatapipeline.h"

#include <QByteArray>
#include <QIODevice>
#include <QDate>
#include <QElapsedTimer>
#include <QtCore/QtLogging>

#include <cstdio>
#ifdef Q_OS_UNIX
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <io.h>
#include <QThread>
#endif

StreamWorker::StreamWorker(int fileId, QObject* parent)
    : QObject(parent), m_fileId(fileId), m_batchSize(AppSettings::streamBatchSize()), m_sampleLineLimit(AppSettings::formatDetectionSampleLines()) {}

StreamWorker::~StreamWorker() {}

void StreamWorker::start() {
    doWork();
}

void StreamWorker::stop() {
    m_stopRequested = true;
}

void StreamWorker::doWork() {
    if (!LogDatabase::instance().createTable(m_fileId)) {
        emit error(m_fileId, "Failed to create database table");
        return;
    }

    qInfo() << "StreamWorker: Ingesting stdin";

    auto store = QSharedPointer<SpillLineStore>::create();
    QString storeError;
    if (!store->open(&storeError)) {
        emit error(m_fileId, storeError);
        return;
    }
    LogLineStoreRegistry::instance().registerStore(m_fileId, store);

    QVector<LineRecord> batch;
    batch.reserve(m_batchSize);

    QByteArray pending;
    char readBuffer[64 * 1024];
    qint32 lineNumber = 0;
    qint64 logicalPosition = 0;
    QElapsedTimer batchTimer;
    batchTimer.start();

    while (!m_stopRequested) {
        if (m_stopRequested) {
            qInfo() << "StreamWorker: Stop requested, aborting ingestion.";
            break;
        }

#ifdef Q_OS_UNIX
        struct pollfd input {fileno(stdin), POLLIN, 0};
        const int ready = ::poll(&input, 1, 200);
        if (ready == 0) {
            flushBatch(batch, lineNumber, logicalPosition);
            batchTimer.restart();
            continue;
        }
        if (ready < 0) {
            if (errno == EINTR) continue;
            emit error(m_fileId, "Failed to poll stdin");
            return;
        }
        const ssize_t bytesRead = ::read(fileno(stdin), readBuffer, sizeof(readBuffer));
        if (bytesRead < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            emit error(m_fileId, "Failed to read stdin");
            return;
        }
        if (bytesRead == 0) break;
#elif defined(Q_OS_WIN)
        const HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
        if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
            emit error(m_fileId, "Invalid stdin handle");
            return;
        }
        const DWORD type = GetFileType(handle);
        if (type == FILE_TYPE_PIPE) {
            DWORD available = 0;
            if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) {
                if (GetLastError() == ERROR_BROKEN_PIPE) break;
                emit error(m_fileId, "Failed to inspect stdin pipe");
                return;
            }
            if (available == 0) {
                QThread::msleep(100);
                flushBatch(batch, lineNumber, logicalPosition);
                batchTimer.restart();
                continue;
            }
        } else if (type == FILE_TYPE_CHAR && WaitForSingleObject(handle, 100) != WAIT_OBJECT_0) {
            flushBatch(batch, lineNumber, logicalPosition);
            batchTimer.restart();
            continue;
        }
        DWORD bytesRead = 0;
        if (!ReadFile(handle, readBuffer, sizeof(readBuffer), &bytesRead, nullptr)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) break;
            emit error(m_fileId, "Failed to read stdin");
            return;
        }
        if (bytesRead == 0) break;
#else
        const size_t bytesRead = std::fread(readBuffer, 1, sizeof(readBuffer), stdin);
        if (bytesRead == 0) {
            if (std::feof(stdin)) {
                break;
            }
            if (std::ferror(stdin)) {
                emit error(m_fileId, "Failed to read stdin");
                return;
            }
            continue;
        }
#endif

        pending.append(readBuffer, static_cast<qsizetype>(bytesRead));
        int newlineIndex = -1;
        while ((newlineIndex = pending.indexOf('\n')) >= 0) {
            QByteArray lineBytes = pending.left(newlineIndex);
            if (lineBytes.endsWith('\r')) {
                lineBytes.chop(1);
            }
            pending.remove(0, newlineIndex + 1);
            appendLine(lineBytes, store, batch, lineNumber, logicalPosition);
            if (batch.size() >= m_batchSize) {
                flushBatch(batch, lineNumber, logicalPosition);
                batchTimer.restart();
            }
        }
        // Publish the first lines promptly, then batch for up to 200 ms.
        if (!batch.isEmpty() && (!m_formatDetectionEmitted || batchTimer.elapsed() >= 200)) {
            flushBatch(batch, lineNumber, logicalPosition);
            batchTimer.restart();
        }
    }

    if (!pending.isEmpty()) {
        if (pending.endsWith('\r')) {
            pending.chop(1);
        }
        appendLine(pending, store, batch, lineNumber, logicalPosition);
    }

    if (!batch.isEmpty()) {
        flushBatch(batch, lineNumber, logicalPosition);
    }

    if (!m_formatDetectionEmitted) {
        const LogFormatDetectionResult detectedFormat = FormatDetector::detect("stdin", m_sampleLines);
        MetadataPipeline::instance().setDetectedFormat(m_fileId, detectedFormat);
        emit formatDetected(m_fileId, detectedFormat);
        m_formatDetectionEmitted = true;
    }

    emit progressUpdate(m_fileId, logicalPosition, logicalPosition, lineNumber);
    emit finished(m_fileId);

    qInfo() << "StreamWorker: Ingestion complete." << lineNumber << "lines," << logicalPosition << "bytes.";
}

void StreamWorker::appendLine(const QByteArray& lineBytes, QSharedPointer<SpillLineStore> store,
                              QVector<LineRecord>& batch, qint32& lineNumber, qint64& logicalPosition) {
    const QString line = QString::fromUtf8(lineBytes);
    if (!m_formatDetectionEmitted && m_sampleLines.size() < m_sampleLineLimit && !line.trimmed().isEmpty()) {
        m_sampleLines.append(line.trimmed());
    }

    store->appendLine(lineBytes, logicalPosition);
    batch.append(LineRecord(line, logicalPosition, lineNumber));
    logicalPosition += lineBytes.size() + 1;
    ++lineNumber;
}

void StreamWorker::flushBatch(QVector<LineRecord>& batch, qint32 lineNumber, qint64 logicalPosition) {
    if (batch.isEmpty()) {
        return;
    }
    const bool firstBatch = !m_formatDetectionEmitted;
    if (firstBatch) {
        const LogFormatDetectionResult detectedFormat = FormatDetector::detect("stdin", m_sampleLines);
        MetadataPipeline::instance().setDetectedFormat(m_fileId, detectedFormat);
        emit formatDetected(m_fileId, detectedFormat);
        m_formatDetectionEmitted = true;
    }
    MetadataPipeline::instance().enqueueBatch(m_fileId, batch);
    LogDatabase::instance().insertBatch(m_fileId, batch);
    batch.clear();
    if (firstBatch) qInfo() << "StreamWorker: First batch published.";

    emit chunkInserted(m_fileId, lineNumber);
    emit progressUpdate(m_fileId, logicalPosition, 0, lineNumber);
}
