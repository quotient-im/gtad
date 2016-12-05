#include "translator.h"

#include "model.h"
#include "analyzer.h"
#include "exception.h"
#include "printer.h"

using namespace std;

enum ErrorCode
{
    _Base = TranslatorCodes,
    CannotCreateOutputDir, CannotWriteToFile
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
        auto paths = {
                "/api/client-server",
                "/api/client-server/definitions",
        };
        for (auto path: paths)
        {
            QStringList filesList =
                    QDir(filePath + path).entryList(QDir::Readable|QDir::Files);
            filesList.removeAll("error.yaml");
            filesList.removeAll("security.yaml");
            filesList.removeAll("event-schemas");
            for(auto fn: filesList)
                (*this)(filePath % path % "/" % fn);
        }
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

    Analyzer a(filePath.toStdString());
    Printer p(&hFile, &cppFile, "QMatrixClient::ServerApi");

    p.print(a.getModel());
}
