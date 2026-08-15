#include "fieldextractor.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>

namespace {
void flattenJsonValue(const QString& path, const QJsonValue& value, LogFields& out) {
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (object.isEmpty()) {
            out << qMakePair(path, QString("{}"));
            return;
        }
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            flattenJsonValue(path + "." + it.key(), it.value(), out);
        }
        return;
    }

    if (value.isString()) {
        QString text = value.toString();
        const bool simple = !text.isEmpty()
            && text.indexOf(QRegularExpression("[\\s=,{}\\[\\]\"]")) < 0;
        text.replace('\\', "\\\\");
        text.replace('"', "\\\"");
        out << qMakePair(path, simple ? text : QString("\"%1\"").arg(text));
        return;
    }
    if (value.isBool()) {
        out << qMakePair(path, value.toBool() ? QString("true") : QString("false"));
        return;
    }
    if (value.isDouble()) {
        out << qMakePair(path, QString::number(value.toDouble(), 'g', 15));
        return;
    }
    if (value.isNull() || value.isUndefined()) {
        out << qMakePair(path, QString("null"));
        return;
    }

    const QJsonDocument document = value.isArray()
        ? QJsonDocument(value.toArray())
        : QJsonDocument(value.toObject());
    out << qMakePair(path, QString::fromUtf8(document.toJson(QJsonDocument::Compact)));
}

LogFields restrictToConfiguredValues(const LogFields& fields, const QStringList& valueNames) {
    if (valueNames.isEmpty()) {
        return fields;
    }

    LogFields selected;
    selected.reserve(valueNames.size());
    for (const QString& name : valueNames) {
        for (const auto& field : fields) {
            if (field.first.compare(name, Qt::CaseInsensitive) == 0) {
                selected << field;
                break;
            }
        }
    }
    return selected;
}
}

LogFields FieldExtractor::extract(const QString& raw, const LogFormatDetectionResult& formatDetection) {
    if (formatDetection.detected) {
        if (formatDetection.format.json) {
            return restrictToConfiguredValues(extractJson(raw), formatDetection.format.valueNames);
        }

        const LogFields regexFields = extractRegex(raw, formatDetection);
        if (!regexFields.isEmpty()) {
            return restrictToConfiguredValues(regexFields, formatDetection.format.valueNames);
        }
    }

    const LogFields jsonFields = extractJson(raw);
    if (!jsonFields.isEmpty()) {
        return jsonFields;
    }
    return extractKeyValue(raw);
}

LogFields FieldExtractor::extractJson(const QString& raw) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(raw.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }

    LogFields fields;
    const QJsonObject object = document.object();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        flattenJsonValue(it.key(), it.value(), fields);
    }
    return fields;
}

LogFields FieldExtractor::extractRegex(const QString& raw, const LogFormatDetectionResult& formatDetection) {
    for (const LogFormatPattern& pattern : formatDetection.format.patterns) {
        if (!formatDetection.patternName.isEmpty() && pattern.name != formatDetection.patternName) {
            continue;
        }

        const QRegularExpressionMatch match = pattern.regex.match(raw);
        if (!match.hasMatch()) {
            continue;
        }

        LogFields fields;
        const QStringList names = pattern.regex.namedCaptureGroups();
        for (const QString& name : names) {
            if (!name.isEmpty()) {
                fields << qMakePair(name, match.captured(name));
            }
        }
        return fields;
    }
    return {};
}

LogFields FieldExtractor::extractKeyValue(const QString& raw) {
    static const QRegularExpression fieldPattern(
        QStringLiteral(R"field((?:^|\s)([A-Za-z0-9_:@.-]+)\s*=\s*(?:"((?:\\.|[^"\\])*)"|'((?:\\.|[^'\\])*)'|([^\s]+)))field"));

    LogFields fields;
    QRegularExpressionMatchIterator matches = fieldPattern.globalMatch(raw);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        QString value;
        if (match.capturedStart(2) >= 0) {
            value = match.captured(2);
        } else if (match.capturedStart(3) >= 0) {
            value = match.captured(3);
        } else {
            value = match.captured(4);
        }
        fields << qMakePair(match.captured(1), value);
    }
    return fields;
}
