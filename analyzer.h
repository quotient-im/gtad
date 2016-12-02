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

#include <yaml-cpp/node/node.h>
#include <yaml-cpp/node/type.h>
#include <string>

#include "model.h"

class Analyzer
{
    public:
        using Node = YAML::Node;
        using NodeType = YAML::NodeType;

        explicit Analyzer(const std::string& fn) : fileName(fn) { }

        std::vector<CallConfigModel> getModels() const;

    private:
        std::string fileName;

        const Node& assert(const Node& node, NodeType::value checkedType) const;
        Node get(const Node& node, const std::string& subnodeName,
                 NodeType::value checkedType) const;

        std::string getString(const Node& node, const std::string& subnodeName) const
        {
            return get(node, subnodeName, NodeType::Scalar).as<std::string>();
        }

        QString getTypename(const Node& node) const;

};
