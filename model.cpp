#include "model.h"

#include <algorithm>
#include <functional>
#include <locale>

using namespace std;

TypeUsage::TypeUsage(const ObjectSchema& schema)
    : scope(schema.scope), name(schema.name), baseName(schema.name)
{ }

TypeUsage TypeUsage::instantiate(TypeUsage&& innerType) const
{
    TypeUsage tu = *this;
    tu.innerTypes.emplace_back(innerType);

    auto& tuImports = tu.lists["imports"];
    const auto singleInnerImport = innerType.attributes.find("imports");
    if (singleInnerImport != innerType.attributes.end())
        tuImports.push_back(singleInnerImport->second);
    const auto innerImports = innerType.lists.find("imports");
    if (innerImports != innerType.lists.end())
        tuImports.insert(tuImports.end(),
             innerImports->second.begin(), innerImports->second.end());

    return tu;
}

string VarDecl::setupDefault(const TypeUsage& type, string defaultValue)
{
    return !defaultValue.empty() ? defaultValue :
        type.name == "bool" ? "false" :
        type.name == "int" ? "0" :
        "{}";
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
    return s;
}

void eraseSuffix(string* path, const string& suffix)
{
    auto trimAt = path->size() - suffix.size();
    if (path->find(suffix, trimAt) != string::npos)
        path->erase(trimAt);
}

string withoutSuffix(const string& path, const string& suffix)
{
    return path.substr(0, path.find(suffix, path.size() - suffix.size()));
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

    for (auto i = begin(); i != end();)
    {
        auto i1 = std::find(i, end(), '{');
        auto i2 = std::find(i1, end(), '}');
        if (i1 == end())
        {
            parts.emplace_back(i, end(), Literal);
            break;
        }
        if (i2 == end())
            throw ModelException("Unbalanced braces in the path: " + *this);

        parts.emplace_back(i, i1, Literal);
        parts.emplace_back(i1 + 1, i2, Variable);
        i = i2 + 1;
    }
}

const array<string, 4> Call::paramsBlockNames
    { { "path", "query", "header", "body" } };

size_t getParamsBlockIndex(const string& name)
{
    for (Call::params_type::size_type i = 0; i < 4; ++i)
        if (Call::paramsBlockNames[i] == name)
            return i;

    throw ModelException("Unknown params block name: " + name);
}

const Call::params_type& Call::getParamsBlock(const string& blockName) const
{
    return allParams[getParamsBlockIndex(blockName)];
}

Call::params_type& Call::getParamsBlock(const string& blockName)
{
    return allParams[getParamsBlockIndex(blockName)];
}

Call::params_type Call::collateParams() const
{
    params_type allCollated;
    for (auto c: allParams)
        allCollated.insert(allCollated.end(), c.begin(), c.end());

    stable_partition(allCollated.begin(), allCollated.end(),
                     mem_fn(&VarDecl::isRequired));
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
