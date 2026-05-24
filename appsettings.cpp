#include "appsettings.h"

#include <QSettings>

namespace {
constexpr int DefaultRecentFilesLimit = 15;
constexpr int DefaultSearchHistoryLimit = 20;
constexpr int DefaultRefreshDebounceMs = 1500;
constexpr int DefaultFileBatchSize = 2000;
constexpr int DefaultStreamBatchSize = 10000;
constexpr int DefaultProcessBatchSize = 10000;
constexpr int DefaultMetadataRegexScanLimit = 1024;

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
