#include "logwidget.h"
#include "fileworker.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtLogging>

LogWidget::LogWidget(const QString& filePath, int fileId, QWidget* parent)
    : QWidget(parent)
    , m_filePath(filePath)
    , m_fileId(fileId)
{
    setupUi();

    QFileInfo fi(filePath);
    m_fileSize = fi.size();
    m_labelSize->setText(QString::number(m_fileSize / 1024.0 / 1024.0, 'f', 2) + " MB");

    m_workerThread = new QThread(this);
    m_worker = new FileWorker(filePath, fileId);
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started, m_worker, &FileWorker::start);
    connect(m_worker, &FileWorker::progressUpdate, this, &LogWidget::onProgressUpdate);
    connect(m_worker, &FileWorker::chunkInserted, this, &LogWidget::onChunkInserted);
    connect(m_worker, &FileWorker::finished, this, &LogWidget::onFinished);
    connect(m_worker, &FileWorker::error, this, &LogWidget::onError);
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_workerThread->start();
}

LogWidget::~LogWidget() {
    m_refreshTimer->stop();
    if (m_worker) {
        m_worker->stop();
    }
    if (m_workerThread) {
        m_workerThread->quit();
        if (!m_workerThread->wait(8000)) {
            qWarning() << "LogWidget: Worker thread did not finish, terminating.";
            m_workerThread->terminate();
            m_workerThread->wait(1000);
        }
    }
    LogDatabase::instance().dropTable(m_fileId);
    qInfo() << "LogWidget: Cleaned up fileId" << m_fileId << m_filePath;
}

void LogWidget::setupUi() {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(4, 4, 4, 4);
    m_mainLayout->setSpacing(4);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(REFRESH_DEBOUNCE_MS);
    connect(m_refreshTimer, &QTimer::timeout, this, &LogWidget::refreshData);

    {
        auto* toolBar = new QHBoxLayout();
        m_wrapCheck = new QCheckBox("Wrap", this);
        m_wrapCheck->setToolTip("Toggle word wrap in text view");
        toolBar->addWidget(m_wrapCheck);
        m_showLineNumberCheck = new QCheckBox("Line #", this);
        m_showLineNumberCheck->setToolTip("Show the line number prefix in the text view");
        m_showLineNumberCheck->setChecked(true);
        toolBar->addWidget(m_showLineNumberCheck);
        m_showTimestampCheck = new QCheckBox("Timestamp", this);
        m_showTimestampCheck->setToolTip("Show the detected timestamp prefix in the text view");
        m_showTimestampCheck->setChecked(true);
        toolBar->addWidget(m_showTimestampCheck);
        toolBar->addStretch();
        m_mainLayout->addLayout(toolBar);
        connect(m_wrapCheck, &QCheckBox::toggled, this, &LogWidget::onWrapToggled);
        connect(m_showLineNumberCheck, &QCheckBox::toggled, this, [this](bool) { applyBufferToView(); });
        connect(m_showTimestampCheck, &QCheckBox::toggled, this, [this](bool) { applyBufferToView(); });
    }

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    {
        auto* viewContainer = new QWidget(this);
        auto* viewLayout = new QVBoxLayout(viewContainer);
        viewLayout->setContentsMargins(0, 0, 0, 0);
        viewLayout->setSpacing(2);

        auto* viewBar = new QHBoxLayout();
        auto* contentsLabel = new QLabel("Contents", this);
        contentsLabel->setStyleSheet("font-weight: bold;");
        viewBar->addWidget(contentsLabel);
        viewLayout->addLayout(viewBar);

        m_textFindBar = new QWidget(this);
        m_textFindBar->setVisible(true);
        {
            auto* fl = new QHBoxLayout(m_textFindBar);
            fl->setContentsMargins(2, 2, 2, 2);
            fl->setSpacing(4);

            fl->addWidget(new QLabel("Find:", m_textFindBar));

            m_textFindCombo = new QComboBox(m_textFindBar);
            m_textFindCombo->setEditable(true);
            m_textFindCombo->setInsertPolicy(QComboBox::NoInsert);
            m_textFindCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            m_textFindCombo->lineEdit()->setPlaceholderText("Search in text... (Enter)");
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

            m_textFindRegex = new QCheckBox("Regex", m_textFindBar);
            fl->addWidget(m_textFindRegex);

            m_textFindCase = new QCheckBox("Aa", m_textFindBar);
            fl->addWidget(m_textFindCase);

            m_textFindStatus = new QLabel("", m_textFindBar);
            m_textFindStatus->setMinimumWidth(80);
            fl->addWidget(m_textFindStatus);
        }
        viewLayout->addWidget(m_textFindBar);

        connect(m_textFindCombo->lineEdit(), &QLineEdit::returnPressed, this, &LogWidget::onTextFindSearch);
        connect(m_textFindCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int) { onTextFindSearch(); });
        connect(m_textFindFirst, &QPushButton::clicked, this, &LogWidget::onTextFindFirst);
        connect(m_textFindPrev, &QPushButton::clicked, this, &LogWidget::onTextFindPrev);
        connect(m_textFindNext, &QPushButton::clicked, this, &LogWidget::onTextFindNext);
        connect(m_textFindLast, &QPushButton::clicked, this, &LogWidget::onTextFindLast);
        connect(m_textFindClear, &QPushButton::clicked, this, &LogWidget::onTextFindClear);
        connect(m_textFindRegex, &QCheckBox::toggled, this, [this](bool) { onTextFindSearch(); });
        connect(m_textFindCase, &QCheckBox::toggled, this, [this](bool) { onTextFindSearch(); });

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
        m_logScrollBar->setPageStep(DEFAULT_BUFFER);
        m_logScrollBar->setToolTip("Navigate rows (position = first row in buffer)");
        connect(m_logScrollBar, &QScrollBar::valueChanged, this, [this](int value) {
            setPointer(value);
        });

        auto* contentBox = new QHBoxLayout();
        contentBox->setSpacing(2);
        contentBox->addWidget(m_textBrowser, 1);
        contentBox->addWidget(m_logScrollBar);
        viewLayout->addLayout(contentBox, 1);
        splitter->addWidget(viewContainer);

        m_textBrowser->installEventFilter(this);
    }

    {
        auto* filterContainer = new QWidget(this);
        auto* filterLayout = new QVBoxLayout(filterContainer);
        filterLayout->setContentsMargins(0, 0, 0, 0);
        filterLayout->setSpacing(6);

        filterLayout->addWidget(new QLabel("<b>Search & Time</b>", this));

        filterLayout->addWidget(new QLabel("FTS5:", filterContainer));
        m_searchEdit = new QLineEdit(filterContainer);
        m_searchEdit->setPlaceholderText("Full-text search...");
        m_searchEdit->setToolTip("Full-text search across raw log lines");
        filterLayout->addWidget(m_searchEdit);
        m_searchButton = new QPushButton("Search", filterContainer);
        filterLayout->addWidget(m_searchButton);

        m_fromCheck = new QCheckBox("From", filterContainer);
        filterLayout->addWidget(m_fromCheck);
        m_fromDateTimeEdit = new QDateTimeEdit(QDateTime::currentDateTimeUtc().addDays(-1), filterContainer);
        m_fromDateTimeEdit->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
        m_fromDateTimeEdit->setCalendarPopup(true);
        m_fromDateTimeEdit->setToolTip("Lower timestamp bound (UTC-converted)");
        m_fromDateTimeEdit->setEnabled(false);
        filterLayout->addWidget(m_fromDateTimeEdit);

        m_toCheck = new QCheckBox("To", filterContainer);
        filterLayout->addWidget(m_toCheck);
        m_toDateTimeEdit = new QDateTimeEdit(QDateTime::currentDateTimeUtc(), filterContainer);
        m_toDateTimeEdit->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
        m_toDateTimeEdit->setCalendarPopup(true);
        m_toDateTimeEdit->setToolTip("Upper timestamp bound (inclusive, UTC-converted)");
        m_toDateTimeEdit->setEnabled(false);
        filterLayout->addWidget(m_toDateTimeEdit);

        m_onlyTimestampedCheck = new QCheckBox("Only with timestamp", filterContainer);
        filterLayout->addWidget(m_onlyTimestampedCheck);

        filterLayout->addWidget(new QLabel("Sort by:", filterContainer));
        m_sortCombo = new QComboBox(filterContainer);
        m_sortCombo->addItems({"Line Number", "Timestamp"});
        filterLayout->addWidget(m_sortCombo);

        filterLayout->addWidget(new QLabel("Order:", filterContainer));
        m_sortOrderCombo = new QComboBox(filterContainer);
        m_sortOrderCombo->addItems({"Ascending", "Descending"});
        filterLayout->addWidget(m_sortOrderCombo);

        auto* applyBtn = new QPushButton("Apply", filterContainer);
        filterLayout->addWidget(applyBtn);
        filterLayout->addStretch();

        connect(m_searchButton, &QPushButton::clicked, this, &LogWidget::onApplyFilters);
        connect(m_searchEdit, &QLineEdit::returnPressed, this, &LogWidget::onApplyFilters);
        connect(applyBtn, &QPushButton::clicked, this, &LogWidget::onApplyFilters);
        connect(m_fromCheck, &QCheckBox::toggled, m_fromDateTimeEdit, &QWidget::setEnabled);
        connect(m_toCheck, &QCheckBox::toggled, m_toDateTimeEdit, &QWidget::setEnabled);
        connect(m_fromCheck, &QCheckBox::toggled, this, [this](bool) { onApplyFilters(); });
        connect(m_toCheck, &QCheckBox::toggled, this, [this](bool) { onApplyFilters(); });
        connect(m_onlyTimestampedCheck, &QCheckBox::toggled, this, [this](bool) { onApplyFilters(); });
        connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { onApplyFilters(); });
        connect(m_sortOrderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { onApplyFilters(); });
        connect(m_fromDateTimeEdit, &QDateTimeEdit::dateTimeChanged, this, [this](const QDateTime&) {
            if (m_fromCheck && m_fromCheck->isChecked()) {
                onApplyFilters();
            }
        });
        connect(m_toDateTimeEdit, &QDateTimeEdit::dateTimeChanged, this, [this](const QDateTime&) {
            if (m_toCheck && m_toCheck->isChecked()) {
                onApplyFilters();
            }
        });

        splitter->addWidget(filterContainer);
    }

    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);
    m_mainLayout->addWidget(splitter, 1);

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

        statusBar->addWidget(new QLabel("Buffer:", this));
        m_bufferSizeSpin = new QSpinBox(this);
        m_bufferSizeSpin->setRange(100, 50000);
        m_bufferSizeSpin->setValue(DEFAULT_BUFFER);
        m_bufferSizeSpin->setSingleStep(100);
        m_bufferSizeSpin->setMaximumWidth(80);
        statusBar->addWidget(m_bufferSizeSpin);
        connect(m_bufferSizeSpin, &QSpinBox::editingFinished, this, [this]() {
            m_logScrollBar->setPageStep(m_bufferSizeSpin->value());
            setPointer(m_bufferPointer, true);
        });

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
}

void LogWidget::onProgressUpdate(int fileId, qint64 bytesProcessed, qint64 totalBytes, qint32 linesProcessed) {
    if (fileId != m_fileId) {
        return;
    }

    int percent = totalBytes > 0 ? static_cast<int>(bytesProcessed * 100 / totalBytes) : 0;
    m_progressBar->setValue(percent);
    m_labelLines->setText(QString::number(linesProcessed));
    m_totalLines = linesProcessed;
}

void LogWidget::onChunkInserted(int fileId, qint32 totalLinesInserted) {
    if (fileId != m_fileId) {
        return;
    }

    m_totalLines = totalLinesInserted;
    m_labelLines->setText(QString::number(totalLinesInserted));
    m_refreshTimer->start();
}

void LogWidget::onFinished(int fileId) {
    if (fileId != m_fileId) {
        return;
    }

    m_refreshTimer->stop();
    m_progressBar->setValue(100);

    fillBuffer();
    if (m_logScrollBar) {
        m_logScrollBar->blockSignals(true);
        m_logScrollBar->setMaximum(qMax(0, m_totalRowCount - 1));
        m_logScrollBar->blockSignals(false);
    }
    applyBufferToView();

    m_labelState->setText(QString("Ready - %1 rows").arg(m_totalRowCount));
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
    setPointer(0, true);
}

void LogWidget::onWrapToggled(bool checked) {
    m_textBrowser->setLineWrapMode(checked ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);
}

void LogWidget::refreshData() {
    setPointer(m_bufferPointer, true);
}

qint64 LogWidget::currentFromTimestampMs() const {
    if (!m_fromCheck || !m_fromCheck->isChecked() || !m_fromDateTimeEdit) {
        return -1;
    }
    qint64 fromMs = m_fromDateTimeEdit->dateTime().toUTC().toMSecsSinceEpoch();
    qint64 toMs = currentToTimestampMs();
    if (toMs >= 0 && fromMs > toMs) {
        return toMs;
    }
    return fromMs;
}

qint64 LogWidget::currentToTimestampMs() const {
    if (!m_toCheck || !m_toCheck->isChecked() || !m_toDateTimeEdit) {
        return -1;
    }
    qint64 toMs = m_toDateTimeEdit->dateTime().toUTC().toMSecsSinceEpoch() + 999;
    qint64 fromMs = -1;
    if (m_fromCheck && m_fromCheck->isChecked() && m_fromDateTimeEdit) {
        fromMs = m_fromDateTimeEdit->dateTime().toUTC().toMSecsSinceEpoch();
    }
    if (fromMs >= 0 && fromMs > toMs) {
        return fromMs;
    }
    return toMs;
}

bool LogWidget::onlyWithTimestamp() const {
    return m_onlyTimestampedCheck && m_onlyTimestampedCheck->isChecked();
}

SortMode LogWidget::currentSortMode() const {
    return (m_sortCombo && m_sortCombo->currentIndex() == 1) ? SortMode::Timestamp : SortMode::LineNumber;
}

SortOrder LogWidget::currentSortOrder() const {
    return (m_sortOrderCombo && m_sortOrderCombo->currentIndex() == 1) ? SortOrder::Descending : SortOrder::Ascending;
}

void LogWidget::queryRows(int offset, int limit, QVector<QVector<QString>>& outRows, int& totalCount) const {
    QStringList headers;
    LogDatabase::instance().queryRows(
        m_fileId,
        offset,
        limit,
        m_searchEdit ? m_searchEdit->text().trimmed() : QString(),
        currentFromTimestampMs(),
        currentToTimestampMs(),
        onlyWithTimestamp(),
        currentSortMode(),
        currentSortOrder(),
        outRows,
        headers,
        totalCount);
}

void LogWidget::setPointer(int p, bool force) {
    int bufN = m_bufferSizeSpin ? m_bufferSizeSpin->value() : DEFAULT_BUFFER;
    int maxP = qMax(0, m_totalRowCount - 1);
    p = qBound(0, p, maxP);

    int delta = p - m_bufferPointer;
    if (!force && delta == 0) {
        return;
    }

    m_bufferPointer = p;

    if (force || qAbs(delta) >= bufN) {
        fillBuffer();
    } else {
        updateBufferDelta(delta);
    }

    if (m_logScrollBar) {
        m_logScrollBar->blockSignals(true);
        m_logScrollBar->setMaximum(qMax(0, m_totalRowCount - 1));
        m_logScrollBar->setValue(m_bufferPointer);
        m_logScrollBar->blockSignals(false);
    }

    applyBufferToView();
    updateStatusLabel();
    checkPrefetch();
}

void LogWidget::checkPrefetch() {
    if (m_buffer.isEmpty() || m_totalRowCount == 0) {
        return;
    }

    int bufN = m_bufferSizeSpin ? m_bufferSizeSpin->value() : DEFAULT_BUFFER;

    int bufferEnd = m_bufferPointer + m_buffer.size();
    int rowsBelow = m_totalRowCount - bufferEnd;
    if (rowsBelow > 0) {
        int distToBottomEdge = m_buffer.size() - bufN;
        if (distToBottomEdge < PREFETCH_MARGIN) {
            int fetchCount = qMin(PREFETCH_MARGIN, rowsBelow);
            QVector<QVector<QString>> newRows;
            int total = 0;
            queryRows(bufferEnd, fetchCount, newRows, total);
            m_totalRowCount = total;
            for (const auto& row : newRows) {
                m_buffer.append(row);
            }

            int maxBuf = bufN + 2 * PREFETCH_MARGIN;
            if (m_buffer.size() > maxBuf) {
                int trim = m_buffer.size() - maxBuf;
                m_buffer.remove(0, trim);
                m_bufferPointer += trim;
            }
        }
    }

    if (m_bufferPointer > 0) {
        int distToTopEdge = m_bufferPointer;
        if (distToTopEdge < PREFETCH_MARGIN) {
            int fetchCount = qMin(PREFETCH_MARGIN, m_bufferPointer);
            int fetchStart = m_bufferPointer - fetchCount;
            QVector<QVector<QString>> newRows;
            int total = 0;
            queryRows(fetchStart, fetchCount, newRows, total);
            m_totalRowCount = total;
            newRows.append(m_buffer);
            m_buffer = newRows;
            m_bufferPointer = fetchStart;

            int maxBuf = bufN + 2 * PREFETCH_MARGIN;
            if (m_buffer.size() > maxBuf) {
                m_buffer.resize(maxBuf);
            }
        }
    }
}

void LogWidget::fillBuffer() {
    int bufN = m_bufferSizeSpin ? m_bufferSizeSpin->value() : DEFAULT_BUFFER;
    int total = 0;
    QStringList headers;
    LogDatabase::instance().queryRows(
        m_fileId,
        m_bufferPointer,
        bufN,
        m_searchEdit ? m_searchEdit->text().trimmed() : QString(),
        currentFromTimestampMs(),
        currentToTimestampMs(),
        onlyWithTimestamp(),
        currentSortMode(),
        currentSortOrder(),
        m_buffer,
        headers,
        total);
    m_bufferHeaders = headers;
    m_totalRowCount = total;
}

void LogWidget::updateBufferDelta(int delta) {
    int bufN = m_bufferSizeSpin ? m_bufferSizeSpin->value() : DEFAULT_BUFFER;
    int absDelta = qAbs(delta);
    int oldN = m_buffer.size();

    QVector<QVector<QString>> newRows;
    int total = 0;

    if (delta > 0) {
        int keep = qMax(0, oldN - absDelta);
        int fetchOffset = m_bufferPointer + keep;
        int fetchCount = bufN - keep;

        if (fetchCount > 0) {
            queryRows(fetchOffset, fetchCount, newRows, total);
        }

        if (absDelta < oldN) {
            m_buffer.remove(0, absDelta);
        } else {
            m_buffer.clear();
        }

        for (const auto& row : newRows) {
            m_buffer.append(row);
        }
    } else {
        int keep = qMax(0, oldN - absDelta);
        int fetchCount = bufN - keep;

        m_buffer.resize(keep);

        if (fetchCount > 0) {
            queryRows(m_bufferPointer, fetchCount, newRows, total);
        }

        QVector<QVector<QString>> combined = newRows;
        combined.append(m_buffer);
        m_buffer = combined;
    }

    if (total > 0 || (delta != 0 && m_totalRowCount == 0)) {
        m_totalRowCount = total;
    }
}

void LogWidget::applyBufferToView() {
    QString html;
    html.reserve(m_buffer.size() * 320);
    html += "<html><body style=\"margin:0; font-family:'Monospace'; font-size:9pt;\">";
    html += QString("<div style=\"white-space:%1;\">")
                .arg(m_wrapCheck && m_wrapCheck->isChecked() ? "pre-wrap" : "pre");
    for (const auto& row : m_buffer) {
        html += buildRowHtml(row);
    }
    html += "</div></body></html>";

    m_textBrowser->setHtml(html);

    if (m_textFindBar && m_textFindBar->isVisible() && m_textFindCombo && !m_textFindCombo->currentText().isEmpty()) {
        onTextFindSearch();
    } else {
        onTextFindClear();
    }
}

QString LogWidget::buildRowHtml(const QVector<QString>& row) const {
    const int lineNumberIdx = m_bufferHeaders.indexOf("line_number");
    const int rawIdx = m_bufferHeaders.indexOf("raw");
    const int timestampIdx = m_bufferHeaders.indexOf("timestamp_text");

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

    if (m_showTimestampCheck && m_showTimestampCheck->isChecked() && timestampIdx >= 0 && timestampIdx < row.size()) {
        const QString timestamp = row[timestampIdx].trimmed();
        if (!timestamp.isEmpty()) {
            parts << QString("<span style=\"color:#2aa198; font-style:italic;\">%1</span>")
                         .arg(timestamp.toHtmlEscaped());
        }
    }

    QString prefix;
    if (!parts.isEmpty()) {
        prefix = parts.join(" <span style=\"color:#586e75;\">|</span> ");
        prefix += " <span style=\"color:#586e75;\">|</span> ";
    }

    const QString raw = (rawIdx >= 0 && rawIdx < row.size()) ? row[rawIdx].toHtmlEscaped() : QString();
    return prefix + raw + "\n";
}

void LogWidget::updateStatusLabel() {
    if (m_totalRowCount == 0 || m_buffer.isEmpty()) {
        m_labelState->setText("Rows 0-0 of 0");
        return;
    }

    int firstRow = m_bufferPointer + 1;
    int lastRow = m_bufferPointer + m_buffer.size();
    m_labelState->setText(QString("Rows %1-%2 of %3").arg(firstRow).arg(lastRow).arg(m_totalRowCount));
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
            setPointer(m_bufferPointer + steps);
        }
        return true;
    }

    if (event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        int bufN = m_bufferSizeSpin ? m_bufferSizeSpin->value() : DEFAULT_BUFFER;
        switch (ke->key()) {
        case Qt::Key_Down: setPointer(m_bufferPointer + 1); return true;
        case Qt::Key_Up: setPointer(m_bufferPointer - 1); return true;
        case Qt::Key_PageDown: setPointer(m_bufferPointer + bufN); return true;
        case Qt::Key_PageUp: setPointer(m_bufferPointer - bufN); return true;
        case Qt::Key_Home: setPointer(0, true); return true;
        case Qt::Key_End: setPointer(qMax(0, m_totalRowCount - 1), true); return true;
        default: break;
        }
    }

    return QWidget::eventFilter(obj, event);
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

static void applyTextFindHighlights(
    QTextBrowser* browser,
    const QList<QTextEdit::ExtraSelection>& all,
    int current)
{
    QList<QTextEdit::ExtraSelection> merged = all;
    if (current >= 0 && current < all.size()) {
        QTextEdit::ExtraSelection active = all[current];
        active.format.setBackground(QColor("#FF9900"));
        active.format.setForeground(QColor("#000000"));
        merged[current] = active;
    }
    browser->setExtraSelections(merged);

    if (current >= 0 && current < all.size()) {
        QTextCursor c = browser->textCursor();
        c.setPosition(all[current].cursor.selectionStart());
        browser->setTextCursor(c);
        browser->ensureCursorVisible();
    }
}

void LogWidget::onTextFindSearch() {
    if (!m_textBrowser || !m_textFindCombo) {
        return;
    }

    m_textFindHighlights.clear();
    m_textFindCurrent = -1;

    QString pattern = m_textFindCombo->currentText();
    if (pattern.isEmpty()) {
        m_textBrowser->setExtraSelections({});
        if (m_textFindStatus) {
            m_textFindStatus->setText("");
        }
        return;
    }

    if (!m_textFindHistory.contains(pattern)) {
        m_textFindHistory.prepend(pattern);
        if (m_textFindHistory.size() > 20) {
            m_textFindHistory.removeLast();
        }
        m_textFindCombo->blockSignals(true);
        m_textFindCombo->clear();
        m_textFindCombo->addItems(m_textFindHistory);
        m_textFindCombo->setCurrentText(pattern);
        m_textFindCombo->blockSignals(false);
    }

    QRegularExpression::PatternOptions opts = QRegularExpression::NoPatternOption;
    if (!m_textFindCase || !m_textFindCase->isChecked()) {
        opts |= QRegularExpression::CaseInsensitiveOption;
    }
    QString regexStr = (m_textFindRegex && m_textFindRegex->isChecked())
        ? pattern
        : QRegularExpression::escape(pattern);
    QRegularExpression re(regexStr, opts);
    if (!re.isValid()) {
        if (m_textFindStatus) {
            m_textFindStatus->setText("Invalid regex");
        }
        return;
    }

    QTextDocument* doc = m_textBrowser->document();
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::Start);

    QTextCharFormat baseFormat;
    baseFormat.setBackground(QColor("#FFE066"));
    baseFormat.setForeground(QColor("#000000"));

    while (true) {
        cursor = doc->find(re, cursor);
        if (cursor.isNull()) {
            break;
        }
        QTextEdit::ExtraSelection sel;
        sel.cursor = cursor;
        sel.format = baseFormat;
        m_textFindHighlights.append(sel);
    }

    int total = m_textFindHighlights.size();
    if (total == 0) {
        m_textBrowser->setExtraSelections({});
        if (m_textFindStatus) {
            m_textFindStatus->setText("No matches");
        }
        return;
    }

    m_textFindCurrent = 0;
    applyTextFindHighlights(m_textBrowser, m_textFindHighlights, m_textFindCurrent);
    if (m_textFindStatus) {
        m_textFindStatus->setText(QString("%1 of %2").arg(1).arg(total));
    }
}

void LogWidget::onTextFindNext() {
    int total = m_textFindHighlights.size();
    if (total == 0) {
        onTextFindSearch();
        return;
    }

    if (m_textFindCurrent == total - 1) {
        int bufferEnd = m_bufferPointer + m_buffer.size();
        if (bufferEnd < m_totalRowCount) {
            int step = qMin(PREFETCH_MARGIN, m_totalRowCount - bufferEnd);
            setPointer(m_bufferPointer + step, true);
            if (!m_textFindHighlights.isEmpty()) {
                m_textFindCurrent = 0;
                applyTextFindHighlights(m_textBrowser, m_textFindHighlights, m_textFindCurrent);
                if (m_textFindStatus) {
                    m_textFindStatus->setText(QString("%1 of %2").arg(1).arg(m_textFindHighlights.size()));
                }
            }
            return;
        }
    }

    m_textFindCurrent = (m_textFindCurrent + 1) % total;
    applyTextFindHighlights(m_textBrowser, m_textFindHighlights, m_textFindCurrent);
    if (m_textFindStatus) {
        m_textFindStatus->setText(QString("%1 of %2").arg(m_textFindCurrent + 1).arg(total));
    }
}

void LogWidget::onTextFindPrev() {
    int total = m_textFindHighlights.size();
    if (total == 0) {
        onTextFindSearch();
        return;
    }

    if (m_textFindCurrent == 0) {
        if (m_bufferPointer > 0) {
            int step = qMin(PREFETCH_MARGIN, m_bufferPointer);
            setPointer(m_bufferPointer - step, true);
            if (!m_textFindHighlights.isEmpty()) {
                m_textFindCurrent = m_textFindHighlights.size() - 1;
                applyTextFindHighlights(m_textBrowser, m_textFindHighlights, m_textFindCurrent);
                if (m_textFindStatus) {
                    m_textFindStatus->setText(QString("%1 of %2").arg(m_textFindCurrent + 1).arg(m_textFindHighlights.size()));
                }
            }
            return;
        }
    }

    m_textFindCurrent = (m_textFindCurrent - 1 + total) % total;
    applyTextFindHighlights(m_textBrowser, m_textFindHighlights, m_textFindCurrent);
    if (m_textFindStatus) {
        m_textFindStatus->setText(QString("%1 of %2").arg(m_textFindCurrent + 1).arg(total));
    }
}

void LogWidget::onTextFindFirst() {
    int total = m_textFindHighlights.size();
    if (total == 0) {
        onTextFindSearch();
        return;
    }

    if (m_bufferPointer > 0) {
        setPointer(0, true);
        if (!m_textFindHighlights.isEmpty()) {
            m_textFindCurrent = 0;
            applyTextFindHighlights(m_textBrowser, m_textFindHighlights, m_textFindCurrent);
            if (m_textFindStatus) {
                m_textFindStatus->setText(QString("1 of %1").arg(m_textFindHighlights.size()));
            }
        }
        return;
    }

    m_textFindCurrent = 0;
    applyTextFindHighlights(m_textBrowser, m_textFindHighlights, m_textFindCurrent);
    if (m_textFindStatus) {
        m_textFindStatus->setText(QString("1 of %1").arg(total));
    }
}

void LogWidget::onTextFindLast() {
    int total = m_textFindHighlights.size();
    if (total == 0) {
        onTextFindSearch();
        return;
    }

    int bufferEnd = m_bufferPointer + m_buffer.size();
    if (bufferEnd < m_totalRowCount) {
        setPointer(qMax(0, m_totalRowCount - 1), true);
        if (!m_textFindHighlights.isEmpty()) {
            m_textFindCurrent = m_textFindHighlights.size() - 1;
            applyTextFindHighlights(m_textBrowser, m_textFindHighlights, m_textFindCurrent);
            if (m_textFindStatus) {
                m_textFindStatus->setText(QString("%1 of %2").arg(m_textFindCurrent + 1).arg(m_textFindHighlights.size()));
            }
        }
        return;
    }

    m_textFindCurrent = total - 1;
    applyTextFindHighlights(m_textBrowser, m_textFindHighlights, m_textFindCurrent);
    if (m_textFindStatus) {
        m_textFindStatus->setText(QString("%1 of %2").arg(total).arg(total));
    }
}

void LogWidget::onTextFindClear() {
    m_textFindHighlights.clear();
    m_textFindCurrent = -1;
    if (m_textBrowser) {
        m_textBrowser->setExtraSelections({});
    }
    if (m_textFindStatus) {
        m_textFindStatus->setText("");
    }
}
