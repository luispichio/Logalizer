#include "settingsdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
QSpinBox* spinBox(QWidget* parent, int minValue, int maxValue, int step, const QString& suffix = QString()) {
    auto* box = new QSpinBox(parent);
    box->setRange(minValue, maxValue);
    box->setSingleStep(step);
    box->setSuffix(suffix);
    return box;
}

QLabel* noteLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setStyleSheet("color:#6c757d;");
    return label;
}
}

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUi();
    loadValues(AppSettings::load());
}

void SettingsDialog::setupUi() {
    setWindowTitle("Settings");
    resize(560, 420);

    auto* mainLayout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);

    {
        auto* page = new QWidget(tabs);
        auto* layout = new QFormLayout(page);
        layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        m_recentFilesLimit = spinBox(page, 1, 50, 1);
        m_searchHistoryLimit = spinBox(page, 1, 100, 1);
        m_refreshDebounceMs = spinBox(page, 100, 10000, 100, " ms");

        layout->addRow("Recent files limit:", m_recentFilesLimit);
        layout->addRow("Search history limit:", m_searchHistoryLimit);
        layout->addRow("Refresh debounce:", m_refreshDebounceMs);
        layout->addRow(noteLabel("Refresh and history settings apply to new tabs. Recent files limit applies when saving settings.", page));

        tabs->addTab(page, "General");
    }

    {
        auto* page = new QWidget(tabs);
        auto* layout = new QFormLayout(page);
        layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        m_fileBatchSize = spinBox(page, 100, 100000, 500);
        m_streamBatchSize = spinBox(page, 100, 100000, 500);
        m_processBatchSize = spinBox(page, 100, 100000, 500);

        layout->addRow("File batch size:", m_fileBatchSize);
        layout->addRow("Stdin batch size:", m_streamBatchSize);
        layout->addRow("Command batch size:", m_processBatchSize);
        layout->addRow(noteLabel("Batch sizes apply to new ingestions. Defaults are conservative for large log files.", page));

        tabs->addTab(page, "Performance");
    }

    {
        auto* page = new QWidget(tabs);
        auto* layout = new QFormLayout(page);
        layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        m_jsonEnabled = new QCheckBox("Enable JSON helper by default", page);
        m_jsonCompact = new QCheckBox("Compact mode by default", page);
        m_jsonOnlyValues = new QCheckBox("Only values by default", page);
        m_jsonFieldFilter = new QLineEdit(page);
        m_jsonFieldFilter->setPlaceholderText("level,msg,user.id,-metadata.*");

        layout->addRow(m_jsonEnabled);
        layout->addRow(m_jsonCompact);
        layout->addRow(m_jsonOnlyValues);
        layout->addRow("Default field filter:", m_jsonFieldFilter);

        tabs->addTab(page, "JSON Helper");
    }

    {
        auto* page = new QWidget(tabs);
        auto* layout = new QFormLayout(page);
        layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        m_metadataRegexScanLimit = spinBox(page, 128, 8192, 128);
        m_metadataPreferRegexRules = new QCheckBox("Prefer regex rules over built-in detection", page);

        layout->addRow("Regex scan limit:", m_metadataRegexScanLimit);
        layout->addRow(m_metadataPreferRegexRules);
        layout->addRow(noteLabel("Advanced format and rule management is reserved for automatic log format detection.", page));

        tabs->addTab(page, "Metadata");
    }

    {
        auto* page = new QWidget(tabs);
        auto* layout = new QFormLayout(page);
        layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        m_formatDetectionEnabled = new QCheckBox("Enable automatic format detection", page);
        m_formatDetectionSampleLines = spinBox(page, 10, 5000, 50);
        m_formatDetectionUserDirectory = new QLineEdit(page);
        m_formatDetectionCustomJson = new QPlainTextEdit(page);
        m_formatDetectionCustomJson->setPlaceholderText("{\n  \"my_format\": {\n    \"regex\": {\n      \"line\": { \"pattern\": \"...\" }\n    },\n    \"timestamp-field\": \"timestamp\",\n    \"level-field\": \"level\"\n  }\n}");
        m_formatDetectionCustomJson->setMinimumHeight(120);

        layout->addRow(m_formatDetectionEnabled);
        layout->addRow("Sample lines:", m_formatDetectionSampleLines);
        layout->addRow("User formats directory:", m_formatDetectionUserDirectory);
        layout->addRow("Custom definitions JSON:", m_formatDetectionCustomJson);
        layout->addRow(noteLabel("Supports a tolerant subset of LNAV format definitions. Unknown properties are ignored.", page));

        tabs->addTab(page, "Format Detection");
    }

    {
        auto* page = new QWidget(tabs);
        auto* layout = new QFormLayout(page);
        layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

        m_aiEnabled = new QCheckBox("Enable AI features", page);
        m_aiProvider = new QLineEdit(page);
        m_aiEndpoint = new QLineEdit(page);
        m_aiModel = new QLineEdit(page);
        m_aiProvider->setPlaceholderText("openai, local, custom...");
        m_aiEndpoint->setPlaceholderText("https://...");
        m_aiModel->setPlaceholderText("model name");

        layout->addRow(m_aiEnabled);
        layout->addRow("Provider:", m_aiProvider);
        layout->addRow("Endpoint:", m_aiEndpoint);
        layout->addRow("Model:", m_aiModel);
        layout->addRow(noteLabel("AI integration is not implemented yet. Future actions will require explicit confirmation before sending selected log text.", page));

        tabs->addTab(page, "AI");
    }

    mainLayout->addWidget(tabs);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
}

void SettingsDialog::loadValues(const AppSettingsValues& values) {
    m_recentFilesLimit->setValue(values.recentFilesLimit);
    m_searchHistoryLimit->setValue(values.searchHistoryLimit);
    m_refreshDebounceMs->setValue(values.refreshDebounceMs);

    m_fileBatchSize->setValue(values.fileBatchSize);
    m_streamBatchSize->setValue(values.streamBatchSize);
    m_processBatchSize->setValue(values.processBatchSize);

    m_jsonEnabled->setChecked(values.jsonEnabled);
    m_jsonCompact->setChecked(values.jsonCompact);
    m_jsonOnlyValues->setChecked(values.jsonOnlyValues);
    m_jsonFieldFilter->setText(values.jsonFieldFilter);

    m_metadataRegexScanLimit->setValue(values.metadataRegexScanLimit);
    m_metadataPreferRegexRules->setChecked(values.metadataPreferRegexRules);

    m_formatDetectionEnabled->setChecked(values.formatDetectionEnabled);
    m_formatDetectionSampleLines->setValue(values.formatDetectionSampleLines);
    m_formatDetectionUserDirectory->setText(values.formatDetectionUserDirectory);
    m_formatDetectionCustomJson->setPlainText(values.formatDetectionCustomJson);

    m_aiEnabled->setChecked(values.aiEnabled);
    m_aiProvider->setText(values.aiProvider);
    m_aiEndpoint->setText(values.aiEndpoint);
    m_aiModel->setText(values.aiModel);
}

AppSettingsValues SettingsDialog::values() const {
    AppSettingsValues values;

    values.recentFilesLimit = m_recentFilesLimit->value();
    values.searchHistoryLimit = m_searchHistoryLimit->value();
    values.refreshDebounceMs = m_refreshDebounceMs->value();

    values.fileBatchSize = m_fileBatchSize->value();
    values.streamBatchSize = m_streamBatchSize->value();
    values.processBatchSize = m_processBatchSize->value();

    values.jsonEnabled = m_jsonEnabled->isChecked();
    values.jsonCompact = m_jsonCompact->isChecked();
    values.jsonOnlyValues = m_jsonOnlyValues->isChecked();
    values.jsonFieldFilter = m_jsonFieldFilter->text();

    values.metadataRegexScanLimit = m_metadataRegexScanLimit->value();
    values.metadataPreferRegexRules = m_metadataPreferRegexRules->isChecked();

    values.formatDetectionEnabled = m_formatDetectionEnabled->isChecked();
    values.formatDetectionSampleLines = m_formatDetectionSampleLines->value();
    values.formatDetectionUserDirectory = m_formatDetectionUserDirectory->text();
    values.formatDetectionCustomJson = m_formatDetectionCustomJson->toPlainText();

    values.aiEnabled = m_aiEnabled->isChecked();
    values.aiProvider = m_aiProvider->text();
    values.aiEndpoint = m_aiEndpoint->text();
    values.aiModel = m_aiModel->text();

    return values;
}
