#include "fileworker.h"
#include "appsettings.h"
#include "formatdetector.h"
#include "loglinestore.h"
#include "logdatabase.h"
#include "metadatapipeline.h"

#include <QFile>
#include <QFileInfo>
#include <QDate>
#include <QRegularExpression>
#include <QtCore/QtLogging>
#include <QThread>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace {
struct FileIdentity {
    quint64 device = 0;
    quint64 file = 0;
};

bool fileIdentity(const QString& path, FileIdentity& identity) {
#ifdef Q_OS_UNIX
    struct stat info {};
    if (::stat(QFile::encodeName(path).constData(), &info) != 0) return false;
    identity.device = static_cast<quint64>(info.st_dev);
    identity.file = static_cast<quint64>(info.st_ino);
    return true;
#elif defined(Q_OS_WIN)
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info {};
    const bool ok = GetFileInformationByHandle(handle, &info);
    CloseHandle(handle);
    if (!ok) return false;
    identity.device = info.dwVolumeSerialNumber;
    identity.file = (static_cast<quint64>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    return true;
#else
    Q_UNUSED(path)
    Q_UNUSED(identity)
    return true;
#endif
}

QDate referenceDateForPath(const QString& fileName) {
    const QString text = QFileInfo(fileName).fileName();
    const QRegularExpression compact("(\\d{4})(\\d{2})(\\d{2})");
    QRegularExpressionMatch match = compact.match(text);
    if (match.hasMatch()) {
        const QDate date(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt());
        if (date.isValid()) {
            return date;
        }
    }

    const QRegularExpression separated("(\\d{4})[-_.](\\d{2})[-_.](\\d{2})");
    match = separated.match(text);
    if (match.hasMatch()) {
        const QDate date(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt());
        if (date.isValid()) {
            return date;
        }
    }

    const QFileInfo info(fileName);
    return info.exists() ? info.lastModified().date() : QDate::currentDate();
}
}

FileWorker::FileWorker(const QString& fileName, int fileId, QObject* parent)
    : QObject(parent), m_fileName(fileName), m_fileId(fileId), m_batchSize(AppSettings::fileBatchSize()), m_sampleLineLimit(AppSettings::formatDetectionSampleLines()) {}

FileWorker::~FileWorker() {}

void FileWorker::start() {
    doWork();
}

void FileWorker::stop() {
    m_stopRequested = true;
}

void FileWorker::doWork() {
    bool reload = false;
    while (!m_stopRequested) {
    QFileInfo fileInfo(m_fileName);
    if (!fileInfo.exists()) {
        if (reload) {
            QThread::msleep(250);
            continue;
        }
        emit error(m_fileId, QString("File does not exist: %1").arg(m_fileName));
        return;
    }

    if (reload) {
        emit reloading(m_fileId);
        MetadataPipeline::instance().cancelFile(m_fileId);
        LogLineStoreRegistry::instance().unregisterStore(m_fileId);
        LogDatabase::instance().dropTable(m_fileId);
        MetadataPipeline::instance().forgetFile(m_fileId);
    }
    if (!LogDatabase::instance().createTable(m_fileId)) {
        emit error(m_fileId, "Failed to create database table");
        return;
    }

    qint64 totalBytes = fileInfo.size();
    qInfo() << "FileWorker: Ingesting" << m_fileName;
    MetadataPipeline::instance().setReferenceDate(m_fileId, referenceDateForPath(m_fileName));

    FileIdentity originalIdentity;
    if (!fileIdentity(m_fileName, originalIdentity)) {
        reload = true;
        continue;
    }
    auto store = QSharedPointer<MmapLineStore>::create(m_fileName);
    QString storeError;
    if (!store->open(&storeError)) {
        emit error(m_fileId, storeError);
        return;
    }
    totalBytes = store->mappedSize();
    FileIdentity openedIdentity;
    if (!fileIdentity(m_fileName, openedIdentity)
        || openedIdentity.device != originalIdentity.device || openedIdentity.file != originalIdentity.file) {
        reload = true;
        continue;
    }
    LogLineStoreRegistry::instance().registerStore(m_fileId, store);

    QVector<LineRecord> batch;
    batch.reserve(m_batchSize);

    qint32 totalLines = store->lineCount();
    QStringList sampleLines;
    sampleLines.reserve(qMin<int>(m_sampleLineLimit, totalLines));
    for (qint32 lineNumber = 0; lineNumber < totalLines && sampleLines.size() < m_sampleLineLimit; ++lineNumber) {
        const QString line = QString::fromUtf8(store->lineBytes(lineNumber)).trimmed();
        if (!line.isEmpty()) {
            sampleLines.append(line);
        }
    }
    const LogFormatDetectionResult detectedFormat = FormatDetector::detect(m_fileName, sampleLines);
    MetadataPipeline::instance().setDetectedFormat(m_fileId, detectedFormat);
    emit formatDetected(m_fileId, detectedFormat);

    qint64 bytesProcessed = 0;

    for (qint32 lineNumber = 0; lineNumber < totalLines; ++lineNumber) {
        if (m_stopRequested) {
            qInfo() << "FileWorker: Stop requested, aborting ingestion.";
            break;
        }

        const qint64 posBefore = store->filePosition(lineNumber);
        const QByteArray lineBytes = store->lineBytes(lineNumber);
        bytesProcessed = lineNumber + 1 < totalLines
            ? store->filePosition(lineNumber + 1)
            : totalBytes;
        const QString line = QString::fromUtf8(lineBytes);

        batch.append(LineRecord(line, posBefore, lineNumber));

        if (batch.size() >= m_batchSize) {
            MetadataPipeline::instance().enqueueBatch(m_fileId, batch);
            LogDatabase::instance().insertBatch(m_fileId, batch);
            batch.clear();

            emit chunkInserted(m_fileId, lineNumber + 1);
            emit progressUpdate(m_fileId, bytesProcessed, totalBytes, lineNumber + 1);
        }
    }

    if (!m_stopRequested && !batch.isEmpty()) {
        MetadataPipeline::instance().enqueueBatch(m_fileId, batch);
        LogDatabase::instance().insertBatch(m_fileId, batch);
        emit chunkInserted(m_fileId, totalLines);
    }

    emit progressUpdate(m_fileId, totalBytes, totalBytes, totalLines);
    emit finished(m_fileId);

    qInfo() << "FileWorker: Ingestion complete." << totalLines << "lines,"
            << totalBytes << "bytes.";

    // Keep the source open and poll at a bounded rate. No disk I/O occurs on
    // the UI thread; the stop flag also interrupts the idle tailer promptly.
    reload = false;
    qint64 observedBytes = totalBytes;
    while (!m_stopRequested) {
        QThread::msleep(250);
        QFileInfo current(m_fileName);
        if (!current.exists()) {
            reload = true;
            break;
        }
        const qint64 size = current.size();
        FileIdentity currentIdentity;
        if (!fileIdentity(m_fileName, currentIdentity)
            || currentIdentity.device != originalIdentity.device || currentIdentity.file != originalIdentity.file) {
            reload = true;
            break;
        }
        if (size < observedBytes) {
            reload = true;
            break;
        }
        if (size == observedBytes) continue;
        if (store->hasPartialLine()) {
            // Do not repeatedly reload a large file while a producer is still
            // writing one unterminated line. Reload once it is complete.
            QFile input(m_fileName);
            if (!input.open(QIODevice::ReadOnly) || !input.seek(observedBytes)) {
                reload = true;
                break;
            }
            bool completed = false;
            while (!input.atEnd() && !m_stopRequested) {
                const QByteArray bytes = input.read(64 * 1024);
                if (bytes.isEmpty()) break;
                if (bytes.contains('\n')) { completed = true; break; }
            }
            observedBytes = size;
            if (completed) {
                reload = true;
                break;
            }
            continue;
        }
        if (!store->appendToSize(size, &storeError)) {
            qWarning() << "FileWorker: Cannot extend mapping; reloading:" << storeError;
            reload = true;
            break;
        }
        const qint32 newLines = store->lineCount();
        for (qint32 line = totalLines; line < newLines && !m_stopRequested; ++line) {
            batch.append(LineRecord(QString::fromUtf8(store->lineBytes(line)), store->filePosition(line), line));
            if (batch.size() >= m_batchSize) {
                LogDatabase::instance().insertBatch(m_fileId, batch);
                MetadataPipeline::instance().enqueueBatch(m_fileId, batch);
                batch.clear();
                emit chunkInserted(m_fileId, line + 1);
            }
        }
        if (!batch.isEmpty() && !m_stopRequested) {
            LogDatabase::instance().insertBatch(m_fileId, batch);
            MetadataPipeline::instance().enqueueBatch(m_fileId, batch);
            batch.clear();
            emit chunkInserted(m_fileId, newLines);
        }
        totalLines = newLines;
        totalBytes = size;
        observedBytes = size;
        emit progressUpdate(m_fileId, size, size, totalLines);
    }
    }
}
