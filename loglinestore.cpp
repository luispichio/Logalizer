#include "loglinestore.h"

#include <QFile>
#include <QWriteLocker>
#include <QDebug>

QString LogLineStore::lineText(int lineNumber) const {
    return QString::fromUtf8(lineBytes(lineNumber));
}

MmapLineStore::MmapLineStore(const QString& fileName)
    : m_fileName(fileName) {}

MmapLineStore::~MmapLineStore() {
    if (m_file && m_mappedData) {
        m_file->unmap(m_mappedData);
    }
    delete m_file;
}

bool MmapLineStore::open(QString* errorMessage) {
    m_file = new QFile(m_fileName);
    if (!m_file->open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QString("Cannot mmap file: %1").arg(m_file->errorString());
        }
        return false;
    }

    m_mappedSize = m_file->size();
    if (m_mappedSize == 0) {
        return true;
    }

    m_mappedData = m_file->map(0, m_mappedSize);
    if (!m_mappedData) {
        if (errorMessage) {
            *errorMessage = QString("Cannot mmap file: %1").arg(m_file->errorString());
        }
        return false;
    }

    qint64 lineStart = 0;
    for (qint64 i = 0; i < m_mappedSize; ++i) {
        if (m_mappedData[i] != '\n') {
            continue;
        }
        qint64 lineEnd = i;
        if (lineEnd > lineStart && m_mappedData[lineEnd - 1] == '\r') {
            --lineEnd;
        }
        m_offsets.append(lineStart);
        m_lengths.append(static_cast<qint32>(lineEnd - lineStart));
        lineStart = i + 1;
    }

    if (lineStart < m_mappedSize) {
        qint64 lineEnd = m_mappedSize;
        if (lineEnd > lineStart && m_mappedData[lineEnd - 1] == '\r') {
            --lineEnd;
        }
        m_offsets.append(lineStart);
        m_lengths.append(static_cast<qint32>(lineEnd - lineStart));
    }
    return true;
}

int MmapLineStore::lineCount() const {
    return m_offsets.size();
}

qint64 MmapLineStore::filePosition(int lineNumber) const {
    if (lineNumber < 0 || lineNumber >= m_offsets.size()) {
        return 0;
    }
    return m_offsets[lineNumber];
}

QByteArray MmapLineStore::lineBytes(int lineNumber) const {
    if (!m_mappedData || lineNumber < 0 || lineNumber >= m_offsets.size()) {
        return QByteArray();
    }

    const qint64 offset = m_offsets[lineNumber];
    const qint32 length = m_lengths[lineNumber];
    if (offset < 0 || length < 0 || offset + length > m_mappedSize) {
        return QByteArray();
    }
    return QByteArray(reinterpret_cast<const char*>(m_mappedData + offset), length);
}

SpillLineStore::SpillLineStore() = default;

SpillLineStore::~SpillLineStore() {
    delete m_file;
}

bool SpillLineStore::open(QString* errorMessage) {
    m_file = new QTemporaryFile();
    if (!m_file->open()) {
        if (errorMessage) {
            *errorMessage = QString("Cannot create temporary spill file: %1").arg(m_file->errorString());
        }
        return false;
    }
    return true;
}

bool SpillLineStore::appendLine(const QByteArray& lineBytes, qint64 logicalPosition) {
    if (!m_file) {
        return false;
    }

    QMutexLocker ioLocker(&m_ioMutex);
    const qint64 offset = m_file->pos();
    if (m_file->write(lineBytes) != lineBytes.size()) {
        qWarning() << "SpillLineStore: Failed to write line to temporary file:" << m_file->errorString();
        return false;
    }

    QWriteLocker locker(&m_lock);
    m_offsets.append(offset);
    m_positions.append(logicalPosition);
    m_lengths.append(lineBytes.size());
    return true;
}

int SpillLineStore::lineCount() const {
    QReadLocker locker(&m_lock);
    return m_offsets.size();
}

qint64 SpillLineStore::filePosition(int lineNumber) const {
    QReadLocker locker(&m_lock);
    if (lineNumber < 0 || lineNumber >= m_positions.size()) {
        return 0;
    }
    return m_positions[lineNumber];
}

QByteArray SpillLineStore::lineBytes(int lineNumber) const {
    {
        QReadLocker locker(&m_lock);
        const auto it = m_cache.constFind(lineNumber);
        if (it != m_cache.constEnd()) {
            return it.value();
        }
    }

    qint64 offset = 0;
    qint32 length = 0;
    {
        QReadLocker locker(&m_lock);
        if (!m_file || lineNumber < 0 || lineNumber >= m_offsets.size()) {
            return QByteArray();
        }
        offset = m_offsets[lineNumber];
        length = m_lengths[lineNumber];
    }

    QMutexLocker ioLocker(&m_ioMutex);
    if (!m_file->seek(offset)) {
        return QByteArray();
    }
    QByteArray bytes = m_file->read(length);

    {
        QWriteLocker locker(&m_lock);
        if (!m_cache.contains(lineNumber)) {
            m_cache.insert(lineNumber, bytes);
            m_cacheOrder.append(lineNumber);
            while (m_cacheOrder.size() > MaxCachedLines) {
                const int evicted = m_cacheOrder.takeFirst();
                m_cache.remove(evicted);
            }
        }
    }

    return bytes;
}

LogLineStoreRegistry& LogLineStoreRegistry::instance() {
    static LogLineStoreRegistry registry;
    return registry;
}

void LogLineStoreRegistry::registerStore(int fileId, const QSharedPointer<LogLineStore>& store) {
    QMutexLocker locker(&m_mutex);
    m_stores.insert(fileId, store);
}

QSharedPointer<LogLineStore> LogLineStoreRegistry::store(int fileId) const {
    QMutexLocker locker(&m_mutex);
    return m_stores.value(fileId);
}

void LogLineStoreRegistry::unregisterStore(int fileId) {
    QMutexLocker locker(&m_mutex);
    m_stores.remove(fileId);
}
