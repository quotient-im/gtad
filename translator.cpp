#include "translator.h"

#include <QtCore/QDir>
#include <QtCore/QStringBuilder>

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
    : _outputDirPath(outputDirPath)
{
    if (!_outputDirPath.endsWith('/'))
        _outputDirPath.append('/');
    QDir d { _outputDirPath };
    if (!d.exists() && !d.mkpath("."))
        fail(CannotCreateOutputDir, "Cannot create output directory");
}

void Translator::operator()(QString filePath, QString basePath) const
{
    if (!basePath.isEmpty() && !basePath.endsWith('/'))
        basePath.append('/');

    QString fullPath = basePath + filePath;
    QFileInfo inputFileInfo { fullPath };
    if (inputFileInfo.isDir())
    {
        if (!fullPath.endsWith('/'))
            fullPath.append('/');
        auto paths = { "", "definitions/" };
        for (auto path: paths)
        {
            QStringList filesList =
                    QDir(fullPath + path).entryList(QDir::Readable|QDir::Files);
            if (filesList.empty())
                continue;

            QDir oDir(_outputDirPath + path);
            if (!oDir.exists() && !oDir.mkpath("."))
                fail(CannotCreateOutputDir, "Cannot create output directory");

            // FIXME: These exclusions should be external to the generator
            filesList.removeAll("error.yaml");
            filesList.removeAll("security.yaml");
            filesList.removeAll("event-schemas");
            filesList.removeAll("content-repo.yaml"); // Temporarily
            filesList.removeAll("cas_login_redirect.yaml");
            filesList.removeAll("cas_login_ticket.yaml");
            filesList.removeAll("old_sync.yaml"); // Deprecated
            filesList.removeAll("room_initial_sync.yaml"); // Deprecated
            for(auto fn: filesList)
                (*this)(path + fn, fullPath);
        }
        return;
    }

    Analyzer a(filePath.toStdString(), basePath.toStdString());
    Model m { a.loadModel() };
    m.nsName = "QMatrixClient::ServerApi";
    Printer(_outputDirPath.toStdString(), a.getFilenameBase()).print(m);
}

