#include "swagger_analyzer.h"

#include <iostream>
using std::cerr;
using std::endl;
using std::string;

using YAML::Node;
using YAML::NodeType;

static const char* typenames[] = { "Undefined", "Null", "Scalar", "Sequence", "Map" };

const Node& SwaggerAnalyzer::assert(const Node& node, NodeType::value checkedType)
{
    if (node && node.Type() == checkedType)
        return node;

    cerr << fileName << ":" << node.Mark().line + 1 << ": the node ";
    if (node)
        cerr << "has a wrong type (expected "
             << typenames[checkedType] << ", got " << typenames[node.Type()] << endl;
    else
        cerr << "is undefined" << endl;
    exit(Error);
}

Node SwaggerAnalyzer::get(const Node& node, const string& subnodeName,
                      NodeType::value checkedType)
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
    exit(Error);
}

QString SwaggerAnalyzer::getTypename(const Node& node)
{
    string yamlType = getString(node, "type");
    if (yamlType == "array")
    {
        auto arrayElType = get(node, "items", NodeType::Map);

        auto innerType = getString(arrayElType, "type");
        if (innerType == "string")
            return "QStringList";
        // TODO: items can have [properties], too;
        // we'll have to create a separate struct
        // to describe such type
    }
    QString qtype =
            yamlType == "string" ? "QString" :
            yamlType == "integer" || yamlType == "number" ? "int" :
            yamlType == "boolean" ? "bool" :
            yamlType == "array" ? "QVariantList" :
            yamlType == "object" ? "QVariant" : "";
    if (!qtype.isEmpty())
        return qtype;

    cerr << fileName << ":" << node.Mark().line + 1 << ": unknown type: " << yamlType;
    exit(Error);
}
