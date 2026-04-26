#include "logwidget.h"
#include "fileworker.h"
#include "logdatabase.h"

#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QCheckBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QSplitter>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <QtLogging>
#include <QRegularExpression>
#include <QShortcut>

// ─── Constructor / Destructor ─────────────────────────────────────────────────

LogWidget::LogWidget(const QString& filePath, int fileId, QWidget *parent)
    : QWidget(parent)
    , m_filePath(filePath)
    , m_fileId(fileId)
{
    setupUi();

    QFileInfo fi(filePath);
    m_fileSize = fi.size();
    m_labelSize->setText(QString::number(m_fileSize / 1024.0 / 1024.0, 'f', 2) + " MB");

    // Worker in separate thread
    m_workerThread = new QThread(this);
    m_worker = new FileWorker(filePath, fileId);
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started,    m_worker, &FileWorker::start);
    connect(m_worker, &FileWorker::schemaReady,   this, &LogWidget::onSchemaReady);
    connect(m_worker, &FileWorker::progressUpdate,this, &LogWidget::onProgressUpdate);
    connect(m_worker, &FileWorker::chunkInserted, this, &LogWidget::onChunkInserted);
    connect(m_worker, &FileWorker::finished,      this, &LogWidget::onFinished);
    connect(m_worker, &FileWorker::error,         this, &LogWidget::onError);
    connect(m_workerThread, &QThread::finished,   m_worker, &QObject::deleteLater);

    m_workerThread->start();
}

LogWidget::~LogWidget() {
    m_refreshTimer->stop();
    if (m_worker) m_worker->stop();          // sets atomic m_stopRequested = true
    if (m_workerThread) {
        m_workerThread->quit();
        // Wait generously; worker checks m_stopRequested every batch (~5000 lines ≈ fast)
        if (!m_workerThread->wait(8000)) {
            qWarning() << "LogWidget: Worker thread did not finish, terminating.";
            m_workerThread->terminate();
            m_workerThread->wait(1000);
        }
    }
    // dropTable runs after thread is guaranteed done → no concurrent inserts
    LogDatabase::instance().dropTable(m_fileId);
    qInfo() << "LogWidget: Cleaned up fileId" << m_fileId << m_filePath;
}

// ─── UI Setup ────────────────────────────────────────────────────────────────

void LogWidget::setupUi() {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(4, 4, 4, 4);
    m_mainLayout->setSpacing(4);

    // ── Debounce timer for chunk-driven refreshes ─────────────────
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(REFRESH_DEBOUNCE_MS);
    connect(m_refreshTimer, &QTimer::timeout, this, &LogWidget::refreshData);

    // ── Toolbar ─────────────────────────────────────────────────────
    {
        auto* toolBar = new QHBoxLayout();
        m_wrapCheck = new QCheckBox("Wrap", this);
        m_wrapCheck->setToolTip("Toggle word wrap in text view");
        toolBar->addWidget(m_wrapCheck);
        toolBar->addStretch();
        m_mainLayout->addLayout(toolBar);

        connect(m_wrapCheck, &QCheckBox::toggled, this, &LogWidget::onWrapToggled);
    }


    // ── Splitter: view area (left) | filter panel (right) ────────────
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // ── Left: text view ──────────────────────────────────────────────
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

        // ── Text Find Bar ──────────────────────────────────────────────
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
            m_textFindFirst->setToolTip("First occurrence");
            fl->addWidget(m_textFindFirst);

            m_textFindPrev = new QPushButton("◀", m_textFindBar);
            m_textFindPrev->setMaximumWidth(28);
            m_textFindPrev->setToolTip("Previous occurrence (Shift+F3)");
            fl->addWidget(m_textFindPrev);

            m_textFindNext = new QPushButton("▶", m_textFindBar);
            m_textFindNext->setMaximumWidth(28);
            m_textFindNext->setToolTip("Next occurrence (F3)");
            fl->addWidget(m_textFindNext);

            m_textFindLast = new QPushButton("⏭", m_textFindBar);
            m_textFindLast->setMaximumWidth(28);
            m_textFindLast->setToolTip("Last occurrence");
            fl->addWidget(m_textFindLast);

            m_textFindClear = new QPushButton("✕ Clear", m_textFindBar);
            m_textFindClear->setToolTip("Clear highlights");
            fl->addWidget(m_textFindClear);

            m_textFindRegex = new QCheckBox("Regex", m_textFindBar);
            m_textFindRegex->setToolTip("Use regular expressions");
            fl->addWidget(m_textFindRegex);

            m_textFindCase = new QCheckBox("Aa", m_textFindBar);
            m_textFindCase->setToolTip("Case-sensitive search");
            fl->addWidget(m_textFindCase);

            m_textFindStatus = new QLabel("", m_textFindBar);
            m_textFindStatus->setMinimumWidth(80);
            fl->addWidget(m_textFindStatus);
        }
        viewLayout->addWidget(m_textFindBar);

        // ── Wire text find bar ───────────────────────────────────────────
        connect(m_textFindCombo->lineEdit(), &QLineEdit::returnPressed,
                this, &LogWidget::onTextFindSearch);
        connect(m_textFindCombo, QOverload<int>::of(&QComboBox::activated),
                this, [this](int) { onTextFindSearch(); });
        connect(m_textFindFirst, &QPushButton::clicked, this, &LogWidget::onTextFindFirst);
        connect(m_textFindPrev,  &QPushButton::clicked, this, &LogWidget::onTextFindPrev);
        connect(m_textFindNext,  &QPushButton::clicked, this, &LogWidget::onTextFindNext);
        connect(m_textFindLast,  &QPushButton::clicked, this, &LogWidget::onTextFindLast);
        connect(m_textFindClear, &QPushButton::clicked, this, &LogWidget::onTextFindClear);
        // Re-search when regex/case toggled
        connect(m_textFindRegex, &QCheckBox::toggled, this, [this](bool) { onTextFindSearch(); });
        connect(m_textFindCase,  &QCheckBox::toggled, this, [this](bool) { onTextFindSearch(); });

        // Ctrl+F shortcut: toggle visibility (bar starts visible; Ctrl+F hides/shows)
        auto* findShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this);
        connect(findShortcut, &QShortcut::activated, this, &LogWidget::onToggleTextFindBar);

        // F3 / Shift+F3 shortcuts
        auto* nextShortcut = new QShortcut(QKeySequence(Qt::Key_F3), this);
        connect(nextShortcut, &QShortcut::activated, this, &LogWidget::onTextFindNext);
        auto* prevShortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3), this);
        connect(prevShortcut, &QShortcut::activated, this, &LogWidget::onTextFindPrev);

        // Escape closes the find bar
        auto* escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        connect(escShortcut, &QShortcut::activated, this, [this]() {
            if (m_textFindBar && m_textFindBar->isVisible())
                m_textFindBar->setVisible(false);
        });


        m_textBrowser = new QTextBrowser(this);
        m_textBrowser->setFont(QFont("Monospace", 9));
        m_textBrowser->setLineWrapMode(QTextEdit::NoWrap);

        // Hide native vertical scrollbar — replaced by m_logScrollBar
        m_textBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        // Custom independent vertical scrollbar
        m_logScrollBar = new QScrollBar(Qt::Vertical, this);
        m_logScrollBar->setMinimum(0);
        m_logScrollBar->setMaximum(0);
        m_logScrollBar->setSingleStep(1);
        m_logScrollBar->setPageStep(DEFAULT_BUFFER);
        m_logScrollBar->setToolTip("Navigate rows (position = first row in buffer)");
        connect(m_logScrollBar, &QScrollBar::valueChanged, this, [this](int value) {
            setPointer(value);
        });

        // Layout: [textBrowser | logScrollBar]
        auto* contentBox = new QHBoxLayout();
        contentBox->setSpacing(2);
        contentBox->addWidget(m_textBrowser, 1);
        contentBox->addWidget(m_logScrollBar);
        viewLayout->addLayout(contentBox, 1);
        splitter->addWidget(viewContainer);

        // Event filter: intercept wheel/arrow keys to move the buffer pointer
        m_textBrowser->installEventFilter(this);
    }

    // ── Right: dynamic filter panel ───────────────────────────────────
    {
        auto* filterContainer = new QWidget(this);
        auto* filterOuterLayout = new QVBoxLayout(filterContainer);
        filterOuterLayout->setContentsMargins(0, 0, 0, 0);
        filterOuterLayout->setSpacing(4);

        filterOuterLayout->addWidget(new QLabel("<b>Column Filters</b>", this));

        // ── FTS5 search row (moved from top bar) ────────────────────────
        {
            auto* ftsRow = new QHBoxLayout();
            ftsRow->setSpacing(4);
            ftsRow->addWidget(new QLabel("FTS5:", filterContainer));
            m_searchEdit = new QLineEdit(filterContainer);
            m_searchEdit->setPlaceholderText("Full-text search...");
            m_searchEdit->setToolTip("Full-text search across all indexed columns (FTS5)");
            ftsRow->addWidget(m_searchEdit, 1);
            m_searchButton = new QPushButton("Search", filterContainer);
            ftsRow->addWidget(m_searchButton);
            filterOuterLayout->addLayout(ftsRow);

            connect(m_searchButton, &QPushButton::clicked,    this, &LogWidget::onApplyFilters);
            connect(m_searchEdit,   &QLineEdit::returnPressed, this, &LogWidget::onApplyFilters);
        }

        // Scroll area contains the filter rows
        auto* scrollArea = new QScrollArea(this);
        scrollArea->setWidgetResizable(true);
        scrollArea->setMinimumWidth(300);

        auto* scrollWidget = new QWidget();
        m_filterLayout = new QVBoxLayout(scrollWidget);
        m_filterLayout->setContentsMargins(4, 4, 4, 4);
        m_filterLayout->setSpacing(2);
        m_filterLayout->addStretch();  // rows inserted before this stretch

        scrollArea->setWidget(scrollWidget);
        filterOuterLayout->addWidget(scrollArea, 1);

        auto* addFilterBtn = new QPushButton("＋ Add Filter", this);
        connect(addFilterBtn, &QPushButton::clicked, this, [this]() { addFilterRow(); });
        filterOuterLayout->addWidget(addFilterBtn);

        auto* applyBtn = new QPushButton("Apply Filters", this);
        connect(applyBtn, &QPushButton::clicked, this, &LogWidget::onApplyFilters);
        filterOuterLayout->addWidget(applyBtn);

        splitter->addWidget(filterContainer);
    }

    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);
    m_mainLayout->addWidget(splitter, 1);

    // ── Status bar ────────────────────────────────────────────────────
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
        m_bufferSizeSpin->setToolTip("Number of rows in the sliding buffer (N)");
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

// ─── Slots ───────────────────────────────────────────────────────────────────

void LogWidget::onSchemaReady(int fileId, QVector<ColumnDef> columns) {
    if (fileId != m_fileId) return;

    m_columns = columns;

    // Build filter column list and update any existing filter rows
    updateFilterColumns();

    // Initial render: show whatever rows are already in the DB
    // (ingestion may still be running; debounce timer handles further updates)
    refreshData();

    qInfo() << "LogWidget: Schema ready for fileId" << fileId
            << "–" << columns.size() << "dynamic columns"
            << "| Text view only";
}

void LogWidget::onProgressUpdate(int fileId, qint64 bytesProcessed, qint64 totalBytes, qint32 linesProcessed) {
    if (fileId != m_fileId) return;

    int percent = (totalBytes > 0) ? static_cast<int>(bytesProcessed * 100 / totalBytes) : 0;
    m_progressBar->setValue(percent);
    m_labelLines->setText(QString::number(linesProcessed));
    m_totalLines = linesProcessed;
}

void LogWidget::onChunkInserted(int fileId, qint32 totalLinesInserted) {
    if (fileId != m_fileId) return;

    m_totalLines = totalLinesInserted;
    m_labelLines->setText(QString::number(totalLinesInserted));

    // Debounce: reset the timer, refresh fires 1.5s after the last chunk.
    // This prevents hammering the UI/mutex during active ingestion.
    m_refreshTimer->start();
}

void LogWidget::onFinished(int fileId) {
    if (fileId != m_fileId) return;

    m_refreshTimer->stop();  // cancel debounce, do final refresh now
    m_progressBar->setValue(100);

    // Full buffer fill (totalRowCount may have grown significantly during ingestion)
    fillBuffer();
    if (m_logScrollBar) {
        m_logScrollBar->blockSignals(true);
        m_logScrollBar->setMaximum(qMax(0, m_totalRowCount - 1));
        m_logScrollBar->blockSignals(false);
    }
    applyBufferToView();

    m_labelState->setText(
        QString("Ready — %1 rows").arg(m_totalRowCount));

    emit loadingFinished(fileId);
    qInfo() << "LogWidget: Loading finished for fileId" << fileId;
}

void LogWidget::onError(int fileId, QString message) {
    if (fileId != m_fileId) return;
    m_refreshTimer->stop();
    m_labelState->setText("Error: " + message);
    qCritical() << "LogWidget: Error for fileId" << fileId << ":" << message;
}

void LogWidget::onApplyFilters() {
    setPointer(0, true);  // reset to start, force full buffer fill
}

void LogWidget::onWrapToggled(bool checked) {
    m_textBrowser->setLineWrapMode(checked ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);
}

// ─── Filter Panel ─────────────────────────────────────────────────────────────

void LogWidget::updateFilterColumns() {
    m_filterColumnNames.clear();
    m_filterColumnNames << "raw" << "line_number";
    for (const auto& col : m_columns)
        m_filterColumnNames << col.name;

    // Refresh column combo in all existing filter rows
    for (auto& row : m_filterRows) {
        if (!row.columnCombo) continue;
        QString current = row.columnCombo->currentText();
        row.columnCombo->blockSignals(true);
        row.columnCombo->clear();
        row.columnCombo->addItems(m_filterColumnNames);
        int idx = row.columnCombo->findText(current);
        row.columnCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        row.columnCombo->blockSignals(false);
    }
}

void LogWidget::addFilterRow(const QString& column, const QString& value) {
    FilterRow row;
    row.container = new QWidget();
    auto* hl = new QHBoxLayout(row.container);
    hl->setContentsMargins(0, 1, 0, 1);
    hl->setSpacing(4);

    row.logicCombo = new QComboBox();
    row.logicCombo->addItems({"AND", "OR", "NOT"});
    row.logicCombo->setMaximumWidth(58);
    row.logicCombo->setFixedHeight(24);
    hl->addWidget(row.logicCombo);

    row.columnCombo = new QComboBox();
    QStringList cols = m_filterColumnNames.isEmpty()
                           ? QStringList{"raw", "line_number"}
                           : m_filterColumnNames;
    row.columnCombo->addItems(cols);
    row.columnCombo->setMaximumWidth(130);
    row.columnCombo->setFixedHeight(24);
    if (!column.isEmpty()) {
        int idx = row.columnCombo->findText(column);
        if (idx >= 0) row.columnCombo->setCurrentIndex(idx);
    }
    hl->addWidget(row.columnCombo);

    row.opCombo = new QComboBox();
    row.opCombo->addItems({"contains", "=", "!=", ">", "<"});
    row.opCombo->setMaximumWidth(80);
    row.opCombo->setFixedHeight(24);
    hl->addWidget(row.opCombo);

    row.valueEdit = new QLineEdit();
    row.valueEdit->setPlaceholderText("value...");
    row.valueEdit->setFixedHeight(24);
    if (!value.isEmpty()) row.valueEdit->setText(value);
    connect(row.valueEdit, &QLineEdit::returnPressed, this, &LogWidget::onApplyFilters);
    hl->addWidget(row.valueEdit, 1);

    row.removeBtn = new QPushButton("×");
    row.removeBtn->setMaximumWidth(24);
    row.removeBtn->setFixedHeight(24);
    row.removeBtn->setToolTip("Remove this filter");
    QWidget* container = row.container;
    connect(row.removeBtn, &QPushButton::clicked, this, [this, container]() {
        removeFilterRow(container);
    });
    hl->addWidget(row.removeBtn);

    // Insert before the trailing stretch
    int stretchPos = m_filterLayout->count() - 1;
    m_filterLayout->insertWidget(stretchPos, row.container);

    m_filterRows.append(row);
}

void LogWidget::removeFilterRow(QWidget* container) {
    for (int i = 0; i < m_filterRows.size(); i++) {
        if (m_filterRows[i].container == container) {
            m_filterLayout->removeWidget(container);
            container->deleteLater();
            m_filterRows.removeAt(i);
            break;
        }
    }
}

QVector<Filter> LogWidget::collectFilters() const {
    QVector<Filter> filters;

    for (const auto& row : m_filterRows) {
        if (!row.valueEdit) continue;
        QString value = row.valueEdit->text().trimmed();
        if (value.isEmpty()) continue;

        Filter f;
        f.column = row.columnCombo->currentText();
        f.value  = value;

        QString logic = row.logicCombo->currentText();
        if      (logic == "OR")  f.logic = Filter::Or;
        else if (logic == "NOT") f.logic = Filter::Not;
        else                     f.logic = Filter::And;

        QString op = row.opCombo->currentText();
        if      (op == "=")  f.op = Filter::Equals;
        else if (op == "!=") f.op = Filter::NotEquals;
        else if (op == ">")  f.op = Filter::GreaterThan;
        else if (op == "<")  f.op = Filter::LessThan;
        else                 f.op = Filter::Contains;

        filters.append(f);
    }

    return filters;
}

// ─── Data Refresh (convenience wrapper) ──────────────────────────────────────

void LogWidget::refreshData() {
    setPointer(m_bufferPointer, /*force=*/true);
}

// ─── Virtual Scroll Core ──────────────────────────────────────────────────────

void LogWidget::setPointer(int p, bool force) {
    int bufN = m_bufferSizeSpin ? m_bufferSizeSpin->value() : DEFAULT_BUFFER;
    int maxP = qMax(0, m_totalRowCount - 1);
    p = qBound(0, p, maxP);

    int delta = p - m_bufferPointer;
    if (!force && delta == 0) return;

    m_bufferPointer = p;

    if (force || qAbs(delta) >= bufN) {
        fillBuffer();
    } else {
        updateBufferDelta(delta);
    }

    // Keep scrollbar in sync without triggering valueChanged
    if (m_logScrollBar) {
        m_logScrollBar->blockSignals(true);
        m_logScrollBar->setMaximum(qMax(0, m_totalRowCount - 1));
        m_logScrollBar->setValue(m_bufferPointer);
        m_logScrollBar->blockSignals(false);
    }

    applyBufferToView();
    updateStatusLabel();

    // Proactive prefetch: extend buffer if near an edge
    checkPrefetch();
}

void LogWidget::checkPrefetch() {
    if (m_buffer.isEmpty() || m_totalRowCount == 0) return;
    int bufN = m_bufferSizeSpin ? m_bufferSizeSpin->value() : DEFAULT_BUFFER;
    auto filters = collectFilters();
    QString fts  = m_searchEdit ? m_searchEdit->text().trimmed() : QString();

    // ── Prefetch downward (near bottom of buffer) ───────────────────
    int bufferEnd  = m_bufferPointer + m_buffer.size();  // absolute index after last row
    int rowsBelow  = m_totalRowCount - bufferEnd;         // rows in DB not yet in buffer
    if (rowsBelow > 0) {
        // Distance from the bottom of the VISIBLE window to the bottom of the buffer
        // (we treat the visible window as the last `bufN` rows of the buffer for simplicity)
        int distToBottomEdge = m_buffer.size() - bufN;
        if (distToBottomEdge < PREFETCH_MARGIN) {
            int fetchCount = qMin(PREFETCH_MARGIN, rowsBelow);
            QVector<QVector<QString>> newRows;
            QStringList headers;
            int total = 0;
            LogDatabase::instance().queryRows(
                m_fileId, bufferEnd, fetchCount,
                filters, fts, newRows, headers, total);
            if (total > 0) m_totalRowCount = total;
            for (const auto& r : newRows) m_buffer.append(r);

            // Trim from top if buffer grew too large
            int maxBuf = bufN + 2 * PREFETCH_MARGIN;
            if (m_buffer.size() > maxBuf) {
                int trim = m_buffer.size() - maxBuf;
                m_buffer.remove(0, trim);
                m_bufferPointer += trim;
            }
        }
    }

    // ── Prefetch upward (near top of buffer) ─────────────────────
    if (m_bufferPointer > 0) {
        int distToTopEdge = m_bufferPointer;  // rows in DB above the buffer start
        if (distToTopEdge < PREFETCH_MARGIN) {
            int fetchCount = qMin(PREFETCH_MARGIN, m_bufferPointer);
            int fetchStart = m_bufferPointer - fetchCount;
            QVector<QVector<QString>> newRows;
            QStringList headers;
            int total = 0;
            LogDatabase::instance().queryRows(
                m_fileId, fetchStart, fetchCount,
                filters, fts, newRows, headers, total);
            if (total > 0) m_totalRowCount = total;
            // Prepend
            newRows.append(m_buffer);
            m_buffer = newRows;
            m_bufferPointer = fetchStart;

            // Trim from bottom if buffer grew too large
            int maxBuf = bufN + 2 * PREFETCH_MARGIN;
            if (m_buffer.size() > maxBuf)
                m_buffer.resize(maxBuf);
        }
    }
}

void LogWidget::fillBuffer() {
    int bufN = m_bufferSizeSpin ? m_bufferSizeSpin->value() : DEFAULT_BUFFER;
    auto filters = collectFilters();
    QString fts  = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    int total    = 0;

    LogDatabase::instance().queryRows(
        m_fileId, m_bufferPointer, bufN,
        filters, fts, m_buffer, m_bufferHeaders, total);

    m_totalRowCount = total;
}

void LogWidget::updateBufferDelta(int delta) {
    // delta > 0 : moving DOWN  → drop 'delta' rows from top, fetch 'delta' at bottom
    // delta < 0 : moving UP    → drop 'delta' rows from bottom, fetch 'delta' at top
    int bufN     = m_bufferSizeSpin ? m_bufferSizeSpin->value() : DEFAULT_BUFFER;
    int absDelta = qAbs(delta);
    int oldN     = m_buffer.size();
    auto filters = collectFilters();
    QString fts  = m_searchEdit ? m_searchEdit->text().trimmed() : QString();

    QVector<QVector<QString>> newRows;
    QStringList headers;
    int total = 0;

    if (delta > 0) {
        // Keep tail of old buffer; fetch new rows at the new bottom
        int keep        = qMax(0, oldN - absDelta);
        int fetchOffset = m_bufferPointer + keep;   // first new row absolute offset
        int fetchCount  = bufN - keep;

        if (fetchCount > 0)
            LogDatabase::instance().queryRows(
                m_fileId, fetchOffset, fetchCount,
                filters, fts, newRows, headers, total);

        if (absDelta < oldN)
            m_buffer.remove(0, absDelta);  // drop from top
        else
            m_buffer.clear();

        for (const auto& r : newRows) m_buffer.append(r);

    } else {  // delta < 0
        // Keep head of old buffer; fetch new rows at the new top
        int keep       = qMax(0, oldN - absDelta);
        int fetchCount = bufN - keep;

        m_buffer.resize(keep);  // drop from bottom

        if (fetchCount > 0)
            LogDatabase::instance().queryRows(
                m_fileId, m_bufferPointer, fetchCount,
                filters, fts, newRows, headers, total);

        // Prepend new rows
        QVector<QVector<QString>> combined = newRows;
        combined.append(m_buffer);
        m_buffer = combined;
    }

    if (total > 0) m_totalRowCount = total;
}

void LogWidget::applyBufferToView() {
    int rawIdx = m_bufferHeaders.indexOf("raw");
    if (rawIdx < 0) rawIdx = qMin(1, m_bufferHeaders.size() - 1);

    QString text;
    text.reserve(m_buffer.size() * 200);
    for (const auto& row : m_buffer)
        if (rawIdx < row.size())
            text += row[rawIdx] + "\n";

    m_textBrowser->setPlainText(text);

    // Re-apply highlights because setPlainText() replaces the document contents.
    if (m_textFindBar && m_textFindBar->isVisible() && m_textFindCombo && !m_textFindCombo->currentText().isEmpty())
        onTextFindSearch();
    else
        onTextFindClear();
}

void LogWidget::updateStatusLabel() {
    int bufN = m_buffer.size();
    m_labelState->setText(
        QString("Rows %1–%2 of %3")
            .arg(m_bufferPointer + 1)
            .arg(m_bufferPointer + bufN)
            .arg(m_totalRowCount));
}

// ─── Event Filter (wheel / arrow keys route to buffer pointer) ────────────────

bool LogWidget::eventFilter(QObject* obj, QEvent* event) {
    if (obj != m_textBrowser)
        return QWidget::eventFilter(obj, event);

    if (event->type() == QEvent::Wheel) {
        auto* we  = static_cast<QWheelEvent*>(event);
        int angle = we->angleDelta().y();
        // 1 wheel notch = 15 degrees; move ~3 rows per notch
        int steps = -(angle / 15);
        if (steps != 0) setPointer(m_bufferPointer + steps);
        return true;  // consume: don't let native scroll run
    }

    if (event->type() == QEvent::KeyPress) {
        auto* ke  = static_cast<QKeyEvent*>(event);
        int bufN  = m_bufferSizeSpin ? m_bufferSizeSpin->value() : DEFAULT_BUFFER;
        switch (ke->key()) {
        case Qt::Key_Down:     setPointer(m_bufferPointer + 1);    return true;
        case Qt::Key_Up:       setPointer(m_bufferPointer - 1);    return true;
        case Qt::Key_PageDown: setPointer(m_bufferPointer + bufN); return true;
        case Qt::Key_PageUp:   setPointer(m_bufferPointer - bufN); return true;
        case Qt::Key_Home:     setPointer(0, true);                return true;
        case Qt::Key_End:      setPointer(qMax(0, m_totalRowCount - 1), true); return true;
        default: break;
        }
    }

    return QWidget::eventFilter(obj, event);
}

// ─── Text Find Bar Slots ──────────────────────────────────────────────────────────────

void LogWidget::onToggleTextFindBar() {
    if (!m_textFindBar) return;

    bool wasVisible = m_textFindBar->isVisible();
    m_textFindBar->setVisible(!wasVisible);

    if (!wasVisible) {
        m_textFindCombo->lineEdit()->setFocus();
        m_textFindCombo->lineEdit()->selectAll();
    } else {
        // Hide → clear highlights as well
        onTextFindClear();
    }
}

static void applyTextFindHighlights(
    QTextBrowser* browser,
    const QList<QTextEdit::ExtraSelection>& all,
    int current)
{
    // Build a merged list: base (yellow) for all, override (orange) for current
    QList<QTextEdit::ExtraSelection> merged = all;
    if (current >= 0 && current < all.size()) {
        QTextEdit::ExtraSelection active = all[current];
        active.format.setBackground(QColor("#FF9900"));
        active.format.setForeground(QColor("#000000"));
        merged[current] = active;
    }
    browser->setExtraSelections(merged);

    // Scroll to the active match
    if (current >= 0 && current < all.size()) {
        QTextCursor c = browser->textCursor();
        c.setPosition(all[current].cursor.selectionStart());
        browser->setTextCursor(c);
        browser->ensureCursorVisible();
    }
}

void LogWidget::onTextFindSearch() {
    if (!m_textBrowser || !m_textFindCombo) return;

    m_textFindHighlights.clear();
    m_textFindCurrent = -1;

    QString pattern = m_textFindCombo->currentText();
    if (pattern.isEmpty()) {
        m_textBrowser->setExtraSelections({});
        if (m_textFindStatus) m_textFindStatus->setText("");
        return;
    }

    // Update history
    if (!m_textFindHistory.contains(pattern)) {
        m_textFindHistory.prepend(pattern);
        if (m_textFindHistory.size() > 20)
            m_textFindHistory.removeLast();
        // Rebuild combo items keeping the current text
        m_textFindCombo->blockSignals(true);
        m_textFindCombo->clear();
        m_textFindCombo->addItems(m_textFindHistory);
        m_textFindCombo->setCurrentText(pattern);
        m_textFindCombo->blockSignals(false);
    }

    // Build regex
    QRegularExpression::PatternOptions opts = QRegularExpression::NoPatternOption;
    if (!m_textFindCase || !m_textFindCase->isChecked())
        opts |= QRegularExpression::CaseInsensitiveOption;
    QString regexStr = (m_textFindRegex && m_textFindRegex->isChecked())
                       ? pattern
                       : QRegularExpression::escape(pattern);
    QRegularExpression re(regexStr, opts);
    if (!re.isValid()) {
        if (m_textFindStatus) m_textFindStatus->setText("Invalid regex");
        return;
    }

    // Find all occurrences in the document
    QTextDocument* doc = m_textBrowser->document();
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::Start);

    QTextCharFormat baseFormat;
    baseFormat.setBackground(QColor("#FFE066"));
    baseFormat.setForeground(QColor("#000000"));

    while (true) {
        cursor = doc->find(re, cursor);
        if (cursor.isNull()) break;
        QTextEdit::ExtraSelection sel;
        sel.cursor = cursor;
        sel.format = baseFormat;
        m_textFindHighlights.append(sel);
    }

    int total = m_textFindHighlights.size();
    if (total == 0) {
        m_textBrowser->setExtraSelections({});
        if (m_textFindStatus) m_textFindStatus->setText("No matches");
        return;
    }

    m_textFindCurrent = 0;
    applyTextFindHighlights(m_textBrowser, m_textFindHighlights, m_textFindCurrent);
    if (m_textFindStatus)
        m_textFindStatus->setText(QString("%1 of %2").arg(1).arg(total));
}

void LogWidget::onTextFindNext() {
    int total = m_textFindHighlights.size();
    if (total == 0) { onTextFindSearch(); return; }

    if (m_textFindCurrent == total - 1) {
        // At last highlight: try to fetch more rows downward
        int bufferEnd = m_bufferPointer + m_buffer.size();
        if (bufferEnd < m_totalRowCount) {
            int step = qMin(PREFETCH_MARGIN, m_totalRowCount - bufferEnd);
            setPointer(m_bufferPointer + step, true);
            // onTextFindSearch() is called by applyBufferToView()
            // Navigate to the first highlight beyond the old last one
            if (!m_textFindHighlights.isEmpty()) {
                m_textFindCurrent = 0;
                applyTextFindHighlights(m_textBrowser, m_textFindHighlights, m_textFindCurrent);
                if (m_textFindStatus)
                    m_textFindStatus->setText(
                        QString("%1 of %2").arg(1).arg(m_textFindHighlights.size()));
            }
            return;
        }
        // No more rows: wrap to beginning
    }

    m_textFindCurrent = (m_textFindCurrent + 1) % total;
    applyTextFindHighlights(m_textBrowser, m_textFindHighlights, m_textFindCurrent);
    if (m_textFindStatus)
        m_textFindStatus->setText(QString("%1 of %2").arg(m_textFindCurrent + 1).arg(total));
}

void LogWidget::onTextFindPrev() {
    int total = m_textFindHighlights.size();
    if (total == 0) { onTextFindSearch(); return; }

    if (m_textFindCurrent == 0) {
        // At first highlight: try to fetch more rows upward
        if (m_bufferPointer > 0) {
            int step = qMin(PREFETCH_MARGIN, m_bufferPointer);
            setPointer(m_bufferPointer - step, true);
            // Navigate to the last highlight in the new buffer
            if (!m_textFindHighlights.isEmpty()) {
                m_textFindCurrent = m_textFindHighlights.size() - 1;
                applyTextFindHighlights(m_textBrowser, m_textFindHighlights, m_textFindCurrent);
                if (m_textFindStatus)
                    m_textFindStatus->setText(
                        QString("%1 of %2").arg(m_textFindCurrent + 1)
                                          .arg(m_textFindHighlights.size()));
            }
            return;
        }
        // No more rows above: wrap to end
    }

    m_textFindCurrent = (m_textFindCurrent - 1 + total) % total;
    applyTextFindHighlights(m_textBrowser, m_textFindHighlights, m_textFindCurrent);
    if (m_textFindStatus)
        m_textFindStatus->setText(QString("%1 of %2").arg(m_textFindCurrent + 1).arg(total));
}

void LogWidget::onTextFindFirst() {
    int total = m_textFindHighlights.size();
    if (total == 0) { onTextFindSearch(); return; }

    if (m_bufferPointer > 0) {
        // Jump to DB start, then search from there
        setPointer(0, true);
        if (!m_textFindHighlights.isEmpty()) {
            m_textFindCurrent = 0;
            applyTextFindHighlights(m_textBrowser, m_textFindHighlights, m_textFindCurrent);
            if (m_textFindStatus)
                m_textFindStatus->setText(
                    QString("1 of %1").arg(m_textFindHighlights.size()));
        }
        return;
    }

    m_textFindCurrent = 0;
    applyTextFindHighlights(m_textBrowser, m_textFindHighlights, m_textFindCurrent);
    if (m_textFindStatus)
        m_textFindStatus->setText(QString("1 of %1").arg(total));
}

void LogWidget::onTextFindLast() {
    int total = m_textFindHighlights.size();
    if (total == 0) { onTextFindSearch(); return; }

    int bufferEnd = m_bufferPointer + m_buffer.size();
    if (bufferEnd < m_totalRowCount) {
        // Jump to DB end, then search from there
        setPointer(qMax(0, m_totalRowCount - 1), true);
        if (!m_textFindHighlights.isEmpty()) {
            m_textFindCurrent = m_textFindHighlights.size() - 1;
            applyTextFindHighlights(m_textBrowser, m_textFindHighlights, m_textFindCurrent);
            if (m_textFindStatus)
                m_textFindStatus->setText(
                    QString("%1 of %2").arg(m_textFindCurrent + 1)
                                      .arg(m_textFindHighlights.size()));
        }
        return;
    }

    m_textFindCurrent = total - 1;
    applyTextFindHighlights(m_textBrowser, m_textFindHighlights, m_textFindCurrent);
    if (m_textFindStatus)
        m_textFindStatus->setText(QString("%1 of %2").arg(total).arg(total));
}

void LogWidget::onTextFindClear() {
    m_textFindHighlights.clear();
    m_textFindCurrent = -1;
    if (m_textBrowser) m_textBrowser->setExtraSelections({});
    if (m_textFindStatus) m_textFindStatus->setText("");
}
