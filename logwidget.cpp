#include "logwidget.h"
#include "fileworker.h"
#include "logdatabase.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QCheckBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTableView>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <QtLogging>
#include <algorithm>

// ─── NumericAwareProxyModel ───────────────────────────────────────────────────
// Defined here (no Q_OBJECT needed: we only override lessThan, no new signals/slots).
class NumericAwareProxyModel : public QSortFilterProxyModel {
public:
    explicit NumericAwareProxyModel(QObject* parent = nullptr)
        : QSortFilterProxyModel(parent) {}
protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override {
        QString ls = left.data(Qt::DisplayRole).toString();
        QString rs = right.data(Qt::DisplayRole).toString();
        bool lok = false, rok = false;
        double ln = ls.toDouble(&lok);
        double rn = rs.toDouble(&rok);
        if (lok && rok) return ln < rn;   // numeric comparison
        return ls < rs;                    // lexicographic fallback
    }
};

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

    // ── Debounce timer for chunk-driven refreshes ─────────────────────
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(REFRESH_DEBOUNCE_MS);
    connect(m_refreshTimer, &QTimer::timeout, this, &LogWidget::refreshData);

    // ── Search bar ────────────────────────────────────────────────────
    {
        auto* topBar = new QHBoxLayout();
        topBar->addWidget(new QLabel("Search (FTS5):", this));
        m_searchEdit = new QLineEdit(this);
        m_searchEdit->setPlaceholderText("Full-text search...");
        topBar->addWidget(m_searchEdit, 1);
        m_searchButton = new QPushButton("Search", this);
        topBar->addWidget(m_searchButton);
        m_highlightCheck = new QCheckBox("Highlight", this);
        topBar->addWidget(m_highlightCheck);
        m_filterOnlyCheck = new QCheckBox("Filter only", this);
        m_filterOnlyCheck->setChecked(true);
        topBar->addWidget(m_filterOnlyCheck);
        m_mainLayout->addLayout(topBar);

        connect(m_searchButton, &QPushButton::clicked,    this, &LogWidget::onApplyFilters);
        connect(m_searchEdit,   &QLineEdit::returnPressed, this, &LogWidget::onApplyFilters);
    }

    // ── Splitter: view area (left) | filter panel (right) ────────────
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // ── Left: view controls + stacked view ───────────────────────────
    {
        auto* viewContainer = new QWidget(this);
        auto* viewLayout = new QVBoxLayout(viewContainer);
        viewLayout->setContentsMargins(0, 0, 0, 0);
        viewLayout->setSpacing(2);

        auto* viewBar = new QHBoxLayout();
        auto* contentsLabel = new QLabel("Contents", this);
        contentsLabel->setStyleSheet("font-weight: bold;");
        m_viewToggleButton = new QPushButton("Text View", this);
        m_viewToggleButton->setCheckable(true);
        m_wrapCheck = new QCheckBox("Wrap", this);
        viewBar->addWidget(contentsLabel);
        viewBar->addStretch();
        viewBar->addWidget(m_viewToggleButton);
        viewBar->addWidget(m_wrapCheck);
        viewLayout->addLayout(viewBar);

        m_viewStack = new QStackedWidget(this);

        // Table view (index 0)
        m_tableView = new QTableView(this);
        m_tableView->setAlternatingRowColors(true);
        // Allow selecting individual cells across rows/columns
        m_tableView->setSelectionBehavior(QAbstractItemView::SelectItems);
        m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_tableView->verticalHeader()->setVisible(false);
        m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_tableView->setSortingEnabled(true);
        m_tableView->horizontalHeader()->setSortIndicatorShown(true);
        m_tableView->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);

        m_tableModel = new QStandardItemModel(this);
        // NumericAwareProxyModel: sorts numbers as doubles, strings lexicographically.
        m_proxyModel = new NumericAwareProxyModel(this);
        m_proxyModel->setSourceModel(m_tableModel);
        m_proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
        m_tableView->setModel(m_proxyModel);
        // Disable Qt's automatic sort-on-header-click; we manage 3-state manually.
        m_tableView->setSortingEnabled(false);
        m_tableView->horizontalHeader()->setSortIndicatorShown(false);

        // Text view (index 1)
        m_textBrowser = new QTextBrowser(this);
        m_textBrowser->setFont(QFont("Monospace", 9));
        m_textBrowser->setLineWrapMode(QTextEdit::NoWrap);

        m_viewStack->addWidget(m_tableView);    // 0
        m_viewStack->addWidget(m_textBrowser);  // 1
        m_viewStack->setCurrentIndex(0);
        // Note: viewStack is added to contentBox together with m_logScrollBar below

        connect(m_viewToggleButton, &QPushButton::toggled, this, &LogWidget::onToggleView);
        connect(m_wrapCheck, &QCheckBox::toggled, this, &LogWidget::onWrapToggled);
        connect(m_tableView->horizontalHeader(), &QHeaderView::customContextMenuRequested,
                this, &LogWidget::onHeaderContextMenu);
        connect(m_tableView, &QTableView::clicked, this, &LogWidget::onCellClicked);

        // 3-state sort: none → asc → desc → none
        connect(m_tableView->horizontalHeader(), &QHeaderView::sectionClicked,
                this, [this](int col) {
            if (m_sortColumn == col) {
                m_sortCycle = (m_sortCycle + 1) % 3;
            } else {
                m_sortColumn = col;
                m_sortCycle  = 1;
            }
            if (m_sortCycle == 0) {
                m_proxyModel->sort(-1, Qt::AscendingOrder);
                m_tableView->horizontalHeader()->setSortIndicatorShown(false);
            } else {
                Qt::SortOrder ord = (m_sortCycle == 1) ? Qt::AscendingOrder : Qt::DescendingOrder;
                m_tableView->horizontalHeader()->setSortIndicatorShown(true);
                m_tableView->horizontalHeader()->setSortIndicator(col, ord);
                m_proxyModel->sort(col, ord);
            }
        });

        // Hide native vertical scrollbars — replaced by m_logScrollBar
        m_tableView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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

        // Layout: [viewStack | logScrollBar]
        auto* contentBox = new QHBoxLayout();
        contentBox->setSpacing(2);
        contentBox->addWidget(m_viewStack, 1);
        contentBox->addWidget(m_logScrollBar);
        viewLayout->addLayout(contentBox, 1);
        splitter->addWidget(viewContainer);

        // Event filter: intercept wheel/arrow keys to move the buffer pointer
        m_tableView->installEventFilter(this);
        m_textBrowser->installEventFilter(this);

        // Ctrl+C copy
        auto* copyAction = new QAction("Copy", this);
        copyAction->setShortcut(QKeySequence::Copy);
        addAction(copyAction);
        connect(copyAction, &QAction::triggered, this, &LogWidget::onCopySelection);
    }

    // ── Right: dynamic filter panel ───────────────────────────────────
    {
        auto* filterContainer = new QWidget(this);
        auto* filterOuterLayout = new QVBoxLayout(filterContainer);
        filterOuterLayout->setContentsMargins(0, 0, 0, 0);
        filterOuterLayout->setSpacing(4);

        filterOuterLayout->addWidget(new QLabel("<b>Column Filters</b>", this));

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

// ─── Helper: switch views ─────────────────────────────────────────────────────

void LogWidget::switchToTextView() {
    m_viewStack->setCurrentIndex(1);
    m_viewToggleButton->blockSignals(true);
    m_viewToggleButton->setChecked(true);
    m_viewToggleButton->setText("Table View");
    m_viewToggleButton->blockSignals(false);
}

void LogWidget::switchToTableView() {
    m_viewStack->setCurrentIndex(0);
    m_viewToggleButton->blockSignals(true);
    m_viewToggleButton->setChecked(false);
    m_viewToggleButton->setText("Text View");
    m_viewToggleButton->blockSignals(false);
}

// ─── Slots ───────────────────────────────────────────────────────────────────

void LogWidget::onSchemaReady(int fileId, QVector<ColumnDef> columns) {
    if (fileId != m_fileId) return;

    m_columns = columns;
    m_hasDynamicColumns = !columns.isEmpty();

    // Default column visibility: hide 'raw' when dynamic columns exist
    m_columnVisibility.clear();
    m_columnVisibility["line_number"] = true;
    m_columnVisibility["raw"]         = !m_hasDynamicColumns;
    for (const auto& col : columns)
        m_columnVisibility[col.name] = true;

    // Default view: text if no dynamic columns (plain text log)
    if (!m_hasDynamicColumns) {
        switchToTextView();
    }

    // Build filter column list and update any existing filter rows
    updateFilterColumns();

    qInfo() << "LogWidget: Schema ready for fileId" << fileId
            << "–" << columns.size() << "dynamic columns"
            << "| Default view:" << (m_hasDynamicColumns ? "table" : "text");
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

void LogWidget::onToggleView() {
    if (m_viewToggleButton->isChecked()) {
        m_viewStack->setCurrentIndex(1);
        m_viewToggleButton->setText("Table View");
    } else {
        m_viewStack->setCurrentIndex(0);
        m_viewToggleButton->setText("Text View");
    }
    applyBufferToView();  // re-render current buffer in the new view
}

void LogWidget::onWrapToggled(bool checked) {
    m_textBrowser->setLineWrapMode(checked ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);
}

// ── Header right-click: toggle column visibility ──────────────────────────

void LogWidget::onHeaderContextMenu(const QPoint& pos) {
    QMenu menu(this);
    menu.setTitle("Toggle Columns");

    int count = m_tableModel->columnCount();
    for (int i = 0; i < count; i++) {
        QString colName = m_tableModel->headerData(i, Qt::Horizontal).toString();
        auto* action = menu.addAction(colName);
        action->setCheckable(true);
        action->setChecked(!m_tableView->isColumnHidden(i));
        connect(action, &QAction::toggled, this, [this, i, colName](bool visible) {
            m_tableView->setColumnHidden(i, !visible);
            m_columnVisibility[colName] = visible;
        });
    }

    menu.exec(m_tableView->horizontalHeader()->mapToGlobal(pos));
}

// ── Ctrl+C: copy selection to clipboard ──────────────────────────────────

void LogWidget::onCopySelection() {
    QModelIndexList indexes = m_tableView->selectionModel()->selectedIndexes();
    if (indexes.isEmpty()) return;

    std::sort(indexes.begin(), indexes.end(), [](const QModelIndex& a, const QModelIndex& b) {
        return a.row() != b.row() ? a.row() < b.row() : a.column() < b.column();
    });

    QString text;
    int prevRow = -1;
    for (const auto& idx : indexes) {
        if (idx.row() != prevRow) {
            if (prevRow != -1) text += "\n";
            prevRow = idx.row();
        } else {
            text += "\t";
        }
        text += idx.data().toString();
    }

    QApplication::clipboard()->setText(text);
}

// ── Cell click: populate a filter row with column + value ─────────────────

void LogWidget::onCellClicked(const QModelIndex& proxyIndex) {
    if (!proxyIndex.isValid()) return;

    QModelIndex srcIdx = m_proxyModel->mapToSource(proxyIndex);
    if (!srcIdx.isValid()) return;

    QString colName   = m_tableModel->headerData(srcIdx.column(), Qt::Horizontal).toString();
    QString cellValue = m_tableModel->data(srcIdx).toString();

    if (colName.isEmpty() || cellValue.isEmpty()) return;

    // Try to fill an existing empty filter for the same column first
    for (auto& row : m_filterRows) {
        if (row.columnCombo->currentText() == colName && row.valueEdit->text().isEmpty()) {
            row.valueEdit->setText(cellValue);
            return;
        }
    }

    // Otherwise, add a new pre-filled filter row
    addFilterRow(colName, cellValue);
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

// ─── Column Visibility ────────────────────────────────────────────────────────

void LogWidget::applyColumnVisibility() {
    int count = m_tableModel->columnCount();
    for (int i = 0; i < count; i++) {
        QString colName = m_tableModel->headerData(i, Qt::Horizontal).toString();
        bool visible = m_columnVisibility.value(colName, true);
        m_tableView->setColumnHidden(i, !visible);
    }
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
    // ── Table model ───────────────────────────────────────────────────
    m_tableModel->clear();
    m_tableModel->setHorizontalHeaderLabels(m_bufferHeaders);

    for (const auto& row : m_buffer) {
        QList<QStandardItem*> items;
        items.reserve(row.size());
        for (const auto& cell : row) {
            auto* item = new QStandardItem(cell);
            item->setEditable(false);
            items.append(item);
        }
        m_tableModel->appendRow(items);
    }

    applyColumnVisibility();
    for (int i = 0; i < m_tableModel->columnCount(); i++)
        if (!m_tableView->isColumnHidden(i))
            m_tableView->resizeColumnToContents(i);

    // ── Text view ─────────────────────────────────────────────────────
    if (m_viewStack->currentIndex() == 1) {
        int rawIdx = m_bufferHeaders.indexOf("raw");
        if (rawIdx < 0) rawIdx = qMin(1, m_bufferHeaders.size() - 1);
        QString text;
        text.reserve(m_buffer.size() * 200);
        for (const auto& row : m_buffer)
            if (rawIdx < row.size())
                text += row[rawIdx] + "\n";
        m_textBrowser->setPlainText(text);
    }
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
    if (obj != m_tableView && obj != m_textBrowser)
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
