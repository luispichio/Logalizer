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
class QHBoxLayout;
class QStackedWidget;
class QScrollBar;
class QSpinBox;

// A single row in the dynamic filter panel:
// [Logic▼] [Column▼] [Operator▼] [Value          ] [×]
struct FilterRow {
    QComboBox* logicCombo  = nullptr;  // AND / OR / NOT
    QComboBox* columnCombo = nullptr;  // column name (original)
    QComboBox* opCombo     = nullptr;  // contains / = / != / > / <
    QLineEdit* valueEdit   = nullptr;
    QPushButton* removeBtn = nullptr;
    QWidget* container     = nullptr;  // owning row widget
};

class LogWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LogWidget(const QString& filePath, int fileId, QWidget* parent = nullptr);
    ~LogWidget();

    int     fileId()   const { return m_fileId; }
    QString filePath() const { return m_filePath; }

signals:
    void loadingFinished(int fileId);

private slots:
    void onSchemaReady(int fileId, QVector<ColumnDef> columns);
    void onProgressUpdate(int fileId, qint64 bytesProcessed, qint64 totalBytes, qint32 linesProcessed);
    void onChunkInserted(int fileId, qint32 totalLinesInserted);
    void onFinished(int fileId);
    void onError(int fileId, QString message);

    void onApplyFilters();   // resets p=0, forces full buffer fill
    void onToggleView();
    void onWrapToggled(bool checked);

    void onHeaderContextMenu(const QPoint& pos);
    void onCopySelection();
    void onCellClicked(const QModelIndex& proxyIndex);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupUi();
    void updateFilterColumns();
    void addFilterRow(const QString& column = QString(), const QString& value = QString());
    void removeFilterRow(QWidget* container);
    void applyColumnVisibility();
    void refreshData();                          // convenience: setPointer(p, force=true)
    QVector<Filter> collectFilters() const;
    void switchToTextView();
    void switchToTableView();
    void updateStatusLabel();

    // ── Virtual scroll / buffer ──────────────────────────────────────
    // The view always shows a sliding window of BUFFER rows from the DB.
    // p = m_bufferPointer = absolute row offset of the first row in the buffer.
    // Moving p by d: fetches only |d| new rows (delta), keeps the rest.
    void setPointer(int p, bool force = false); // set p, update buffer & view
    void fillBuffer();                          // full DB fetch at p
    void updateBufferDelta(int delta);          // incremental fetch (±d rows)
    void applyBufferToView();                   // push m_buffer → model + text view

    // ── Identity ─────────────────────────────────────────────────────
    QString m_filePath;
    int     m_fileId;

    // ── Worker thread ────────────────────────────────────────────────
    QThread*    m_workerThread = nullptr;
    FileWorker* m_worker       = nullptr;

    // ── Schema & column metadata ─────────────────────────────────────
    QVector<ColumnDef> m_columns;
    QStringList        m_filterColumnNames;
    bool               m_hasDynamicColumns = false;

    // ── Per-column visibility ────────────────────────────────────────
    QMap<QString, bool> m_columnVisibility;

    // ── Ingestion debounce timer ─────────────────────────────────────
    QTimer* m_refreshTimer = nullptr;
    static constexpr int REFRESH_DEBOUNCE_MS = 1500;

    // ── Main layout ──────────────────────────────────────────────────
    QVBoxLayout* m_mainLayout = nullptr;

    // ── Search bar ───────────────────────────────────────────────────
    QLineEdit*   m_searchEdit      = nullptr;
    QPushButton* m_searchButton    = nullptr;
    QCheckBox*   m_highlightCheck  = nullptr;
    QCheckBox*   m_filterOnlyCheck = nullptr;

    // ── View area (stacked: table | text) + custom scrollbar ─────────
    QPushButton*          m_viewToggleButton = nullptr;
    QStackedWidget*       m_viewStack        = nullptr;
    QTableView*           m_tableView        = nullptr;
    QTextBrowser*         m_textBrowser      = nullptr;
    QStandardItemModel*   m_tableModel       = nullptr;
    QSortFilterProxyModel* m_proxyModel      = nullptr;
    QCheckBox*            m_wrapCheck        = nullptr;
    QScrollBar*           m_logScrollBar     = nullptr;  // independent vertical scrollbar

    // ── Filter panel ─────────────────────────────────────────────────
    QGroupBox*   m_filterGroup  = nullptr;
    QVBoxLayout* m_filterLayout = nullptr;
    QVector<FilterRow> m_filterRows;

    // ── Status bar ───────────────────────────────────────────────────
    QLabel*       m_labelSize    = nullptr;
    QLabel*       m_labelLines   = nullptr;
    QLabel*       m_labelState   = nullptr;
    QProgressBar* m_progressBar  = nullptr;
    QSpinBox*     m_bufferSizeSpin = nullptr;  // configurable N (buffer size)

    // ── 3-state sort (none → asc → desc → none) ──────────────────────
    int m_sortColumn = -1;
    int m_sortCycle  = 0;  // 0=none 1=asc 2=desc

    // ── Virtual scroll state ─────────────────────────────────────────
    static constexpr int DEFAULT_BUFFER = 5000;
    int           m_bufferPointer = 0;    // p: absolute DB offset of buffer start
    int           m_totalRowCount = 0;    // total rows matching current filters
    QVector<QVector<QString>> m_buffer;   // N-row sliding window
    QStringList   m_bufferHeaders;        // column display names (stable post-schema)

    // ── File metadata ────────────────────────────────────────────────
    qint64  m_fileSize   = 0;
    qint32  m_totalLines = 0;
};

#endif // LOGWIDGET_H
