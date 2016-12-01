#pragma once

#include <QtCore/QDir>
#include <QtCore/QTextStream>
#include <QtCore/QHash>

class ApiGenerator
{
    public:
        explicit ApiGenerator(const QString& outputDirPath);
        void operator()(const QString& filePath) const;

    private:
        QDir outputDir;
};
