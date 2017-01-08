/******************************************************************************
 * Copyright (C) 2016 Kitsune Ral <kitsune-ral@users.sf.net>
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

#pragma once

#include <string>

#include <yaml-cpp/node/node.h>
#include <yaml-cpp/node/type.h>

#include "model.h"

class Translator;

class Analyzer
{
    public:
        using Node = YAML::Node;
        using NodeType = YAML::NodeType;

        Analyzer(const std::string& filePath, const std::string& basePath,
                 const Translator& translator);

        Model loadModel();

    private:
        std::string fileName;
        std::string baseDir;
        Model model;
        const Translator& translator;

        Node loadYaml() const;

        const Node& assert(const Node& node, NodeType::value checkedType) const;
        Node get(const Node& node, const std::string& subnodeName,
                 NodeType::value checkedType, bool allowNonexistent = false) const;

        template <typename T>
        T get(const Node& node, const std::string& subnodeName) const
        {
            return get(node, subnodeName, NodeType::Scalar).as<T>();
        }
        template <typename T>
        T get(const Node& node, const std::string& subnodeName,
              const T& defaultValue) const
        {
            if (Node n = get(node, subnodeName, NodeType::Scalar, true))
                return n.as<T>();
            else
                return defaultValue;
        }

        TypeUsage resolveType(const Node& node, bool constRef);

        void addParameter(const std::string& name, const Node& node, Call& call,
                          bool required, const std::string& in = "body");

};
