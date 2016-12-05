#pragma once

#include <QtCore/QStringList>

#include "model.h"

class QFile;

class Printer
{
    public:
        Printer(QFile* h, QFile* cpp, const QString& nameSpace);

        void print(const Model& model);

    private:
        QFile* hFile;
        QFile* cppFile;
        QStringList namespaces;
};
