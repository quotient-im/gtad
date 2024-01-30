#include "model.h"

#include <algorithm>
#include <functional>
#include <locale>

using namespace std;

string Identifier::qualifiedName() const
{
    return (call ? call->name + roleToChar(role) : "") + name;
}

TypeUsage::TypeUsage(const ObjectSchema& schema)
    : Identifier(static_cast<const Identifier&>(schema)), baseName(schema.name)
{ }

TypeUsage TypeUsage::specialize(vector<TypeUsage>&& params) const
{
    auto tu = *this;
    tu.paramTypes = params;
    return tu;
}

void capitalize(string& s, string::size_type pos = 0)
{
    if (pos < s.size())
        s[pos] = toupper(s[pos], locale::classic());
}

string capitalizedCopy(string s)
{
    capitalize(s);
    return s;
}

string camelCase(string s)
{
    string::size_type pos = 0;
    while (pos < s.size())
    {
        capitalize(s, pos);
        pos = s.find_first_of("/_ .-:", pos);
        if (pos == string::npos)
            break;
        // Do not erase '_' at the beginning or the end of an identifier
        if (s[pos] != '_' || (pos != 0 && pos != s.size() - 1))
            s.erase(pos, 1);
        else
            ++pos;
    }
    // Remove all remaining non-identifier characters
    s.erase(remove_if(s.begin(), s.end(),
                      [] (auto c) { return !isalnum(c) && c != '_'; }),
            s.end());
    return s;
}

string VarDecl::toString(bool withDefault) const
{
    auto result = type.name + " " + name;
    if (withDefault && !required)
        result += " = " + (defaultValue.empty() ? "(empty)" : defaultValue);
    return result;
}

Path::Path(string path)
    : string(std::move(path))
{
    if (empty())
        throw ModelException("Path cannot be empty");

    // Working around quirks in the current Matrix CS API definition
    // (still applies to any other spec as well)
    while (back() == ' ' || back() == '/')
        pop_back();

    for (size_type i = 0; i < size();)
    {
        const auto i1 = find('{', i);
        if (i1 == npos)
        {
            parts.emplace_back(i, npos, Literal);
            break;
        }
        const auto i2 = find('}', i1);
        if (i2 == npos)
            throw ModelException("Unbalanced braces in the path: " + *this);

        parts.emplace_back(i, i1 - i, Literal);
        parts.emplace_back(i1 + 1, i2 - i1 - 1, Variable);
        i = i2 + 1;
    }
}

const array<string, 3> Call::ParamGroups {{"path"s, "query"s, "header"s}};

auto getParamsBlockIndex(const string& name)
{
    for (Call::params_type::size_type i = 0; i < 4; ++i)
        if (Call::ParamGroups[i] == name)
            return i;

    throw ModelException("Unknown params block name: " + name);
}

Call::params_type& Call::getParamsBlock(const string& blockName)
{
    return params[getParamsBlockIndex(blockName)];
}

Call::params_type Call::collateParams() const
{
    params_type allCollated;
    for (auto c: params)
        allCollated.insert(allCollated.end(), c.begin(), c.end());
    dispatchVisit(
        body,
        [&allCollated](const FlatSchema& unpacked) {
            allCollated.insert(allCollated.end(), unpacked.fields.begin(),
                               unpacked.fields.end());
            if (unpacked.hasPropertyMap())
                allCollated.emplace_back(unpacked.propertyMap);
        },
        [&allCollated](const VarDecl& packed) {
            allCollated.emplace_back(packed);
        },
        [](monostate) {});
    return allCollated;
}

Call& Model::addCall(Path path, string verb, string operationId, bool needsToken)
{
    if (callClasses.empty())
        callClasses.emplace_back();
    auto& cc = callClasses.back();
    cc.calls.emplace_back(std::move(path), std::move(verb),
                          std::move(operationId), needsToken);
    return cc.calls.back();
}

void Model::addSchema(ObjectSchema&& schema)
{
    auto dupIt = find_if(types.begin(), types.end(),
            [&](const ObjectSchema& s)
            {
                return s.call == schema.call &&
                        s.name == schema.name;
            });
    if (dupIt != types.end())
        return;

    addImportsFrom(schema);
    types.emplace_back(std::move(schema));
}

void Model::addImportsFrom(const ObjectSchema& s)
{
    for (const auto& pt : s.parentTypes)
        addImportsFrom(pt);
    addImportsFrom(static_cast<const FlatSchema&>(s));
}

void Model::addImportsFrom(const FlatSchema& s)
{
    for (const auto& f : s.fields)
        addImportsFrom(f.type);
    if (!s.propertyMap.type.empty())
        addImportsFrom(s.propertyMap.type);
}

void Model::addImportsFrom(const TypeUsage& type)
{
    const auto renderer = type.attributes.at("_importRenderer");
    const auto singleTypeImport = type.attributes.find("imports");
    if (singleTypeImport != type.attributes.end())
        imports.emplace(singleTypeImport->second, renderer);
    const auto typeImportsIt = type.lists.find("imports");
    if (typeImportsIt != type.lists.end())
        for (auto&& import : typeImportsIt->second)
            imports.emplace(import, renderer);
    for (const auto& paramType : type.paramTypes)
        addImportsFrom(paramType);
}

void Model::clear()
{
    apiSpec.clear();
    imports.clear();
    types.clear();
    defaultServers.clear();
    callClasses.clear();
}
