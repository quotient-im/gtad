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

#include "util.h"

#include <utility>
#include <vector>
#include <array>
#include <unordered_set>
#include <unordered_map>

std::string capitalizedCopy(std::string s);
std::string camelCase(std::string s);
void eraseSuffix(std::string* path, const std::string& suffix);
std::string withoutSuffix(const std::string& path, const std::string& suffix);

template <typename T>
[[nodiscard]] inline std::string qualifiedName(const T& type)
{
    const auto _name = type.name.empty() ? "(anonymous)" : type.name;
    return type.scope.empty() ? _name : type.scope + '.' + _name;
}

struct ObjectSchema;

struct TypeUsage
{
    using imports_type = std::vector<std::string>;

    std::string scope;
    std::string name; //< As transformed for the generated code
    std::string baseName; //< As used in the API definition
    std::unordered_map<std::string, std::string> attributes;
    std::unordered_map<std::string, std::vector<std::string>> lists;
    std::vector<TypeUsage> innerTypes; //< Parameter types for type templates

    TypeUsage() = default;
    explicit TypeUsage(std::string typeName) : name(std::move(typeName)) { }
    explicit TypeUsage(const ObjectSchema& schema);

    TypeUsage instantiate(std::vector<TypeUsage>&& innerTypes) const;

    [[nodiscard]] bool empty() const { return name.empty(); }

    void addImport(imports_type::value_type import_text)
    {
        lists["imports"].emplace_back(move(import_text));
    }
};

struct VarDecl
{
    TypeUsage type;
    std::string name; //< Identifier in the generated code
    std::string baseName; //< As used in the API
    std::string description;
    bool required = false;
    std::string defaultValue;

    VarDecl() = default;
    VarDecl(TypeUsage type, std::string name, std::string baseName,
            std::string description = {}, bool required = true,
            std::string defaultValue = {})
        : type(std::move(type)), name(std::move(name))
        , baseName(std::move(baseName)), description(std::move(description))
        , required(required), defaultValue(std::move(defaultValue))
    { }

    std::string toString(bool withDefault = false) const;
};

using VarDecls = std::vector<VarDecl>;

using InOut = unsigned short;
static constexpr InOut In = 0x1;
static constexpr InOut Out = 0x2;

struct Scope
{
    // Either empty (top-level) or a Call name (only for ObjectSchema)
    std::string scope;
    std::string name;

    Scope() = default;
    explicit Scope(std::string name, std::string scope = {})
        : scope(std::move(scope)), name(std::move(name))
    { }
    [[nodiscard]] std::string qualifiedName() const
    {
        const auto _name = name.empty() ? "(anonymous)" : name;
        return scope.empty() ? _name : scope + '.' + _name;
    }
};

template <>
[[nodiscard]] inline std::string qualifiedName(const Scope& scope)
{
    return scope.qualifiedName();
}

struct ObjectSchema : Scope
{
    std::string description;
    std::vector<TypeUsage> parentTypes;
    std::vector<VarDecl> fields;
    VarDecl propertyMap;
    InOut inOut = 0;

    bool empty() const { return parentTypes.empty() && fields.empty(); }
    bool trivial() const { return parentTypes.size() == 1 && fields.empty(); }
};

struct Path : public std::string
{
    explicit Path (std::string path);
    Path(const Path& other) = default;
    Path& operator=(const Path& other) = default;
    Path(Path&&) = default;
    Path& operator=(Path&&) = default;

    enum PartKind { Literal, Variable };
    using part_type = std::tuple<size_type /*from*/, size_type /*to*/, PartKind>;
    std::vector<part_type> parts;
};

struct Response
{
    std::string code;
    std::string description;
    VarDecls headers = {};
    VarDecls properties = {};
};

struct ExternalDocs
{
    std::string description;
    std::string url;
};

struct Call : Scope
{
    using params_type = VarDecls;

    Call(Path callPath, std::string callVerb, std::string callName,
         bool callNeedsSecurity)
        : Scope(std::move(callName)), path(std::move(callPath))
        , verb(std::move(callVerb)), needsSecurity(callNeedsSecurity)
    { }
    ~Call() = default;
    Call(Call&) = delete;
    Call operator=(Call&) = delete;
    Call(Call&&) = default;
    Call operator=(Call&&) = delete;

    static const std::array<std::string, 4> paramsBlockNames;
    const Call::params_type& getParamsBlock(const std::string& blockName) const;
    Call::params_type& getParamsBlock(const std::string& blockName);
    params_type collateParams() const;

    Path path;
    std::string verb;
    std::string summary;
    std::string description;
    ExternalDocs externalDocs;
    std::array<params_type, 4> allParams;
    params_type& pathParams() { return allParams[0]; }
    params_type& queryParams() { return allParams[1]; }
    params_type& headerParams() { return allParams[2]; }
    params_type& bodyParams() { return allParams[3]; }
    const params_type& bodyParams() const { return allParams[3]; }
    // TODO: Embed proper securityDefinitions representation.
    bool needsSecurity;
    bool inlineBody = false;
    bool inlineResponse = false;

    std::vector<std::string> producedContentTypes;
    std::vector<std::string> consumedContentTypes;
    std::vector<Response> responses;
};

struct CallClass
{
    std::vector<Call> calls;
};

inline std::string Swagger() { return "swagger"; }
inline std::string OpenAPI3() { return "openapi"; }
inline std::string RAML() { return "raml"; }
inline std::string JSONSchema() { return "json-schema"; }

struct Model
{
    using string = std::string;
    using imports_type = std::unordered_set<string>;
    using schemas_type = std::vector<ObjectSchema>;

    const string fileDir;
    const string srcFilename;
    std::vector<string> dstFiles;

    string apiSpec;
    int apiSpecVersion = 0; // Encoded as xyy, x - major, yy - minor component

    string hostAddress;
    string basePath;
    imports_type imports;
    schemas_type types;
    std::vector<CallClass> callClasses;

    Model(string fileDir, string fileName)
        : fileDir(move(fileDir)), srcFilename(move(fileName))
    { }
    ~Model() = default;
    Model(const Model&) = delete;
    Model operator=(const Model&) = delete;
    Model(Model&&) = default;
    Model& operator=(Model&&) = delete;
    Call& addCall(Path path, string verb, string operationId, bool needsToken);
    void addVarDecl(VarDecls& varList, VarDecl var);
    void addSchema(const ObjectSchema& schema);
    void addImports(const TypeUsage& type);

    bool empty() const { return callClasses.empty() && types.empty(); }
    bool trivial() const
    {
        return callClasses.empty() &&
                types.size() == 1 && types.front().trivial();
    }
};

struct ModelException : Exception
{
    using Exception::Exception;
};

