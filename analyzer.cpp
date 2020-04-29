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

using namespace std;

Model initModel(string path)
{
    eraseSuffix(&path, ".yaml");
    auto dirPos = path.rfind('/');
    // The below code assumes that npos == -1 and that unsigned overflow works
    return Model(path.substr(0, dirPos + 1), path.substr(dirPos + 1));
}

Analyzer::Analyzer(string filePath, string basePath,
                   const Translator& translator)
    : fileName(filePath)
    , _baseDir(move(basePath))
    , model(initModel(move(filePath)))
    , _translator(translator)
{ }

TypeUsage Analyzer::analyzeTypeUsage(const YamlMap& node, InOut inOut,
                                     string scope, IsTopLevel isTopLevel)
{
    auto yamlTypeNode = node["type"];

    if (yamlTypeNode && yamlTypeNode.IsSequence())
        return analyzeMultitype(yamlTypeNode.asSequence(), inOut, scope);

    auto yamlType = yamlTypeNode.as<string>("object");
    if (yamlType == "array")
    {
        if (auto yamlElemType = node["items"].asMap())
            if (!yamlElemType.empty())
            {
                auto elemType =
                    analyzeTypeUsage(yamlElemType, inOut, move(scope));
                const auto& protoType =
                    _translator.mapType("array", elemType.baseName,
                        camelCase(node["title"].as<string>(
                                           elemType.baseName + "[]")));
                return protoType.instantiate({move(elemType)});
            }

        return _translator.mapType("array");
    }
    if (yamlType == "object")
    {
        auto schema = analyzeSchema(node, inOut, scope);
        if (isTopLevel && schema.empty() && bool(inOut&Out))
            return {}; // The type returned by this API is void

        if (schema.trivial()) // An alias for another type
            return schema.parentTypes.front();

        if (!schema.name.empty()) // Only ever filled for non-empty schemas
        {
            model.addSchema(schema);
            auto tu = _translator.mapType("schema", schema.name);
            tu.scope = schema.scope;
            tu.name = tu.baseName = schema.name;
            return tu;
        }
        // An In empty object is schemaless but existing, mapType("object")
        // Also, a nameless non-empty schema is now treated as a generic
        // mapType("object"). TODO, low priority: ad-hoc typing (via tuples?)
    }
    auto tu = _translator.mapType(yamlType, node["format"].as<string>(""));
    if (!tu.empty())
        return tu;

    throw YamlException(node, "Unknown type: " + yamlType);
}

TypeUsage Analyzer::analyzeMultitype(const YamlSequence& yamlTypes, InOut inOut,
                                     const string& scope)
{
    vector<TypeUsage> tus;
    for (const auto& yamlType: yamlTypes)
        tus.emplace_back(yamlType.IsScalar()
                         ? _translator.mapType(yamlType.as<string>())
                         : analyzeTypeUsage(yamlType, inOut, scope));

    string baseTypes;
    for (const auto& t: tus)
    {
        if (!baseTypes.empty())
            baseTypes.push_back(',');
        baseTypes += t.baseName;
    }

    const auto& protoType = _translator.mapType("variant", baseTypes, baseTypes);
    cout << "Using " << protoType.name << " for a multitype: "
         << baseTypes << endl;
    return protoType.instantiate(move(tus));
}

ObjectSchema Analyzer::analyzeSchema(const YamlMap& yamlSchema, InOut inOut,
                                     string scope, const string& locus,
                                     SubschemasStrategy subschemasStrategy)
{
    ObjectSchema schema;
    schema.inOut = inOut;

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

    // Suppress inlining for trivial schemas. This is needed because otherwise
    // GTAD agressively inlines data structures that have their own
    // name and meaning, with consumers of the generated API having
    // to rebuild those structures back from their pieces.
    if (refPaths.size() == 1
        && !(innerPropertiesFound || properties || additionalProperties))
        subschemasStrategy = ImportSubschemas;

    auto name = yamlSchema["title"].as<string>("");
    for (const auto& refPath: refPaths)
    {
        // First try to resolve refPath in types map; if there's no match, load
        // the schema from the reference path.
        auto tu = _translator.mapType("$ref", refPath);
        if (!tu.empty())
        {
            cout << "Using type " << tu.name << " for " << refPath << endl;
            schema.parentTypes.emplace_back(move(tu));
            continue;
        }
        // The referenced file's path is relative to the current file's path;
        // we have to append a path to the current file's directory in order to
        // find the file.
        cout << "Sub-processing schema in "
             << model.fileDir << "./" << refPath << endl;
        auto&& refModel = // TODO, #33: Only load the model here
                _translator.processFile(model.fileDir + refPath, _baseDir);
        if (refModel.types.empty())
            throw YamlException(yamlSchema, "The target file has no schemas");

        auto&& refSchema = refModel.types.back();

        // TODO, #33: File generation should be before using dstFiles.

        // XXX: maybe distinguish between interface files (that should be
        // imported, such as headers in C/C+) and implementation files
        // (that should not). For now, refModel.dstFiles.front() assumes that
        // there's only one interface file, and it's at the front of the list.
        auto&& importFile = refModel.dstFiles.empty() ? string{}
                                    : "\"" + refModel.dstFiles.front() + "\"";

        if (subschemasStrategy == ImportSubschemas && !refModel.trivial()) {
            tu.name = refSchema.name;
            tu.baseName = tu.name.empty() ? refPath : tu.name;
            tu.addImport(move(importFile));
            schema.parentTypes.emplace_back(move(tu));
            continue;
        }

        // Instead of adding another type, just inline parts of the subschema.
        move(refSchema.parentTypes.begin(), refSchema.parentTypes.end(),
             back_inserter(schema.parentTypes)); // XXX: Recurse?

        // FIXME: The below is ugly. We try to have a cake and eat it,
        // inline a schema but still use other definitions from its file.
        // That doesn't look right. On the other hand, having an additional
        // level of structure just because it's so defined in
        // the API description (but not in the working API) is not pretty
        // either.
        for (auto field: refSchema.fields) {
            // Watch out for supplementary schemas defined in the same file
            if (any_of(refModel.types.begin(), refModel.types.end() - 1,
                       [&field](const ObjectSchema& s) {
                           return field.type.name == s.name
                                  && field.type.scope == s.scope;
                       })) {
                field.type.addImport(importFile);
            }
            schema.fields.emplace_back(move(field));
        }
    }

    if (auto yamlOneOf = yamlSchema["oneOf"].asSequence())
        schema.parentTypes.emplace_back(
                    analyzeMultitype(yamlOneOf, inOut, scope));

    // Process inline schemas in allOf ($refs were processed earlier)
    if (auto yamlAllOf = yamlSchema["allOf"].asSequence())
        for (const auto& yamlEntry: yamlAllOf)
            if (auto yamlMap = yamlEntry.asMap(); yamlMap && !yamlMap["$ref"])
                addParamsFromSchema(schema.fields, {}, {}, true,
         				analyzeSchema(yamlMap, inOut, scope,
                                      locus + " (inner definition)"));

    if (name.empty() && schema.trivial())
        name = schema.parentTypes.back().name;
    if (!name.empty())
    {
        auto tu = _translator.mapType("schema", name);
        if (!tu.empty())
        {
            cout << "Using type " << tu.name << " for schema " << name << endl;
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
    schema.scope.swap(scope); // Use schema.scope, not scope after this line
    schema.description = yamlSchema["description"].as<string>("");

    if (schema.empty() && yamlSchema["type"].as<string>("object") != "object")
    {
        auto parentType = analyzeTypeUsage(yamlSchema, inOut, schema.scope);
        if (!parentType.empty())
            schema.parentTypes.emplace_back(move(parentType));
    }

    if (properties)
    {
        const auto requiredList = yamlSchema["required"].asSequence();
        for (const auto& property: properties)
        {
            const auto baseName = property.first.as<string>();
            auto required = any_of(requiredList.begin(), requiredList.end(),
                                [&baseName](const YamlNode& n)
                                    { return baseName == n.as<string>(); } );
            addVarDecl(schema.fields,
                       analyzeTypeUsage(property.second, inOut, schema.scope),
                       baseName, schema,
                       property.second["description"].as<string>(""), required);
        }
    }
    if (additionalProperties)
    {
        TypeUsage tu;
        string description;
        switch (additionalProperties.Type())
        {
            case YAML::NodeType::Map:
            {
                auto elemType = analyzeTypeUsage(additionalProperties.asMap(),
                                                 inOut, schema.scope);
                const auto& protoType =
                    _translator.mapType("map", elemType.baseName,
                                           "string->" + elemType.baseName);
                tu = protoType.instantiate({move(elemType)});
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

        if (!tu.empty())
        {
            if (schema.empty())
                schema.parentTypes = { std::move(tu) };
            else
                schema.propertyMap = makeVarDecl(std::move(tu),
                    "additionalProperties", schema, move(description));
        }
    }

    if (!schema.empty()) {
        cout << yamlSchema.location() << ": Found "
             << (!locus.empty() ? locus + " schema"
                 : "schema " + schema.qualifiedName())
             << " for "
             << (schema.inOut == In ? "input" :
                 schema.inOut == Out ? "output" :
                 schema.inOut == (In|Out) ? "in/out" : "undefined use");
        if (schema.trivial())
            cout << " mapped to "
                 << qualifiedName(schema.parentTypes.front()) << endl;
        else
            cout << " (parent(s): " << schema.parentTypes.size()
                 << ", field(s): " << schema.fields.size() << ")" << endl;
    }
    return schema;
}

void Analyzer::addParamsFromSchema(VarDecls& varList, const Scope& scope,
        const string& baseName, bool required, const ObjectSchema& paramSchema)
{
    if (paramSchema.trivial())
    {
        // The schema consists of a single parent type, use that type instead.
        addVarDecl(varList, paramSchema.parentTypes.front(), baseName, scope,
                   paramSchema.description, required);
    } else if (paramSchema.name.empty())
    {
        for (const auto& parentType: paramSchema.parentTypes)
            addVarDecl(varList, parentType, parentType.name, scope, "", required);
        for (const auto & param: paramSchema.fields)
            model.addVarDecl(varList, param);
    } else {
        model.addSchema(paramSchema);
        addVarDecl(varList, TypeUsage(paramSchema.name), baseName, scope,
                   "", required);
    }
}

vector<string> loadContentTypes(const YamlMap& yaml, const char* keyName)
{
    if (auto yamlTypes = yaml[keyName].asSequence())
        return yamlTypes.asStrings();
    return {};
}

Model Analyzer::loadModel(const pair_vector_t<string>& substitutions,
                          InOut inOut)
{
    cout << "Loading from " << _baseDir + fileName << endl;
    auto yaml = YamlMap::loadFromFile(_baseDir + fileName, substitutions);

    // Detect which file we have: API description or just data definition
    // TODO: This should be refactored to two separate methods, since we shouldn't
    // allow loading an API description file referenced from another API description.
    if (const auto paths = yaml.get("paths", true).asMap())
    {
        {
            if (yaml["swagger"].as<string>("") != "2.0")
                throw Exception(
                        "This software only supports swagger version 2.0 for now");
            model.apiSpec = Swagger();
            model.apiSpecVersion = 200; // 2.0
        }

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

                cout << yamlCall.location() << ": Found operation "
                     << operationId
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
                    auto&& in = yamlParam.get("in").as<string>();
                    auto required = yamlParam["required"].as<bool>(false);
                    if (!required && in == "path") {
                        clog << yamlParam.location() << ": warning: '" << name
                             << "' is in path but has no 'required' attribute"
                             << " - treating as required anyway" << endl;
                        required = true;
                    }
//                    clog << "Parameter: " << name << endl;
//                    for (const YamlNodePair p: yamlParam)
//                    {
//                        clog << "  At " << p.first.location() << ": "
//                                        << p.first.as<string>() << endl;
//                    }
                    if (in != "body") {
                        addVarDecl(call.getParamsBlock(in),
                            analyzeTypeUsage(yamlParam, In, call.name, TopLevel),
                            name, call, yamlParam["description"].as<string>(""),
                            required, yamlParam["default"].as<string>(""));
                        continue;
                    }

                    auto&& bodySchema =
                        analyzeSchema(yamlParam.get("schema"), In, call.name,
                                      "request body", InlineSubschemas);
                    if (bodySchema.empty()) {
                        // Special case: an empty schema for a body parameter
                        // means a freeform object.
                        call.inlineBody = true;
                        addVarDecl(call.params[InBody],
                                   _translator.mapType("object"), name, call,
                                   yamlParam["description"].as<string>(""),
                                   false);
                    } else {
                        // If the schema consists of a single parent type,
                        // inline that type.
                        if (bodySchema.trivial())
                            call.inlineBody = true;
                        addParamsFromSchema(call.params[InBody], call,
                                            name, required, bodySchema);
                    }
                }
                const auto yamlResponses = yamlCall.get("responses").asMap();
                if (const auto yamlResponse = yamlResponses["200"].asMap()) {
                    Response response {
                        "200", yamlResponse.get("description").as<string>()
                    };
                    if (auto yamlHeaders = yamlResponse["headers"])
                        for (const auto& yamlHeader: yamlHeaders.asMap())
                            addVarDecl(response.headers,
                                analyzeTypeUsage(yamlHeader.second, Out,
                                                 call.name, TopLevel),
                                yamlHeader.first.as<string>(), call,
                                yamlHeader.second["description"].as<string>(""),
                                false);

                    if (auto yamlSchema = yamlResponse["schema"]) {
                        auto&& responseSchema =
                            analyzeSchema(yamlSchema, Out, call.name,
                                          "response", InlineSubschemas);
                        if (responseSchema.trivial()) {
                            call.inlineResponse = true;
                            if (responseSchema.description.empty())
                                responseSchema.description =
                                    response.description;
                        }
                        if (!responseSchema.empty())
                            addParamsFromSchema(response.properties, call,
                                "data", true, responseSchema);
                    }
                    call.responses.emplace_back(move(response));
                }
            }
        } catch (ModelException& me) {
            throw YamlException(yaml_path.first, me.message);
        }
    } else {
        auto schema = analyzeSchema(yaml, inOut);
        if (schema.name.empty())
            schema.name = camelCase(model.srcFilename);
        model.addSchema(schema);
    }
    return move(model);
}
