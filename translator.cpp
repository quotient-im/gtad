#include "translator.h"

#include <string>

#include <QtCore/QDir>

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

void Translator::operator()(QString path) const
{
    QFileInfo inputFileInfo { path };

    if (inputFileInfo.isDir())
    {
        if (!path.isEmpty() && !path.endsWith('/'))
            path.push_back('/');
        QStringList filesList = QDir(path).entryList(QDir::Readable|QDir::Files);
        for (auto fn: filesList)
        {
            if (fn != "content-repo.yaml" &&
                    fn != "cas_login_redirect.yaml" &&
                    fn != "cas_login_ticket.yaml" &&
                    fn != "old_sync.yaml" &&
                    fn != "room_initial_sync.yaml")
                doProcessFile(fn.toStdString(), path.toStdString());
        }
        return;
    }
    doProcessFile(path.toStdString(), "");
}

Model Translator::processFile(string filePath, string baseFilePath) const
{
    // Strip the filename from baseFilePath, leaving only its directory
    auto dirPos = baseFilePath.rfind('/');
    if (dirPos == string::npos)
        baseFilePath.clear();
    else
        baseFilePath.erase(dirPos + 1);

    return doProcessFile(filePath, baseFilePath);
}

Model Translator::doProcessFile(string filePath, string baseDirPath) const
{
    Model m = Analyzer(filePath, baseDirPath, *this).loadModel();
    m.nsName = "QMatrixClient::ServerApi";
    Printer(_outputDirPath.toStdString(), m.filenameBase).print(m);
    return m;
}
