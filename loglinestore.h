#ifndef LOGLINESTORE_H
#define LOGLINESTORE_H

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QReadWriteLock>
#include <QSharedPointer>
#include <QString>
#include <QTemporaryFile>
#include <QVector>

class QFile;

class LogLineStore
{
public:
    virtual ~LogLineStore() = default;

    virtual int lineCount() const = 0;
    virtual qint64 filePosition(int lineNumber) const = 0;
    virtual QByteArray lineBytes(int lineNumber) const = 0;

    QString lineText(int lineNumber) const;
};

class MmapLineStore final : public LogLineStore
{
public:
    explicit MmapLineStore(const QString& fileName);
    ~MmapLineStore() override;

    bool open(QString* errorMessage = nullptr);

    int lineCount() const override;
    qint64 filePosition(int lineNumber) const override;
    QByteArray lineBytes(int lineNumber) const override;

private:
    QString m_fileName;
    QFile* m_file = nullptr;
    uchar* m_mappedData = nullptr;
    qint64 m_mappedSize = 0;
    QVector<qint64> m_offsets;
    QVector<qint32> m_lengths;
};

class SpillLineStore final : public LogLineStore
{
public:
    SpillLineStore();
    ~SpillLineStore() override;

    bool open(QString* errorMessage = nullptr);
    bool appendLine(const QByteArray& lineBytes, qint64 logicalPosition);

    int lineCount() const override;
    qint64 filePosition(int lineNumber) const override;
    QByteArray lineBytes(int lineNumber) const override;

private:
    QTemporaryFile* m_file = nullptr;
    QVector<qint64> m_offsets;
    QVector<qint64> m_positions;
    QVector<qint32> m_lengths;
    mutable QHash<int, QByteArray> m_cache;
    mutable QVector<int> m_cacheOrder;
    mutable QReadWriteLock m_lock;
    mutable QMutex m_ioMutex;

    static constexpr int MaxCachedLines = 4096;
};

class LogLineStoreRegistry
{
public:
    static LogLineStoreRegistry& instance();

    void registerStore(int fileId, const QSharedPointer<LogLineStore>& store);
    QSharedPointer<LogLineStore> store(int fileId) const;
    void unregisterStore(int fileId);

private:
    LogLineStoreRegistry() = default;

    mutable QMutex m_mutex;
    QHash<int, QSharedPointer<LogLineStore>> m_stores;
};

#endif // LOGLINESTORE_H
