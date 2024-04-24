#include "model.h"

#include <algorithm>
#include <locale>

using namespace std;

string Identifier::qualifiedName() const
{
    return (call ? call->name + roleToChar(role) : "") + name;
}

TypeUsage::TypeUsage(const ObjectSchema& schema)
    : Identifier(static_cast<const Identifier&>(schema)), baseName(schema.name)
{}

void TypeUsage::assignName(string setName, string setBaseName)
{
    if (!name.empty())
        throw ModelException("It's not allowed to overwrite used type name if it's already set");
    name = std::move(setName);
    baseName = setBaseName.empty() ? name : std::move(setBaseName);
}

TypeUsage TypeUsage::specialize(vector<TypeUsage>&& params) const
{
    auto tu = *this;
    tu.paramTypes = params;
    return tu;
}

void toUpper(char& c) { c = toupper(c, locale::classic()); }

string titleCased(string s)
{
    string::size_type pos = 0;
    while (pos < s.size())
    {
        toUpper(s[pos]);
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
    std::erase_if(s, [] (auto c) { return !isalnum(c) && c != '_'; });
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
            parts.emplace_back(i, npos, PartType::Literal);
            break;
        }
        const auto i2 = find('}', i1);
        if (i2 == npos)
            throw ModelException("Unbalanced braces in the path: " + *this);

        parts.emplace_back(i, i1 - i, PartType::Literal);
        parts.emplace_back(i1 + 1, i2 - i1 - 1, PartType::Variable);
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
            if (unpacked.hasAdditionalProperties())
                allCollated.emplace_back(unpacked.additionalProperties);
        },
        [&allCollated](const VarDecl& packed) {
            allCollated.emplace_back(packed);
        },
        [](monostate) {});
    return allCollated;
}

Call& Model::addCall(Path path, string verb, string operationId, bool deprecated, bool needsToken)
{
    if (callClasses.empty())
        callClasses.emplace_back();
    auto& cc = callClasses.back();
    cc.calls.emplace_back(std::move(path), std::move(verb),
                          std::move(operationId), deprecated, needsToken);
    return cc.calls.back();
}

void Model::addSchema(ObjectSchema&& schema, const TypeUsage& tu)
{
    auto dupIt = find_if(types.begin(), types.end(), [&](const schemaholder_type& s) {
        return s.first->call == schema.call && s.first->name == schema.name;
    });
    if (dupIt != types.end())
        return;

    addImportsFrom(schema);
    types.emplace_back(std::make_unique<const ObjectSchema>(std::move(schema)), tu);
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
    if (!s.additionalProperties.type.empty())
        addImportsFrom(s.additionalProperties.type);
}

void Model::addImportsFrom(const TypeUsage& type)
{
    const auto renderer = type.importRenderer;
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
    apiSpec = ApiSpec::Undefined;
    imports.clear();
    types.clear();
    defaultServers.clear();
    callClasses.clear();
}
