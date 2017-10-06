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

TypeUsage Analyzer::tryResolveParentTypes(const YamlMap& yamlSchema)
{
    auto refFilename = yamlSchema["$ref"].as<string>("");
    if (refFilename.empty())
        if (auto allOf = yamlSchema.get("allOf", true).asSequence())
            // TODO: Multiple inheritance
            refFilename = allOf.get(0).asMap().get("$ref").as<string>();
    if (refFilename.empty())
        return TypeUsage("");

    // The referenced file's path is relative to the current file's path;
    // we have to append a path to the current file's directory in order to
    // find the file.
    const auto processResult =
        translator.processFile(refFilename, baseDir + model.fileDir);
    const Model& m = processResult.first; // Looking forward to switching to C++17
    const auto& filesList = processResult.second;
    if (m.types.empty())
    {
        cerr << "File " << refFilename << " has no object schemas" << endl;
        fail(InvalidSchemaDefinition);
    }
    if (m.types.size() > 1)
    {
        cerr << "Warning: File " << refFilename
             << " has more than one object schema, picking the first one"
             << endl;
    }

    return TypeUsage(m.types.begin()->first,
                     filesList.empty() ? string{} : "\"" + filesList.front() + "\"");
}

TypeUsage Analyzer::analyzeType(const YamlMap& node, Analyzer::InOut inOut)
{
    TypeUsage refTU = tryResolveParentTypes(node);
    if (!refTU.empty())
        return refTU;

    auto yamlTypeNode = node.get("type");
    if (yamlTypeNode.IsSequence())
        return translator.mapType("object"); // TODO: Multitype support

    auto yamlType = yamlTypeNode.as<string>();
    if (yamlType == "array")
    {
        auto elementType = node.get("items").asMap();
        if (elementType.size() > 0)
            return translator.mapArrayType(analyzeType(elementType, inOut));

        cerr << node.location() << ": an array must have a non-zero items map";
        fail(InvalidSchemaDefinition);
    }
    if (yamlType == "object")
    {
        analyzeSchema(node);
        if (auto yamlProperties = node.get("properties", true).asMap())
        {
            if (yamlProperties.size() == 0)
                return TypeUsage("");
        } else if (inOut == Out)
            return TypeUsage("");
        // The logic above is:
        // - An In empty object is a schemaless but existing object.
        // - An Out empty object is a non-existent object, "void".
    }
    TypeUsage tu = translator.mapType(yamlType, node["format"].as<string>(""));
    if (!tu.name.empty())
        return tu;

    cerr << node.location() << ": unknown type: " << yamlType;
    fail(UnknownParameterType);
}

ObjectSchema Analyzer::analyzeSchema(const YamlMap& yamlSchema)
{
    // This is called at the following points:
    // 1. Parsing a JSON Schema document (empty schema means an empty object)
    // 2. Parsing the list of body parameters (empty schema means any object)
    // 3. Parsing the list of result parts (empty schema means an empty object)
    ObjectSchema s;
    TypeUsage refTU = tryResolveParentTypes(yamlSchema);
    if (!refTU.empty())
        s.parentTypes.emplace_back(std::move(refTU));

    if (const auto properties = yamlSchema["properties"].asMap())
    {
        const auto requiredList = properties["required"].asSequence();
        for (const NodePair property: properties)
        {
            const auto name = property.first.as<string>();
            // The below is actually const but iterator_base<>::operator!=()
            // is declared non-const in yaml-cpp :-|
            auto requiredIt =
                find_if(requiredList.begin(), requiredList.end(),
                        [=](const Node& n) { return name == n.as<string>(); });

            s.fields.emplace_back(analyzeType(property.second, In),
                                  name, requiredIt != requiredList.end());
        }
    }
    cout << "Parsed object schema: " << s.fields.size() << " property(ies), "
         << s.parentTypes.size() << " parent type(s)"
         << endl;
    return s;
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

                const auto yamlResponses = yamlCall.get("responses").asMap();
                if (auto normalResponse = yamlResponses["200"].asMap())
                {
                    if (auto respSchema = normalResponse["schema"].asMap())
                    {
                        auto schema = analyzeSchema(respSchema);
                        if (!schema.fields.empty())
                        {
                            cerr << "Not implemented: skipping " << path << " - " << verb
                                 << ": non-trivial '200' response" << endl;
                            continue;
                        }
                    }
                }
                else
                {
                    cerr << "Not implemented: skipping " << path << " - " << verb
                         << ": no '200' response" << endl;
                    continue;
                }

                auto operationId = yamlCall.get("operationId").as<string>();
                bool needsToken = false;
                if (const auto security = yamlCall["security"].asSequence())
                    needsToken = security[0]["accessToken"].IsDefined();

                // TODO: Pass the response type
                Call& call = model.addCall(path, verb, operationId,
                                           needsToken, {});

                cout << "Loading " << operationId << ": "
                     << path << " - " << verb << endl;

                for (const YamlMap yamlParam:
                        yamlCall["parameters"].asSequence())
                {
                    auto name = yamlParam.get("name").as<string>();
                    const auto in = yamlParam.get("in").as<string>();
                    auto required =
                        in == "path" || yamlParam["required"].as<bool>(false);
                    if (in != "body")
                    {
                        model.addCallParam(call,
                            VarDecl(analyzeType(yamlParam, In), name, required),
                            in);
                        continue;
                    }

                    auto bodyParamSchema = analyzeSchema(yamlParam.get("schema"));
                    if (bodyParamSchema.parentTypes.empty())
                    {
                        // Special case: an empty schema for a body
                        // parameter means a freeform object
                        if (bodyParamSchema.fields.empty())
                            model.addCallParam(call,
                                VarDecl(translator.mapType("object"), name,
                                        false));
                        else
                            for (const auto & param: bodyParamSchema.fields)
                                model.addCallParam(call, param);
                    } else if (bodyParamSchema.parentTypes.size() == 1 &&
                               bodyParamSchema.fields.empty())
                    {
                        // Bare reference to another file where the type is
                        // defined.
                        model.addCallParam(call,
                            VarDecl(bodyParamSchema.parentTypes.front(),
                                    name, required));
                    } else
                    {
                        const auto typeName = camelCase(name);
                        model.types.emplace(typeName,
                                            std::move(bodyParamSchema));
                        model.addCallParam(call,
                            VarDecl(TypeUsage(typeName), name, required));
                    }
                }
            }
        }
    } else {
        model.types.emplace(camelCase(model.filename), analyzeSchema(yaml));
        for (auto type: model.types)
        {
            for (auto parentType: type.second.parentTypes)
                model.addImports(parentType);
            for (auto field: type.second.fields)
                model.addImports(field.type);
        }
    }
    return std::move(model);
}
