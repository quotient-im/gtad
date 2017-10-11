#include "model.h"

#include <iostream>
#include <locale>
#include <regex>

#include "exception.h"

enum {
    _Base = InternalErrors, UnknownParamBlock, UnbalancedBracesInPath
};

using namespace std;

TypeUsage TypeUsage::operator()(const TypeUsage& innerType) const
{
    TypeUsage tu = *this;
    tu.innerTypes.push_back(innerType);

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

string VarDecl::setupDefault(TypeUsage type, string defaultValue)
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
        pos = s.find_first_of("/_ .", pos);
        if (pos == string::npos)
            break;
        s.erase(pos, 1);
    }
    return s;
}

void eraseSuffix(string* path, const string& suffix)
{
    auto trimAt = path->size() - suffix.size();
    if (path->find(suffix, trimAt) != string::npos)
        path->erase(trimAt);
}

string dropSuffix(string path, const string& suffix)
{
    auto trimAt = path.size() - suffix.size();
    return path.find(suffix, trimAt) != string::npos ?
           string(path.begin(), path.end() - suffix.size()) : std::move(path);
}

Call& Model::addCall(std::string path, std::string verb, std::string operationId,
                     bool needsToken)
{
    transform(verb.begin(), verb.end(), verb.begin(),
              [] (char c) { return toupper(c, locale::classic()); });

    callClasses.emplace_back();
    auto& cc = callClasses.back();
    cc.callOverloads.emplace_back(std::move(path), std::move(verb),
                                  std::move(operationId), needsToken);
    return cc.callOverloads.back();
}

vector<string> splitPath(const string& path)
{
    vector<string> parts;
    for (auto i = path.begin(); i != path.end();)
    {
        auto i1 = find(i, path.end(), '{');
        auto i2 = find(i1, path.end(), '}');
        if (i1 == path.end())
        {
            parts.emplace_back('"' + string{i, path.end()} + '"');
            break;
        }
        if (i2 == path.end())
            fail(UnbalancedBracesInPath, "The path has '{' without matching '}'");

        cout << string {i, i1};
        cout << string {i1+1, i2};
        parts.emplace_back('"' + string{i, i1} + '"');
        parts.emplace_back(i1 + 1, i2);
        i = i2 + 1;
    }
    return parts;
}

Call::params_type& Call::getParamsBlock(const string& name)
{
    static const char* const map[] { "path", "query", "header", "body" };
    for (params_type::size_type i = 0; i < 4; ++i)
        if (map[i] == name)
            return allParams[i];

    cerr << "Unknown params block value: "<< name << endl;
    fail(UnknownParamBlock);
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

void Model::addVarDecl(VarDecls& varList, VarDecl var)
{
    addImports(var.type);
    varList.emplace_back(move(var));
}

void Model::addImports(const TypeUsage& type)
{
    const auto oldSize = imports.size();
    const auto singleTypeImport = type.attributes.find("imports");
    if (singleTypeImport != type.attributes.end())
        imports.insert(singleTypeImport->second);
    const auto typeImportsIt = type.lists.find("imports");
    if (typeImportsIt != type.lists.end())
    {
        const auto& typeImports = typeImportsIt->second;
        imports.insert(typeImports.begin(), typeImports.end());
    }
    if (imports.size() != oldSize)
        cout << "Model '" << filename
             << "': added " << imports.size() - oldSize << " import(s)" << endl;
}
