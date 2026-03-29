#ifndef LOGWIDGET_H
#define LOGWIDGET_H

#include <QWidget>
#include <QThread>
#include <QVector>
#include <QStandardItemModel>
#include "schemadetector.h"
#include "logdatabase.h"

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

struct FilterWidget {
    QComboBox* logicCombo = nullptr;   // AND / OR / NOT
    QLabel* label = nullptr;
    QWidget* input = nullptr;          // QLineEdit, QComboBox, or custom
    ColumnDef column;
};

class LogWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LogWidget(const QString& filePath, int fileId, QWidget *parent = nullptr);
    ~LogWidget();

    int fileId() const { return m_fileId; }
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

private:
    void setupUi();
    void buildFilterWidgets(const QVector<ColumnDef>& columns);
    void refreshData();
    QVector<Filter> collectFilters() const;

    QString m_filePath;
    int m_fileId;

    // Worker thread
    QThread* m_workerThread = nullptr;
    FileWorker* m_worker = nullptr;

    // Schema
    QVector<ColumnDef> m_columns;

    // UI elements
    QVBoxLayout* m_mainLayout = nullptr;

    // Search bar
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_searchButton = nullptr;

    // View toggle
    QPushButton* m_viewToggleButton = nullptr;
    QStackedWidget* m_viewStack = nullptr;
    QTableView* m_tableView = nullptr;
    QTextBrowser* m_textBrowser = nullptr;
    QStandardItemModel* m_tableModel = nullptr;

    // Checkboxes
    QCheckBox* m_highlightCheck = nullptr;
    QCheckBox* m_filterOnlyCheck = nullptr;
    QCheckBox* m_wrapCheck = nullptr;

    // Dynamic filters panel
    QGroupBox* m_filterGroup = nullptr;
    QVBoxLayout* m_filterLayout = nullptr;
    QVector<FilterWidget> m_filterWidgets;

    // Status bar
    QLabel* m_labelSize = nullptr;
    QLabel* m_labelLines = nullptr;
    QLabel* m_labelState = nullptr;
    QProgressBar* m_progressBar = nullptr;

    // State
    qint64 m_fileSize = 0;
    qint32 m_totalLines = 0;
    bool m_isTableView = true;

    // Pagination
    int m_currentPage = 0;
    static constexpr int PAGE_SIZE = 10000;
};

#endif // LOGWIDGET_H
