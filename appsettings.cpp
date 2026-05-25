#include "appsettings.h"

#include <QStandardPaths>
#include <QSettings>

namespace {
constexpr int DefaultRecentFilesLimit = 15;
constexpr int DefaultSearchHistoryLimit = 20;
constexpr int DefaultRefreshDebounceMs = 1500;
constexpr int DefaultFileBatchSize = 2000;
constexpr int DefaultStreamBatchSize = 10000;
constexpr int DefaultProcessBatchSize = 10000;
constexpr int DefaultMetadataRegexScanLimit = 1024;
constexpr int DefaultFormatDetectionSampleLines = 200;

int boundedInt(const QSettings& settings, const char* key, int defaultValue, int minValue, int maxValue) {
    return qBound(minValue, settings.value(key, defaultValue).toInt(), maxValue);
}
}

AppSettingsValues AppSettings::load() {
    QSettings settings("Logalizer", "Logalizer");
    AppSettingsValues values;

    values.recentFilesLimit = boundedInt(settings, "mainWindow/recentFilesLimit", DefaultRecentFilesLimit, 1, 50);
    values.searchHistoryLimit = boundedInt(settings, "logWidget/searchHistoryLimit", DefaultSearchHistoryLimit, 1, 100);
    values.refreshDebounceMs = boundedInt(settings, "logWidget/refreshDebounceMs", DefaultRefreshDebounceMs, 100, 10000);

    values.fileBatchSize = boundedInt(settings, "ingestion/fileBatchSize", DefaultFileBatchSize, 100, 100000);
    values.streamBatchSize = boundedInt(settings, "ingestion/streamBatchSize", DefaultStreamBatchSize, 100, 100000);
    values.processBatchSize = boundedInt(settings, "ingestion/processBatchSize", DefaultProcessBatchSize, 100, 100000);

    values.jsonEnabled = settings.value("logWidget/jsonEnabled", false).toBool();
    values.jsonCompact = settings.value("logWidget/jsonCompact", true).toBool();
    values.jsonOnlyValues = settings.value("logWidget/jsonOnlyValues", false).toBool();
    values.jsonFieldFilter = settings.value("logWidget/jsonFieldFilter", QString()).toString();

    values.metadataRegexScanLimit = boundedInt(settings, "metadata/regexScanLimit", DefaultMetadataRegexScanLimit, 128, 8192);
    values.metadataPreferRegexRules = settings.value("metadata/preferRegexRules", false).toBool();
    values.timestampDisplayMode = settings.value("logWidget/timestampDisplayMode", "iso-utc").toString();
    if (values.timestampDisplayMode != "original" && values.timestampDisplayMode != "iso-local"
        && values.timestampDisplayMode != "iso-utc" && values.timestampDisplayMode != "custom") {
        values.timestampDisplayMode = "iso-utc";
    }
    values.timestampCustomFormat = settings.value("logWidget/timestampCustomFormat", "yyyy-MM-dd HH:mm:ss.zzz").toString();

    const QString defaultFormatsDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/formats";
    values.formatDetectionEnabled = settings.value("formatDetection/enabled", true).toBool();
    values.formatDetectionSampleLines = boundedInt(settings, "formatDetection/sampleLines", DefaultFormatDetectionSampleLines, 10, 5000);
    values.formatDetectionUserDirectory = settings.value("formatDetection/userDirectory", defaultFormatsDir).toString();
    values.formatDetectionCustomJson = settings.value("formatDetection/customDefinitionsJson", QString()).toString();

    values.aiEnabled = settings.value("ai/enabled", false).toBool();
    values.aiProvider = settings.value("ai/provider", QString()).toString();
    values.aiEndpoint = settings.value("ai/endpoint", QString()).toString();
    values.aiModel = settings.value("ai/model", QString()).toString();

    return values;
}

void AppSettings::save(const AppSettingsValues& values) {
    QSettings settings("Logalizer", "Logalizer");

    settings.setValue("mainWindow/recentFilesLimit", qBound(1, values.recentFilesLimit, 50));
    settings.setValue("logWidget/searchHistoryLimit", qBound(1, values.searchHistoryLimit, 100));
    settings.setValue("logWidget/refreshDebounceMs", qBound(100, values.refreshDebounceMs, 10000));

    settings.setValue("ingestion/fileBatchSize", qBound(100, values.fileBatchSize, 100000));
    settings.setValue("ingestion/streamBatchSize", qBound(100, values.streamBatchSize, 100000));
    settings.setValue("ingestion/processBatchSize", qBound(100, values.processBatchSize, 100000));

    settings.setValue("logWidget/jsonEnabled", values.jsonEnabled);
    settings.setValue("logWidget/jsonCompact", values.jsonCompact);
    settings.setValue("logWidget/jsonOnlyValues", values.jsonOnlyValues);
    settings.setValue("logWidget/jsonFieldFilter", values.jsonFieldFilter.trimmed());

    settings.setValue("metadata/regexScanLimit", qBound(128, values.metadataRegexScanLimit, 8192));
    settings.setValue("metadata/preferRegexRules", values.metadataPreferRegexRules);
    settings.setValue("logWidget/timestampDisplayMode", values.timestampDisplayMode);
    settings.setValue("logWidget/timestampCustomFormat", values.timestampCustomFormat.trimmed());

    settings.setValue("formatDetection/enabled", values.formatDetectionEnabled);
    settings.setValue("formatDetection/sampleLines", qBound(10, values.formatDetectionSampleLines, 5000));
    settings.setValue("formatDetection/userDirectory", values.formatDetectionUserDirectory.trimmed());
    settings.setValue("formatDetection/customDefinitionsJson", values.formatDetectionCustomJson.trimmed());

    settings.setValue("ai/enabled", values.aiEnabled);
    settings.setValue("ai/provider", values.aiProvider.trimmed());
    settings.setValue("ai/endpoint", values.aiEndpoint.trimmed());
    settings.setValue("ai/model", values.aiModel.trimmed());
}

int AppSettings::recentFilesLimit() {
    return load().recentFilesLimit;
}

int AppSettings::searchHistoryLimit() {
    return load().searchHistoryLimit;
}

int AppSettings::refreshDebounceMs() {
    return load().refreshDebounceMs;
}

int AppSettings::fileBatchSize() {
    return load().fileBatchSize;
}

int AppSettings::streamBatchSize() {
    return load().streamBatchSize;
}

int AppSettings::processBatchSize() {
    return load().processBatchSize;
}

bool AppSettings::formatDetectionEnabled() {
    return load().formatDetectionEnabled;
}

int AppSettings::formatDetectionSampleLines() {
    return load().formatDetectionSampleLines;
}
