#include "logformat.h"

#include "appsettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtCore/QtLogging>

namespace {
void addFormatFromObject(const QString& id, const QJsonObject& object, const QString& source, QVector<LogFormatDefinition>& formats) {
    LogFormatDefinition format;
    format.id = id;
    format.title = object.value("title").toString(id);
    format.description = object.value("description").toString();
    format.source = source;
    format.filePattern = object.value("file-pattern").toString();
    format.json = object.value("json").toBool(false) || object.value("file-type").toString().compare("json", Qt::CaseInsensitive) == 0;
    format.timestampField = object.value("timestamp-field").toString();
    format.levelField = object.value("level-field").toString();
    format.bodyField = object.value("body-field").toString();
    format.timestampDivisor = object.value("timestamp-divisor").toDouble(1.0);

    const QJsonArray timestampFormats = object.value("timestamp-format").toArray();
    for (const QJsonValue& value : timestampFormats) {
        const QString text = value.toString();
        if (!text.isEmpty()) {
            format.timestampFormats.append(text);
        }
    }

    if (!format.filePattern.isEmpty()) {
        format.fileRegex = QRegularExpression(format.filePattern, QRegularExpression::CaseInsensitiveOption);
        if (!format.fileRegex.isValid()) {
            qWarning() << "LogFormatRegistry: invalid file-pattern" << format.id << format.fileRegex.errorString();
            format.fileRegex = QRegularExpression();
        }
    }

    const QJsonObject regexObject = object.value("regex").toObject();
    for (auto it = regexObject.constBegin(); it != regexObject.constEnd(); ++it) {
        const QJsonObject patternObject = it.value().toObject();
        const QString pattern = patternObject.value("pattern").toString();
        if (pattern.isEmpty()) {
            continue;
        }

        LogFormatPattern compiled;
        compiled.name = it.key();
        compiled.pattern = pattern;
        compiled.regex = QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
        compiled.regex.optimize();
        if (!compiled.regex.isValid()) {
            qWarning() << "LogFormatRegistry: invalid regex" << format.id << compiled.name << compiled.regex.errorString();
            continue;
        }
        format.patterns.append(compiled);
    }

    if (format.timestampField.isEmpty()) {
        for (const LogFormatPattern& pattern : format.patterns) {
            if (pattern.regex.namedCaptureGroups().contains("timestamp")) {
                format.timestampField = "timestamp";
                break;
            }
        }
    }
    if (format.bodyField.isEmpty()) {
        for (const LogFormatPattern& pattern : format.patterns) {
            if (pattern.regex.namedCaptureGroups().contains("body")) {
                format.bodyField = "body";
                break;
            }
        }
    }

    const QJsonObject levelObject = object.value("level").toObject();
    for (auto it = levelObject.constBegin(); it != levelObject.constEnd(); ++it) {
        if (it.value().isString()) {
            format.levelPatterns.insert(it.key(), it.value().toString());
        } else if (it.value().isDouble()) {
            format.levelPatterns.insert(it.key(), QString::number(it.value().toInt()));
        }
    }

    const QJsonObject valueObject = object.value("value").toObject();
    for (auto it = valueObject.constBegin(); it != valueObject.constEnd(); ++it) {
        format.valueNames.append(it.key());
    }

    if (!format.json && format.patterns.isEmpty()) {
        return;
    }

    formats.append(format);
}

void loadJsonDocument(const QJsonDocument& document, const QString& source, QVector<LogFormatDefinition>& formats) {
    if (!document.isObject()) {
        qWarning() << "LogFormatRegistry: skipping non-object format source" << source;
        return;
    }

    const QJsonObject root = document.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (it.key().startsWith('$') || !it.value().isObject()) {
            continue;
        }
        addFormatFromObject(it.key(), it.value().toObject(), source, formats);
    }
}

void loadJsonText(const QString& json, const QString& source, QVector<LogFormatDefinition>& formats) {
    if (json.trimmed().isEmpty()) {
        return;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "LogFormatRegistry: invalid format JSON" << source << error.errorString();
        return;
    }
    loadJsonDocument(document, source, formats);
}

void loadJsonFile(const QString& filePath, QVector<LogFormatDefinition>& formats) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "LogFormatRegistry: cannot open" << filePath << file.errorString();
        return;
    }
    loadJsonText(QString::fromUtf8(file.readAll()), filePath, formats);
}

void loadDirectory(const QString& path, QVector<LogFormatDefinition>& formats) {
    QDir dir(path);
    if (!dir.exists()) {
        return;
    }

    const QStringList files = dir.entryList(QStringList{"*.json"}, QDir::Files, QDir::Name);
    for (const QString& fileName : files) {
        loadJsonFile(dir.filePath(fileName), formats);
    }
}

void addBuiltInFormats(QVector<LogFormatDefinition>& formats) {
    static constexpr const char* builtIns = R"JSON(
{
  "json_generic": {
    "title": "Generic JSON Lines",
    "json": true,
    "timestamp-field": "@timestamp",
    "level-field": "level",
    "body-field": "message"
  },
  "syslog_basic": {
    "title": "Basic Syslog",
    "regex": {
      "line": { "pattern": "^(?<timestamp>[A-Z][a-z]{2}\\s+\\d{1,2}\\s+\\d{2}:\\d{2}:\\d{2})\\s+(?<host>\\S+)\\s+(?<process>[\\w\\-.]+)(?:\\[(?<pid>\\d+)\\])?:\\s+(?<message>.*)$" }
    },
    "timestamp-field": "timestamp",
    "body-field": "message"
  },
  "apache_combined": {
    "title": "Apache Combined Access Log",
    "regex": {
      "line": { "pattern": "^(?<remote_addr>\\S+)\\s+\\S+\\s+\\S+\\s+\\[(?<timestamp>[^\\]]+)\\]\\s+\\\"(?<method>\\S+)\\s+(?<path>\\S+)\\s+[^\\\"]+\\\"\\s+(?<status>\\d{3})\\s+(?<bytes>\\S+)(?:\\s+\\\"(?<referer>[^\\\"]*)\\\"\\s+\\\"(?<user_agent>[^\\\"]*)\\\")?.*$" }
    },
    "timestamp-field": "timestamp",
    "body-field": "path",
    "level-field": "status"
  },
  "logfmt": {
    "title": "Logfmt",
    "regex": {
      "line": { "pattern": "^(?=.*\\w+=).*$" }
    },
    "timestamp-field": "time",
    "level-field": "level",
    "body-field": "msg"
  }
}
)JSON";
    loadJsonText(QString::fromUtf8(builtIns), "built-in", formats);
}
}

QString LogFormatDefinition::displayName() const {
    return title.isEmpty() ? id : title;
}

QVector<LogFormatDefinition> LogFormatRegistry::loadFormats() {
    QVector<LogFormatDefinition> formats;
    addBuiltInFormats(formats);

    loadDirectory(QCoreApplication::applicationDirPath() + "/formats", formats);
    loadDirectory(QDir(QCoreApplication::applicationDirPath()).filePath("../formats"), formats);
    loadDirectory(QDir::current().filePath("formats"), formats);

    const AppSettingsValues settings = AppSettings::load();
    loadDirectory(settings.formatDetectionUserDirectory, formats);
    loadJsonText(settings.formatDetectionCustomJson, "settings", formats);

    return formats;
}
