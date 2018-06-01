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
using namespace std::placeholders;

Model initModel(string path)
{
    eraseSuffix(&path, ".yaml");
    auto dirPos = path.rfind('/');
    // The below code assumes that npos == -1 and that unsigned overflow works
    return Model(path.substr(0, dirPos + 1), path.substr(dirPos + 1));
}

Analyzer::Analyzer(string filePath, string basePath,
                   const Translator& translator)
        : fileName(filePath), baseDir(move(basePath))
        , model(initModel(move(filePath))), translator(translator)
{ }

TypeUsage Analyzer::analyzeType(const YamlMap& node, InOut inOut, string scope,
                                IsTopLevel isTopLevel)
{
    auto yamlTypeNode = node["type"];

    if (yamlTypeNode && yamlTypeNode.IsSequence())
        return translator.mapType("object"); // TODO: Multitype/variant support

    auto yamlType = yamlTypeNode.as<string>("object");
    if (yamlType == "array")
    {
        if (auto yamlElemType = node["items"].asMap())
            if (!yamlElemType.empty())
            {
                auto elemType =
                    analyzeType(yamlElemType, inOut, move(scope));
                const auto& protoType =
                    translator.mapType("array", elemType.baseName,
                        camelCase(node["title"].as<string>(
                                           '[' + elemType.baseName + ']')));
                return protoType.instantiate(move(elemType));
            }

        return translator.mapType("array");
    }
    if (yamlType == "object")
    {
        auto schema = analyzeSchema(node, inOut, scope);
        if (schema.empty())
        {
            if (const auto propertyMap = node["additionalProperties"])
                switch (propertyMap.Type())
                {
                    case YAML::NodeType::Map:
                    {
                        auto elemType =
                                analyzeType(propertyMap.asMap(), inOut, scope);
                        const auto& protoType =
                                translator.mapType("map", elemType.baseName,
                                    camelCase(node["title"].as<string>(
                                        "{string:" + elemType.baseName + '}')));
                        return protoType.instantiate(move(elemType));
                    }
                    case YAML::NodeType::Scalar:
                        if (propertyMap.as<bool>()) // Generic map
                            return translator.mapType("map");
                        else // Additional properties forbidden - no special treatment
                            break;
                    default:
                        throw YamlException(propertyMap,
                            "additionalProperties should be either a boolean or a map");
                }

            if (isTopLevel && inOut&Out)
                return TypeUsage(""); // The type returned by this API is void
        }
        if (!schema.name.empty()) // Only ever filled for non-empty schemas
        {
            model.addSchema(schema);
            auto tu = translator.mapType("schema");
            tu.scope = schema.scope;
            tu.name = tu.baseName = schema.name;
            return tu;
        }
        if (schema.trivial()) // An alias for another type
            return schema.parentTypes.front();
        // An In empty object is schemaless but existing, mapType("object")
        // Also, a nameless non-empty schema is now treated as a generic
        // mapType("object"). TODO, low priority: ad-hoc typing (via tuples?)
    }
    auto tu = translator.mapType(yamlType, node["format"].as<string>(""));
    if (!tu.empty())
        return tu;

    throw YamlException(node, "Unknown type: " + yamlType);
}

ObjectSchema Analyzer::analyzeSchema(const YamlMap& yamlSchema, InOut inOut,
                                     string scope, string locus)
{
    ObjectSchema schema;
    schema.inOut = inOut;

    vector<string> refPaths;
    if (auto yamlRef = yamlSchema["$ref"])
        refPaths.emplace_back(yamlRef.as<string>());
    else if (auto yamlAllOf = yamlSchema["allOf"].asSequence())
    {
        refPaths.resize(yamlAllOf.size());
        transform(yamlAllOf.begin(), yamlAllOf.end(), refPaths.begin(),
                  [](YamlMap p) { return p.get("$ref").as<string>(); });
    }

    for (const auto& refPath: refPaths)
    {
        // First try to resolve refPath in types map; if there's no match, load
        // the schema from the reference path.
        auto tu = translator.mapType("$ref", refPath);
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
        const Model& m =
                translator.processFile(model.fileDir + refPath, baseDir);
        if (m.types.empty())
            throw YamlException(yamlSchema, "The target file has no schemas");

        tu.name = m.types.back().name;
        // TODO: distinguish between interface files (that should be imported,
        // headers in C/C+) and implementation files (that should not).
        // filesList.front() assumes that there's only one interface file, and
        // it's at the front of the list, which is very naive.
        tu.addImport(m.dstFiles.empty() ? string{}
                                        : "\"" + m.dstFiles.front() + "\"");
        schema.parentTypes.emplace_back(move(tu));
    }

    if (schema.empty() && yamlSchema["type"].as<string>("object") != "object")
    {
        auto parentType = analyzeType(yamlSchema, inOut, scope);
        if (!parentType.empty())
            schema.parentTypes.emplace_back(move(parentType));
    }

    if (const auto properties = yamlSchema["properties"].asMap())
    {
        const auto requiredList = yamlSchema["required"].asSequence();
        for (const YamlNodePair property: properties)
        {
            const auto baseName = property.first.as<string>();
            auto required = any_of(requiredList.begin(), requiredList.end(),
                                [&baseName](const YamlNode& n)
                                    { return baseName == n.as<string>(); } );
            addVarDecl(schema.fields, analyzeType(property.second, inOut, scope),
                       baseName, required);
        }
    }
    if (!schema.empty())
    {
        auto name = camelCase(yamlSchema["title"].as<string>(""));
        // Checking for name difference commented out due to #28
        if (!schema.trivial()/* || name != qualifiedName(s.parentTypes.front()) */)
        {
            // If the schema is not just an alias for another type, name it.
            schema.name = move(name);
            schema.scope.swap(scope);
        }

        if (!schema.name.empty() || !schema.trivial())
        {
            cout << yamlSchema.location() << ": Found "
                 << (!locus.empty() ? locus + " schema"
                     : "schema " + qualifiedName(schema))
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
    }
    return schema;
}

void Analyzer::addParamsFromSchema(VarDecls& varList,
        const string& baseName, bool required, const ObjectSchema& paramSchema)
{
    if (paramSchema.parentTypes.empty())
    {
        for (const auto & param: paramSchema.fields)
            model.addVarDecl(varList, param);
    } else if (paramSchema.trivial())
    {
        // The schema consists of a single parent type, use that type instead.
        addVarDecl(varList, paramSchema.parentTypes.front(), baseName, required);
    } else
    {
        cerr << "Warning: found non-trivial schema for " << baseName
             << "; these are not supported, expect invalid parameter set"
             << endl;
        const auto typeName =
            paramSchema.name.empty() ? camelCase(baseName) : paramSchema.name;
        model.addSchema(paramSchema);
        addVarDecl(varList, TypeUsage(typeName), baseName, required);
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
    cout << "Loading from " << baseDir + fileName << endl;
    auto yaml = YamlMap::loadFromFile(baseDir + fileName, substitutions);

    // Detect which file we have: API description or just data definition
    // TODO: This should be refactored to two separate methods, since we shouldn't
    // allow loading an API description file referenced from another API description.
    if (const auto paths = yaml.get("paths", true).asMap())
    {
        if (yaml.get("swagger").as<string>() != "2.0")
            throw Exception(
                    "This software only supports swagger version 2.0 for now");

        auto defaultConsumed = loadContentTypes(yaml, "consumes");
        auto defaultProduced = loadContentTypes(yaml, "produces");
        model.hostAddress = yaml["host"].as<string>("");
        model.basePath = yaml["basePath"].as<string>("");

        for (const YamlNodePair& yaml_path: paths)
        try {
            const Path path { yaml_path.first.as<string>() };

            for (const YamlNodePair& yaml_call_pair: yaml_path.second.asMap())
            {
                auto verb = yaml_call_pair.first.as<string>();
                const YamlMap yamlCall { yaml_call_pair.second };
                auto operationId = yamlCall.get("operationId").as<string>();
                bool needsSecurity = false;
                if (const auto security = yamlCall["security"].asSequence())
                    needsSecurity = security[0]["accessToken"].IsDefined();

                cout << yamlCall.location() << ": Found operation "
                     << operationId
                     << " (" << path << ", " << verb << ')' << endl;

                Call& call =
                    model.addCall(path, move(verb), move(operationId),
                                  needsSecurity);

                call.consumedContentTypes =
                        loadContentTypes(yamlCall, "consumes");
                if (call.consumedContentTypes.empty())
                    call.consumedContentTypes = defaultConsumed;
                call.producedContentTypes =
                        loadContentTypes(yamlCall, "produces");
                if (call.producedContentTypes.empty())
                    call.producedContentTypes = defaultProduced;

                const auto yamlParams = yamlCall["parameters"].asSequence();
                for (const YamlMap yamlParam: yamlParams)
                {
                    const auto& name = yamlParam.get("name").as<string>();
                    auto&& in = yamlParam.get("in").as<string>();
                    auto required = yamlParam["required"].as<bool>(false);
                    if (!required && in == "path")
                    {
                        clog << yamlParam.location() << ": warning: '" << name
                             << "' is in path but has no 'required' attribute"
                             << " - treating as required anyway" << endl;
                        required = true;
                    }
//                    clog << "Parameter: " << name << endl;
//                    for (const YamlNodePair p: yamlParam)
//                    {
//                        clog << "  At " << p.first.location() << ": " << p.first.as<string>() << endl;
//                    }
                    if (in != "body")
                    {
                        addVarDecl(call.getParamsBlock(in),
                            analyzeType(yamlParam, In, call.name, TopLevel),
                            name, required, yamlParam["default"].as<string>(""));
                        continue;
                    }

                    auto&& bodySchema =
                        analyzeSchema(yamlParam.get("schema"), In, call.name,
                                      "request body");
                    if (bodySchema.empty())
                    {
                        // Special case: an empty schema for a body parameter
                        // means a freeform object.
                        call.inlineBody = true;
                        addVarDecl(call.bodyParams(),
                            translator.mapType("object"), name, false);
                    } else {
                        // The schema consists of a single parent type, inline that type.
                        if (bodySchema.trivial())
                            call.inlineBody = true;
                        addParamsFromSchema(call.bodyParams(),
                                            name, required, bodySchema);
                    }
                }
                const auto yamlResponses = yamlCall.get("responses").asMap();
                if (const auto yamlResponse = yamlResponses["200"].asMap())
                {
                    Response response { "200" };
                    if (auto yamlHeaders = yamlResponse["headers"])
                        for (const auto& yamlHeader: yamlHeaders.asMap())
                        {
                            addVarDecl(response.headers,
                                analyzeType(yamlHeader.second, Out, call.name,
                                            TopLevel),
                                yamlHeader.first.as<string>(), false);
                        }
                    if (auto yamlSchema = yamlResponse["schema"])
                    {
                        auto&& responseSchema =
                            analyzeSchema(yamlSchema, Out, call.name, "response");
                        if (!responseSchema.empty())
                            addParamsFromSchema(response.properties,
                                "content", true, responseSchema);
                    }
                    call.responses.emplace_back(move(response));
                }
            }
        }
        catch (ModelException& me)
        {
            throw YamlException(yaml_path.first, me.message);
        }
    } else {
        auto schema = analyzeSchema(yaml, inOut);
        if (schema.name.empty())
            schema.name = camelCase(model.srcFilename);
        model.addSchema(schema);
    }
    return std::move(model);
}
