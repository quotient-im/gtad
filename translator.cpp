#include "translator.h"

#include "analyzer.h"
#include "printer.h"

#include "yaml.h"

#include <QtCore/QDir>

#include <regex>

using namespace std;

TypeUsage parseTypeEntry(const YamlNode& yamlTypeNode)
{
    using YAML::NodeType;
    if (yamlTypeNode.Type() == NodeType::Scalar)
        return TypeUsage(yamlTypeNode.as<string>());

    const auto yamlTypeMap = yamlTypeNode.asMap();
    TypeUsage typeUsage { yamlTypeMap["type"].as<string>("UNNAMED") };
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
    return typeUsage;
}

Translator::Translator(const QString& configFilePath, QString outputDirPath)
    : _outputDirPath(outputDirPath.endsWith('/') ?
                     move(outputDirPath) : outputDirPath + '/')
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
//                clog << "Mapped type " << type.first.as<string>()
//                     << " to " << formatsMap.back().second.name << endl;
                break;
            }
            case YAML::NodeType::Sequence: // A list of formats for the type
            {
                for (const YamlMap formatPattern: typeValue.asSequence())
                {
                    if (formatPattern.size() != 1)
                        throw YamlException(formatPattern,
                                            "Malformed types map");

                    const auto formatPair = *formatPattern.begin();
                    auto formatName = formatPair.first.as<string>();
                    if (formatName.empty())
                        formatName = "//"; // Empty format means all formats
                    formatsMap.emplace_back(move(formatName),
                                            parseTypeEntry(formatPair.second));
//                    clog << "Mapped format " << formatsMap.back().first
//                         << " to " << formatsMap.back().second.name << endl;
                }
                break;
            }
            default:
                throw YamlException(type.second, "Malformed types map");
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
        move(env), outputFiles, configDir.toStdString(),
        _outputDirPath.toStdString(), configY["outFilesList"].as<string>("") });
}

Translator::~Translator() = default;

TypeUsage Translator::mapType(const string& swaggerType,
                              const string& swaggerFormat,
                              const string& baseName) const
{
    for (const auto& swTypePair: _typesMap)
        if (swTypePair.first == swaggerType)
            for (const auto& swFormatPair: swTypePair.second)
            {
                const auto& swFormat = swFormatPair.first;
                if (swFormat == swaggerFormat ||
                    (swFormat.front() == '/' && swFormat.back() == '/' &&
                     regex_match(swaggerFormat,
                                 regex(++swFormat.begin(), --swFormat.end()))))
                {
                    // FIXME (#22): The below is a source of great inefficiency.
                    // TypeUsage should become a handle to an instance of
                    // a newly-made TypeDefinition type that would own all
                    // the stuff TypeUsage now has, except innerTypes
                    auto tu = swFormatPair.second;
                    // Fallback chain: baseName, swaggerFormat, swaggerType
                    tu.baseName = baseName.empty() ? swaggerFormat.empty() ?
                                swaggerType : swaggerFormat : baseName;
                    return tu;
                }
            }
    return TypeUsage("");
}

pair<Model, vector<string>> Translator::processFile(string filePath,
                                                    string baseDirPath) const
{
    Model m = Analyzer(filePath, baseDirPath, *this).loadModel(_substitutions);
    if (m.callClasses.empty() && m.types.empty())
        return make_pair(move(m), vector<string>());

    QDir d { _outputDirPath + m.fileDir.c_str() };
    if (!d.exists() && !d.mkpath("."))
        throw Exception { "Cannot create output directory" };
    auto fileNames = _printer->print(m);

    return make_pair(move(m), move(fileNames));
}

