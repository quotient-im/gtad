#pragma once

#include <yaml-cpp/yaml.h>
#include <string>
#include <QtCore/QString>

class SwaggerAnalyzer
{
    public:
        enum { Error = 100 };
        using Node = YAML::Node;
        using NodeType = YAML::NodeType;

        explicit SwaggerAnalyzer(const std::string& fn) : fileName(fn) { }

        const Node& assert(const Node& node, NodeType::value checkedType);
        Node get(const Node& node, const std::string& subnodeName,
                 NodeType::value checkedType);

        std::string getString(const Node& node, const std::string& subnodeName)
        {
            return get(node, subnodeName, NodeType::Scalar).as<std::string>();
        }

        QString getTypename(const Node& node);

    private:
        std::string fileName;
};
