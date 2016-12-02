#include "translator.h"

#include "model.h"
#include "analyzer.h"
#include "scope.h"
#include "exception.h"

#include <iostream>
#include <regex>

using namespace std;

enum ErrorCode
{
    _Base = TranslatorCodes,
    CannotCreateOutputDir, CannotWriteToFile, CannotResolveOverloadParameters,
};

Translator::Translator(const QString& outputDirPath)
    : outputDir { outputDirPath }
{
    if (!outputDir.exists() && !outputDir.mkpath("."))
        fail(CannotCreateOutputDir, "Cannot create output directory");
}

void Translator::operator()(const QString& filePath) const
{
    QFileInfo inputFileInfo { filePath };

    if (inputFileInfo.isDir())
    {
        QDir inputDir(filePath, {"*.yaml"}, QDir::Name, QDir::Readable|QDir::Files);
        auto filesList = inputDir.entryList();
        for(auto fn:filesList)
            (*this)(inputFileInfo.filePath() + fn);
        return;
    }

    QString bareFilename = inputFileInfo.fileName();
    if (bareFilename.endsWith(".yaml", Qt::CaseInsensitive))
        bareFilename.chop(5);
    else if (bareFilename.endsWith(".yml", Qt::CaseInsensitive))
        bareFilename.chop(4);

    QFile hFile(outputDir.absolutePath() % "/" % bareFilename % ".h");
    if (!hFile.open(QIODevice::WriteOnly|QIODevice::Text))
        fail(CannotWriteToFile, "Couldn't open .h file for writing");

    QFile cppFile(outputDir.absolutePath() % "/" % bareFilename % ".cpp");
    if (!cppFile.open(QIODevice::WriteOnly|QIODevice::Text))
        fail(CannotWriteToFile, "Couldn't open .cpp file for writing");

    // Read the input file into the model

    auto models = Analyzer(filePath.toStdString()).getModels();

    // Dump the model to the C++ files

    using namespace CppPrinting;

    QTextStream hText(&hFile);
    QTextStream cppText(&cppFile);
    {
        QString s = "// This is an auto-generated file; don't edit!\n\n";
        hText << s;
        cppText << s;
    }
    cppText << "#include \"" << bareFilename << ".h\"\n";

    hText << "#pragma once\n\n"
             "#include \"../servercallsetup.h\"\n\n";

    {
        hText << "namespace QMatrixClient\n";
        Scope ns1(hText, "{", "}");
        hText << offset << "namespace ServerApi\n";
        Scope ns2(hText, "{", "}");

        for (auto model: models)
            model.printTo(hText, cppText);
    }

    hFile.close();
    cppFile.close();
}
