#ifndef LOGWIDGET_H
#define LOGWIDGET_H

#include <QMap>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include "logdatabase.h"
#include "schemadetector.h"

class FileWorker;
class QTableView;
class QTextBrowser;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QLabel;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QVBoxLayout;
class QStackedWidget;
class QScrollBar;
class QSpinBox;

// A single row in the dynamic filter panel:
// [Logic▼] [Column▼] [Operator▼] [Value          ] [×]
struct FilterRow {
    QComboBox* logicCombo  = nullptr;  // AND / OR / NOT
    QComboBox* columnCombo = nullptr;  // column name
    QComboBox* opCombo     = nullptr;  // contains / = / != / > / <
    QLineEdit* valueEdit   = nullptr;
    QPushButton* removeBtn = nullptr;
    QWidget* container     = nullptr;  // owning row widget
};

class LogWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LogWidget(const QString& filePath, int fileId, QWidget *parent = nullptr);
    ~LogWidget();

    int fileId() const   { return m_fileId; }
    QString filePath() const { return m_filePath; }

signals:
    void loadingFinished(int fileId);

private slots:
    void onSchemaReady(int fileId, QVector<ColumnDef> columns);
    void onProgressUpdate(int fileId, qint64 bytesProcessed, qint64 totalBytes, qint32 linesProcessed);
    void onChunkInserted(int fileId, qint32 totalLinesInserted);
    void onFinished(int fileId);
    void onError(int fileId, QString message);

    void onApplyFilters();
    void onToggleView();
    void onWrapToggled(bool checked);

    void onHeaderContextMenu(const QPoint& pos);
    void onCopySelection();
    void onCellClicked(const QModelIndex& proxyIndex);

private:
    void setupUi();
    void updateFilterColumns();
    void addFilterRow(const QString& column = QString(), const QString& value = QString());
    void removeFilterRow(QWidget* container);
    void applyColumnVisibility();
    void refreshData();
    QVector<Filter> collectFilters() const;
    void switchToTextView();
    void switchToTableView();

    QString m_filePath;
    int     m_fileId;

    // Worker thread
    QThread*     m_workerThread = nullptr;
    FileWorker*  m_worker       = nullptr;

    // Schema & state
    QVector<ColumnDef> m_columns;
    QStringList        m_filterColumnNames; // available columns for filter combos
    bool               m_hasDynamicColumns = false;

    // Per-column visibility (persists across refreshData calls)
    QMap<QString, bool> m_columnVisibility;

    // Debounce timer: avoids refreshData on every chunk during ingestion
    QTimer* m_refreshTimer = nullptr;
    static constexpr int REFRESH_DEBOUNCE_MS = 1500;

    // UI — main layout
    QVBoxLayout* m_mainLayout = nullptr;

    // Search bar
    QLineEdit*   m_searchEdit    = nullptr;
    QPushButton* m_searchButton  = nullptr;
    QCheckBox*   m_highlightCheck = nullptr;
    QCheckBox*   m_filterOnlyCheck = nullptr;

    // View toggle
    QPushButton*         m_viewToggleButton = nullptr;
    QStackedWidget*      m_viewStack        = nullptr;
    QTableView*          m_tableView        = nullptr;
    QTextBrowser*        m_textBrowser      = nullptr;
    QStandardItemModel*  m_tableModel       = nullptr;
    QSortFilterProxyModel* m_proxyModel     = nullptr;
    QCheckBox*           m_wrapCheck        = nullptr;

    // Dynamic filter panel
    QGroupBox*   m_filterGroup  = nullptr;
    QVBoxLayout* m_filterLayout = nullptr; // inside scroll widget
    QVector<FilterRow> m_filterRows;

    // Status bar
    QLabel*       m_labelSize  = nullptr;
    QLabel*       m_labelLines = nullptr;
    QLabel*       m_labelState = nullptr;
    QProgressBar* m_progressBar = nullptr;

    // Pagination controls (in status bar)
    QSpinBox*     m_offsetSpin = nullptr;   // starting row offset
    QSpinBox*     m_limitSpin  = nullptr;   // rows per page

    // 3-state sort state (managed by sectionClicked lambda)
    int m_sortColumn = -1;  // -1 = no active sort
    int m_sortCycle  = 0;   // 0=none  1=asc  2=desc

    // Pagination / scroll state
    int  m_lastTotalCount = 0;
    bool m_pageChanging   = false;  // true while refreshData populates model
    bool m_scrollToBottom = false;  // request view to scroll to bottom after next refresh

    // State
    qint64  m_fileSize   = 0;
    qint32  m_totalLines = 0;
    int     m_currentPage = 0;
};

#endif // LOGWIDGET_H
