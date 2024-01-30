/******************************************************************************
 * Copyright (C) 2017 Kitsune Ral <kitsune-ral@users.sf.net>
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

#include "yaml.h"

#include <yaml-cpp/node/parse.h>

#include <regex>

using Node = YAML::Node;
using NodeType = YAML::NodeType;
using std::string;

YamlException::YamlException(const YamlNode& node, std::string_view msg) noexcept
    : Exception(node.location().append(": ").append(msg))
{}

namespace {
YAML::Node makeNodeFromFile(const string& fileName, const subst_list_t& replacePairs)
{
    if (replacePairs.empty())
        return YAML::LoadFile(fileName);

    auto fileContents = readFile(fileName);
    if (fileContents.empty())
        throw YAML::BadFile(fileName);
    for (const auto& [pattn, subst] : replacePairs)
        fileContents = std::regex_replace(fileContents, std::regex(pattn), subst.value_or(""));
    return YAML::Load(fileContents);
}
}

YamlNode YamlNode::fromFile(std::string fileName, const subst_list_t& replacePairs)
{
    return {makeNodeFromFile(fileName, replacePairs), std::make_shared<string>(fileName),
            AllowUndefined{}};
}

void YamlNode::checkType(YAML::NodeType::value checkedType) const
{
    using namespace std::string_literals;
    // Follows the YAML::NodeType::value enum; if that enum changes, this has
    // to be changed too (but it probably would only change if YAML standard is updated).
    static constexpr std::array typenames{"Undefined"s, "Null"s, "Scalar"s, "Sequence"s, "Map"s};

    if (Type() != checkedType)
        throw YamlException(*this, "The node has a wrong type (expected " + typenames[checkedType]
                                       + ", got " + typenames[Type()] + ")");
}
