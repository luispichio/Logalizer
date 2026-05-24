#include "logwidget.h"
#include "appsettings.h"
#include "fileworker.h"
#include "loglinestore.h"
#include "metadatapipeline.h"
#include "processworker.h"
#include "streamworker.h"
#include "loglineparser.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShortcut>
#include <QSettings>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtCore/QtLogging>

#include <algorithm>

namespace {
QStringList splitJsonFilterTokens(const QString& text) {
    QString normalized = text;
    normalized.replace(',', ' ');
    return normalized.simplified().split(' ', Qt::SkipEmptyParts);
}
}

LogWidget::LogWidget(const QString& filePath, int fileId, QWidget* parent)
    : LogWidget(SourceType::File, filePath, fileId, parent)
{
}

LogWidget::LogWidget(SourceType sourceType, const QString& displayName, int fileId, QWidget* parent)
    : QWidget(parent)
    , m_filePath(displayName)
    , m_fileId(fileId)
    , m_sourceType(sourceType)
{
    setupUi();

    if (m_sourceType == SourceType::File) {
        QFileInfo fi(displayName);
        m_fileSize = fi.size();
        m_labelSize->setText(QString::number(m_fileSize / 1024.0 / 1024.0, 'f', 2) + " MB");
    } else {
        m_labelSize->setText("stream");
        m_progressBar->setRange(0, 0);
    }

    m_workerThread = new QThread(this);
    if (m_sourceType == SourceType::File) {
        m_worker = new FileWorker(displayName, fileId);
        m_worker->moveToThread(m_workerThread);

        connect(m_workerThread, &QThread::started, m_worker, &FileWorker::start);
        connect(m_worker, &FileWorker::progressUpdate, this, &LogWidget::onProgressUpdate);
        connect(m_worker, &FileWorker::chunkInserted, this, &LogWidget::onChunkInserted);
        connect(m_worker, &FileWorker::finished, this, &LogWidget::onFinished);
        connect(m_worker, &FileWorker::error, this, &LogWidget::onError);
        connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    } else if (m_sourceType == SourceType::Stdin) {
        m_streamWorker = new StreamWorker(fileId);
        m_streamWorker->moveToThread(m_workerThread);

        connect(m_workerThread, &QThread::started, m_streamWorker, &StreamWorker::start);
        connect(m_streamWorker, &StreamWorker::progressUpdate, this, &LogWidget::onProgressUpdate);
        connect(m_streamWorker, &StreamWorker::chunkInserted, this, &LogWidget::onChunkInserted);
        connect(m_streamWorker, &StreamWorker::finished, this, &LogWidget::onFinished);
        connect(m_streamWorker, &StreamWorker::error, this, &LogWidget::onError);
        connect(m_workerThread, &QThread::finished, m_streamWorker, &QObject::deleteLater);
    } else {
        m_processWorker = new ProcessWorker(displayName, fileId);
        m_processWorker->moveToThread(m_workerThread);

        connect(m_workerThread, &QThread::started, m_processWorker, &ProcessWorker::start);
        connect(m_processWorker, &ProcessWorker::progressUpdate, this, &LogWidget::onProgressUpdate);
        connect(m_processWorker, &ProcessWorker::chunkInserted, this, &LogWidget::onChunkInserted);
        connect(m_processWorker, &ProcessWorker::finished, this, &LogWidget::onFinished);
        connect(m_processWorker, &ProcessWorker::error, this, &LogWidget::onError);
        connect(m_workerThread, &QThread::finished, m_processWorker, &QObject::deleteLater);
    }

    m_workerThread->start();
}

LogWidget::~LogWidget() {
    saveSettings();
    m_refreshTimer->stop();
    if (m_metadataStatusTimer) {
        m_metadataStatusTimer->stop();
    }
    if (m_worker) {
        m_worker->stop();
    }
    if (m_processWorker) {
        m_processWorker->stop();
    }
    if (m_streamWorker) {
        m_streamWorker->stop();
    }
    if (m_workerThread) {
        m_workerThread->quit();
        if (!m_workerThread->wait(8000)) {
            qWarning() << "LogWidget: Worker thread did not finish, terminating.";
            m_workerThread->terminate();
            m_workerThread->wait(1000);
        }
    }
    MetadataPipeline::instance().cancelFile(m_fileId);
    LogDatabase::instance().dropTable(m_fileId);
    LogLineStoreRegistry::instance().unregisterStore(m_fileId);
    qInfo() << "LogWidget: Cleaned up fileId" << m_fileId << m_filePath;
}

void LogWidget::setupUi() {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(4, 4, 4, 4);
    m_mainLayout->setSpacing(4);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(AppSettings::refreshDebounceMs());
    connect(m_refreshTimer, &QTimer::timeout, this, &LogWidget::refreshData);

    m_metadataStatusTimer = new QTimer(this);
    m_metadataStatusTimer->setInterval(1000);
    connect(m_metadataStatusTimer, &QTimer::timeout, this, [this]() {
        updateMetadataStatusLabel();
        if (sortByTimestamp()) {
            refreshData();
        }
    });
    m_metadataStatusTimer->start();

    {
        auto* viewContainer = new QWidget(this);
        auto* viewLayout = new QVBoxLayout(viewContainer);
        viewLayout->setContentsMargins(0, 0, 0, 0);
        viewLayout->setSpacing(2);

        auto* ftsBlock = new QVBoxLayout();
        ftsBlock->setContentsMargins(0, 0, 0, 0);
        ftsBlock->setSpacing(0);

        auto* ftsBar = new QHBoxLayout();
        ftsBar->setContentsMargins(2, 2, 2, 2);
        ftsBar->setSpacing(4);
        ftsBar->addWidget(new QLabel("Filter:", viewContainer));
        m_searchCombo = new QComboBox(viewContainer);
        m_searchCombo->setEditable(true);
        m_searchCombo->setInsertPolicy(QComboBox::NoInsert);
        m_searchCombo->setMinimumWidth(420);
        m_searchCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_searchCombo->lineEdit()->setPlaceholderText("Filter lines with FTS5... (Enter)");
        m_searchCombo->setToolTip("Filter indexed log lines with an FTS5 expression");
        ftsBar->addWidget(m_searchCombo, 1);
        m_searchButton = new QPushButton("Apply", viewContainer);
        ftsBar->addWidget(m_searchButton);

        ftsBlock->addLayout(ftsBar);

        m_searchStatus = new QLabel("", viewContainer);
        m_searchStatus->setToolTip("FTS filter status");
        m_searchStatus->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        m_searchStatus->setStyleSheet("color:#6c757d;");

        viewLayout->addLayout(ftsBlock);

        connect(m_searchButton, &QPushButton::clicked, this, &LogWidget::onApplyFilters);
        connect(m_searchCombo->lineEdit(), &QLineEdit::returnPressed, this, &LogWidget::onApplyFilters);
        connect(m_searchCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int) { onApplyFilters(); });

        auto* jsonBar = new QHBoxLayout();
        jsonBar->setContentsMargins(2, 0, 2, 2);
        jsonBar->setSpacing(6);

        m_jsonHelperCheck = new QCheckBox("JSON", viewContainer);
        m_jsonHelperCheck->setToolTip("Parse visible JSON lines while rendering");
        jsonBar->addWidget(m_jsonHelperCheck);

        m_jsonCompactCheck = new QCheckBox("Compact", viewContainer);
        m_jsonCompactCheck->setToolTip("Show JSON as compact key=value pairs aligned within the visible buffer");
        m_jsonCompactCheck->setChecked(true);
        jsonBar->addWidget(m_jsonCompactCheck);

        m_jsonOnlyValuesCheck = new QCheckBox("Only values", viewContainer);
        m_jsonOnlyValuesCheck->setToolTip("Show only JSON values, without field names");
        jsonBar->addWidget(m_jsonOnlyValuesCheck);

        jsonBar->addWidget(new QLabel("Fields:", viewContainer));

        m_jsonFieldFilterCombo = new QComboBox(viewContainer);
        m_jsonFieldFilterCombo->setEditable(true);
        m_jsonFieldFilterCombo->setInsertPolicy(QComboBox::NoInsert);
        m_jsonFieldFilterCombo->setMinimumWidth(420);
        m_jsonFieldFilterCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_jsonFieldFilterCombo->lineEdit()->setPlaceholderText("level,msg,user.id,-metadata.*");
        m_jsonFieldFilterCombo->setToolTip("Include fields by name/path, exclude with '-', wildcard prefixes with .*.");
        jsonBar->addWidget(m_jsonFieldFilterCombo, 1);

        viewLayout->addLayout(jsonBar);

        connect(m_jsonHelperCheck, &QCheckBox::toggled, this, [this](bool) { applyBufferToView(); saveSettings(); });
        connect(m_jsonFieldFilterCombo->lineEdit(), &QLineEdit::textChanged, this, [this]() { applyBufferToView(); });
        connect(m_jsonFieldFilterCombo->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
            rememberComboText(m_jsonFieldFilterCombo, m_jsonFieldFilterHistory, "logWidget/jsonFieldFilterHistory");
            saveSettings();
        });
        connect(m_jsonFieldFilterCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int) { applyBufferToView(); });
        connect(m_jsonCompactCheck, &QCheckBox::toggled, this, [this](bool) { applyBufferToView(); saveSettings(); });
        connect(m_jsonOnlyValuesCheck, &QCheckBox::toggled, this, [this](bool) { applyBufferToView(); saveSettings(); });

        m_textFindBar = new QWidget(this);
        m_textFindBar->setVisible(true);
        {
            auto* findBlock = new QVBoxLayout(m_textFindBar);
            findBlock->setContentsMargins(0, 0, 0, 0);
            findBlock->setSpacing(0);

            auto* fl = new QHBoxLayout();
            fl->setContentsMargins(2, 2, 2, 2);
            fl->setSpacing(4);

            fl->addWidget(new QLabel("Find:", m_textFindBar));

            m_textFindCombo = new QComboBox(m_textFindBar);
            m_textFindCombo->setEditable(true);
            m_textFindCombo->setInsertPolicy(QComboBox::NoInsert);
            m_textFindCombo->setMinimumWidth(420);
            m_textFindCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            m_textFindCombo->lineEdit()->setPlaceholderText("Find words in filtered lines... (Enter)");
            fl->addWidget(m_textFindCombo, 1);

            m_textFindFirst = new QPushButton("⏮", m_textFindBar);
            m_textFindFirst->setMaximumWidth(28);
            fl->addWidget(m_textFindFirst);

            m_textFindPrev = new QPushButton("◀", m_textFindBar);
            m_textFindPrev->setMaximumWidth(28);
            fl->addWidget(m_textFindPrev);

            m_textFindNext = new QPushButton("▶", m_textFindBar);
            m_textFindNext->setMaximumWidth(28);
            fl->addWidget(m_textFindNext);

            m_textFindLast = new QPushButton("⏭", m_textFindBar);
            m_textFindLast->setMaximumWidth(28);
            fl->addWidget(m_textFindLast);

            m_textFindClear = new QPushButton("✕ Clear", m_textFindBar);
            fl->addWidget(m_textFindClear);

            findBlock->addLayout(fl);

            m_textFindStatus = new QLabel("", m_textFindBar);
            m_textFindStatus->setToolTip("Find status");
            m_textFindStatus->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
            m_textFindStatus->setStyleSheet("color:#6c757d;");
        }
        viewLayout->addWidget(m_textFindBar);

        auto* toolBar = new QHBoxLayout();
        toolBar->setContentsMargins(2, 0, 2, 2);
        toolBar->setSpacing(8);
        toolBar->addWidget(new QLabel("View:", viewContainer));
        m_wrapCheck = new QCheckBox("Wrap", viewContainer);
        m_wrapCheck->setToolTip("Toggle word wrap in text view");
        toolBar->addWidget(m_wrapCheck);
        m_showLineNumberCheck = new QCheckBox("Line #", viewContainer);
        m_showLineNumberCheck->setToolTip("Show the line number prefix in the text view");
        m_showLineNumberCheck->setChecked(true);
        toolBar->addWidget(m_showLineNumberCheck);
        m_showTimestampCheck = new QCheckBox("Timestamp", viewContainer);
        m_showTimestampCheck->setToolTip("Show detected timestamp prefix when available");
        toolBar->addWidget(m_showTimestampCheck);
        m_showLogLevelCheck = new QCheckBox("Level", viewContainer);
        m_showLogLevelCheck->setToolTip("Show detected log level prefix when available");
        toolBar->addWidget(m_showLogLevelCheck);
        m_sortTimestampCheck = new QCheckBox("Sort time", viewContainer);
        m_sortTimestampCheck->setToolTip("Show timestamped rows ordered by detected timestamp");
        toolBar->addWidget(m_sortTimestampCheck);
        toolBar->addStretch();
        viewLayout->addLayout(toolBar);

        connect(m_wrapCheck, &QCheckBox::toggled, this, &LogWidget::onWrapToggled);
        connect(m_showLineNumberCheck, &QCheckBox::toggled, this, [this](bool) { applyBufferToView(); saveSettings(); });
        connect(m_showTimestampCheck, &QCheckBox::toggled, this, [this](bool) { fillBuffer(); applyBufferToView(); saveSettings(); });
        connect(m_showLogLevelCheck, &QCheckBox::toggled, this, [this](bool) { fillBuffer(); applyBufferToView(); saveSettings(); });
        connect(m_sortTimestampCheck, &QCheckBox::toggled, this, [this](bool) { setPointer(0, true); updateMetadataStatusLabel(); saveSettings(); });

        connect(m_textFindCombo->lineEdit(), &QLineEdit::returnPressed, this, &LogWidget::onTextFindSearch);
        connect(m_textFindCombo->lineEdit(), &QLineEdit::textChanged, this, [this]() {
            m_findWords = currentFindWords();
            applyBufferToView();
        });
        connect(m_textFindCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int) { onTextFindSearch(); });
        connect(m_textFindFirst, &QPushButton::clicked, this, &LogWidget::onTextFindFirst);
        connect(m_textFindPrev, &QPushButton::clicked, this, &LogWidget::onTextFindPrev);
        connect(m_textFindNext, &QPushButton::clicked, this, &LogWidget::onTextFindNext);
        connect(m_textFindLast, &QPushButton::clicked, this, &LogWidget::onTextFindLast);
        connect(m_textFindClear, &QPushButton::clicked, this, &LogWidget::onTextFindClear);

        auto* findShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this);
        connect(findShortcut, &QShortcut::activated, this, &LogWidget::onToggleTextFindBar);
        auto* nextShortcut = new QShortcut(QKeySequence(Qt::Key_F3), this);
        connect(nextShortcut, &QShortcut::activated, this, &LogWidget::onTextFindNext);
        auto* prevShortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3), this);
        connect(prevShortcut, &QShortcut::activated, this, &LogWidget::onTextFindPrev);
        auto* escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        connect(escShortcut, &QShortcut::activated, this, [this]() {
            if (m_textFindBar && m_textFindBar->isVisible()) {
                m_textFindBar->setVisible(false);
            }
        });

        m_textBrowser = new QTextBrowser(this);
        m_textBrowser->setFont(QFont("Monospace", 9));
        m_textBrowser->setLineWrapMode(QTextEdit::NoWrap);
        m_textBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        m_logScrollBar = new QScrollBar(Qt::Vertical, this);
        m_logScrollBar->setMinimum(0);
        m_logScrollBar->setMaximum(0);
        m_logScrollBar->setSingleStep(1);
        m_logScrollBar->setPageStep(1);
        m_logScrollBar->setToolTip("Navigate rows (position = first row in buffer)");
        connect(m_logScrollBar, &QScrollBar::valueChanged, this, [this](int value) {
            setPointer(value, false, value < m_bufferPointer);
        });

        auto* contentBox = new QHBoxLayout();
        contentBox->setSpacing(2);
        contentBox->addWidget(m_textBrowser, 1);
        contentBox->addWidget(m_logScrollBar);
        viewLayout->addLayout(contentBox, 1);
        m_mainLayout->addWidget(viewContainer, 1);

        m_textBrowser->installEventFilter(this);
    }

    {
        auto* statusBar = new QHBoxLayout();
        statusBar->addWidget(new QLabel("Size:", this));
        m_labelSize = new QLabel("?", this);
        statusBar->addWidget(m_labelSize);
        statusBar->addSpacing(12);
        statusBar->addWidget(new QLabel("Lines:", this));
        m_labelLines = new QLabel("0", this);
        statusBar->addWidget(m_labelLines);
        statusBar->addSpacing(12);
        statusBar->addWidget(m_searchStatus);
        statusBar->addSpacing(12);
        statusBar->addWidget(m_textFindStatus);
        statusBar->addSpacing(12);
        m_metadataStatus = new QLabel("Meta: pending", this);
        m_metadataStatus->setToolTip("Detected metadata parsing progress");
        m_metadataStatus->setStyleSheet("color:#6c757d;");
        statusBar->addWidget(m_metadataStatus);
        statusBar->addSpacing(12);

        statusBar->addStretch();
        m_labelState = new QLabel("Loading...", this);
        statusBar->addWidget(m_labelState);
        m_progressBar = new QProgressBar(this);
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);
        m_progressBar->setMaximumWidth(300);
        statusBar->addWidget(m_progressBar);
        m_mainLayout->addLayout(statusBar);
    }

    loadSettings();
}

void LogWidget::onProgressUpdate(int fileId, qint64 bytesProcessed, qint64 totalBytes, qint32 linesProcessed) {
    if (fileId != m_fileId) {
        return;
    }

    if (totalBytes > 0) {
        if (m_progressBar->minimum() == 0 && m_progressBar->maximum() == 0) {
            m_progressBar->setRange(0, 100);
        }
        int percent = static_cast<int>(bytesProcessed * 100 / totalBytes);
        m_progressBar->setValue(percent);
    }
    m_labelLines->setText(QString::number(linesProcessed));
    m_totalLines = linesProcessed;
}

void LogWidget::onChunkInserted(int fileId, qint32 totalLinesInserted) {
    if (fileId != m_fileId) {
        return;
    }

    m_totalLines = totalLinesInserted;
    m_labelLines->setText(QString::number(totalLinesInserted));

    if (m_buffer.isEmpty()) {
        refreshData();
        return;
    }

    m_refreshTimer->start();
}

void LogWidget::onFinished(int fileId) {
    if (fileId != m_fileId) {
        return;
    }

    m_refreshTimer->stop();
    if (m_progressBar->minimum() == 0 && m_progressBar->maximum() == 0) {
        m_progressBar->setRange(0, 100);
    }
    m_progressBar->setValue(100);

    fillBuffer();
    updateScrollBar();
    applyBufferToView();

    updateStatusLabel();
    emit loadingFinished(fileId);
    qInfo() << "LogWidget: Loading finished for fileId" << fileId;
}

void LogWidget::onError(int fileId, QString message) {
    if (fileId != m_fileId) {
        return;
    }
    m_refreshTimer->stop();
    m_labelState->setText("Error: " + message);
    qCritical() << "LogWidget: Error for fileId" << fileId << ":" << message;
}

void LogWidget::onApplyFilters() {
    const QString query = m_searchCombo ? m_searchCombo->currentText().trimmed() : QString();
    if (query.isEmpty()) {
        m_ftsFilter.clear();
        if (m_searchStatus) {
            m_searchStatus->setText("");
        }
        saveSettings();
        setPointer(0, true);
        return;
    }

    rememberComboText(m_searchCombo, m_ftsFilterHistory, "logWidget/ftsFilterHistory");
    m_ftsFilter = query;
    const int line = LogDatabase::instance().findMatchLine(m_fileId, QString(), query, 0, false);
    if (line < 0) {
        if (m_searchStatus) {
            m_searchStatus->setText("No matches");
        }
        setPointer(0, true);
        return;
    }

    if (m_searchStatus) {
        m_searchStatus->setText(QString("Filter active from line %1").arg(line + 1));
    }
    setPointer(line, true);
    saveSettings();
}

void LogWidget::onWrapToggled(bool checked) {
    m_textBrowser->setLineWrapMode(checked ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);
    saveSettings();
    QTimer::singleShot(0, this, [this]() { setPointer(m_bufferPointer, true); });
}

void LogWidget::refreshData() {
    setPointer(m_bufferPointer, true);
}

bool LogWidget::includeMetadataInRows() const {
    return sortByTimestamp()
        || (m_showTimestampCheck && m_showTimestampCheck->isChecked())
        || (m_showLogLevelCheck && m_showLogLevelCheck->isChecked());
}

bool LogWidget::sortByTimestamp() const {
    return m_sortTimestampCheck && m_sortTimestampCheck->isChecked();
}

int LogWidget::currentRowSpace() const {
    return sortByTimestamp()
        ? LogDatabase::instance().timestampRowCount(m_fileId, m_ftsFilter)
        : static_cast<int>(m_totalLines);
}

int LogWidget::visibleRowCount() const {
    if (!m_textBrowser) {
        return 1;
    }

    const int lineHeight = qMax(1, QFontMetrics(m_textBrowser->font()).lineSpacing());
    return qMax(1, (m_textBrowser->viewport()->height() / lineHeight) + 1);
}

void LogWidget::updateScrollBar() {
    if (!m_logScrollBar) {
        return;
    }

    const int visibleRows = visibleRowCount();
    const bool hasFilter = !m_ftsFilter.trimmed().isEmpty();
    const int rowSpace = currentRowSpace();
    m_logScrollBar->blockSignals(true);
    m_logScrollBar->setPageStep(visibleRows);
    m_logScrollBar->setMaximum(qMax(0, rowSpace - ((hasFilter && !sortByTimestamp()) ? 1 : visibleRows)));
    m_logScrollBar->setValue(qBound(0, m_bufferPointer, m_logScrollBar->maximum()));
    m_logScrollBar->blockSignals(false);
}

void LogWidget::setPointer(int p, bool force, bool backwards) {
    const bool hasFilter = !m_ftsFilter.trimmed().isEmpty();
    const int rowSpace = currentRowSpace();
    int maxP = qMax(0, rowSpace - ((hasFilter && !sortByTimestamp()) ? 1 : visibleRowCount()));
    p = qBound(0, p, maxP);

    if (hasFilter && !sortByTimestamp()) {
        const int filtered = filteredLineAt(p, backwards);
        if (filtered >= 0) {
            p = qBound(0, filtered, maxP);
        }
    }

    int delta = p - m_bufferPointer;
    if (!force && delta == 0) {
        return;
    }

    m_bufferPointer = p;
    fillBuffer();
    updateScrollBar();

    applyBufferToView();
    updateStatusLabel();
}

int LogWidget::filteredLineAt(int lineNumber, bool backwards) const {
    const QString filter = m_ftsFilter.trimmed();
    if (filter.isEmpty()) {
        return qBound(0, lineNumber, qMax(0, static_cast<int>(m_totalLines) - 1));
    }

    const int bounded = qBound(0, lineNumber, qMax(0, static_cast<int>(m_totalLines) - 1));
    int line = LogDatabase::instance().findFilteredLine(m_fileId, filter, bounded, backwards);
    if (line < 0) {
        line = LogDatabase::instance().findFilteredLine(m_fileId, filter, bounded, !backwards);
    }
    return line;
}

void LogWidget::moveFiltered(int steps) {
    if (steps == 0) {
        return;
    }

    if (m_ftsFilter.trimmed().isEmpty()) {
        setPointer(m_bufferPointer + steps);
        return;
    }

    if (sortByTimestamp()) {
        setPointer(m_bufferPointer + steps);
        return;
    }

    const bool backwards = steps < 0;
    int line = m_bufferPointer;
    int remaining = qAbs(steps);
    while (remaining-- > 0) {
        const int candidate = LogDatabase::instance().findFilteredLine(
            m_fileId,
            m_ftsFilter,
            line + (backwards ? -1 : 1),
            backwards);
        if (candidate < 0 || candidate == line) {
            break;
        }
        line = candidate;
    }

    setPointer(line, false, backwards);
}

void LogWidget::fillBuffer() {
    int bufN = visibleRowCount();
    QStringList headers;
    LogDatabase::instance().queryRows(
        m_fileId,
        m_bufferPointer,
        bufN,
        m_ftsFilter,
        includeMetadataInRows(),
        sortByTimestamp(),
        m_buffer,
        headers);
    m_bufferHeaders = headers;
}

void LogWidget::applyBufferToView() {
    const int horizontalScrollValue = (m_textBrowser && m_textBrowser->horizontalScrollBar())
        ? m_textBrowser->horizontalScrollBar()->value()
        : 0;

    QMap<QString, int> jsonFieldWidths;
    if (m_jsonHelperCheck && m_jsonHelperCheck->isChecked()
        && m_jsonCompactCheck && m_jsonCompactCheck->isChecked()) {
        const int lineNumberIdx = m_bufferHeaders.indexOf("line_number");
        const auto store = LogLineStoreRegistry::instance().store(m_fileId);
        if (lineNumberIdx >= 0 && store) {
            for (const auto& row : m_buffer) {
                if (lineNumberIdx >= row.size()) {
                    continue;
                }
                const auto fields = jsonFieldsForRaw(store->lineText(row[lineNumberIdx].toInt()));
                for (const auto& field : fields) {
                    jsonFieldWidths[field.first] = qMax(jsonFieldWidths.value(field.first), field.first.size());
                }
            }
        }
    }

    QString html;
    html.reserve(m_buffer.size() * 320);
    html += "<html><body style=\"margin:0; font-family:'Monospace'; font-size:9pt;\">";
    html += QString("<div style=\"white-space:%1;\">")
                .arg(m_wrapCheck && m_wrapCheck->isChecked() ? "pre-wrap" : "pre");
    for (const auto& row : m_buffer) {
        html += buildRowHtml(row, jsonFieldWidths);
    }
    html += "</div></body></html>";

    m_textBrowser->setHtml(html);

    if (m_textBrowser) {
        m_textBrowser->setExtraSelections({});
        QTimer::singleShot(0, this, [this, horizontalScrollValue]() {
            if (!m_textBrowser || !m_textBrowser->horizontalScrollBar()) {
                return;
            }
            auto* bar = m_textBrowser->horizontalScrollBar();
            bar->setValue(qBound(0, horizontalScrollValue, bar->maximum()));
        });
    }
}

QString LogWidget::buildRowHtml(const QVector<QString>& row, const QMap<QString, int>& jsonFieldWidths) const {
    const int lineNumberIdx = m_bufferHeaders.indexOf("line_number");
    const int timestampIdx = m_bufferHeaders.indexOf("timestamp_text");
    const int levelIdx = m_bufferHeaders.indexOf("level");

    QStringList parts;
    if (m_showLineNumberCheck && m_showLineNumberCheck->isChecked() && lineNumberIdx >= 0 && lineNumberIdx < row.size()) {
        bool ok = false;
        const int lineNumber = row[lineNumberIdx].toInt(&ok);
        const QString displayNumber = ok
            ? QString::number(lineNumber + 1).rightJustified(6, '0')
            : row[lineNumberIdx].rightJustified(6, '0');
        parts << QString("<span style=\"color:#7f8fa6; font-weight:600;\">%1</span>")
                     .arg(displayNumber.toHtmlEscaped());
    }

    if (m_showTimestampCheck && m_showTimestampCheck->isChecked()
        && timestampIdx >= 0 && timestampIdx < row.size() && !row[timestampIdx].isEmpty()) {
        parts << QString("<span style=\"color:#6c757d;\">%1</span>")
                     .arg(row[timestampIdx].toHtmlEscaped());
    }

    if (m_showLogLevelCheck && m_showLogLevelCheck->isChecked()
        && levelIdx >= 0 && levelIdx < row.size()) {
        const LogLevel level = static_cast<LogLevel>(row[levelIdx].toInt());
        const QString levelText = logLevelToString(level).toUpper();
        if (!levelText.isEmpty()) {
            QString color = "#6c757d";
            if (level == LogLevel::Warn) {
                color = "#b7791f";
            } else if (level == LogLevel::Error || level == LogLevel::Fatal) {
                color = "#c53030";
            } else if (level == LogLevel::Info) {
                color = "#2b6cb0";
            } else if (level == LogLevel::Debug || level == LogLevel::Trace) {
                color = "#718096";
            }
            parts << QString("<span style=\"color:%1; font-weight:700;\">%2</span>")
                         .arg(color, levelText.toHtmlEscaped());
        }
    }

    QString prefix;
    if (!parts.isEmpty()) {
        prefix = parts.join(" <span style=\"color:#586e75;\">|</span> ");
        prefix += " <span style=\"color:#586e75;\">|</span> ";
    }

    QString rawText;
    if (lineNumberIdx >= 0 && lineNumberIdx < row.size()) {
        if (const auto store = LogLineStoreRegistry::instance().store(m_fileId)) {
            rawText = store->lineText(row[lineNumberIdx].toInt());
        }
    }
    const QString display = formatJsonLine(rawText, jsonFieldWidths);
    const QString raw = highlightFindWords(display);
    return prefix + raw + "\n";
}

QString LogWidget::formatJsonLine(const QString& raw, const QMap<QString, int>& jsonFieldWidths) const {
    if (!m_jsonHelperCheck || !m_jsonHelperCheck->isChecked()) {
        return raw;
    }

    const auto fields = jsonFieldsForRaw(raw);
    if (fields.isEmpty()) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            return QString();
        }
        return raw;
    }

    if (m_jsonOnlyValuesCheck && m_jsonOnlyValuesCheck->isChecked()) {
        QStringList values;
        values.reserve(fields.size());
        for (const auto& field : fields) {
            values << field.second;
        }
        return values.join("  ");
    }

    if (!m_jsonCompactCheck || !m_jsonCompactCheck->isChecked()) {
        return jsonFilterToCompactObject(fields);
    }

    QStringList parts;
    parts.reserve(fields.size());
    for (const auto& field : fields) {
        const int width = jsonFieldWidths.value(field.first, field.first.size());
        parts << QString("%1=%2").arg(field.first.leftJustified(width, ' '), field.second);
    }
    return parts.join("  ");
}

QVector<QPair<QString, QString>> LogWidget::jsonFieldsForRaw(const QString& raw) const {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return {};
    }

    QVector<QPair<QString, QString>> fields;
    const QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        flattenJsonValue(it.key(), it.value(), fields);
    }

    QVector<QPair<QString, QString>> filtered;
    filtered.reserve(fields.size());
    for (const auto& field : fields) {
        if (jsonFieldAllowed(field.first)) {
            filtered << field;
        }
    }
    return filtered;
}

void LogWidget::flattenJsonValue(const QString& path, const QJsonValue& value, QVector<QPair<QString, QString>>& out) const {
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        if (obj.isEmpty()) {
            out << qMakePair(path, QString("{}"));
            return;
        }
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            flattenJsonValue(path + "." + it.key(), it.value(), out);
        }
        return;
    }

    out << qMakePair(path, jsonValueToText(value));
}

QString LogWidget::jsonValueToText(const QJsonValue& value) const {
    if (value.isString()) {
        QString text = value.toString();
        const bool simple = !text.isEmpty()
            && text.indexOf(QRegularExpression("[\\s=,{}\\[\\]\"]")) < 0;
        text.replace('\\', "\\\\");
        text.replace('"', "\\\"");
        return simple ? text : QString("\"%1\"").arg(text);
    }
    if (value.isBool()) {
        return value.toBool() ? "true" : "false";
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble(), 'g', 15);
    }
    if (value.isNull() || value.isUndefined()) {
        return "null";
    }

    QJsonDocument doc;
    if (value.isArray()) {
        doc = QJsonDocument(value.toArray());
    } else if (value.isObject()) {
        doc = QJsonDocument(value.toObject());
    }
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

QString LogWidget::jsonFilterToCompactObject(const QVector<QPair<QString, QString>>& fields) const {
    QJsonObject obj;
    for (const auto& field : fields) {
        obj.insert(field.first, field.second);
    }
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QStringList LogWidget::jsonFilterTokens(bool excludes) const {
    if (!m_jsonFieldFilterCombo) {
        return {};
    }

    QStringList tokens;
    for (QString token : splitJsonFilterTokens(m_jsonFieldFilterCombo->currentText())) {
        const bool isExclude = token.startsWith('-');
        if (isExclude) {
            token.remove(0, 1);
        }
        token = token.trimmed();
        if (!token.isEmpty() && isExclude == excludes) {
            tokens << token;
        }
    }
    return tokens;
}

bool LogWidget::jsonFieldAllowed(const QString& path) const {
    const QStringList includes = jsonFilterTokens(false);
    const QStringList excludes = jsonFilterTokens(true);

    bool included = includes.isEmpty();
    for (const QString& include : includes) {
        if (jsonPathMatches(path, include)) {
            included = true;
            break;
        }
    }
    if (!included) {
        return false;
    }

    for (const QString& exclude : excludes) {
        if (jsonPathMatches(path, exclude)) {
            return false;
        }
    }
    return true;
}

bool LogWidget::jsonPathMatches(const QString& path, const QString& pattern) const {
    if (pattern.endsWith(".*")) {
        const QString prefix = pattern.left(pattern.size() - 1);
        return path.startsWith(prefix, Qt::CaseInsensitive);
    }
    return path.compare(pattern, Qt::CaseInsensitive) == 0;
}

QString LogWidget::highlightFindWords(const QString& raw) const {
    if (raw.isEmpty() || m_findWords.isEmpty()) {
        return raw.toHtmlEscaped();
    }

    QVector<QPair<int, int>> ranges;
    for (const QString& word : m_findWords) {
        int pos = 0;
        while ((pos = raw.indexOf(word, pos, Qt::CaseInsensitive)) >= 0) {
            ranges << qMakePair(pos, pos + word.size());
            pos += qMax(1, word.size());
        }
    }

    if (ranges.isEmpty()) {
        return raw.toHtmlEscaped();
    }

    std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b) {
        return a.first == b.first ? a.second < b.second : a.first < b.first;
    });

    QVector<QPair<int, int>> merged;
    for (const auto& range : ranges) {
        if (merged.isEmpty() || range.first > merged.last().second) {
            merged << range;
        } else {
            merged.last().second = qMax(merged.last().second, range.second);
        }
    }

    QString html;
    html.reserve(raw.size() + merged.size() * 20);
    int cursor = 0;
    for (const auto& range : merged) {
        html += raw.mid(cursor, range.first - cursor).toHtmlEscaped();
        html += "<span style=\"background-color:#fff59d; color:#111111;\">";
        html += raw.mid(range.first, range.second - range.first).toHtmlEscaped();
        html += "</span>";
        cursor = range.second;
    }
    html += raw.mid(cursor).toHtmlEscaped();
    return html;
}

QStringList LogWidget::currentFindWords() const {
    if (!m_textFindCombo) {
        return {};
    }

    QStringList words;
    const QStringList parts = m_textFindCombo->currentText().simplified().split(' ', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        if (!words.contains(part, Qt::CaseInsensitive)) {
            words << part;
        }
    }
    return words;
}

void LogWidget::loadSettings() {
    QSettings settings("Logalizer", "Logalizer");
    m_searchHistoryLimit = AppSettings::searchHistoryLimit();

    loadComboHistory(m_searchCombo, m_ftsFilterHistory, "logWidget/ftsFilterHistory");
    loadComboHistory(m_jsonFieldFilterCombo, m_jsonFieldFilterHistory, "logWidget/jsonFieldFilterHistory");
    loadComboHistory(m_textFindCombo, m_textFindHistory, "logWidget/textFindHistory");

    if (m_wrapCheck) {
        m_wrapCheck->setChecked(settings.value("logWidget/wrap", false).toBool());
    }
    if (m_showLineNumberCheck) {
        m_showLineNumberCheck->setChecked(settings.value("logWidget/showLineNumbers", true).toBool());
    }
    if (m_showTimestampCheck) {
        m_showTimestampCheck->setChecked(settings.value("logWidget/showTimestamp", false).toBool());
    }
    if (m_showLogLevelCheck) {
        m_showLogLevelCheck->setChecked(settings.value("logWidget/showLogLevel", false).toBool());
    }
    if (m_sortTimestampCheck) {
        m_sortTimestampCheck->setChecked(settings.value("logWidget/sortByTimestamp", false).toBool());
    }
    const AppSettingsValues appSettings = AppSettings::load();
    if (m_jsonHelperCheck) {
        m_jsonHelperCheck->setChecked(appSettings.jsonEnabled);
    }
    if (m_jsonCompactCheck) {
        m_jsonCompactCheck->setChecked(appSettings.jsonCompact);
    }
    if (m_jsonOnlyValuesCheck) {
        m_jsonOnlyValuesCheck->setChecked(appSettings.jsonOnlyValues);
    }
    if (m_jsonFieldFilterCombo) {
        m_jsonFieldFilterCombo->setCurrentText(appSettings.jsonFieldFilter);
    }
}

void LogWidget::saveSettings() const {
    QSettings settings("Logalizer", "Logalizer");

    if (m_wrapCheck) {
        settings.setValue("logWidget/wrap", m_wrapCheck->isChecked());
    }
    if (m_showLineNumberCheck) {
        settings.setValue("logWidget/showLineNumbers", m_showLineNumberCheck->isChecked());
    }
    if (m_showTimestampCheck) {
        settings.setValue("logWidget/showTimestamp", m_showTimestampCheck->isChecked());
    }
    if (m_showLogLevelCheck) {
        settings.setValue("logWidget/showLogLevel", m_showLogLevelCheck->isChecked());
    }
    if (m_sortTimestampCheck) {
        settings.setValue("logWidget/sortByTimestamp", m_sortTimestampCheck->isChecked());
    }
    if (m_jsonHelperCheck) {
        settings.setValue("logWidget/jsonEnabled", m_jsonHelperCheck->isChecked());
    }
    if (m_jsonCompactCheck) {
        settings.setValue("logWidget/jsonCompact", m_jsonCompactCheck->isChecked());
    }
    if (m_jsonOnlyValuesCheck) {
        settings.setValue("logWidget/jsonOnlyValues", m_jsonOnlyValuesCheck->isChecked());
    }
    if (m_jsonFieldFilterCombo) {
        settings.setValue("logWidget/jsonFieldFilter", m_jsonFieldFilterCombo->currentText().trimmed());
    }
    settings.setValue("logWidget/ftsFilterHistory", m_ftsFilterHistory);
    settings.setValue("logWidget/jsonFieldFilterHistory", m_jsonFieldFilterHistory);
    settings.setValue("logWidget/textFindHistory", m_textFindHistory);
}

void LogWidget::loadComboHistory(QComboBox* combo, QStringList& history, const QString& settingsKey) {
    if (!combo) {
        return;
    }

    QSettings settings("Logalizer", "Logalizer");
    history = settings.value(settingsKey).toStringList();
    history.removeAll(QString());

    combo->blockSignals(true);
    const QString current = combo->currentText();
    combo->clear();
    combo->addItems(history);
    combo->setCurrentText(current);
    combo->blockSignals(false);
}

void LogWidget::rememberComboText(QComboBox* combo, QStringList& history, const QString& settingsKey) {
    if (!combo) {
        return;
    }

    const QString text = combo->currentText().trimmed();
    if (text.isEmpty()) {
        return;
    }

    history.removeAll(text);
    history.prepend(text);
    const int limit = qMax(1, m_searchHistoryLimit);
    while (history.size() > limit) {
        history.removeLast();
    }

    combo->blockSignals(true);
    combo->clear();
    combo->addItems(history);
    combo->setCurrentText(text);
    combo->blockSignals(false);

    QSettings settings("Logalizer", "Logalizer");
    settings.setValue(settingsKey, history);
}

void LogWidget::updateStatusLabel() {
    const int rowSpace = currentRowSpace();
    if (rowSpace == 0 || m_buffer.isEmpty()) {
        if (sortByTimestamp()) {
            m_labelState->setText("Timestamp rows 0-0 of 0");
        } else {
            m_labelState->setText(m_ftsFilter.isEmpty() ? "Rows 0-0 of 0" : "Filter: no rows at this position");
        }
        return;
    }

    const int lastLineIdx = m_bufferHeaders.indexOf("line_number");
    int firstRow = m_bufferPointer + 1;
    int lastRow = firstRow + m_buffer.size() - 1;
    if (!sortByTimestamp() && lastLineIdx >= 0 && !m_buffer.isEmpty() && lastLineIdx < m_buffer.first().size()) {
        firstRow = m_buffer.first()[lastLineIdx].toInt() + 1;
    }
    if (!sortByTimestamp() && lastLineIdx >= 0 && !m_buffer.isEmpty() && lastLineIdx < m_buffer.last().size()) {
        lastRow = m_buffer.last()[lastLineIdx].toInt() + 1;
    }
    const QString prefix = sortByTimestamp()
        ? "Timestamp rows"
        : (m_ftsFilter.isEmpty() ? "Rows" : "Filtered rows");
    m_labelState->setText(QString("%1 %2-%3 of %4").arg(prefix).arg(firstRow).arg(lastRow).arg(rowSpace));
}

void LogWidget::updateMetadataStatusLabel() {
    if (!m_metadataStatus) {
        return;
    }

    const MetadataProgress progress = MetadataPipeline::instance().progress(m_fileId);
    if (progress.queuedLines == 0 && progress.droppedLines == 0) {
        m_metadataStatus->setText("Meta: pending");
        return;
    }

    const int percent = progress.queuedLines > 0
        ? static_cast<int>(qMin<qint64>(100, progress.processedLines * 100 / progress.queuedLines))
        : 0;
    QString text = QString("Meta: %1% (%2 tagged)").arg(percent).arg(progress.taggedLines);
    if (progress.droppedLines > 0) {
        text += QString(" partial, %1 dropped").arg(progress.droppedLines);
    }
    m_metadataStatus->setText(text);
}

bool LogWidget::eventFilter(QObject* obj, QEvent* event) {
    if (obj != m_textBrowser) {
        return QWidget::eventFilter(obj, event);
    }

    if (event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        int angle = we->angleDelta().y();
        int steps = -(angle / 15);
        if (steps != 0) {
            moveFiltered(steps);
        }
        return true;
    }

    if (event->type() == QEvent::Resize) {
        QTimer::singleShot(0, this, [this]() { setPointer(m_bufferPointer, true); });
        return QWidget::eventFilter(obj, event);
    }

    if (event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        int pageRows = visibleRowCount();
        switch (ke->key()) {
        case Qt::Key_Down: moveFiltered(1); return true;
        case Qt::Key_Up: moveFiltered(-1); return true;
        case Qt::Key_PageDown: moveFiltered(pageRows); return true;
        case Qt::Key_PageUp: moveFiltered(-pageRows); return true;
        case Qt::Key_Home: setPointer(0, true, false); return true;
        case Qt::Key_End: setPointer(qMax(0, static_cast<int>(m_totalLines) - 1), true, true); return true;
        default: break;
        }
    }

    return QWidget::eventFilter(obj, event);
}

void LogWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    QTimer::singleShot(0, this, [this]() { setPointer(m_bufferPointer, true); });
}

void LogWidget::onToggleTextFindBar() {
    if (!m_textFindBar) {
        return;
    }

    bool wasVisible = m_textFindBar->isVisible();
    m_textFindBar->setVisible(!wasVisible);
    if (!wasVisible) {
        m_textFindCombo->lineEdit()->setFocus();
        m_textFindCombo->lineEdit()->selectAll();
    } else {
        onTextFindClear();
    }
}

void LogWidget::onTextFindSearch() {
    jumpToTextMatch(m_bufferPointer, false, "No match");
}

void LogWidget::jumpToTextMatch(int fromLineNumber, bool backwards, const QString& notFoundText) {
    if (!m_textFindCombo) {
        return;
    }

    const QString query = m_textFindCombo->currentText().trimmed();
    m_findWords = currentFindWords();
    if (query.isEmpty()) {
        m_findWords.clear();
        applyBufferToView();
        if (m_textFindStatus) {
            m_textFindStatus->setText("");
        }
        return;
    }

    rememberComboText(m_textFindCombo, m_textFindHistory, "logWidget/textFindHistory");

    const int boundedFrom = qBound(0, fromLineNumber, qMax(0, static_cast<int>(m_totalLines) - 1));
    const int line = LogDatabase::instance().findTextLine(m_fileId, m_ftsFilter, m_findWords, boundedFrom, backwards);
    if (line < 0) {
        applyBufferToView();
        if (m_textFindStatus) {
            m_textFindStatus->setText(notFoundText);
        }
        return;
    }

    setPointer(line, true);
    if (m_textFindStatus) {
        m_textFindStatus->setText(QString("Found line %1").arg(line + 1));
    }
    saveSettings();
}

void LogWidget::onTextFindNext() {
    jumpToTextMatch(m_bufferPointer + 1, false, "No next match");
}

void LogWidget::onTextFindPrev() {
    jumpToTextMatch(m_bufferPointer - 1, true, "No previous match");
}

void LogWidget::onTextFindFirst() {
    jumpToTextMatch(0, false, "No match");
}

void LogWidget::onTextFindLast() {
    jumpToTextMatch(qMax(0, static_cast<int>(m_totalLines) - 1), true, "No match");
}

void LogWidget::onTextFindClear() {
    if (m_textFindCombo) {
        m_textFindCombo->setCurrentText("");
    }
    m_findWords.clear();
    if (m_textBrowser) {
        m_textBrowser->setExtraSelections({});
    }
    applyBufferToView();
    if (m_textFindStatus) {
        m_textFindStatus->setText("");
    }
}
