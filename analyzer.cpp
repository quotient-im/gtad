#include "analyzer.h"

#include <iostream>
#include <algorithm>

#include <yaml-cpp/yaml.h>

#include "exception.h"
#include "translator.h"

using namespace std;
using namespace std::placeholders;

enum {
    CannotReadFromInput = AnalyzerCodes, UnknownParameterType,
    YamlFailsSchema
};

using YAML::Node;
using YAML::NodeType;
using NodePair = pair<Node, Node>;

Model initModel(string path)
{
    if (path.find(".yaml", path.size() - 5) != string::npos)
        path.erase(path.size() - 5);
    auto dirPos = path.rfind('/');
    if (dirPos == string::npos)
        return Model("", path);
    else
        return Model(path.substr(0, dirPos + 1), path.substr(dirPos + 1));
}

Analyzer::Analyzer(const std::string& filePath, const std::string& basePath,
                   const Translator& translator)
        : fileName(filePath), baseDir(basePath)
        , model(initModel(filePath)), translator(translator)
{ }

Node Analyzer::loadYaml() const
{
    try {
        cout << "Loading from " << baseDir + fileName << endl;
        return YAML::LoadFile(baseDir + fileName);
    }
    catch (YAML::BadFile &)
    {
        fail(CannotReadFromInput, "Couldn't read YAML from input");
    }
}

static const char* typenames[] = { "Undefined", "Null", "Scalar", "Sequence", "Map" };

const Node& Analyzer::assert(const Node& node, NodeType::value checkedType) const
{
    if (node)
    {
        if (node.Type() == checkedType)
            return node;

        cerr << fileName << ":" << node.Mark().line + 1 << ": the node "
             << "has a wrong type (expected " << typenames[checkedType]
             << ", got " << typenames[node.Type()] << endl;
    }
    else
        cerr << fileName << ": Analyzer::assert() on undefined node; check"
                "the higher-level node with get()" << endl;
    fail(YamlFailsSchema);
}

Node Analyzer::get(const Node& node, const string& subnodeName,
                   NodeType::value checkedType, bool allowNonexistent) const
{
    Node subnode = node[subnodeName];
    if (allowNonexistent || (subnode.IsDefined() && subnode.Type() == checkedType))
        return subnode;

    cerr << fileName << ":" << node.Mark().line + 1 << ": " << subnodeName;
    if (subnode)
        cerr << " has a wrong type (expected " << typenames[checkedType]
             << ", got " << typenames[subnode.Type()] << endl;
    else
        cerr << " is undefined" << endl;
    fail(YamlFailsSchema);
}

TypeUsage Analyzer::analyzeType(const Node& node, Analyzer::InOut inOut,
                                bool constRef)
{
    assert(node, NodeType::Map);

    auto refFilename = get<string>(node, "$ref", "");
    if (refFilename.empty())
        if (auto allOf = get(node, "allOf", NodeType::Sequence, true))
            // Fortunately, we have no multiple inheritance in CS API specs
            refFilename = get<string>(allOf[0], "$ref");
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
            fail(YamlFailsSchema);
        }
        if (m.types.size() > 1)
        {
            cerr << "File " << refFilename
                 << " has more than one data structure definition" << endl;
            fail(YamlFailsSchema);
        }

        return { m.types.back().name, "\"" + m.fileDir + m.filename + ".h\"" };
        // In theory, there can be more [properties] after [allOf]
    }

    auto yamlType = get<string>(node, "type");
    if (yamlType == "array")
    {
        auto elementType = get(node, "items", NodeType::Map);
        if (elementType.size() > 0)
            return translator.mapArrayType(
                    analyzeType(elementType, inOut, false), constRef);
    }
    if (yamlType == "object")
    {
        if (auto objectFieldsDef = get(node, "properties", NodeType::Map, true))
        {
            if (objectFieldsDef.size() == 0)
                return TypeUsage("");
            // TODO: Create a structure and fill it with the properties
        } else if (inOut == Out)
            return TypeUsage("");
    }
    TypeUsage tu =
            translator.mapType(yamlType, get<string>(node, "format", ""), constRef);
    if (!tu.name.empty())
        return tu;

    cerr << fileName << ":" << node.Mark().line + 1 << ": unknown type: " << yamlType;
    fail(UnknownParameterType);
}

void Analyzer::addParameter(const string& name, const Node& node, Call& call,
                            bool required, const string& in)
{
    model.addCallParam(call, analyzeType(node, In, true), name, required, in);
}

Model Analyzer::loadModel()
{
    Node yaml = loadYaml();
    assert(yaml, NodeType::Map);

    // Detect which file we have: calls or data definition
    if (auto paths = get(yaml, "paths", NodeType::Map, true))
    {
        auto produces = yaml["produces"];
        bool allCallsReturnJson = produces.size() == 1 &&
                                  produces.begin()->as<string>() ==
                                  "application/json";

        for (const NodePair& yaml_path: paths)
        {
            string path = yaml_path.first.as<string>();
            // Working around quirks in the current CS API definition
            while (*path.rbegin() == ' ' || *path.rbegin() == '/')
                path.erase(path.size() - 1);

            assert(yaml_path.second, NodeType::Map);
            for (const NodePair& yaml_call_pair: yaml_path.second)
            {
                string verb = yaml_call_pair.first.as<string>();
                Node yamlCall = assert(yaml_call_pair.second, NodeType::Map);

                auto yamlResponses = get(yamlCall, "responses", NodeType::Map);
                if (auto normalResponse = yamlResponses["200"])
                {
                    if (auto respSchema = get(normalResponse, "schema",
                                              NodeType::Map, true))
                    {
                        TypeUsage tu = analyzeType(respSchema, Out, false);
                        if (!tu.name.empty())
                        {
                            cerr << "Warning: skipping " << path << " - " << verb
                                 << ": non-trivial '200' response" << endl;
                            continue;
                        }
                    }
                }
                else
                {
                    cerr << "Warning: skipping " << path << " - " << verb
                         << ": no '200' response" << endl;
                    continue;
                }

                bool needsToken = false;
                if (auto security =
                        get(yamlCall, "security", NodeType::Sequence, true))
                    needsToken = security[0]["accessToken"].IsDefined();
                Call& call = model.addCall(path, verb, needsToken, "");

                cout << "Loading " << path << " - " << verb << endl;

                for (const auto& yamlParam:
                        get(yamlCall, "parameters", NodeType::Sequence, true))
                {
                    assert(yamlParam, NodeType::Map);
                    auto name = get<string>(yamlParam, "name");
                    auto in = get<string>(yamlParam, "in");
                    auto required =
                            in == "path" || get<bool>(yamlParam, "required", false);
                    if (in == "body")
                    {
                        auto schema = get(yamlParam, "schema", NodeType::Map);
                        if (auto properties = get(schema, "properties",
                                                  NodeType::Map, true))
                        {
                            auto requiredList = get(properties, "required",
                                                    NodeType::Sequence, true);
                            for (const NodePair& property: properties)
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

