#ifndef LOGWIDGET_H
#define LOGWIDGET_H

#include <QThread>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <QTextEdit>
#include "logdatabase.h"

class FileWorker;
class QTextBrowser;
class QLineEdit;
class QProgressBar;
class QLabel;
class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QVBoxLayout;
class QScrollBar;
class QSpinBox;
class QPushButton;

class LogWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LogWidget(const QString& filePath, int fileId, QWidget* parent = nullptr);
    ~LogWidget();

    int fileId() const { return m_fileId; }
    QString filePath() const { return m_filePath; }

signals:
    void loadingFinished(int fileId);

private slots:
    void onProgressUpdate(int fileId, qint64 bytesProcessed, qint64 totalBytes, qint32 linesProcessed);
    void onChunkInserted(int fileId, qint32 totalLinesInserted);
    void onFinished(int fileId);
    void onError(int fileId, QString message);

    void onApplyFilters();
    void onWrapToggled(bool checked);
    void onToggleTextFindBar();
    void onTextFindSearch();
    void onTextFindNext();
    void onTextFindPrev();
    void onTextFindFirst();
    void onTextFindLast();
    void onTextFindClear();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupUi();
    void refreshData();
    void updateStatusLabel();
    qint64 currentFromTimestampMs() const;
    qint64 currentToTimestampMs() const;
    bool onlyWithTimestamp() const;
    SortMode currentSortMode() const;
    SortOrder currentSortOrder() const;
    void queryRows(int offset, int limit, QVector<QVector<QString>>& outRows, int& totalCount) const;

    void setPointer(int p, bool force = false);
    void fillBuffer();
    void updateBufferDelta(int delta);
    void applyBufferToView();
    void checkPrefetch();

    QString m_filePath;
    int m_fileId;

    QThread* m_workerThread = nullptr;
    FileWorker* m_worker = nullptr;

    QTimer* m_refreshTimer = nullptr;
    static constexpr int REFRESH_DEBOUNCE_MS = 1500;

    QVBoxLayout* m_mainLayout = nullptr;

    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_searchButton = nullptr;
    QCheckBox* m_fromCheck = nullptr;
    QDateTimeEdit* m_fromDateTimeEdit = nullptr;
    QCheckBox* m_toCheck = nullptr;
    QDateTimeEdit* m_toDateTimeEdit = nullptr;
    QCheckBox* m_onlyTimestampedCheck = nullptr;
    QComboBox* m_sortCombo = nullptr;
    QComboBox* m_sortOrderCombo = nullptr;

    QWidget* m_textFindBar = nullptr;
    QComboBox* m_textFindCombo = nullptr;
    QPushButton* m_textFindFirst = nullptr;
    QPushButton* m_textFindPrev = nullptr;
    QPushButton* m_textFindNext = nullptr;
    QPushButton* m_textFindLast = nullptr;
    QPushButton* m_textFindClear = nullptr;
    QCheckBox* m_textFindRegex = nullptr;
    QCheckBox* m_textFindCase = nullptr;
    QLabel* m_textFindStatus = nullptr;
    QStringList m_textFindHistory;
    QList<QTextEdit::ExtraSelection> m_textFindHighlights;
    int m_textFindCurrent = -1;

    QTextBrowser* m_textBrowser = nullptr;
    QCheckBox* m_wrapCheck = nullptr;
    QScrollBar* m_logScrollBar = nullptr;

    QLabel* m_labelSize = nullptr;
    QLabel* m_labelLines = nullptr;
    QLabel* m_labelState = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QSpinBox* m_bufferSizeSpin = nullptr;

    static constexpr int DEFAULT_BUFFER = 5000;
    static constexpr int PREFETCH_MARGIN = 1000;
    int m_bufferPointer = 0;
    int m_totalRowCount = 0;
    QVector<QVector<QString>> m_buffer;
    QStringList m_bufferHeaders;

    qint64 m_fileSize = 0;
    qint32 m_totalLines = 0;
};

#endif // LOGWIDGET_H
