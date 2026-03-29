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
    if (m_worker) m_worker->stop();
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(3000);
    }
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
        m_proxyModel = new QSortFilterProxyModel(this);
        m_proxyModel->setSourceModel(m_tableModel);
        m_proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
        m_tableView->setModel(m_proxyModel);

        // Text view (index 1)
        m_textBrowser = new QTextBrowser(this);
        m_textBrowser->setFont(QFont("Monospace", 9));
        m_textBrowser->setLineWrapMode(QTextEdit::NoWrap);

        m_viewStack->addWidget(m_tableView); // 0
        m_viewStack->addWidget(m_textBrowser); // 1
        m_viewStack->setCurrentIndex(0);

        viewLayout->addWidget(m_viewStack, 1);
        splitter->addWidget(viewContainer);

        connect(m_viewToggleButton, &QPushButton::toggled, this, &LogWidget::onToggleView);
        connect(m_wrapCheck, &QCheckBox::toggled, this, &LogWidget::onWrapToggled);
        connect(m_tableView->horizontalHeader(), &QHeaderView::customContextMenuRequested,
                this, &LogWidget::onHeaderContextMenu);
        connect(m_tableView, &QTableView::clicked, this, &LogWidget::onCellClicked);

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
    m_labelState->setText("Ready");
    m_progressBar->setValue(100);
    refreshData();

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
    m_currentPage = 0;
    refreshData();
}

void LogWidget::onToggleView() {
    if (m_viewToggleButton->isChecked()) {
        m_viewStack->setCurrentIndex(1);
        m_viewToggleButton->setText("Table View");
    } else {
        m_viewStack->setCurrentIndex(0);
        m_viewToggleButton->setText("Text View");
    }
    refreshData();
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

// ─── Data Refresh ─────────────────────────────────────────────────────────────

void LogWidget::refreshData() {
    QVector<Filter> filters  = collectFilters();
    QString         ftsQuery = m_searchEdit->text().trimmed();

    QVector<QVector<QString>> rows;
    QStringList headers;
    int totalCount = 0;

    bool ok = LogDatabase::instance().queryRows(
        m_fileId, m_currentPage * PAGE_SIZE, PAGE_SIZE,
        filters, ftsQuery, rows, headers, totalCount);

    if (!ok) {
        qWarning() << "LogWidget::refreshData: Query failed for fileId" << m_fileId;
        return;
    }

    // ── Populate table model ──────────────────────────────────────────
    m_tableModel->clear();
    m_tableModel->setHorizontalHeaderLabels(headers);

    for (const auto& row : rows) {
        QList<QStandardItem*> items;
        for (const auto& cell : row) {
            auto* item = new QStandardItem(cell);
            item->setEditable(false);
            items.append(item);
        }
        m_tableModel->appendRow(items);
    }

    // Apply saved column visibility (hides 'raw' if dynamic columns exist, etc.)
    applyColumnVisibility();

    // Resize only visible columns to avoid wasted time on hidden wide columns
    for (int i = 0; i < m_tableModel->columnCount(); i++) {
        if (!m_tableView->isColumnHidden(i))
            m_tableView->resizeColumnToContents(i);
    }

    // ── Populate text view if active ──────────────────────────────────
    if (m_viewStack->currentIndex() == 1) {
        int rawIndex = headers.indexOf("raw");
        if (rawIndex < 0) rawIndex = qMin(1, headers.size() - 1);

        QString text;
        text.reserve(rows.size() * 200);
        for (const auto& row : rows) {
            if (rawIndex < row.size())
                text += row[rawIndex] + "\n";
        }
        m_textBrowser->setPlainText(text);
    }

    m_labelState->setText(
        QString("Showing %1 of %2 rows").arg(rows.size()).arg(totalCount));
}
