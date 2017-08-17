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

#include "exception.h"

#include <yaml-cpp/node/parse.h>

using Node = YAML::Node;
using NodeType = YAML::NodeType;
using std::cerr;
using std::endl;
using std::string;

enum {
    CannotReadFromInput = ParserCodes, IncorrectYamlStructure
};

// Follows the YAML::NodeType::value enum; if that enum changes, this has
// to be changed too (but it probably would only change if YAML standard is updated).
static const char* const typenames[] {
    "Undefined", "Null", "Scalar", "Sequence", "Map"
};

YamlSequence YamlNode::asSequence() const
{
    if (IsDefined())
        checkType(YAML::NodeType::Sequence);
    return YamlSequence(*this);
}

YamlMap YamlNode::asMap() const
{
    if (IsDefined())
        checkType(YAML::NodeType::Map);
    return YamlMap(*this);
}

void YamlNode::checkType(NodeType::value checkedType) const
{
    if (Type() == checkedType)
        return;

    cerr << location() << ": the node has a wrong type (expected "
         << typenames[checkedType] << ", got " << typenames[Type()] << ")" << endl;

    structureFail();
}

void YamlNode::structureFail() const
{
    fail(IncorrectYamlStructure);
}

YAML::Node makeNodeFromFile(const string& fileName)
{
    try {
        return YAML::LoadFile(fileName);
    }
    catch (YAML::BadFile &)
    {
        fail(CannotReadFromInput, "Couldn't read YAML from input");
    }
}

YamlMap YamlMap::loadFromFile(const std::string& fileName)
{
    return YamlNode(makeNodeFromFile(fileName), fileName);
}

