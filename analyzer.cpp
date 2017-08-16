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
    CannotReadFromInput = AnalyzerCodes, UnknownParameterType,
    InvalidDataDefinition
};

void eraseSuffix(string* path, const string& suffix)
{
    auto trimAt = path->size() - suffix.size();
    if (path->find(suffix, trimAt) != string::npos)
        path->erase(trimAt);
}

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

TypeUsage Analyzer::analyzeType(const YamlMap& node, Analyzer::InOut inOut,
                                bool constRef)
{
    auto refFilename = node["$ref"].as<string>("");
    if (refFilename.empty())
        if (auto allOf = node.getSequence("allOf", true))
            // Fortunately, we have no multiple inheritance in CS API specs
            refFilename = YamlMap(allOf[0]).getScalar("$ref").as<string>();
    if (!refFilename.empty())
    {
        // The referenced file's path is relative to the current file's path;
        // we have to append a path to the current file's directory in order to
        // find the file.
        Model m = translator.processFile(refFilename, baseDir + model.fileDir);
        if (m.types.empty())
        {
            cerr << "File " << refFilename
                 << " doesn't have data definitions" << endl;
            fail(InvalidDataDefinition);
        }
        if (m.types.size() > 1)
        {
            cerr << "File " << refFilename
                 << " has more than one data structure definition" << endl;
            fail(InvalidDataDefinition);
        }

        return { m.types.back().name, "\"" + m.fileDir + m.filename + ".h\"" };
        // In theory, there can be more [properties] after [allOf]
    }

    auto yamlType = node.getScalar("type").as<string>();
    if (yamlType == "array")
    {
        auto elementType = node.getMap("items");
        if (elementType.size() > 0)
            return translator.mapArrayType(
                    analyzeType(elementType, inOut, false), constRef);
    }
    if (yamlType == "object")
    {
        if (auto objectFieldsDef = node.getMap("properties", true))
        {
            if (objectFieldsDef.size() == 0)
                return TypeUsage("");
            // TODO: Create a structure and fill it with the properties
        } else if (inOut == Out)
            return TypeUsage("");
    }
    TypeUsage tu =
            translator.mapType(yamlType, node["format"].as<string>(""), constRef);
    if (!tu.name.empty())
        return tu;

    cerr << node.location() << ": unknown type: " << yamlType;
    fail(UnknownParameterType);
}

void Analyzer::addParameter(const string& name, const YamlNode& node, Call& call,
                            bool required, const string& in)
{
    model.addCallParam(call, analyzeType(node, In, true), name, required, in);
}

Model Analyzer::loadModel()
{
    cout << "Loading from " << baseDir + fileName << endl;
    YamlMap yaml(YamlNode(baseDir + fileName));

    // Detect which file we have: calls or data definition
    if (const auto paths = yaml.getMap("paths", true))
    {
        const auto produces = yaml.getSequence("produces");
        bool allCallsReturnJson = produces.size() == 1 &&
                  produces[0].as<string>() == "application/json";

        for (const NodePair& yaml_path: paths)
        {
            string path = yaml_path.first.as<string>();
            // Working around quirks in the current CS API definition
            while (*path.rbegin() == ' ' || *path.rbegin() == '/')
                path.erase(path.size() - 1);

            for (const NodePair& yaml_call_pair: YamlMap(yaml_path.second))
            {
                const string verb = yaml_call_pair.first.as<string>();
                const YamlMap yamlCall { yaml_call_pair.second };

                const auto yamlResponses = yamlCall.getMap("responses");
                if (auto normalResponse = yamlResponses.getMap("200", true))
                {
                    if (auto respSchema = normalResponse.getMap("schema", true))
                    {
                        TypeUsage tu = analyzeType(respSchema, Out, false);
                        if (!tu.name.empty())
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

                auto callName = yamlCall.getScalar("operationId").as<string>();
                bool needsToken = false;
                if (const auto security = yamlCall.getSequence("security", true))
                    needsToken = security[0]["accessToken"].IsDefined();
                Call& call = model.addCall(path, verb, callName, needsToken, "");

                cout << "Loading " << callName << ": "
                     << path << " - " << verb << endl;

                for (const YamlMap yamlParam:
                        yamlCall.getSequence("parameters", true))
                {
                    auto name = yamlParam.getScalar("name").as<string>();
                    const auto in = yamlParam.getScalar("in").as<string>();
                    auto required =
                        in == "path" ||
                            yamlParam.getScalar("required", true).as<bool>(false);
                    if (in == "body")
                    {
                        const auto schema = yamlParam.getMap("schema");
                        if (const auto properties =
                                schema.getMap("properties", true))
                        {
                            const auto requiredList =
                                properties.getSequence("required", true);
                            for (const NodePair property: properties)
                            {
                                name = property.first.as<string>();
                                required = find_if(requiredList.begin(),
                                                   requiredList.end(),
                                                   [=] (const Node& n) {
                                                       return name == n.as<string>();
                                                   }) != requiredList.end();
                                addParameter(name, property.second, call, required);
                            }
                        } else
                            addParameter(name, schema, call, required); // No inline schema details
                    }
                    else
                        addParameter(name, yamlParam, call, required, in);
                }
            }
        }
    } else {
        model.types.emplace_back(convertMultiword(model.filename));
    }
    return std::move(model);
}

