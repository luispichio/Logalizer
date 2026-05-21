#include "metadataconfig.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QtCore/QtLogging>

namespace {
constexpr const char* InitializedKey = "metadata/rulesInitialized";
constexpr const char* TimestampRulesKey = "metadata/timestampRegexRulesJson";
constexpr const char* LevelRulesKey = "metadata/levelRegexRulesJson";

QVector<MetadataRegexRule> defaultTimestampRules() {
    return {
        {"JSON timestamp string", "\\\"(?:@timestamp|timestamp|time|datetime|date|ts)\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"", 1, true},
        {"JSON timestamp epoch", "\\\"(?:timestamp|time|ts)\\\"\\s*:\\s*(\\d{10}|\\d{13})", 1, true},
    };
}

QVector<MetadataRegexRule> defaultLevelRules() {
    return {
        {"JSON level string", "\\\"(?:level|severity|severity_text|log\\.level|lvl)\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"", 1, true},
        {"JSON level number", "\\\"(?:level|severity|lvl)\\\"\\s*:\\s*(\\d+)", 1, true},
    };
}

QByteArray rulesToJson(const QVector<MetadataRegexRule>& rules) {
    QJsonArray array;
    for (const MetadataRegexRule& rule : rules) {
        QJsonObject object;
        object["name"] = rule.name;
        object["pattern"] = rule.pattern;
        object["captureGroup"] = rule.captureGroup;
        object["enabled"] = rule.enabled;
        array.append(object);
    }
    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

QVector<MetadataRegexRule> rulesFromJson(const QString& json) {
    QVector<MetadataRegexRule> rules;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qWarning() << "Metadata config: invalid regex rules JSON:" << parseError.errorString();
        return rules;
    }

    const QJsonArray array = doc.array();
    rules.reserve(array.size());
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        MetadataRegexRule rule;
        rule.name = object.value("name").toString();
        rule.pattern = object.value("pattern").toString();
        rule.captureGroup = object.value("captureGroup").toInt(1);
        rule.enabled = object.value("enabled").toBool(true);
        if (!rule.pattern.isEmpty()) {
            rules.append(rule);
        }
    }
    return rules;
}

QVector<CompiledMetadataRegexRule> compileRules(const QVector<MetadataRegexRule>& rules) {
    QVector<CompiledMetadataRegexRule> compiled;
    compiled.reserve(rules.size());
    for (const MetadataRegexRule& rule : rules) {
        if (!rule.enabled || rule.pattern.isEmpty()) {
            continue;
        }

        QRegularExpression regex(rule.pattern, QRegularExpression::CaseInsensitiveOption);
        regex.optimize();
        if (!regex.isValid()) {
            qWarning() << "Metadata config: invalid regex rule" << rule.name << regex.errorString();
            continue;
        }

        CompiledMetadataRegexRule compiledRule;
        compiledRule.name = rule.name;
        compiledRule.regex = regex;
        compiledRule.captureGroup = qMax(0, rule.captureGroup);
        compiledRule.enabled = true;
        compiled.append(compiledRule);
    }
    return compiled;
}

void seedDefaultsIfNeeded(QSettings& settings) {
    if (settings.value(InitializedKey, false).toBool()) {
        return;
    }

    settings.setValue(TimestampRulesKey, QString::fromUtf8(rulesToJson(defaultTimestampRules())));
    settings.setValue(LevelRulesKey, QString::fromUtf8(rulesToJson(defaultLevelRules())));
    settings.setValue("metadata/regexScanLimit", 1024);
    settings.setValue("metadata/preferRegexRules", false);
    settings.setValue(InitializedKey, true);
}
}

MetadataDetectionConfig loadMetadataDetectionConfig() {
    QSettings settings("Logalizer", "Logalizer");
    seedDefaultsIfNeeded(settings);

    MetadataDetectionConfig config;
    config.regexScanLimit = qBound(128, settings.value("metadata/regexScanLimit", 1024).toInt(), 8192);
    config.preferRegexRules = settings.value("metadata/preferRegexRules", false).toBool();
    config.timestampRules = compileRules(rulesFromJson(settings.value(TimestampRulesKey).toString()));
    config.levelRules = compileRules(rulesFromJson(settings.value(LevelRulesKey).toString()));
    return config;
}
