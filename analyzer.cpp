#include "analyzer.h"

#include <iostream>
#include <algorithm>
#include <functional>

#include <yaml-cpp/yaml.h>

#include "exception.h"
#include "translator.h"

using namespace std;
using namespace std::placeholders;

enum {
    CannotReadFromInput = AnalyzerCodes, UnknownParameterType,
    ComplexTypeOutsideBody,
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

TypeUsage Analyzer::resolveType(const Node& node, bool constRef)
{
    assert(node, NodeType::Map);

    auto refFilename = getString(node, "$ref", "");
    if (refFilename.empty())
        if (auto allOf = get(node, "allOf", NodeType::Sequence, true))
            // Fortunately, we have no multiple inheritance in CS API specs
            refFilename = getString(allOf[0], "$ref");
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

    auto yamlType = getString(node, "type");
    if (yamlType == "array")
    {
        auto arrayElType = get(node, "items", NodeType::Map);
        return translator.mapArrayType(resolveType(arrayElType, false), constRef);
    }
    TypeUsage tu =
            translator.mapType(yamlType, getString(node, "format", ""), constRef);
    if (!tu.name.empty())
        return tu;

    cerr << fileName << ":" << node.Mark().line + 1 << ": unknown type: " << yamlType;
    fail(UnknownParameterType);
}

void Analyzer::addParameter(string name, const Node& node, Call& call,
                            const string& in)
{
    auto type = resolveType(node, true);
    VarDecl p(type.name, name);
    call.addParam(p, in);
    cout << "  Added input parameter for " << in << ": " << p.toString() << endl;

    model.imports.insert(type.imports.begin(), type.imports.end());
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

                bool needsToken = false;
                if (auto security =
                        get(yamlCall, "security", NodeType::Sequence, true))
                    needsToken = security[0]["accessToken"].IsDefined();
                Call& call = model.addCall(path, verb, needsToken, "");

                cout << path << " - " << verb << endl;

                for (auto yamlParam:
                        get(yamlCall, "parameters", NodeType::Sequence, true))
                {
                    assert(yamlParam, NodeType::Map);
                    auto name = getString(yamlParam, "name");
                    auto in = getString(yamlParam, "in");
                    if (in == "body")
                    {
                        auto schema = get(yamlParam, "schema", NodeType::Map);
                        if (auto properties = get(schema, "properties",
                                                  NodeType::Map, true))
                            for (const NodePair& property: properties)
                                addParameter(property.first.as<string>(),
                                             property.second, call);
                        else
                            addParameter(name, schema, call); // No inline schema details
                    }
                    else
                        addParameter(name, yamlParam, call, in);
                }

                auto yamlResponses = get(yamlCall, "responses", NodeType::Map);
                auto normalResponse = yamlResponses["200"];
                if (!normalResponse || (normalResponse && normalResponse["schema"]))
                    cerr << "Warning: Non-trivial responses not supported yet" << endl;
            }
        }
    } else {
        model.types.emplace_back(convertMultiword(model.filename));
    }
    return std::move(model);
}

