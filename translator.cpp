#include "translator.h"

#include "analyzer.h"
#include "exception.h"
#include "printer.h"

#include "yaml.h"

#include <QtCore/QDir>

using namespace std;

enum {
    _Base = GeneralCodes, CannotCreateOutputDir, CannotWriteToFile
};

Translator::Translator(const QString& configFilePath, QString outputDirPath)
    : _outputDirPath(outputDirPath.endsWith('/') ?
                     std::move(outputDirPath) : outputDirPath + '/')
    , _printer(
        [&]() {
            using namespace kainjow::mustache;

            Printer::context_type env;
            const auto configY = YamlMap::loadFromFile(configFilePath.toStdString());

            for (const auto p: configY["env"].asMap())
            {
                const auto pName = p.first.as<string>();
                if (p.second.IsScalar())
                    env.set(pName, p.second.as<string>());
                else
                {
                    const auto pDefinition = *p.second.asMap().begin();
                    const auto pType = pDefinition.first.as<string>();
                    const YamlNode defaultVal = pDefinition.second;
                    if (pType == "set")
                        env.set(pName, { data::type::list });
                    else if (pType == "bool")
                        env.set(pName, { defaultVal.as<bool>() });
                    else
                        env.set(pName, { defaultVal.as<string>() });
                }
            }

            vector<string> outputFiles;
            for (const auto f: configY["files"].asSequence())
                outputFiles.emplace_back(f.as<string>());

            QString configDir = QFileInfo(configFilePath).dir().path();
            if (!configDir.isEmpty())
                configDir += '/';

            return Printer { std::move(env), outputFiles, configDir.toStdString() };
        }())
{
    // TODO: Load types translation table
}

TypeUsage Translator::mapType(const string& swaggerType, const string& swaggerFormat,
                              bool constRef) const
{
    TypeUsage tu =
        swaggerType == "boolean" ? TypeUsage("bool") :
        swaggerType == "integer" ?
            swaggerFormat == "int64" ? TypeUsage("std::int64_t", "<cstdint>") :
            swaggerFormat == "int32" ? TypeUsage("std::int32_t", "<cstdint>") :
            TypeUsage("int") :
        swaggerType == "number" ?
            TypeUsage(swaggerFormat == "float" ? "float" : "double") :
        swaggerType == "string" ?
            (swaggerFormat == "byte" || swaggerFormat == "binary" ?
                TypeUsage("QByteArray", "<QtCore/QByteArray>") :
            swaggerFormat == "date" ? TypeUsage("QDate", "<QtCore/QDate>") :
            swaggerFormat == "date-time" ?
                TypeUsage("QDateTime", "<QtCore/QDateTime>") :
            TypeUsage("QString", "<QtCore/QString>")) :
        swaggerType == "array" ? TypeUsage("QJsonArray", "<QtCore/QJsonArray>") :
        swaggerType == "object" ? TypeUsage("QJsonObject", "<QtCore/QJsonObject>") :
        TypeUsage("");
    if (tu.name.front() == 'Q' && constRef)
        tu.name = "const " + tu.name + '&';
    return tu;
}

TypeUsage Translator::mapArrayType(const TypeUsage& innerType, bool constRef) const
{
    TypeUsage tu =
        innerType.name == "QString" ?
            TypeUsage("QStringList", "<QtCore/QStringList>") :
        TypeUsage("QVector<" + innerType.name + ">",
                     innerType.imports, "<QtCore/QVector>");
    if (tu.name.front() == 'Q' && constRef)
        tu.name = "const " + tu.name + '&';
    return tu;
}

Model Translator::processFile(string filePath, string baseDirPath) const
{
    Model m = Analyzer(filePath, baseDirPath, *this).loadModel();
    if (!m.callClasses.empty() || !m.types.empty())
    {
        if (!m.fileDir.empty())
        {
            QDir d { _outputDirPath + m.fileDir.c_str() };
            if (!d.exists() && !d.mkpath("."))
                fail(CannotCreateOutputDir, "Cannot create output directory");
        }
        _printer.print(m, _outputDirPath.toStdString());
    }

    return m;
}

