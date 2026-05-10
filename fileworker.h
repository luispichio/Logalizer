#ifndef FILEWORKER_H
#define FILEWORKER_H

#include <QObject>
#include <QDateTime>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVector>
#include <atomic>
#include "linerecord.h"

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
    void chunkInserted(int fileId, qint32 totalLinesInserted);
    void finished(int fileId);
    void error(int fileId, QString message);

private:
    void doWork();
    LineRecord parseLine(const QString& line, qint64 offset, qint32 lineNum) const;
    bool extractTimestampFromJson(const QJsonObject& obj,
                                  QString& timestampText,
                                  qint64& timestampUnixMs,
                                  QString& timestampSource) const;
    bool extractTimestampFromRaw(const QString& line,
                                 QString& timestampText,
                                 qint64& timestampUnixMs,
                                 QString& timestampSource) const;
    bool tryParseTimestampText(const QString& input, QString& normalizedText, qint64& unixMs) const;

    QString m_fileName;
    int m_fileId;
    std::atomic<bool> m_stopRequested{false};

    static constexpr int CHUNK_SIZE = 5000;
};

#endif // FILEWORKER_H
