#include "analyzer.h"

#include <iostream>

#include <yaml-cpp/yaml.h>

#include <QtCore/QFileInfo>
#include <QtCore/QDir>

#include "exception.h"

using namespace std;

enum {
    CannotReadFromInput = AnalyzerCodes, UnknownParameterType,
    YamlFailsSchema
};

using YAML::Node;
using YAML::NodeType;

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
    if (node && node.Type() == checkedType)
        return node;

    cerr << fileName << ":" << node.Mark().line + 1 << ": the node ";
    if (node)
        cerr << "has a wrong type (expected "
             << typenames[checkedType] << ", got " << typenames[node.Type()] << endl;
    else
        cerr << "is undefined" << endl;
    fail(YamlFailsSchema);
}

Node Analyzer::get(const Node& node, const string& subnodeName,
                      NodeType::value checkedType) const
{
    if (node)
    {
        YAML::Node subnode = node[subnodeName];
        if (subnode && subnode.Type() == checkedType)
            return subnode;

        cerr << fileName << ":" << node.Mark().line + 1 << ": " << subnodeName;
        if (subnode)
            cerr << " has a wrong type (expected " << typenames[checkedType]
                 << ", got " << typenames[subnode.Type()] << endl;
        else
            cerr << " is undefined" << endl;
    }
    else
        cerr << fileName
             << ": checkNode() on undefined parent node, check your parsing code"
             << endl;
    fail(YamlFailsSchema);
}

pair<string, string> Analyzer::getTypename(const Node& node) const
{
    if (node["$ref"])
    {
        // The referenced file's path is relative to the current file's path;
        // we have to prepend a path to the current file's directory so that
        // YAML-Cpp finds the file.
        QFileInfo currentFileInfo { QString::fromStdString(fileName) };
        string currentFileDirPath = currentFileInfo.dir().path().toStdString();
        if (currentFileDirPath == ".")
            currentFileDirPath.clear();
        else
            currentFileDirPath.push_back('/');

        string localFilePath = getString(node, "$ref");
        Analyzer a(localFilePath, baseDir + currentFileDirPath);
        Model m = a.loadModel();
        if (m.dataModels.empty())
        {
            cerr << "File " << localFilePath
                 << " doesn't have data definitions" << endl;
            fail(YamlFailsSchema);
        }
        if (m.dataModels.size() > 1)
        {
            cerr << "File " << localFilePath
                 << " has more than one data structure definition" << endl;
            fail(YamlFailsSchema);
        }

        return { m.dataModels.back().name, "\"" + a.getFilenameBase() + ".h\"" };
    }

    string yamlType = getString(node, "type");
    if (yamlType == "array")
    {
        auto arrayElType = get(node, "items", NodeType::Map);

        auto innerType = getString(arrayElType, "type");
        if (innerType == "string")
            return { "QStringList", "<QtCore/QStringList>" };
        // TODO: items can have [properties]; we'll have to create a separate struct
        // to describe such type
    }
    pair<string, string> retval =
            yamlType == "string" ? make_pair("QString", "") :
            yamlType == "integer" || yamlType == "number" ? make_pair("int", "") :
            yamlType == "boolean" ? make_pair("bool", "") :
            yamlType == "array" ? make_pair("QVariantList", "<QtCore/QVariantList>") :
            yamlType == "object" ? make_pair("QVariant", "<QtCore/QVariant>") :
            make_pair("", "");
    if (!retval.first.empty())
        return retval;

    cerr << fileName << ":" << node.Mark().line + 1 << ": unknown type: " << yamlType;
    fail(UnknownParameterType);
}

void Analyzer::addParameter(string name, const Node& node, vector<string>& includes,
                            CallOverload& callOverload) const
{
    auto typeDef = getTypename(node);
    callOverload.params.emplace_back(typeDef.first, name);
    cout << "  Added input parameter: "
         << callOverload.params.back().toString();

    if (!typeDef.second.empty() &&
            find(includes.begin(), includes.end(), typeDef.second) == includes.end())
    {
        includes.emplace_back(typeDef.second);
        cout << "(with #include " << typeDef.second << ")";
    }
    cout << endl;
}

string Analyzer::getFilenameBase() const
{
    if (fileName.rfind(".yaml", fileName.size() - 5) != string::npos)
        return fileName.substr(0, fileName.size() - 5);
    return fileName;
}

Model Analyzer::loadModel() const
{
    Node yaml = loadYaml();

    Model model;
    // Detect which file we have, with a call, or just with a data definition
    if (auto paths = yaml["paths"])
    {
        assert(paths, NodeType::Map);

        auto produces = yaml["produces"];
        bool allCallsReturnJson = produces.size() == 1 &&
                                  produces.begin()->as<string>() ==
                                  "application/json";

        for (auto yaml_path: paths)
        {
            string path = yaml_path.first.as<string>();
            while (*path.rbegin() == ' ' || *path.rbegin() == '/')
                path.erase(path.size() - 1);

            assert(yaml_path.second, NodeType::Map);
            for (auto yaml_call_pair: yaml_path.second)
            {
                string verb = yaml_call_pair.first.as<string>();
                Node yaml_call = assert(yaml_call_pair.second, NodeType::Map);
                CallOverload& call = model.addCall(path, verb, "");
                if (auto s = yaml_call["security"])
                {
                    assert(s, NodeType::Sequence);
                    call.needsToken = s[0]["accessToken"].IsDefined();
                }

                cout << path << " - " << verb << endl;

                for (Node yaml_param: yaml_call["parameters"])
                {
                    assert(yaml_param, NodeType::Map);

                    if (yaml_param["type"])
                    {
                        // Got a simple type
                        auto name = getString(yaml_param, "name");
                        addParameter(name, yaml_param, model.includes, call);
                        continue;
                    }
                    // Got a complex type
                    auto schema = get(yaml_param, "schema", NodeType::Map);
                    Node properties = schema["properties"];
                    if (!properties)
                    {
                        // Got a complex type without inner schema details
                        auto name = getString(yaml_param, "name");
                        addParameter(name, schema, model.includes, call);
                        continue;
                    }

                    assert(properties, NodeType::Map);
                    for (auto property: properties)
                    {
                        auto name = property.first.as<string>();
                        addParameter(name, property.second, model.includes, call);
                    }
                }
            }
        }
    } else {
        assert(yaml, NodeType::Map);
        if (auto t = yaml["title"])
            model.dataModels.emplace_back(
                        assert(t, NodeType::Scalar).as<string>());
        else
        {
            auto bareFilename = getFilenameBase();
            auto n = bareFilename.rfind('/');
            if (n != string::npos)
                bareFilename = bareFilename.substr(n + 1);
            model.dataModels.emplace_back(bareFilename);
        }
    }
    return model;
}
