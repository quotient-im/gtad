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

void addTypeAttributes(TypeUsage& typeUsage, const YamlMap& attributesMap)
{
    for (const auto& attr : attributesMap) {
        auto attrName = attr.first.as<string>();
        if (attrName == "type")
            continue;
        switch (attr.second.Type()) {
        case YAML::NodeType::Null:
            typeUsage.attributes.emplace(std::move(attrName), string {});
            break;
        case YAML::NodeType::Scalar:
            typeUsage.attributes.emplace(std::move(attrName),
                                         attr.second.as<string>());
            break;
        case YAML::NodeType::Sequence:
            if (const auto& seq = attr.second.asSequence())
                typeUsage.lists.emplace(std::move(attrName), seq.asStrings());
            break;
        default:
            throw YamlException(attr.second, "Malformed attribute");
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

    const auto yamlTypeMap = yamlTypeNode.asMap();
    TypeUsage typeUsage { yamlTypeMap["type"].as<string>("") };
    addTypeAttributes(typeUsage, yamlTypeMap);
    return typeUsage;
}

TypeUsage parseTargetType(const YamlNode& yamlTypeNode,
                          const YamlMap& commonAttributesYaml)
{
    auto tu = parseTargetType(yamlTypeNode);
    addTypeAttributes(tu, commonAttributesYaml);
    return tu;
}

template <typename FnT>
auto parseEntries(const YamlSequence& entriesYaml, const FnT& inserter,
                  const YamlMap& commonAttributesYaml = {})
    -> enable_if_t<is_void_v<decltype(
        inserter(string(), YamlNode(), YamlMap()))>>
{
    for (const YamlMap typesBlockYaml: entriesYaml) // clazy:exclude=range-loop
    {
        switch (typesBlockYaml.size())
        {
            case 0:
                throw YamlException(typesBlockYaml, "Empty type entry");
            case 1:
            {
                const auto& typeYaml = typesBlockYaml.front();
                inserter(typeYaml.first.as<string>(),
                         typeYaml.second, commonAttributesYaml);
                break;
            }
            case 2:
                if (typesBlockYaml["+on"] && typesBlockYaml["+set"])
                {
                    // #56: merge into the outer common attributes
                    // YamlMap constructors shallow-copy but we need a
                    // one-level-deep copy here to revert to the previous set
                    // after returning from recursion
                    YamlMap newCommonAttributesYaml;
                    // Instead of specialising struct convert<>, simply cast
                    // to YAML::Node that already has a specialisation
                    for (const auto& a : commonAttributesYaml)
                        newCommonAttributesYaml.force_insert(
                            static_cast<const YAML::Node&>(a.first),
                            static_cast<const YAML::Node&>(a.second));
                    for (const auto& a : typesBlockYaml.get("+set").asMap())
                        newCommonAttributesYaml[a.first.as<string>()] = a.second;
                    parseEntries(typesBlockYaml.get("+on").asSequence(),
                                 inserter, newCommonAttributesYaml);
                    break;
                }
                [[fallthrough]];
            default:
                throw YamlException(typesBlockYaml,
                        "Too many entries in the map, check indentation");
        }
    }
}

pair_vector_t<TypeUsage> parseTypeEntry(const YamlNode& targetTypeYaml,
                                        const YamlMap& commonAttributesYaml = {})
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
            parseEntries(targetTypeYaml.asSequence(),
                [&targetTypes](string formatName,
                    const YamlNode& typeYaml, const YamlMap& commonAttrsYaml)
                {
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
                }, commonAttributesYaml);
            return targetTypes;
        }
        default:
            throw YamlException(targetTypeYaml, "Malformed type entry");
    }
}

pair_vector_t<string> loadStringMap(const YamlMap& yaml)
{
    pair_vector_t<string> stringMap;
    for (const auto& subst: yaml)
    {
        auto pattern = subst.first.as<string>();
        if (pattern.empty()) // [[unlikely]]
            clog << subst.first.location()
                 << ": warning: empty pattern in substitutions, skipping"
                 << endl;
        else if (pattern.size() > 1 && pattern.front() != '/'
                 && pattern.back() == '/') // [[unlikely]]
            clog << subst.first.location()
                 << ": warning: invalid regular expression, skipping" << endl
                 << "(use a regex with \\/ to match strings beginning with /)";
        else
        {
            if (pattern.front() == '/' && pattern.back() == '/')
                pattern.pop_back();
            stringMap.emplace_back(pattern, subst.second.as<string>());
        }
    }
    return stringMap;
}

Translator::Translator(const path& configFilePath, path outputDirPath,
                       Verbosity verbosity)
    : _verbosity(verbosity), _outputDirPath(std::move(outputDirPath))
{
    cout << "Using config file at " << configFilePath << endl;
    const auto configY = YamlMap::loadFromFile(configFilePath);

    const auto& analyzerYaml = configY["analyzer"].asMap();
    _substitutions = loadStringMap(analyzerYaml["subst"].asMap());
    _identifiers = loadStringMap(analyzerYaml["identifiers"].asMap());

    parseEntries(analyzerYaml["types"].asSequence(),
        [this](const string& name, const YamlNode& typeYaml,
               const YamlMap& commonAttrsYaml)
        {
            _typesMap.emplace_back(name,
                parseTypeEntry(typeYaml, commonAttrsYaml));
        });

    if (_verbosity == Verbosity::Debug)
        for (const auto& t : _typesMap) {
            clog << "Type " << t.first << ":" << endl;
            for (const auto& f : t.second) {
                clog << "  Format " << (f.first.empty() ? "(none)" : f.first)
                     << ":" << endl
                     << "    mapped to "
                     << (!f.second.name.empty() ? f.second.name : "(none)")
                     << endl;

                if (!f.second.attributes.empty()) {
                    clog << "    attributes:" << endl;
                    for (const auto& a : f.second.attributes)
                        clog << "      " << a.first << " -> " << a.second
                             << endl;
                } else
                    clog << "    no attributes" << endl;

                if (!f.second.lists.empty()) {
                    clog << "    lists:" << endl;
                    for (const auto& l : f.second.lists)
                        clog << "      " << l.first
                             << " (entries: " << l.second.size() << ")" << endl;
                } else
                    clog << "    no lists" << endl;
            }
        }

    Printer::context_type env;
    using namespace kainjow::mustache;
    const auto& mustacheYaml = configY["mustache"].asMap();
    const auto& delimiter = mustacheYaml["delimiter"].as<string>("");
    const auto& envYaml = mustacheYaml["constants"].asMap();
    for (const auto& p: envYaml)
    {
        const auto pName = p.first.as<string>();
        if (p.second.IsScalar())
        {
            env.emplace(pName, p.second.as<string>());
            continue;
        }
        const auto pDefinition = p.second.asMap().front();
        const auto pType = pDefinition.first.as<string>();
        const YamlNode defaultVal = pDefinition.second;
        if (pType == "set")
            env.emplace(pName, data::type::list);
        else if (pType == "bool")
            env.emplace(pName, defaultVal.as<bool>());
        else
            env.emplace(pName, defaultVal.as<string>());
    }
    const auto& partialsYaml = mustacheYaml["partials"].asMap();
    for (const auto& p: partialsYaml)
        env.emplace(p.first.as<string>(),
                    makePartial(p.second.as<string>(), delimiter));

    const auto& templatesYaml = mustacheYaml["templates"].asMap();
    for (auto [templates, nodeName] :
         {pair {&_dataTemplates, "data"}, {&_apiTemplates, "api"}})
        if (auto extToTemplatesYaml = templatesYaml[nodeName].asMap()) {
            for (auto extToTemplateYaml : extToTemplatesYaml)
                templates->emplace_back(extToTemplateYaml.first.as<string>(),
                                        extToTemplateYaml.second.as<string>());
        }

    _printer = make_unique<Printer>(std::move(env), configFilePath.parent_path(),
                                    mustacheYaml["outFilesList"].as<string>(""),
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

TypeUsage Translator::mapType(const string& swaggerType,
                              const string& swaggerFormat,
                              const string& baseName) const
{
    TypeUsage tu;
    for (const auto& [swType, swFormats]: _typesMap)
        if (swType == swaggerType)
            for (const auto& [swFormat, mappedType]: swFormats)
            {
                if (swFormat == swaggerFormat ||
                    (!swFormat.empty() && swFormat.front() == '/' &&
                     regex_search(swaggerFormat,
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

string Translator::mapIdentifier(const string& baseName,
                                 const Identifier* scope, bool required) const
{
    auto scopedName = scope ? scope->qualifiedName() : string();
    scopedName.append(1, '/').append(baseName);
    string newName = baseName;
    for (const auto& entry: _identifiers)
    {
        const auto& pattn = entry.first;
        if (!pattn.empty() && pattn.front() == '/') {
            auto&& replaced = regex_replace(scopedName,
                                            regex(++pattn.begin(), pattn.end()),
                                            entry.second);
            if (replaced != scopedName) {
//                cout << "Regex replace: " << scopedName << " -> " << replaced
//                     << endl;
                newName = replaced;
                break;
            }
        } else if (pattn == baseName || pattn == scopedName) {
            newName = entry.second;
            break;
        }
    }
    if (newName.empty() && required)
        throw Exception(
            "Attempt to skip the required variable '" + baseName
            + "' - check 'identifiers' block in your gtad.yaml");
    return newName;
}
