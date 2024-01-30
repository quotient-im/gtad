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

#include "printer.h"

#include "yaml.h"

#include <regex>

using namespace std;

void addTypeAttributes(TypeUsage& typeUsage, const YamlMap<>& attributesMap)
{
    for (const auto& [attrName, attrData] : attributesMap) {
        if (attrName == "type")
            continue;
        switch (attrData.Type()) {
        case YAML::NodeType::Null:
            typeUsage.attributes.emplace(std::move(attrName), string {});
            break;
        case YAML::NodeType::Scalar:
            typeUsage.attributes.emplace(std::move(attrName), attrData.as<string>());
            break;
        case YAML::NodeType::Sequence:
            if (const auto& seq = attrData.as<YamlSequence<string>>(); seq.size() > 0)
                typeUsage.lists.emplace(std::move(attrName),
                                        vector<string>{seq.begin(), seq.end()});
            break;
        default:
            throw YamlException(attrData, "Malformed attribute");
        }
    }
}

TypeUsage parseTargetType(const YamlNode& yamlTypeNode)
{
    using YAML::NodeType;
    if (yamlTypeNode.Type() == NodeType::Null)
        return {};
    if (yamlTypeNode.Type() == NodeType::Scalar)
        return TypeUsage(yamlTypeNode.as<string>());

    const auto yamlTypeMap = yamlTypeNode.as<YamlMap<>>();
    TypeUsage typeUsage{yamlTypeMap.get<string>("type", {})};
    addTypeAttributes(typeUsage, yamlTypeMap);
    return typeUsage;
}

TypeUsage parseTargetType(const YamlNode& yamlTypeNode,
                          const YamlMap<>& commonAttributesYaml)
{
    auto tu = parseTargetType(yamlTypeNode);
    addTypeAttributes(tu, commonAttributesYaml);
    return tu;
}

void parseEntries(const YamlSequence<YamlMap<>>& entriesYaml,
                  const std::invocable<string, YamlNode, YamlMap<>> auto& inserter,
                  const YamlMap<>& commonAttributesYaml = {})
{
    for (const auto& typesBlockYaml: entriesYaml) // clazy:exclude=range-loop
    {
        switch (typesBlockYaml.size())
        {
            case 0:
                throw YamlException(typesBlockYaml, "Empty type entry");
            case 1:
            {
                const auto& [name, details] = typesBlockYaml.front();
                inserter(name, details, commonAttributesYaml);
                break;
            }
            case 2: {
                const auto setYaml = typesBlockYaml.maybeGet<YamlMap<>>("+set");
                const auto onYaml = typesBlockYaml.maybeGet<YamlSequence<YamlMap<>>>("+on");
                if (setYaml && onYaml)
                {
                    // #56: merge into the outer common attributes
                    // YamlMap constructors shallow-copy but we need a
                    // one-level-deep copy here to revert to the previous set
                    // after returning from recursion
                    YamlMap<> newCommonAttributesYaml;
                    // Instead of specialising struct convert<>, simply cast
                    // to YAML::Node that already has a specialisation
                    for (const auto& [k, v] : commonAttributesYaml)
                        newCommonAttributesYaml.force_insert<string, YAML::Node>(k,v);
                    for (const auto& [k, v] : *setYaml)
                        newCommonAttributesYaml.force_insert<string, YAML::Node>(k, v);
                    parseEntries(*onYaml, inserter, newCommonAttributesYaml);
                    break;
                }
                if (bool(setYaml) != bool(onYaml)) [[unlikely]]
                    throw YamlException(typesBlockYaml,
                                        "+set and +on block should always be used together, "
                                        "one can not occur without another");
                [[fallthrough]];
            }
            default:
                throw YamlException(typesBlockYaml,
                        "Too many entries in the map, check indentation");
        }
    }
}

pair_vector_t<TypeUsage> parseTypeEntry(const YamlNode& targetTypeYaml,
                                        const YamlMap<>& commonAttributesYaml = {})
{
    switch (targetTypeYaml.Type())
    {
        case YAML::NodeType::Scalar: // Use a type with no regard to format
        case YAML::NodeType::Map: // Same, with attributes for the target type
        {
            return { { {},
                       parseTargetType(targetTypeYaml, commonAttributesYaml) } };
        }
        case YAML::NodeType::Sequence: // A list of formats for the type
        {
            pair_vector_t<TypeUsage> targetTypes;
            parseEntries(
                targetTypeYaml.as<YamlSequence<YamlMap<>>>(),
                [&targetTypes](string formatName, const YamlNode& typeYaml,
                               const YamlMap<>& commonAttrsYaml) {
                    if (formatName.empty())
                        formatName = "/"; // Empty format means all formats
                    else if (formatName.size() > 1 &&
                             formatName.front() == '/' &&
                             formatName.back() == '/')
                    {
                        formatName.pop_back();
                    }
                    targetTypes.emplace_back(std::move(formatName),
                        parseTargetType(typeYaml, commonAttrsYaml));
                },
                commonAttributesYaml);
            return targetTypes;
        }
        default:
            throw YamlException(targetTypeYaml, "Malformed type entry");
    }
}

subst_list_t loadStringMap(const YamlMap<>& yaml, string_view key)
{
    subst_list_t stringMap;
    for (const auto& [patternNode, subst] : yaml.maybeGet<YamlGenericMap>(key)) {
        auto pattern = patternNode.as<string>();
        if (pattern.empty()) [[unlikely]]
            clog << patternNode.location()
                 << ": warning: empty pattern in substitutions, skipping\n";
        else if (pattern.size() > 1 && pattern.front() != '/' && pattern.back() == '/') [[unlikely]]
            clog << patternNode.location() << ": warning: invalid regular expression, skipping\n"
                    "(use a regex with \\/ to match strings beginning with /)\n";
        else {
            if (pattern.front() == '/' && pattern.back() == '/')
                pattern.pop_back();
            stringMap.emplace_back(pattern, subst.as<string>());
        }
    }
    return stringMap;
}

Translator::Translator(const path& configFilePath, path outputDirPath,
                       Verbosity verbosity)
    : _verbosity(verbosity), _outputDirPath(std::move(outputDirPath))
{
    cout << "Using config file at " << configFilePath << endl;
    const auto configY = YamlNode::fromFile(configFilePath).as<YamlMap<YamlMap<>>>();

    if (const auto& analyzerYaml = configY["analyzer"]) {
        _substitutions = loadStringMap(*analyzerYaml, "subst");
        _identifiers = loadStringMap(*analyzerYaml, "identifiers");

        parseEntries(
            analyzerYaml->get<YamlSequence<YamlMap<>>>("types", {}),
            [this](const string& name, const YamlNode& typeYaml, const YamlMap<>& commonAttrsYaml) {
                _typesMap.emplace_back(name, parseTypeEntry(typeYaml, commonAttrsYaml));
            });

        if (_verbosity == Verbosity::Debug)
            for (const auto& t : _typesMap) {
                clog << "Type " << t.first << ":" << endl;
                for (const auto& f : t.second) {
                    clog << "  Format " << (f.first.empty() ? "(none)" : f.first) << ":" << endl
                         << "    mapped to " << (!f.second.name.empty() ? f.second.name : "(none)")
                         << endl;

                    if (!f.second.attributes.empty()) {
                        clog << "    attributes:" << endl;
                        for (const auto& a : f.second.attributes)
                            clog << "      " << a.first << " -> " << a.second << endl;
                    } else
                        clog << "    no attributes" << endl;

                    if (!f.second.lists.empty()) {
                        clog << "    lists:" << endl;
                        for (const auto& l : f.second.lists)
                            clog << "      " << l.first << " (entries: " << l.second.size() << ")"
                                 << endl;
                    } else
                        clog << "    no lists" << endl;
                }
            }
    }

    Printer::context_type env;
    using namespace kainjow::mustache;
    const auto& mustacheYaml = configY.get("mustache");
    const auto& delimiter = mustacheYaml.get<string>("delimiter", {});
    for (const auto& [cName, cValue] : mustacheYaml.maybeGet<YamlMap<string>>("constants"))
        env.emplace(cName, cValue);

    for (const auto& [pName, pValue] : mustacheYaml.maybeGet<YamlMap<string>>("partials"))
        env.emplace(pName, makePartial(pValue, delimiter));

    const auto& templatesYaml = mustacheYaml.maybeGet<YamlMap<YamlMap<string>>>("templates");
    for (auto [templates, nodeName] :
         {pair {&_dataTemplates, "data"}, {&_apiTemplates, "api"}})
        for (auto extToTemplateYaml : templatesYaml->maybeGet(nodeName))
            templates->emplace_back(extToTemplateYaml);

    _printer = make_unique<Printer>(std::move(env), configFilePath.parent_path(),
                                    mustacheYaml.get<string>("outFilesList", {}),
                                    delimiter, *this);
}

Translator::~Translator() = default;

Translator::output_config_t Translator::outputConfig(const path& fileStem,
                                                     const Model& model) const
{
    const auto& srcConfig =
        model.apiSpec == ApiSpec::JSONSchema ? _dataTemplates : _apiTemplates;
    output_config_t result;
    for (const auto& [fExtension, fTemplate]: srcConfig)
        result.emplace_back(path(fileStem) += fExtension, fTemplate);
    return result;
}

TypeUsage Translator::mapType(string_view swaggerType, string_view swaggerFormat,
                              string_view baseName) const
{
    TypeUsage tu;
    for (const auto& [swType, swFormats]: _typesMap)
        if (swType == swaggerType)
            for (const auto& [swFormat, mappedType]: swFormats)
            {
                if (swFormat == swaggerFormat
                    || (!swFormat.empty() && swFormat.front() == '/'
                        && regex_search(string(swaggerFormat),
                                        regex(++swFormat.begin(), swFormat.end()))))
                {
                    // FIXME (#22): a source of great inefficiency.
                    // TypeUsage should become a handle to an instance of
                    // a newly-made TypeDefinition type that would own all
                    // the stuff TypeUsage now has, except paramTypes
                    tu = mappedType;
                    goto conclusion;
                }
            }
conclusion:
    // Fallback chain: baseName, swaggerFormat, swaggerType
    tu.baseName = baseName.empty()
                      ? swaggerFormat.empty() ? swaggerType : swaggerFormat
                      : baseName;
    if (auto& renderer = tu.attributes["_importRenderer"]; renderer.empty())
        renderer = "{{_}}"; // Just render the import as is
    return tu;
}

string Translator::mapIdentifier(string_view baseName, const Identifier* scope, bool required) const
{
    auto scopedName = scope ? scope->qualifiedName() : string();
    scopedName.append(1, '/').append(baseName);
    string newName{baseName};
    for (const auto& [pattn, subst]: _identifiers)
    {
        if (!pattn.empty() && pattn.front() == '/') {
            auto&& replaced = regex_replace(scopedName,
                                            regex(++pattn.begin(), pattn.end()),
                                            subst);
            if (replaced != scopedName) {
                if (_verbosity == Verbosity::Debug)
                    cout << "Regex replace: " << scopedName << " -> " << replaced << '\n';
                newName = replaced;
                break;
            }
        } else if (pattn == baseName || pattn == scopedName) {
            newName = subst;
            break;
        }
    }
    if (newName.empty() && required)
        throw Exception("Attempt to skip the required variable '"s.append(baseName).append(
            "' - check 'identifiers' block in your gtad.yaml"));
    return newName;
}
