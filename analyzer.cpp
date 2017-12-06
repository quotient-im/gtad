#include "analyzer.h"

#include "exception.h"
#include "translator.h"

#include "yaml.h"

using namespace std;
using namespace std::placeholders;
using YAML::Node;
using YAML::NodeType;
using NodePair = YamlMap::NodePair;

enum {
    CannotReadFromInput = AnalyzerCodes, IncompatibleSwaggerVersion,
    UnknownParameterType, InvalidSchemaDefinition
};

Model initModel(string path)
{
    eraseSuffix(&path, ".yaml");
    auto dirPos = path.rfind('/');
    // The below code assumes that npos == -1 and that unsigned overflow works
    return Model(path.substr(0, dirPos + 1), path.substr(dirPos + 1));
}

Analyzer::Analyzer(const std::string& filePath, const std::string& basePath,
                   const Translator& translator)
        : fileName(filePath), baseDir(basePath)
        , model(initModel(filePath)), translator(translator)
{ }

ObjectSchema Analyzer::tryResolveRefs(const YamlMap& yamlSchema)
{
    vector<string> refPaths;
    if (auto yamlRef = yamlSchema["$ref"])
        refPaths.emplace_back(yamlRef.as<string>());
    else if (auto yamlAllOf = yamlSchema["allOf"].asSequence())
    {
        refPaths.resize(yamlAllOf.size());
        transform(yamlAllOf.begin(), yamlAllOf.end(), refPaths.begin(),
                  [](YamlMap p) { return p.get("$ref").as<string>(); });
    }
    if (refPaths.empty())
        return {};

    ObjectSchema schema;
    for (const auto& refPath: refPaths)
    {
        // The referenced file's path is relative to the current file's path;
        // we have to append a path to the current file's directory in order to
        // find the file.
        cout << "Sub-processing file: " << refPath << endl
             << "  baseDir: " << baseDir << endl
             << "  model dir: " << model.fileDir << endl;
        const auto processResult =
            translator.processFile(model.fileDir + refPath, baseDir);
        const Model& m = processResult.first; // Looking forward to switching to C++17
        const auto& filesList = processResult.second;
        if (m.types.empty())
        {
            cerr << "File " << refPath << " has no object schemas" << endl;
            fail(InvalidSchemaDefinition);
        }
        schema.parentTypes.emplace_back(
            m.types.back().name,
            filesList.empty() ? string{} : "\"" + filesList.front() + "\"");
        // TODO: distinguish between interface files (that should be imported,
        // headers in C/C+) and implementation files (that should not).
        // filesList.front() assumes that there's only one interface file, and
        // it's at the front of the list, which is very naive.
    }
    return schema;
}

TypeUsage Analyzer::analyzeType(const YamlMap& node, Analyzer::InOut inOut, string scope)
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
                auto elemType = analyzeType(yamlElemType, inOut, move(scope));
                const auto& protoType =
                    translator.mapType("array", elemType.baseName, "array");
                return protoType.instantiate(move(elemType));
            }

        return translator.mapType("array");
    }
    if (yamlType == "object")
    {
        auto schema = analyzeSchema(node, move(scope));
        if (inOut == Out && schema.empty())
            return TypeUsage(""); // Non-existent object, void
        if (!schema.name.empty()) // Only ever filled for non-empty schemas
        {
            model.addSchema(schema);
            TypeUsage tu = translator.mapType("schema");
            tu.scope = schema.scope;
            tu.name = tu.baseName = schema.name;
            return tu;
        }
        // An In empty object is schemaless but existing, mapType("object")
        // Also, a nameless non-empty schema is now treated as a generic
        // mapType("object"). TODO, low priority: ad-hoc typing (via tuples?)
    }
    auto tu = translator.mapType(yamlType, node["format"].as<string>(""));
    if (!tu.empty())
        return tu;

    cerr << node.location() << ": unknown type: " << yamlType;
    fail(UnknownParameterType);
}

ObjectSchema Analyzer::analyzeSchema(const YamlMap& yamlSchema, string scope)
{
    // This is called at the following points:
    // 1. Parsing a JSON Schema document (empty schema means an empty object)
    // 2. Parsing the list of body parameters (empty schema means any object)
    // 3. Parsing the list of result parts (empty schema means an empty object)
    ObjectSchema s = tryResolveRefs(yamlSchema);
    if (s.empty() && yamlSchema["type"].as<string>("object") != "object")
    {
        auto parentType = analyzeType(yamlSchema, In, scope);
        if (!parentType.empty())
            s.parentTypes.emplace_back(move(parentType));
    }

    if (const auto properties = yamlSchema["properties"].asMap())
    {
        const auto requiredList = yamlSchema["required"].asSequence();
        for (const NodePair property: properties)
        {
            const auto name = property.first.as<string>();
            // The below is actually const but iterator_base<>::operator!=()
            // is declared non-const in yaml-cpp :-|
            auto requiredIt =
                find_if(requiredList.begin(), requiredList.end(),
                        [=](const Node& n) { return name == n.as<string>(); });
            s.fields.emplace_back(analyzeType(property.second, In, scope),
                                  name, requiredIt != requiredList.end());
        }
    }
    if (!s.empty())
    {
        s.name = camelCase(yamlSchema["title"].as<string>(""));
        s.scope.swap(scope);
        cout << "Parsed object schema '"
             << (s.name.empty() ? "(nameless)" :
                 s.scope.empty() ? s.name : s.scope + '.' + s.name)
             << "' (parent types: " << s.parentTypes.size()
             << ", properties: " << s.fields.size()
             << ") at " << yamlSchema.location() << endl;
    } else
        cout << "Found empty object schema at " << yamlSchema.location() << endl;
    return s;
}

void Analyzer::addParamsFromSchema(VarDecls& varList,
        std::string name, bool required, const ObjectSchema& paramSchema)
{
    if (paramSchema.parentTypes.empty())
    {
        for (const auto & param: paramSchema.fields)
            model.addVarDecl(varList, param);
    } else if (paramSchema.trivial())
    {
        // The schema consists of a single parent type, use that type instead.
        model.addVarDecl(varList,
            VarDecl(paramSchema.parentTypes.front(), move(name), required));
    } else
    {
        cerr << "Warning: found non-trivial schema for " << name
             << "; these are not supported, expect invalid parameter set"
             << endl;
        const auto typeName =
            paramSchema.name.empty() ? camelCase(name) : paramSchema.name;
        model.addSchema(move(paramSchema));
        model.addVarDecl(varList,
                         VarDecl(TypeUsage(typeName), move(name), required));
    }
}

Model Analyzer::loadModel(const pair_vector_t<string>& substitutions)
{
    cout << "Loading from " << baseDir + fileName << endl;
    auto yaml = YamlMap::loadFromFile(baseDir + fileName, substitutions);

    // Detect which file we have: API description or just data definition
    // TODO: This should be refactored to two separate methods, since we shouldn't
    // allow loading an API description file referenced from another API description.
    if (const auto paths = yaml.get("paths", true).asMap())
    {
        if (yaml.get("swagger").as<string>() != "2.0")
            fail(IncompatibleSwaggerVersion,
                 "This software only supports swagger version 2.0 for now");

        model.hostAddress = yaml["host"].as<string>("");
        model.basePath = yaml["basePath"].as<string>("");

        for (const NodePair& yaml_path: paths)
        {
            string path = yaml_path.first.as<string>();
            // Working around quirks in the current CS API definition
            while (*path.rbegin() == ' ' || *path.rbegin() == '/')
                path.erase(path.size() - 1);

            for (const NodePair& yaml_call_pair: yaml_path.second.asMap())
            {
                const string verb = yaml_call_pair.first.as<string>();
                const YamlMap yamlCall { yaml_call_pair.second };
                const auto operationId = yamlCall.get("operationId").as<string>();
                bool needsSecurity = false;
                if (const auto security = yamlCall["security"].asSequence())
                    needsSecurity = security[0]["accessToken"].IsDefined();

                Call& call = model.addCall(path, verb, operationId, needsSecurity);

                cout << "Loading " << operationId << ": "
                     << path << " - " << verb << endl;

                for (const YamlMap yamlParam:
                        yamlCall["parameters"].asSequence())
                {
                    auto&& name = yamlParam.get("name").as<string>();
                    auto&& in = yamlParam.get("in").as<string>();
                    auto required =
                        in == "path" || yamlParam["required"].as<bool>(false);
                    if (in != "body")
                    {
                        model.addVarDecl(call.getParamsBlock(in),
                            VarDecl(analyzeType(yamlParam, In, call.name),
                                    name, required));
                        continue;
                    }

                    auto&& bodySchema =
                        analyzeSchema(yamlParam.get("schema"), call.name);
                    if (bodySchema.empty())
                    {
                        // Special case: an empty schema for a body parameter
                        // means a freeform object.
                        call.inlineBody = true;
                        model.addVarDecl(call.bodyParams(),
                            VarDecl(translator.mapType("object"), name, false));
                    }
                    else
                    {
                        // The schema consists of a single parent type, inline that type.
                        if (bodySchema.trivial())
                            call.inlineBody = true;
                        addParamsFromSchema(call.getParamsBlock("body"),
                                            name, required, bodySchema);
                    }
                }
                const auto yamlResponses = yamlCall.get("responses").asMap();
                if (const auto yamlResponse = yamlResponses["200"].asMap())
                {
                    Response response { "200", {} };
                    if (auto yamlHeaders = yamlResponse["headers"])
                        for (const auto yamlHeader: yamlHeaders.asMap())
                        {
                            // TODO: fill in headers
                        }
                    if (auto yamlSchema = yamlResponse["schema"])
                    {
                        auto&& responseSchema =
                            analyzeSchema(yamlSchema, call.name);
                        if (!responseSchema.empty())
                            addParamsFromSchema(response.properties,
                                "result", true, responseSchema);
                    }
                    call.responses.emplace_back(move(response));
                }
            }
        }
    } else {
        auto schema = analyzeSchema(yaml, "");
        if (schema.name.empty())
            schema.name = camelCase(model.filename);
        model.addSchema(schema);
    }
    return std::move(model);
}
