#include "translator.h"

#include "analyzer.h"
#include "exception.h"
#include "printer.h"

#include "yaml.h"

#include <QtCore/QDir>

#include <regex>

using namespace std;

enum {
    _Base = GeneralCodes, CannotCreateOutputDir, CannotWriteToFile,
    IncorrectConfigurationFormat
};

TypeUsage parseTypeEntry(const YamlNode& yamlTypeNode)
{
    using YAML::NodeType;
    if (yamlTypeNode.Type() == NodeType::Scalar)
        return TypeUsage(yamlTypeNode.as<string>());

    const auto yamlTypeMap = yamlTypeNode.asMap();
    TypeUsage typeUsage { yamlTypeMap.get("type").as<string>() };
    for (const auto& attr: yamlTypeMap)
    {
        auto attrName = attr.first.as<string>();
        if (attrName == "type")
            continue;
        if (attr.second.Type() == NodeType::Scalar)
            typeUsage.attributes.emplace(move(attrName),
                                         attr.second.as<string>());
        else if (attr.second.Type() == NodeType::Sequence)
        {
            // TODO
            cerr << "List attributes in 'types:' are not implemented" << endl;
        }
    }
    return move(typeUsage);
}

Translator::Translator(const QString& configFilePath, QString outputDirPath)
    : _outputDirPath(outputDirPath.endsWith('/') ?
                     std::move(outputDirPath) : outputDirPath + '/')
{
    const auto configY = YamlMap::loadFromFile(configFilePath.toStdString());

    for (const auto& subst: configY["preprocess"].asMap())
        _substitutions.emplace_back(subst.first.as<string>(),
                                    subst.second.as<string>());

    for (const auto& type: configY["types"].asMap())
    {
        const auto typeValue = type.second;
        pair_vector_t<TypeUsage> formatsMap;
        switch (typeValue.Type())
        {
            case YAML::NodeType::Scalar: // Use a type with no regard to format
            case YAML::NodeType::Map: // Same, with attributes for the target type
            {
                formatsMap.emplace_back(string(), parseTypeEntry(typeValue));
                cout << "Mapped type " << type.first.as<string>()
                     << " to " << formatsMap.back().second.name << endl;
                break;
            }
            case YAML::NodeType::Sequence: // A list of formats for the type
            {
                for (const YamlMap formatPattern: typeValue.asSequence())
                {
                    if (formatPattern.size() != 1)
                    {
                        cerr << formatPattern.location() << ": malformed types map"
                             << endl;
                        fail(IncorrectConfigurationFormat);
                    }
                    const auto formatPair = *formatPattern.begin();
                    auto formatName = formatPair.first.as<string>();
                    if (formatName.empty())
                        formatName = "//"; // Empty format means all formats
                    formatsMap.emplace_back(move(formatName),
                                            parseTypeEntry(formatPair.second));
                    cout << "Mapped format " << formatsMap.back().first
                         << " to " << formatsMap.back().second.name << endl;
                }
                break;
            }
            default:
                cerr << type.second.location() << ": malformed types map" << endl;
                fail(IncorrectConfigurationFormat);
        }
        _typesMap.emplace_back(type.first.as<string>(), move(formatsMap));
    }

    Printer::context_type env;
    for (const auto p: configY["env"].asMap())
    {
        using namespace kainjow::mustache;

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
    for (const auto f: configY["templates"].asSequence())
        outputFiles.emplace_back(f.as<string>());

    QString configDir = QFileInfo(configFilePath).dir().path();
    if (!configDir.isEmpty())
        configDir += '/';

    _printer.reset(new Printer {
        std::move(env), outputFiles, configDir.toStdString(),
        _outputDirPath.toStdString(), configY["outFilesList"].as<string>("") });
}

// FIXME: The below two functions are a source of great inefficiency. Every time
// another type usage is resolved, a TypeUsage copy is created (with all its
// attributes). For mapType(), the situation can be easily solved by pointing to
// the type instead of copying. For mapArrayType() it's more complicated because
// arrays are parameterized types; apparently, the real TypeUsage type should be
// a handle to the actual TypeDef instance that would own all the stuff TypUsage
// now has.

TypeUsage
Translator::mapType(const string& swaggerType, const string& swaggerFormat) const
{
    for (const auto& swTypePair: _typesMap)
    {
        if (swTypePair.first == swaggerType)
        {
            for (const auto& swFormatPair: swTypePair.second)
            {
                const auto swFormat = swFormatPair.first;
                if (swFormat == swaggerFormat)
                    return swFormatPair.second;

                if (swFormat.front() == '/' && swFormat.back() == '/' &&
                    regex_match(swaggerFormat,
                                regex(++swFormat.begin(), --swFormat.end())))
                        return swFormatPair.second;
            }
        }
    }
    return TypeUsage("");
}

TypeUsage Translator::mapArrayType(const TypeUsage& innerType) const
{
    for (const auto& swTypePair: _typesMap)
        if (swTypePair.first == "array")
            return swTypePair.second.back().second(innerType);

    return TypeUsage("");
}

pair<Model, vector<string>> Translator::processFile(string filePath,
                                                    string baseDirPath) const
{
    Model m = Analyzer(filePath, baseDirPath, *this).loadModel(_substitutions);
    if (m.callClasses.empty() && m.types.empty())
        return { std::move(m), {} };

    QDir d { _outputDirPath + m.fileDir.c_str() };
    if (!d.exists() && !d.mkpath("."))
        fail(CannotCreateOutputDir, "Cannot create output directory");
    auto fileNames = _printer->print(m);

    return { std::move(m), std::move(fileNames) };
}

