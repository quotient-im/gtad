/******************************************************************************
 * Copyright (C) 2018 Kitsune Ral <kitsune-ral@users.sf.net>
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

#include "analyzer.h"

#include "translator.h"
#include "yaml.h"

#include <algorithm>

using namespace std;
namespace fs = filesystem;

/// This class helps to save the context and restore it on C++ scope exit.
/// Previous context lifecycle remains the caller's responsibility.
class ContextOverlay {
private:
    Analyzer& _analyzer;
    const Analyzer::Context* _prevContext;
    Analyzer::Context _thisContext;
    const Identifier _scope;

public:
    ContextOverlay(Analyzer& a, fs::path newFileDir, Model* newModel,
                   Identifier newScope = {})
        : _analyzer(a)
        , _prevContext(_analyzer._context)
        , _thisContext{std::move(newFileDir), newModel, move(newScope)}
    {
        _analyzer._context = &_thisContext;
        ++_analyzer._indent;
    }
    template <typename... ArgTs>
    ContextOverlay(Analyzer& a, ArgTs&&... scopeArgs)
        : ContextOverlay(a, a.context().fileDir, a.context().model,
                         {std::forward<ArgTs>(scopeArgs)...})
    { }
    ~ContextOverlay()
    {
        --_analyzer._indent;
        _analyzer._context = _prevContext;
    }
};

Analyzer::models_t Analyzer::_allModels {};

Analyzer::Analyzer(const Translator& translator, fspath basePath)
    : _baseDir(move(basePath))
    , _translator(translator)
{
    if (!fs::is_directory(_baseDir))
        throw Exception("Base path " + _baseDir.string() + " is not a directory");
    cout << "Using " << _baseDir << " as a base directory for YAML/JSON files"
         << endl;
}

TypeUsage Analyzer::analyzeTypeUsage(const YamlMap& node, IsTopLevel isTopLevel)
{
    auto yamlTypeNode = node["type"];

    if (yamlTypeNode && yamlTypeNode.IsSequence())
        return analyzeMultitype(yamlTypeNode.asSequence());

    auto yamlType = yamlTypeNode.as<string>("object");
    if (yamlType == "array")
    {
        if (auto yamlElemType = node["items"].asMap())
            if (!yamlElemType.empty())
            {
                auto&& elemType = analyzeTypeUsage(yamlElemType, TopLevel);
                const auto& protoType =
                    _translator.mapType("array", elemType.baseName,
                                        camelCase(node["title"].as<string>(
                                            elemType.baseName + "[]")));
                return protoType.specialize({move(elemType)});
            }

        return _translator.mapType("array");
    }
    if (yamlType == "object")
    {
        auto schema = analyzeSchema(node);
        if (isTopLevel && schema.empty() && currentRole() == OnlyOut)
            return {}; // The type returned by this API is void

        // If the schema is trivial it is treated as an alias for another type.
        // NB: if the found name or top-level $ref for the schema has any
        // type substitution configured in gtad.yaml, the schema will also
        // be trivial with the substituting type in parentTypes.front()
        if (schema.trivial())
            return schema.parentTypes.front();

        if (!schema.name.empty()) // Only ever filled for non-empty schemas
        {
            currentModel().addSchema(schema);
            // This is just to enrich the type usage with attributes, not
            // for type substitution
            auto tu = _translator.mapType("schema", schema.name);
            tu.name = schema.name;
            tu.call = schema.call;
            return tu;
        }
        // An OnlyIn empty object is schemaless but existing, mapType("object")
        // Also, a nameless non-empty schema is now treated as a generic
        // mapType("object"). TODO, low priority: ad-hoc typing (via tuples?)
    }
    auto tu = _translator.mapType(yamlType, node["format"].as<string>(""));
    if (!tu.empty())
        return tu;

    throw YamlException(node, "Unknown type: " + yamlType);
}

TypeUsage Analyzer::analyzeMultitype(const YamlSequence& yamlTypes)
{
    vector<TypeUsage> tus;
    for (const auto& yamlType: yamlTypes)
        tus.emplace_back(yamlType.IsScalar()
                         ? _translator.mapType(yamlType.as<string>())
                         : analyzeTypeUsage(yamlType));

    string baseTypes;
    for (const auto& t: tus)
    {
        if (!baseTypes.empty())
            baseTypes.push_back(',');
        baseTypes += t.baseName;
    }

    const auto& protoType = _translator.mapType("variant", baseTypes, baseTypes);
    cout << logOffset() << "Using " << protoType.qualifiedName()
         << " for a multitype: " << baseTypes << endl;
    return protoType.specialize(move(tus));
}

ObjectSchema Analyzer::analyzeSchema(const YamlMap& yamlSchema,
                                     SubschemasStrategy subschemasStrategy)
{
    ObjectSchema schema{currentRole(), yamlSchema["description"].as<string>("")};

    // Collect minimum information necessary to decide if subschemas inlining
    // should be suppressed (see also below)
    bool innerPropertiesFound = false;
    vector<string> refPaths;
    if (auto yamlSingleRef = yamlSchema["$ref"])
        refPaths.emplace_back(yamlSingleRef.as<string>());
    else if (auto yamlAllOf = yamlSchema["allOf"].asSequence())
        for (const auto& yamlEntry: yamlAllOf)
            if (YamlMap yamlInnerObject {yamlEntry}; yamlInnerObject) {
                if (auto yamlRef = yamlInnerObject["$ref"])
                    refPaths.emplace_back(yamlRef.as<string>());
                else if (!yamlInnerObject["title"]
                         && yamlInnerObject["type"].as<string>("") == "object") {
                    // Add properties from schema(s) inside allOf (see #52)
                    // making sure not to pull named objects though
                    innerPropertiesFound |=
                            !yamlInnerObject["properties"].asMap().empty() ||
                            yamlInnerObject["additionalProperties"].IsDefined();
                }
            }

    auto properties = yamlSchema["properties"].asMap();
    auto additionalProperties = yamlSchema["additionalProperties"];

    // Suppress inlining if the outer context is non-trivial.
    auto name = yamlSchema["title"].as<string>("");
    if (!name.empty() || refPaths.size() != 1 || innerPropertiesFound
        || properties || additionalProperties)
        subschemasStrategy = ImportSubschemas;

    for (auto&& refPath: refPaths) {
        // First try to resolve refPath in types map
        auto tu = _translator.mapType("$ref", refPath);
        if (!tu.empty()) {
            cout << "Using type " << tu.name << " for " << refPath << endl;
            schema.parentTypes.emplace_back(move(tu));
            continue;
        }

        // No shortcut in the types map; load the schema from the reference path
        auto&& [refModel, importPath] = loadDependency(refPath);
        if (refModel.types.empty())
            throw Exception(refPath + " has no schemas");

        auto&& refSchema = refModel.types.back();

        if (subschemasStrategy == InlineSubschemas) {
            cout << "Inlining the main schema from " << refPath
                 << endl;
            mergeFromSchema(schema, refSchema);
            if (refModel.types.size() > 1)
                currentModel().imports.insert(importPath);
            continue;
        }
        tu.name = refSchema.name;
        tu.baseName = tu.name.empty() ? refPath : tu.name;
        tu.addImport(move(importPath));
        schema.parentTypes.emplace_back(move(tu));
    }

    if (auto yamlOneOf = yamlSchema["oneOf"].asSequence())
        schema.parentTypes.emplace_back(analyzeMultitype(yamlOneOf));

    // Process inline schemas in allOf ($refs were processed earlier)
    if (auto yamlAllOf = yamlSchema["allOf"].asSequence())
        for (const auto& yamlEntry: yamlAllOf)
            if (auto yamlMap = yamlEntry.asMap(); yamlMap && !yamlMap["$ref"])
                mergeFromSchema(schema,analyzeSchema(yamlMap));

    if (name.empty() && schema.trivial())
        name = schema.parentTypes.back().name;
    if (!name.empty())
    {
        auto tu = _translator.mapType("schema", name);
        if (!tu.empty())
        {
            cout << logOffset() << "Using type " << tu.name << " for schema " << name << endl;
            schema.parentTypes = { move(tu) }; // Override the type entirely
            return schema;
        }
    }

    if (properties || additionalProperties ||
            (!schema.empty() && !schema.trivial()))
    {
        // If the schema is not just an alias for another type, name it.
        schema.name = camelCase(name);
    }
    schema.call = currentCall();

    if (schema.empty() && yamlSchema["type"].as<string>("object") != "object")
    {
        auto parentType = analyzeTypeUsage(yamlSchema);
        if (!parentType.empty())
            schema.parentTypes.emplace_back(move(parentType));
    }

    if (properties)
    {
        const auto requiredList = yamlSchema["required"].asSequence();
        for (const auto& property: properties) {
            auto&& baseName = property.first.as<string>();
            auto required = any_of(requiredList.begin(), requiredList.end(),
                                [&baseName](const YamlNode& n)
                                    { return baseName == n.as<string>(); } );
            addVarDecl(schema.fields,
                       analyzeTypeUsage(property.second),
                       move(baseName), schema,
                       property.second["description"].as<string>(""), required);
        }
    }
    if (additionalProperties) {
        TypeUsage tu;
        string description;
        switch (additionalProperties.Type()) {
            case YAML::NodeType::Map: {
                auto elemType =
                    analyzeTypeUsage(additionalProperties.asMap());
                const auto& protoType =
                    _translator.mapType("map", elemType.baseName,
                                        "string->" + elemType.baseName);
                tu = protoType.specialize({move(elemType)});
                description = additionalProperties["description"].as<string>("");
                break;
            }
            case YAML::NodeType::Scalar: // Generic map
                if (additionalProperties.as<bool>())
                    tu = _translator.mapType("map");
                break;
            default:
                throw YamlException(additionalProperties,
                    "additionalProperties should be either a boolean or a map");
        }

        if (!tu.empty()) {
            if (schema.empty())
                schema.parentTypes = {std::move(tu)};
            else
                schema.propertyMap =
                    makeVarDecl(move(tu), "additionalProperties", schema,
                                move(description));
        }
    }

    if (!schema.empty()) {
        cout << logOffset() << yamlSchema.location() << ": Found "
             << "schema " + schema.qualifiedName() << " for " << schema.role;
        if (schema.trivial())
            cout << " mapped to " << schema.parentTypes.front().qualifiedName();
        else {
            cout << " (parent(s): " << schema.parentTypes.size()
                 << ", field(s): " << schema.fields.size();
            if (!schema.propertyMap.type.empty())
                cout << " and a property map";
            cout << ")";
        }
        cout << endl;
    }
    return schema;
}

void Analyzer::mergeFromSchema(ObjectSchema& target,
                               const ObjectSchema& sourceSchema,
                               const string& baseName, bool required)
{
    if (sourceSchema.trivial())
    {
        // The schema consists of a single parent type, use that type instead.
        addVarDecl(target.fields, sourceSchema.parentTypes.front(), baseName,
                   sourceSchema, sourceSchema.description, required);
    } else if (sourceSchema.name.empty())
    {
        for (const auto& parentType: sourceSchema.parentTypes)
            addVarDecl(target.fields, parentType, parentType.name,
                       sourceSchema, "", required);
        for (const auto & param: sourceSchema.fields)
            currentModel().addVarDecl(target.fields, param);
        if (!sourceSchema.hasPropertyMap()) {
            if (target.hasPropertyMap()
                && target.propertyMap.type != sourceSchema.propertyMap.type)
                throw Exception("Conflicting property map types when merging "
                                "properties from "
                                + sourceSchema.qualifiedName());
            currentModel().addImports(sourceSchema.propertyMap.type);
            target.propertyMap = sourceSchema.propertyMap;
        }
    } else {
        currentModel().addSchema(sourceSchema);
        addVarDecl(target.fields, TypeUsage(sourceSchema), baseName,
                   sourceSchema, "", required);
    }
}

inline auto makeModelKey(const string& filePath)
{
    return withoutSuffix(filePath, ".yaml");
}

vector<string> loadContentTypes(const YamlMap& yaml, const char* keyName)
{
    if (auto yamlTypes = yaml[keyName].asSequence())
        return yamlTypes.asStrings();
    return {};
}

const Model& Analyzer::loadModel(const string& filePath, InOut inOut)
{
    cout << "Loading from " << filePath << endl;
    const auto yaml =
        YamlMap::loadFromFile(_baseDir / filePath, _translator.substitutions());
    if (_allModels.count(filePath) > 0) {
        clog << "Warning: the model has been loaded from " << filePath
             << " but will be reloaded again" << endl;
        _allModels.erase(filePath);
    }
    auto&& model = _allModels[makeModelKey(filePath)];
    ContextOverlay _modelContext(*this, fspath(filePath).parent_path(), &model);

    // Detect which file we have: API description or data definition
    if (!yaml["paths"]) {
        // XXX: this branch is yet unused; one day it will load event schemas
        fillDataModel(model, yaml, fspath(filePath).filename());
        return model;
    }

    // The rest is exclusive to API descriptions
    const auto paths = yaml.get("paths", true).asMap();
    if (yaml["swagger"].as<string>("") != "2.0")
        throw Exception(
                "This software only supports swagger version 2.0 for now");

    model.apiSpec = ApiSpec::Swagger;
    model.apiSpecVersion = 20; // Swagger/OpenAPI 2.0

    auto defaultConsumed = loadContentTypes(yaml, "consumes");
    auto defaultProduced = loadContentTypes(yaml, "produces");
    model.hostAddress = yaml["host"].as<string>("");
    model.basePath = yaml["basePath"].as<string>("");

    for (const YamlNodePair& yaml_path: paths)
        try {
            const Path path { yaml_path.first.as<string>() };

            for (const YamlNodePair& yaml_call_pair: yaml_path.second.asMap()) {
                auto verb = yaml_call_pair.first.as<string>();
                const YamlMap yamlCall { yaml_call_pair.second };
                auto operationId = yamlCall.get("operationId").as<string>();

                bool needsSecurity = false;
                if (const auto security = yamlCall["security"].asSequence())
                    needsSecurity = security[0]["accessToken"].IsDefined();

                cout << logOffset() << yamlCall.location()
                     << ": Found operation " << operationId
                     << " (" << path << ", " << verb << ')' << endl;

                Call& call = model.addCall(path, move(verb), move(operationId),
                                           needsSecurity);

                if (auto&& yamlSummary = yamlCall["summary"])
                    call.summary = yamlSummary.as<string>();
                if (auto&& yamlDescription = yamlCall["description"])
                    call.description = yamlDescription.as<string>();
                if (YamlMap yamlExternalDocs = yamlCall["externalDocs"])
                    call.externalDocs = {
                        yamlExternalDocs["description"].as<string>(""),
                        yamlExternalDocs.get("url").as<string>("")
                    };
                call.consumedContentTypes =
                        loadContentTypes(yamlCall, "consumes");
                if (call.consumedContentTypes.empty())
                    call.consumedContentTypes = defaultConsumed;
                call.producedContentTypes =
                        loadContentTypes(yamlCall, "produces");
                if (call.producedContentTypes.empty())
                    call.producedContentTypes = defaultProduced;

                const auto yamlParams = yamlCall["parameters"].asSequence();
                for (const YamlMap yamlParam: yamlParams) {
                    const auto& name = yamlParam.get("name").as<string>();

                    ContextOverlay _inContext(*this, name, OnlyIn, &call);

                    auto&& in = yamlParam.get("in").as<string>();
                    auto required = yamlParam["required"].as<bool>(false);
                    if (!required && in == "path") {
                        cout << logOffset() << yamlParam.location()
                        	 << ": warning: '" << name
                             << "' is in path but has no 'required' attribute"
                             << " - treating as required anyway" << endl;
                        required = true;
                    }
//                    cout << "Parameter: " << name << endl;
//                    for (const YamlNodePair p: yamlParam)
//                        cout << "  At " << p.first.location() << ": "
//                                        << p.first.as<string>() << endl;
//                    }
                    if (in != "body") {
                        addVarDecl(call.getParamsBlock(in),
                            analyzeTypeUsage(yamlParam, TopLevel),
                            name, call, yamlParam["description"].as<string>(""),
                            required, yamlParam["default"].as<string>(""));
                        continue;
                    }

                    auto&& bodySchema =
                        analyzeSchema(yamlParam.get("schema"),
                                      InlineSubschemas);
                    if (bodySchema.empty()) {
                        // Special case: an empty schema for a body parameter
                        // means a freeform object.
                        call.inlineBody = true;
                        addVarDecl(call.body.fields,
                                   _translator.mapType("object"), name, call,
                                   yamlParam["description"].as<string>(""),
                                   false);
                    } else {
                        // If the schema consists of a single parent type,
                        // inline that type.
                        if (bodySchema.trivial())
                            call.inlineBody = true;
                        mergeFromSchema(call.body, bodySchema, name, required);
                    }
                }
                const auto yamlResponses = yamlCall.get("responses").asMap();
                if (const auto yamlResponse = yamlResponses["200"].asMap()) {
                    Response response{ "200" };
                    if (auto yamlHeaders = yamlResponse["headers"])
                        for (const auto& yamlHeader: yamlHeaders.asMap())
                            addVarDecl(response.headers,
                                analyzeTypeUsage(yamlHeader.second, TopLevel),
                                yamlHeader.first.as<string>(), call,
                                yamlHeader.second["description"].as<string>(""),
                                false);

                    if (auto yamlSchema = yamlResponse["schema"]) {
                        response.body = analyzeSchema(yamlSchema,
                                                      InlineSubschemas);
                        if (response.body.trivial()) {
                            call.inlineResponse = true;
                            if (response.body.description.empty())
                                response.body.description =
                                    yamlResponse.get("description").as<string>();
                        }
                    }
                    call.responses.emplace_back(move(response));
                }
            }
        } catch (ModelException& me) {
            throw YamlException(yaml_path.first, me.message);
        }
    return model;
}

pair<const Model&, string> Analyzer::loadDependency(const string& relPath)
{
    const auto& fullPath = context().fileDir / relPath;
    const auto fullPathBase = makeModelKey(fullPath);
    const auto [mIt, unseen] = _allModels.try_emplace(fullPathBase);
    auto& model = mIt->second;
    const pair result {cref(model), _translator.mapImport(fullPathBase)};

    // If there is a matching model just return it
    auto modelRole = InAndOut;
    if (!unseen) {
        if (model.apiSpec != ApiSpec::JSONSchema)
            throw Exception("Dependency model for " + relPath
                            + " is found in the cache but doesn't seem to be"
                              " for a data schema (format " + model.apiSpec
                            + ")");
        if (!model.callClasses.empty())
            throw Exception(
                "Internal error: a JSON Schema model has API definitions");

        if (!model.types.empty()) {
            modelRole = model.types.back().role;
            if (modelRole == InAndOut || modelRole == currentRole()) {
                cout << logOffset() << "Reusing already loaded model for "
                     << relPath << " with role " << modelRole << endl;
                return result;
            }
            cout << logOffset()
                 << "Found existing data model generated for role " << modelRole
                 << "; the model will be reloaded for all roles" << endl;
            modelRole = InAndOut;
            model.clear();
        } else {
            cout << logOffset() << "Warning: empty data model for " << relPath
                 << " has been found in the cache; reloading" << endl;
            modelRole = currentRole();
        }
    }

    cout << logOffset() << "Loading data schema from " << relPath
         << " with role " << modelRole << endl;
    const auto yaml =
        YamlMap::loadFromFile(_baseDir / fullPath, _translator.substitutions());
    ContextOverlay _modelContext(*this, fullPath.parent_path(), &model,
                                 Identifier{{}, modelRole});
    fillDataModel(model, yaml, fspath(fullPathBase).filename());
    return result;
}

void Analyzer::fillDataModel(Model& m, const YamlNode& yaml,
                             const string& filename)
{
    m.apiSpec = ApiSpec::JSONSchema;
    m.apiSpecVersion = 201909; // Only JSON Schema 2019-09 is targeted for now
    auto&& s = analyzeSchema(yaml);
    if (s.name.empty())
        s.name = camelCase(filename);
    m.addSchema(move(s));
}
