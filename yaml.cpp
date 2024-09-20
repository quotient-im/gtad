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

#include <iostream>
#include <regex>

using Node = YAML::Node;
using namespace std;

YamlException::YamlException(const YamlNode& node, string_view msg) noexcept
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
        fileContents = regex_replace(fileContents, regex(pattn), subst.value_or(""));
    return YAML::Load(fileContents);
}

// The parameter has the type auto because views::split() returns a rather hideous-looking type
string unescapeJsonPointerComponent(const auto& escapedComponent)
{
    string result;
    for (bool escaping = false; auto c : escapedComponent) {
        if (escaping) [[unlikely]] {
            switch (c) {
            case '1': result.push_back('/'); break;
            case '0': result.push_back('~'); break;
            default: throw Exception(string("Incorrect JSON Pointer escaping sequence: ~") + c);
            }
            escaping = false;
        } else if (c == '~')
            escaping = true;
        else
            result.push_back(c);
    }
    return result;
}
} // namespace

YamlNode YamlNode::fromFile(const string& fileName, const subst_list_t& replacePairs)
{
    const auto n = makeNodeFromFile(fileName, replacePairs);
    return {n, make_shared<Context>(fileName, n), AllowUndefined{}};
}

void YamlNode::checkType(NodeType checkedType) const
{
    using namespace string_literals;
    // Follows the YAML::NodeType::value enum; if that enum changes, this has
    // to be changed too (but it probably would only change if YAML standard is updated).
    static constexpr array typenames{"Undefined"s, "Null"s, "Scalar"s, "Sequence"s, "Map"s};

    if (Type() != checkedType)
        throw YamlException(*this, "The node has a wrong type (expected " + typenames[checkedType]
                                       + ", got " + typenames[Type()] + ")");
}

YamlNode YamlNode::doResolveRef(OverrideMode overrideMode) const
{
    if (!IsMap()) // NB: will throw if the node is not even defined
        return *this;

    const auto refObj = as<YamlMap<>>();
    auto maybeRef = refObj.maybeGet<string_view>("$ref");
    if (!maybeRef)
        return *this;

    if (!maybeRef->starts_with('#'))
        throw YamlException(refObj, "Non-local $refs are not supported yet");

    if (!maybeRef->starts_with("#/"))
        throw YamlException(refObj, "Malformed JSON Pointer in $ref (must start with /)");
    maybeRef->remove_prefix(2); // Remove leading #/

    // The following closely implements https://datatracker.ietf.org/doc/html/rfc6901#section-4

    // YAML::Node copy constructor is broken (see
    // https://github.com/jbeder/yaml-cpp/issues/1275) so instead of tracking the last
    // (innermost) found value in a plain YamlNode we use an optional and reset it before
    // assigning to a new value.
    optional<YamlNode> currentYaml = refObj.root();
    for (auto unescapedNodeName : *maybeRef | views::split('/')) {
        const auto nodeName = unescapeJsonPointerComponent(unescapedNodeName);
        auto nextYaml = currentYaml->IsMap() ? YamlMap<>(*currentYaml)[nodeName]
                                             : YamlSequence<>(*currentYaml)[stoul(nodeName)];
        if (!nextYaml)
            throw YamlException(
                refObj, "Could not find the value pointed to by $ref, first failing component: "
                            + nodeName);
        if (!nextYaml->IsMap() && !nextYaml->IsSequence())
            throw YamlException(refObj, "Could not resolve JSON Pointer: value at " + nodeName
                                            + " is not a container");
        currentYaml.reset();
        currentYaml = *nextYaml;
    }
    if (overrideMode == ApplyOverrides)
        for (auto overridable : {"summary", "description"})
            if (const auto maybeSummary = refObj.maybeGet<string>(overridable))
                currentYaml->force_insert(overridable, *maybeSummary);

    // https://github.com/OAI/OpenAPI-Specification/blob/main/versions/3.1.0.md#reference-object
    if (refObj.size() > 1 && !ranges::all_of(refObj, [](const pair<string, YamlNode>& p) {
            return p.first == "$ref" || p.first == "summary" || p.first == "description";
        }))
        clog << refObj.location()
             << ": Warning: non-summary, non-description keys next to $ref will be ignored\n";
    return *currentYaml;
}
