#include "analyzer.h"

#include <iostream>

#include <yaml-cpp/yaml.h>

#include <QtCore/QFileInfo>
#include <QtCore/QDir>

#include "exception.h"

using namespace std;

enum {
    CannotReadFromInput = AnalyzerCodes, UnknownParameterType,
    ComplexTypeTargetsOutsideBody,
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
             << ": get() on undefined parent node, check your parsing code"
             << endl;
    fail(YamlFailsSchema);
}

TypeUsage Analyzer::resolveType(const Node& node) const
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
        if (m.types.empty())
        {
            cerr << "File " << localFilePath
                 << " doesn't have data definitions" << endl;
            fail(YamlFailsSchema);
        }
        if (m.types.size() > 1)
        {
            cerr << "File " << localFilePath
                 << " has more than one data structure definition" << endl;
            fail(YamlFailsSchema);
        }

        return TypeUsage(m.types.back().name, "\"" + a.getFilenameBase() + ".h\"");
    }

    string yamlType = getString(node, "type");
    string yamlFormat = node["format"] ? getString(node, "format") : "";
    if (yamlType == "array")
    {
        auto arrayElType = get(node, "items", NodeType::Map);

        auto innerType = getString(arrayElType, "type");
        if (innerType == "string")
            return TypeUsage("QStringList", "<QtCore/QStringList>");
        // TODO: items can have [properties]; we'll have to create a separate struct
        // to describe such type
    }
    TypeUsage resolvedType =
            yamlType == "string" ?
                (yamlFormat == "byte" || yamlFormat == "binary" ?
                     TypeUsage("QByteArray", "<QtCore/QByteArray>") :
                 yamlFormat == "date" ? TypeUsage("QDate", "<QtCore/QDate>") :
                 yamlFormat == "date-time" ?
                    TypeUsage("QDateTIme", "<QtCore/QDateTime>") :
                    TypeUsage("QString")) :
            yamlType == "integer" ?
                (yamlFormat == "int64" ? TypeUsage("std::int64_t", "<cstdint>") :
                 yamlFormat == "int32" ? TypeUsage("std::int32_t", "<cstdint>") :
                                         TypeUsage("int")) :
            yamlType == "number" ?
                (yamlFormat == "float" ? TypeUsage("float") : TypeUsage("double")) :
            yamlType == "boolean" ? TypeUsage("bool") :
            yamlType == "array" ? TypeUsage("QJsonArray", "<QtCore/QJsonArray>") :
            yamlType == "object" ? TypeUsage("QJsonObject", "<QtCore/QJsonObject>") :
            TypeUsage("", "");
    if (!resolvedType.name.empty())
        return resolvedType;

    cerr << fileName << ":" << node.Mark().line + 1 << ": unknown type: " << yamlType;
    fail(UnknownParameterType);
}

void Analyzer::addParameter(string name, const Node& node, vector<string>& includes,
                            Call& callOverload, const string& in) const
{
    auto type = resolveType(node);
    VarDecl p(type.name, name);
    callOverload.addParam(p, in);
    cout << "  Added input parameter for " << in << ": " << p.toString();

    if (!type.import.empty() &&
            find(includes.begin(), includes.end(), type.import) == includes.end())
    {
        includes.emplace_back(type.import);
        cout << "(with #include " << type.import<< ")";
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

                auto s = yaml_call["security"];
                bool needsToken = s.IsDefined() &&
                        assert(s, NodeType::Sequence)[0]["accessToken"].IsDefined();
                Call& call = model.addCall(path, verb, needsToken, "");

                cout << path << " - " << verb << endl;

                if (!yaml_call["parameters"])
                    continue;
                for (Node yaml_param: assert(yaml_call["parameters"], NodeType::Sequence))
                {
                    assert(yaml_param, NodeType::Map);
                    auto in = getString(yaml_param, "in");
                    if (yaml_param["type"])
                    {
                        // Got a simple type
                        auto name = getString(yaml_param, "name");
                        addParameter(name, yaml_param, model.imports, call, in);
                        continue;
                    }
                    if (in != "body")
                    {
                        cerr << "Parameter " << yaml_param["name"]
                             << " has a non-primitive type but tries to be passed "
                             << "through " << in << endl;
                        fail(ComplexTypeTargetsOutsideBody);
                    }
                    // Got a complex type
                    auto schema = get(yaml_param, "schema", NodeType::Map);
                    Node properties = schema["properties"];
                    if (!properties)
                    {
                        // Got a complex type without inner schema details
                        auto name = getString(yaml_param, "name");
                        addParameter(name, schema, model.imports, call);
                        continue;
                    }

                    assert(properties, NodeType::Map);
                    for (auto property: properties)
                    {
                        auto name = property.first.as<string>();
                        addParameter(name, property.second, model.imports, call);
                    }
                }

                auto yamlResponses = get(yaml_call, "responses", NodeType::Map);
                auto normalResponse = yamlResponses["200"];
                if (!normalResponse || (normalResponse && normalResponse["schema"]))
                    cerr << "Warning: Non-trivial responses not supported yet" << endl;
            }
        }
    } else {
        assert(yaml, NodeType::Map);
        auto bareFilename = getFilenameBase();
        auto n = bareFilename.rfind('/');
        if (n != string::npos)
            bareFilename = bareFilename.substr(n + 1);
        model.types.emplace_back(convertMultiword(bareFilename));
    }
    return model;
}
