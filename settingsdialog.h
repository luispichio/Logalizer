#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "appsettings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    AppSettingsValues values() const;

private:
    void setupUi();
    void loadValues(const AppSettingsValues& values);

    QSpinBox* m_recentFilesLimit = nullptr;
    QSpinBox* m_searchHistoryLimit = nullptr;
    QSpinBox* m_refreshDebounceMs = nullptr;

    QSpinBox* m_fileBatchSize = nullptr;
    QSpinBox* m_streamBatchSize = nullptr;
    QSpinBox* m_processBatchSize = nullptr;

    QCheckBox* m_jsonEnabled = nullptr;
    QCheckBox* m_jsonCompact = nullptr;
    QCheckBox* m_jsonOnlyValues = nullptr;
    QLineEdit* m_jsonFieldFilter = nullptr;

    QSpinBox* m_metadataRegexScanLimit = nullptr;
    QCheckBox* m_metadataPreferRegexRules = nullptr;
    QComboBox* m_timestampDisplayMode = nullptr;
    QLineEdit* m_timestampCustomFormat = nullptr;

    QCheckBox* m_formatDetectionEnabled = nullptr;
    QSpinBox* m_formatDetectionSampleLines = nullptr;
    QLineEdit* m_formatDetectionUserDirectory = nullptr;
    QPlainTextEdit* m_formatDetectionCustomJson = nullptr;

    QCheckBox* m_aiEnabled = nullptr;
    QLineEdit* m_aiProvider = nullptr;
    QLineEdit* m_aiEndpoint = nullptr;
    QLineEdit* m_aiModel = nullptr;
};

#endif // SETTINGSDIALOG_H
