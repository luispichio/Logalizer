#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QString>

struct AppSettingsValues {
    int recentFilesLimit = 15;
    int searchHistoryLimit = 20;
    int refreshDebounceMs = 1500;

    int fileBatchSize = 2000;
    int streamBatchSize = 10000;
    int processBatchSize = 10000;

    bool jsonEnabled = false;
    bool jsonCompact = true;
    bool jsonOnlyValues = false;
    QString jsonFieldFilter;

    int metadataRegexScanLimit = 1024;
    bool metadataPreferRegexRules = false;

    bool aiEnabled = false;
    QString aiProvider;
    QString aiEndpoint;
    QString aiModel;
};

class AppSettings
{
public:
    static AppSettingsValues load();
    static void save(const AppSettingsValues& values);

    static int recentFilesLimit();
    static int searchHistoryLimit();
    static int refreshDebounceMs();
    static int fileBatchSize();
    static int streamBatchSize();
    static int processBatchSize();
};

#endif // APPSETTINGS_H
