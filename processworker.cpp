#include "processworker.h"
#include "logdatabase.h"

#include <QMetaObject>
#include <QProcess>
#include <QStringList>
#include <QThread>
#include <QtCore/QtLogging>

ProcessWorker::ProcessWorker(const QString& command, int fileId, QObject* parent)
    : QObject(parent), m_command(command), m_fileId(fileId)
{
    m_batch.reserve(CHUNK_SIZE);
}

ProcessWorker::~ProcessWorker() {
    stop();
}

void ProcessWorker::start() {
    if (!LogDatabase::instance().createTable(m_fileId)) {
        emit error(m_fileId, "Failed to create database table");
        return;
    }

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &ProcessWorker::readProcessOutput);
    connect(m_process, &QProcess::finished, this, &ProcessWorker::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &ProcessWorker::onProcessError);

    qInfo() << "ProcessWorker: Starting command" << m_command;
#ifdef Q_OS_WIN
    m_process->start("cmd.exe", {"/C", m_command});
#else
    m_process->start("/bin/sh", {"-c", m_command});
#endif
}

void ProcessWorker::stop() {
    m_stopRequested = true;
    if (!m_process || m_finished) {
        return;
    }

    auto stopProcess = [process = m_process]() {
        if (process->state() != QProcess::NotRunning) {
            process->terminate();
            if (!process->waitForFinished(3000)) {
                process->kill();
                process->waitForFinished(1000);
            }
        }
    };

    if (QThread::currentThread() == m_process->thread()) {
        stopProcess();
    } else {
        QMetaObject::invokeMethod(m_process, stopProcess, Qt::BlockingQueuedConnection);
    }
}

void ProcessWorker::readProcessOutput() {
    if (!m_process) {
        return;
    }

    m_pending += m_process->readAllStandardOutput();
    flushCompleteLines();
    flushBatch();
}

void ProcessWorker::onProcessFinished(int exitCode, int exitStatus) {
    Q_UNUSED(exitStatus)

    if (m_process) {
        m_pending += m_process->readAllStandardOutput();
    }

    flushCompleteLines();
    flushRemainder();
    flushBatch();

    emit progressUpdate(m_fileId, m_logicalPosition, m_logicalPosition, m_lineNumber);
    emit finished(m_fileId);
    m_finished = true;

    qInfo() << "ProcessWorker: Command finished with exit code" << exitCode
            << "," << m_lineNumber << "lines," << m_logicalPosition << "bytes.";
}

void ProcessWorker::onProcessError(QProcess::ProcessError processError) {
    if (m_stopRequested) {
        return;
    }

    const QString message = m_process
        ? QString("Command failed: %1").arg(m_process->errorString())
        : QString("Command failed with process error %1").arg(processError);
    emit error(m_fileId, message);
}

void ProcessWorker::flushCompleteLines() {
    int newlineIndex = -1;
    while ((newlineIndex = m_pending.indexOf('\n')) >= 0) {
        QByteArray lineBytes = m_pending.left(newlineIndex);
        if (lineBytes.endsWith('\r')) {
            lineBytes.chop(1);
        }
        m_pending.remove(0, newlineIndex + 1);

        const QString line = QString::fromUtf8(lineBytes);
        m_batch.append(LineRecord(line, m_logicalPosition, m_lineNumber));
        m_logicalPosition += lineBytes.size() + 1;
        ++m_lineNumber;

        if (m_batch.size() >= CHUNK_SIZE) {
            flushBatch();
        }
    }
}

void ProcessWorker::flushBatch() {
    if (m_batch.isEmpty()) {
        return;
    }

    LogDatabase::instance().insertBatch(m_fileId, m_batch);
    m_batch.clear();

    emit chunkInserted(m_fileId, m_lineNumber);
    emit progressUpdate(m_fileId, m_logicalPosition, 0, m_lineNumber);
}

void ProcessWorker::flushRemainder() {
    if (m_pending.isEmpty()) {
        return;
    }

    QByteArray lineBytes = m_pending;
    if (lineBytes.endsWith('\r')) {
        lineBytes.chop(1);
    }
    m_pending.clear();

    const QString line = QString::fromUtf8(lineBytes);
    m_batch.append(LineRecord(line, m_logicalPosition, m_lineNumber));
    m_logicalPosition += lineBytes.size();
    ++m_lineNumber;
}
