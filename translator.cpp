/******************************************************************************
 * Copyright (C) 2016 Kitsune Ral <kitsune-ral@users.sf.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

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
    if (yamlTypeNode.Type() == NodeType::Null)
        return TypeUsage("");
    if (yamlTypeNode.Type() == NodeType::Scalar)
        return TypeUsage(yamlTypeNode.as<string>());

    const auto yamlTypeMap = yamlTypeNode.asMap();
    TypeUsage typeUsage { yamlTypeMap["type"].as<string>("") };
    for (const auto& attr: yamlTypeMap)
    {
        auto attrName = attr.first.as<string>();
        if (attrName == "type")
            continue;
        if (attr.second.Type() == NodeType::Scalar)
            typeUsage.attributes.emplace(move(attrName),
                                         attr.second.as<string>());
        else if (const auto& seq = attr.second.asSequence())
            typeUsage.lists.emplace(move(attrName), seq.asStrings());
    }
    return typeUsage;
}

pair_vector_t<string> loadStringMap(const YamlMap& yaml)
{
    pair_vector_t<string> stringMap;
    for (const auto& subst: yaml)
    {
        const auto& pattern = subst.first.as<string>();
        if (Q_UNLIKELY(pattern.empty()))
            clog << subst.first.location()
                 << ": warning: empty pattern in substitutions, skipping" << endl;
        else if (Q_UNLIKELY(pattern.size() > 1 &&
                            pattern.front() != '/' && pattern.back() == '/'))
            clog << subst.first.location()
                 << ": warning: invalid regular expression, skipping" << endl
                 << "(use a regex with \\/ to match strings beginning with /)";
        else
            stringMap.emplace_back(subst.first.as<string>(),
                                       subst.second.as<string>());
    }
    return stringMap;
}

Translator::Translator(const QString& configFilePath, QString outputDirPath)
    : _outputDirPath(outputDirPath.endsWith('/') ?
                     move(outputDirPath) : outputDirPath + '/')
{
    auto cfp = configFilePath.toStdString();
    cout << "Using config file at " << cfp << endl;
    const auto configY = YamlMap::loadFromFile(cfp);

    const auto& analyzerYaml = configY["analyzer"].asMap();
    _substitutions = loadStringMap(analyzerYaml["subst"].asMap());
    _identifiers = loadStringMap(analyzerYaml["identifiers"].asMap());

    const auto& typesYaml = analyzerYaml["types"].asMap();
    for (const auto& type: typesYaml)
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
    const auto& mustacheYaml = configY["mustache"].asMap();
    const auto& envYaml = mustacheYaml["definitions"].asMap();
    for (const auto& p: envYaml)
    {
        using namespace kainjow::mustache;

        const auto pName = p.first.as<string>();
        if (p.second.IsScalar())
            env.set(pName, partial {[s=p.second.as<string>()] { return s; }});
        else
        {
            const auto pDefinition = p.second.asMap().front();
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
    const auto& templatesYaml = mustacheYaml["templates"].asSequence();
    for (const auto& f: templatesYaml)
        outputFiles.emplace_back(f.as<string>());

    QString configDir = QFileInfo(configFilePath).dir().path();
    if (!configDir.isEmpty())
        configDir += '/';

    _printer = std::make_unique<Printer>(
        move(env), outputFiles, configDir.toStdString(),
        _outputDirPath.toStdString(), mustacheYaml["outFilesList"].as<string>("") );
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
                    (swFormat.size() > 1 &&
                     swFormat.front() == '/' && swFormat.back() == '/' &&
                     regex_search(swaggerFormat,
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

string Translator::mapIdentifier(const string& baseName) const
{
    for (const auto& entry: _identifiers)
    {
        const auto& pattn = entry.first;
        if (pattn.size() > 1 && pattn.front() == '/')
            return regex_replace(baseName,
                                 regex(++pattn.begin(), --pattn.end()),
                                 entry.second);

        if (pattn == baseName)
            return entry.second;
    }
    return baseName;
}

Model Translator::processFile(string filePath, string baseDirPath,
                              InOut inOut) const
{
    Model m = Analyzer(move(filePath), move(baseDirPath), *this)
                .loadModel(_substitutions, inOut);
    if (m.callClasses.empty() && m.types.empty())
        return m;

    QDir d { _outputDirPath + m.fileDir.c_str() };
    if (!d.exists() && !d.mkpath("."))
        throw Exception { "Cannot create output directory" };
    m.dstFiles = _printer->print(m);

    return m;
}

