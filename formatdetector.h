#ifndef FORMATDETECTOR_H
#define FORMATDETECTOR_H

#include "logformat.h"

#include <QStringList>

class FormatDetector
{
public:
    static LogFormatDetectionResult detect(const QString& sourceName, const QStringList& sampleLines);
};

#endif // FORMATDETECTOR_H
