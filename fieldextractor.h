#ifndef FIELDEXTRACTOR_H
#define FIELDEXTRACTOR_H

#include "logformat.h"

#include <QPair>
#include <QString>
#include <QVector>

using LogFields = QVector<QPair<QString, QString>>;

class FieldExtractor
{
public:
    static LogFields extract(const QString& raw, const LogFormatDetectionResult& formatDetection);

private:
    static LogFields extractJson(const QString& raw);
    static LogFields extractRegex(const QString& raw, const LogFormatDetectionResult& formatDetection);
    static LogFields extractKeyValue(const QString& raw);
};

#endif // FIELDEXTRACTOR_H
