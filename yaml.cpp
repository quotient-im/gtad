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
using std::cerr;
using std::endl;
using std::string;

// Follows the YAML::NodeType::value enum; if that enum changes, this has
// to be changed too (but it probably would only change if YAML standard is updated).
static string const typenames[] {
    "Undefined", "Null", "Scalar", "Sequence", "Map"
};

YamlSequence YamlNode::asSequence() const
{
    return YamlSequence(*this);
}

YamlMap YamlNode::asMap() const
{
    return YamlMap(*this);
}

void YamlNode::checkType(NodeType::value checkedType) const
{
    if (Type() == checkedType)
        return;

    throw YamlException(*this,
            "The node has a wrong type (expected " + typenames[checkedType] +
            ", got " + typenames[Type()] + ")");
}

YamlNode YamlSequence::get(size_t subnodeIdx, bool allowNonexistent) const
{
    auto subnode = (*this)[subnodeIdx];
    if (allowNonexistent || subnode.IsDefined())
        return subnode;

    throw YamlException(*this,
            "subnode #" + std::to_string(subnodeIdx) + " is undefined");
}

std::vector<string> YamlSequence::asStrings() const
{
    std::vector<string> listVals { size() };
    transform(begin(), end(), listVals.begin(),
              std::mem_fn(&YamlNode::as<string>));
    return listVals;
}

YAML::Node makeNodeFromFile(const string& fileName,
                            const pair_vector_t<string>& replacePairs)
{
    try {
        if (replacePairs.empty())
            return YAML::LoadFile(fileName);

        string fileContents = readFile(fileName);
        if (fileContents.empty())
            throw YAML::BadFile();
        for (const auto& subst: replacePairs)
            fileContents = std::regex_replace(fileContents,
                                 std::regex(subst.first), subst.second);
        return YAML::Load(fileContents);
    }
    catch (YAML::BadFile &)
    {
        throw Exception("Couldn't read YAML from input");
    }
}

YamlMap YamlMap::loadFromFile(const std::string& fileName,
                              const pair_vector_t<std::string>& replacePairs)
{
    return YamlNode(makeNodeFromFile(fileName, replacePairs),
                    std::make_shared<string>(fileName));
}
