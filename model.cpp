#include "model.h"

#include <algorithm>
#include <functional>
#include <locale>

using namespace std;

string Identifier::qualifiedName() const
{
    const auto _name = name.empty() ? "(anonymous)" : name;
    return scope ? scope->name + '.' + _name : _name;
}

TypeUsage::TypeUsage(const ObjectSchema& schema)
    : Identifier(schema), baseName(schema.name)
{ }

TypeUsage TypeUsage::specialize(vector<TypeUsage>&& params) const
{
    TypeUsage tu = *this;
    for (auto&& paramType: params)
    {
        tu.paramTypes.emplace_back(paramType);

        auto& tuImports = tu.lists["imports"];
        const auto singleInnerImport = paramType.attributes.find("imports");
        if (singleInnerImport != paramType.attributes.end())
            tuImports.push_back(singleInnerImport->second);
        const auto innerImports = paramType.lists.find("imports");
        if (innerImports != paramType.lists.end())
            tuImports.insert(tuImports.end(),
                             innerImports->second.begin(),
                             innerImports->second.end());
    }

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

string withoutSuffix(const string& path, const string_view& suffix)
{
    return path.substr(0, path.find(suffix, path.size() - suffix.size()));
}

string VarDecl::toString(bool withDefault) const
{
    auto result = type.name + " " + name;
    if (withDefault && !required)
        result += " = " + (defaultValue.empty() ? "(empty)" : defaultValue);
    return result;
}

Path::Path(string path)
    : string(move(path))
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

const array<string, 3> Call::ParamGroups{{"path"s, "query"s, "header"s}};

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
    allCollated.insert(allCollated.end(), body.fields.begin(), body.fields.end());
    if (body.hasPropertyMap())
    	allCollated.insert(allCollated.end(), body.propertyMap);

    stable_partition(allCollated.begin(), allCollated.end(),
                     [] (const VarDecl& v) { return v.required; });
    return allCollated;
}

Call& Model::addCall(Path path, string verb, string operationId, bool needsToken)
{
    if (callClasses.empty())
        callClasses.emplace_back();
    auto& cc = callClasses.back();
    cc.calls.emplace_back(move(path), move(verb), move(operationId), needsToken);
    return cc.calls.back();
}

void Model::addVarDecl(VarDecls& varList, VarDecl var)
{
    addImports(var.type);
    varList.emplace_back(move(var));
}

void Model::addSchema(const ObjectSchema& schema)
{
    auto dupIt = find_if(types.begin(), types.end(),
            [&](const ObjectSchema& s)
            {
                return s.scope == schema.scope &&
                        s.name == schema.name;
            });
    if (dupIt != types.end())
        return;

    types.emplace_back(schema);
    for (const auto& pt: schema.parentTypes)
        addImports(pt);
    if (!schema.propertyMap.type.empty())
        addImports(schema.propertyMap.type);
}

void Model::addImports(const TypeUsage& type)
{
    const auto singleTypeImport = type.attributes.find("imports");
    if (singleTypeImport != type.attributes.end())
        imports.insert(singleTypeImport->second);
    const auto typeImportsIt = type.lists.find("imports");
    if (typeImportsIt != type.lists.end())
    {
        const auto& typeImports = typeImportsIt->second;
        imports.insert(typeImports.begin(), typeImports.end());
    }
}

void Model::clear()
{
    apiSpec.clear();
    imports.clear();
    types.clear();
    hostAddress.clear();
    basePath.clear();
    callClasses.clear();
}
