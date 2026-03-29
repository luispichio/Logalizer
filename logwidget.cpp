#include "logwidget.h"
#include "fileworker.h"
#include "logdatabase.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableView>
#include <QTextBrowser>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QScrollArea>
#include <QHeaderView>
#include <QFileInfo>
#include <QStandardItemModel>
#include <QtLogging>

LogWidget::LogWidget(const QString& filePath, int fileId, QWidget *parent)
    : QWidget(parent)
    , m_filePath(filePath)
    , m_fileId(fileId)
{
    setupUi();

    // File info
    QFileInfo fi(filePath);
    m_fileSize = fi.size();
    m_labelSize->setText(QString::number(m_fileSize / 1024.0 / 1024.0, 'f', 2) + " MB");

    // Create and start worker in a separate thread
    m_workerThread = new QThread(this);
    m_worker = new FileWorker(filePath, fileId);
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started, m_worker, &FileWorker::start);
    connect(m_worker, &FileWorker::schemaReady, this, &LogWidget::onSchemaReady);
    connect(m_worker, &FileWorker::progressUpdate, this, &LogWidget::onProgressUpdate);
    connect(m_worker, &FileWorker::chunkInserted, this, &LogWidget::onChunkInserted);
    connect(m_worker, &FileWorker::finished, this, &LogWidget::onFinished);
    connect(m_worker, &FileWorker::error, this, &LogWidget::onError);

    // Clean up worker when thread finishes
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_workerThread->start();
}

LogWidget::~LogWidget() {
    // Stop worker
    if (m_worker)
        m_worker->stop();
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(3000);
    }

    // Drop the DB table for this file
    LogDatabase::instance().dropTable(m_fileId);
    qInfo() << "LogWidget: Cleaned up file" << m_fileId << m_filePath;
}

void LogWidget::setupUi() {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(4, 4, 4, 4);
    m_mainLayout->setSpacing(4);

    // ─── Top: Search bar ─────────────────────────────────────────────
    {
        auto* topBar = new QHBoxLayout();

        auto* searchLabel = new QLabel("Search (FTS5):", this);
        m_searchEdit = new QLineEdit(this);
        m_searchEdit->setPlaceholderText("Full-text search query...");
        m_searchButton = new QPushButton("Search", this);

        m_highlightCheck = new QCheckBox("Highlight", this);
        m_filterOnlyCheck = new QCheckBox("Filter only", this);
        m_filterOnlyCheck->setToolTip("Show only matching rows");
        m_filterOnlyCheck->setChecked(true);

        topBar->addWidget(searchLabel);
        topBar->addWidget(m_searchEdit, 1);
        topBar->addWidget(m_searchButton);
        topBar->addWidget(m_highlightCheck);
        topBar->addWidget(m_filterOnlyCheck);

        m_mainLayout->addLayout(topBar);

        connect(m_searchButton, &QPushButton::clicked, this, &LogWidget::onApplyFilters);
        connect(m_searchEdit, &QLineEdit::returnPressed, this, &LogWidget::onApplyFilters);
    }

    // ─── Middle: Content area (splitter: view | filters) ─────────────
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // Left: view stack (table + text)
    {
        auto* viewContainer = new QWidget(this);
        auto* viewLayout = new QVBoxLayout(viewContainer);
        viewLayout->setContentsMargins(0, 0, 0, 0);
        viewLayout->setSpacing(2);

        // View controls
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

        // Stacked widget: 0=table, 1=text
        m_viewStack = new QStackedWidget(this);

        m_tableView = new QTableView(this);
        m_tableView->setAlternatingRowColors(true);
        m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
        m_tableView->horizontalHeader()->setStretchLastSection(true);
        m_tableView->verticalHeader()->setVisible(false);
        m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);

        m_tableModel = new QStandardItemModel(this);
        m_tableView->setModel(m_tableModel);

        m_textBrowser = new QTextBrowser(this);
        m_textBrowser->setFont(QFont("Monospace", 9));
        m_textBrowser->setLineWrapMode(QTextEdit::NoWrap);

        m_viewStack->addWidget(m_tableView);
        m_viewStack->addWidget(m_textBrowser);
        m_viewStack->setCurrentIndex(0);

        viewLayout->addWidget(m_viewStack, 1);
        splitter->addWidget(viewContainer);

        connect(m_viewToggleButton, &QPushButton::toggled, this, &LogWidget::onToggleView);
        connect(m_wrapCheck, &QCheckBox::toggled, this, &LogWidget::onWrapToggled);
    }

    // Right: dynamic filters panel
    {
        auto* filterContainer = new QWidget(this);
        auto* filterOuterLayout = new QVBoxLayout(filterContainer);
        filterOuterLayout->setContentsMargins(0, 0, 0, 0);

        m_filterGroup = new QGroupBox("Column Filters", this);
        m_filterLayout = new QVBoxLayout(m_filterGroup);
        m_filterLayout->setSpacing(4);

        auto* filterInfo = new QLabel("Filters will appear after schema detection...", this);
        filterInfo->setObjectName("filterPlaceholder");
        m_filterLayout->addWidget(filterInfo);
        m_filterLayout->addStretch();

        // Scroll area for many filters
        auto* scrollArea = new QScrollArea(this);
        scrollArea->setWidget(m_filterGroup);
        scrollArea->setWidgetResizable(true);
        scrollArea->setMinimumWidth(250);

        filterOuterLayout->addWidget(scrollArea);

        auto* applyBtn = new QPushButton("Apply Filters", this);
        connect(applyBtn, &QPushButton::clicked, this, &LogWidget::onApplyFilters);
        filterOuterLayout->addWidget(applyBtn);

        splitter->addWidget(filterContainer);
    }

    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);
    m_mainLayout->addWidget(splitter, 1);

    // ─── Bottom: Status bar ──────────────────────────────────────────
    {
        auto* statusBar = new QHBoxLayout();

        statusBar->addWidget(new QLabel("Size:", this));
        m_labelSize = new QLabel("?", this);
        statusBar->addWidget(m_labelSize);

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

// ─── Slots ───────────────────────────────────────────────────────────────

void LogWidget::onSchemaReady(int fileId, QVector<ColumnDef> columns) {
    if (fileId != m_fileId) return;

    m_columns = columns;
    buildFilterWidgets(columns);

    qInfo() << "LogWidget: Schema ready for file" << fileId << "with" << columns.size() << "columns";
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

    // Refresh data view to show partial results
    refreshData();
}

void LogWidget::onFinished(int fileId) {
    if (fileId != m_fileId) return;

    m_labelState->setText("Ready");
    m_progressBar->setValue(100);
    refreshData();

    emit loadingFinished(fileId);
    qInfo() << "LogWidget: Loading finished for file" << fileId;
}

void LogWidget::onError(int fileId, QString message) {
    if (fileId != m_fileId) return;

    m_labelState->setText("Error: " + message);
    qCritical() << "LogWidget: Error for file" << fileId << ":" << message;
}

void LogWidget::onApplyFilters() {
    m_currentPage = 0;
    refreshData();
}

void LogWidget::onToggleView() {
    if (m_viewToggleButton->isChecked()) {
        m_viewStack->setCurrentIndex(1); // text
        m_viewToggleButton->setText("Table View");
    } else {
        m_viewStack->setCurrentIndex(0); // table
        m_viewToggleButton->setText("Text View");
    }
    refreshData();
}

void LogWidget::onWrapToggled(bool checked) {
    m_textBrowser->setLineWrapMode(checked ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);
}

// ─── Filter Widgets ──────────────────────────────────────────────────────

void LogWidget::buildFilterWidgets(const QVector<ColumnDef>& columns) {
    // Remove placeholder
    auto* placeholder = m_filterGroup->findChild<QLabel*>("filterPlaceholder");
    if (placeholder) {
        m_filterLayout->removeWidget(placeholder);
        delete placeholder;
    }

    // Clear old filter widgets
    for (auto& fw : m_filterWidgets) {
        if (fw.logicCombo) delete fw.logicCombo;
        if (fw.label) delete fw.label;
        if (fw.input) delete fw.input;
    }
    m_filterWidgets.clear();

    for (const auto& col : columns) {
        auto* row = new QHBoxLayout();

        FilterWidget fw;
        fw.column = col;

        // Logic combo
        fw.logicCombo = new QComboBox(this);
        fw.logicCombo->addItems({"AND", "OR", "NOT"});
        fw.logicCombo->setMaximumWidth(60);
        row->addWidget(fw.logicCombo);

        // Label
        fw.label = new QLabel(col.name + " (" + ColumnDef::typeToString(col.type) + "):", this);
        fw.label->setMinimumWidth(80);
        row->addWidget(fw.label);

        // Input widget based on type
        switch (col.type) {
        case ColumnDef::Bool: {
            auto* combo = new QComboBox(this);
            combo->addItems({"Any", "true", "false"});
            fw.input = combo;
            break;
        }
        case ColumnDef::Number: {
            auto* edit = new QLineEdit(this);
            edit->setPlaceholderText("e.g. >100, <50, =42");
            fw.input = edit;
            break;
        }
        case ColumnDef::Date: {
            auto* edit = new QLineEdit(this);
            edit->setPlaceholderText("e.g. >2024-01-01");
            fw.input = edit;
            break;
        }
        default: { // String
            auto* edit = new QLineEdit(this);
            edit->setPlaceholderText("contains...");
            fw.input = edit;
            break;
        }
        }

        row->addWidget(fw.input, 1);
        m_filterLayout->addLayout(row);
        m_filterWidgets.append(fw);
    }

    m_filterLayout->addStretch();
}

QVector<Filter> LogWidget::collectFilters() const {
    QVector<Filter> filters;

    for (const auto& fw : m_filterWidgets) {
        QString value;

        if (auto* edit = qobject_cast<QLineEdit*>(fw.input)) {
            value = edit->text().trimmed();
        } else if (auto* combo = qobject_cast<QComboBox*>(fw.input)) {
            value = combo->currentText();
            if (value == "Any") continue;
        }

        if (value.isEmpty()) continue;

        Filter f;
        f.column = fw.column.name;

        // Parse logic
        QString logic = fw.logicCombo->currentText();
        if (logic == "OR") f.logic = Filter::Or;
        else if (logic == "NOT") f.logic = Filter::Not;
        else f.logic = Filter::And;

        // Parse operator from value for number/date
        if (fw.column.type == ColumnDef::Number || fw.column.type == ColumnDef::Date) {
            if (value.startsWith(">")) {
                f.op = Filter::GreaterThan;
                f.value = value.mid(1).trimmed();
            } else if (value.startsWith("<")) {
                f.op = Filter::LessThan;
                f.value = value.mid(1).trimmed();
            } else if (value.startsWith("=")) {
                f.op = Filter::Equals;
                f.value = value.mid(1).trimmed();
            } else {
                f.op = Filter::Contains;
                f.value = value;
            }
        } else if (fw.column.type == ColumnDef::Bool) {
            f.op = Filter::Equals;
            f.value = value;
        } else {
            f.op = Filter::Contains;
            f.value = value;
        }

        filters.append(f);
    }

    return filters;
}

// ─── Data Refresh ────────────────────────────────────────────────────────

void LogWidget::refreshData() {
    QVector<Filter> filters = collectFilters();
    QString ftsQuery = m_searchEdit->text().trimmed();

    QVector<QVector<QString>> rows;
    QStringList headers;
    int totalCount = 0;

    bool ok = LogDatabase::instance().queryRows(
        m_fileId, m_currentPage * PAGE_SIZE, PAGE_SIZE,
        filters, ftsQuery, rows, headers, totalCount);

    if (!ok) {
        qWarning() << "LogWidget::refreshData: Query failed";
        return;
    }

    // Update table view
    m_tableModel->clear();
    m_tableModel->setHorizontalHeaderLabels(headers);

    for (const auto& row : rows) {
        QList<QStandardItem*> items;
        for (const auto& cell : row) {
            items.append(new QStandardItem(cell));
        }
        m_tableModel->appendRow(items);
    }

    m_tableView->resizeColumnsToContents();

    // Update text view
    if (m_viewStack->currentIndex() == 1) {
        QString text;
        // raw column is index 1 in headers
        int rawIndex = headers.indexOf("raw");
        if (rawIndex < 0) rawIndex = 1;

        for (const auto& row : rows) {
            if (rawIndex < row.size())
                text += row[rawIndex] + "\n";
        }
        m_textBrowser->setPlainText(text);
    }

    m_labelState->setText(QString("Showing %1 of %2 rows")
                              .arg(rows.size()).arg(totalCount));
}
