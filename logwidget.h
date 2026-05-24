#ifndef LOGWIDGET_H
#define LOGWIDGET_H

#include <QThread>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <QMap>
#include "logdatabase.h"

class FileWorker;
class ProcessWorker;
class StreamWorker;
class QJsonValue;
class QTextBrowser;
class QLineEdit;
class QProgressBar;
class QLabel;
class QCheckBox;
class QComboBox;
class QVBoxLayout;
class QScrollBar;
class QPushButton;

class LogWidget : public QWidget
{
    Q_OBJECT

public:
    enum class SourceType {
        File,
        Stdin,
        Command
    };

    explicit LogWidget(const QString& filePath, int fileId, QWidget* parent = nullptr);
    explicit LogWidget(SourceType sourceType, const QString& displayName, int fileId, QWidget* parent = nullptr);
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
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUi();
    void refreshData();
    void updateStatusLabel();
    void updateMetadataStatusLabel();
    QString buildRowHtml(const QVector<QString>& row, const QMap<QString, int>& jsonFieldWidths) const;
    bool includeMetadataInRows() const;
    bool sortByTimestamp() const;
    int currentRowSpace() const;
    QString highlightFindWords(const QString& raw) const;
    QString formatJsonLine(const QString& raw, const QMap<QString, int>& jsonFieldWidths) const;
    QVector<QPair<QString, QString>> jsonFieldsForRaw(const QString& raw) const;
    void flattenJsonValue(const QString& path, const QJsonValue& value, QVector<QPair<QString, QString>>& out) const;
    QString jsonValueToText(const QJsonValue& value) const;
    QString jsonFilterToCompactObject(const QVector<QPair<QString, QString>>& fields) const;
    QStringList jsonFilterTokens(bool excludes) const;
    bool jsonFieldAllowed(const QString& path) const;
    bool jsonPathMatches(const QString& path, const QString& pattern) const;
    QStringList currentFindWords() const;
    int visibleRowCount() const;
    void updateScrollBar();
    void jumpToTextMatch(int fromLineNumber, bool backwards, const QString& notFoundText);
    void moveFiltered(int steps);
    int filteredLineAt(int lineNumber, bool backwards) const;

    void setPointer(int p, bool force = false, bool backwards = false);
    void fillBuffer();
    void applyBufferToView();
    void loadSettings();
    void saveSettings() const;
    void loadComboHistory(QComboBox* combo, QStringList& history, const QString& settingsKey);
    void rememberComboText(QComboBox* combo, QStringList& history, const QString& settingsKey);

    QString m_filePath;
    int m_fileId;
    SourceType m_sourceType = SourceType::File;

    QThread* m_workerThread = nullptr;
    FileWorker* m_worker = nullptr;
    ProcessWorker* m_processWorker = nullptr;
    StreamWorker* m_streamWorker = nullptr;

    QTimer* m_refreshTimer = nullptr;
    QTimer* m_metadataStatusTimer = nullptr;

    QVBoxLayout* m_mainLayout = nullptr;

    QComboBox* m_searchCombo = nullptr;
    QPushButton* m_searchButton = nullptr;
    QLabel* m_searchStatus = nullptr;
    QStringList m_ftsFilterHistory;
    QString m_ftsFilter;

    QWidget* m_textFindBar = nullptr;
    QComboBox* m_textFindCombo = nullptr;
    QPushButton* m_textFindFirst = nullptr;
    QPushButton* m_textFindPrev = nullptr;
    QPushButton* m_textFindNext = nullptr;
    QPushButton* m_textFindLast = nullptr;
    QPushButton* m_textFindClear = nullptr;
    QLabel* m_textFindStatus = nullptr;
    QStringList m_textFindHistory;
    QStringList m_findWords;

    QTextBrowser* m_textBrowser = nullptr;
    QCheckBox* m_wrapCheck = nullptr;
    QCheckBox* m_showLineNumberCheck = nullptr;
    QCheckBox* m_showTimestampCheck = nullptr;
    QCheckBox* m_showLogLevelCheck = nullptr;
    QCheckBox* m_sortTimestampCheck = nullptr;
    QCheckBox* m_jsonHelperCheck = nullptr;
    QComboBox* m_jsonFieldFilterCombo = nullptr;
    QCheckBox* m_jsonCompactCheck = nullptr;
    QCheckBox* m_jsonOnlyValuesCheck = nullptr;
    QStringList m_jsonFieldFilterHistory;
    int m_searchHistoryLimit = 20;
    QScrollBar* m_logScrollBar = nullptr;

    QLabel* m_labelSize = nullptr;
    QLabel* m_labelLines = nullptr;
    QLabel* m_labelState = nullptr;
    QLabel* m_metadataStatus = nullptr;
    QProgressBar* m_progressBar = nullptr;

    int m_bufferPointer = 0;
    QVector<QVector<QString>> m_buffer;
    QStringList m_bufferHeaders;

    qint64 m_fileSize = 0;
    qint32 m_totalLines = 0;
};

#endif // LOGWIDGET_H
