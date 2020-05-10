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
#include <list>
#include <unordered_set>
#include <unordered_map>

std::string capitalizedCopy(std::string s);
std::string camelCase(std::string s);
void eraseSuffix(std::string* path, const std::string& suffix);
std::string withoutSuffix(const std::string& path,
                          const std::string_view& suffix);

struct Call;

struct Identifier
{
    /// As transformed for the generated code, not what's in YAML
    std::string name;
    /// For now, only non-empty in case of ObjectSchemas and their TypeUsages
    const Call* scope = nullptr;

    [[nodiscard]] std::string qualifiedName() const;
};

struct ObjectSchema;

struct TypeUsage : Identifier
{
    using imports_type = std::vector<std::string>;

    std::string baseName; //< As used in the API definition
    std::unordered_map<std::string, std::string> attributes;
    std::unordered_map<std::string, std::vector<std::string>> lists;
    std::vector<TypeUsage> innerTypes; //< Parameter types for type templates

    TypeUsage() = default;
    explicit TypeUsage(std::string typeName) : Identifier{move(typeName)} { }
    explicit TypeUsage(const ObjectSchema& schema);

    [[nodiscard]] TypeUsage instantiate(std::vector<TypeUsage>&& innerTypes) const;

    [[nodiscard]] bool empty() const { return name.empty(); }

    void addImport(imports_type::value_type name)
    {
        lists["imports"].emplace_back(move(name));
    }

    [[nodiscard]] bool operator==(const TypeUsage& other) const
    {
        return name == other.name && scope == other.scope
               && baseName == other.baseName && attributes == other.attributes
               && lists == other.lists && innerTypes == other.innerTypes;
    }
    [[nodiscard]] bool operator!=(const TypeUsage& other) const
    {
        return !operator==(other);
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

    [[nodiscard]] std::string toString(bool withDefault = false) const;
};

using VarDecls = std::vector<VarDecl>;

using InOut = unsigned short;
static constexpr InOut In = 0x1;
static constexpr InOut Out = 0x2;

struct ObjectSchema : Identifier
{
    std::string description;
    std::vector<TypeUsage> parentTypes;
    std::vector<VarDecl> fields;
    VarDecl propertyMap;
    InOut inOut = 0;

    explicit ObjectSchema(InOut inOut = 0, std::string description = {}) :
        description(move(description)), inOut(inOut)
    { }

    [[nodiscard]] bool empty() const
    {
        return parentTypes.empty() && fields.empty();
        // If parentTypes and fields are empty, propertyMap must not exist
    }
    [[nodiscard]] bool trivial() const
    {
        return parentTypes.size() == 1 && fields.empty()
               && propertyMap.name.empty();
    }
    [[nodiscard]] bool hasPropertyMap() const
    {
        return !propertyMap.type.empty();
    }
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
    explicit Response(std::string code, std::string description = {}) :
        code(move(code)), body(Out, move(description))
    { }
    std::string code;
    VarDecls headers;
    ObjectSchema body;
};

struct ExternalDocs
{
    std::string description;
    std::string url;
};

enum Location : size_t { InPath = 0, InQuery = 1, InHeaders = 2 };

struct Call : Identifier
{
    using params_type = VarDecls;

    Call(Path callPath, std::string callVerb, std::string callName,
         bool callNeedsSecurity)
        : Identifier{move(callName)}, path(move(callPath))
        , verb(std::move(callVerb)), needsSecurity(callNeedsSecurity)
    { }
    ~Call() = default;
    Call(Call&) = delete;
    Call operator=(Call&) = delete;
    // Moving is not allowed because Call is used by pointer for scoping
    Call(Call&&) = delete;
    Call operator=(Call&&) = delete;

    [[nodiscard]] params_type& getParamsBlock(const std::string& blockName);
    [[nodiscard]] params_type collateParams() const;

    Path path;
    std::string verb;
    std::string summary;
    std::string description;
    ExternalDocs externalDocs;
    static const std::array<std::string, 3> ParamGroups;
    std::array<params_type, 3> params;
    ObjectSchema body{In};
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
    // Using std::list because it doesn't move the storage around
    std::list<Call> calls;
};

inline std::string Swagger() { return "swagger"; }
inline std::string OpenAPI3() { return "openapi"; }
inline std::string RAML() { return "raml"; }
inline std::string JSONSchema() { return "json-schema"; }

struct Model {
    using string = std::string;
    using imports_type = std::unordered_set<string>;
    using schemas_type = std::vector<ObjectSchema>;

    const string fileDir;
    const string srcFilename;
    std::vector<string> dstFiles;

    string apiSpec;
    /// Spec version liberally encoded in a int, e.g. 200 for Swagger 2.0
    /// or 201909 for JSON Schema 2019-09
    int apiSpecVersion = 0;

    imports_type imports;
    schemas_type types;

    string hostAddress;
    string basePath;
    std::list<CallClass> callClasses;

    Model(string fileDir, string fileName)
        : fileDir(move(fileDir)), srcFilename(move(fileName))
    { }
    ~Model() = default;
    Call& addCall(Path path, string verb, string operationId, bool needsToken);
    void addVarDecl(VarDecls& varList, VarDecl var);
    void addSchema(const ObjectSchema& schema);
    void addImports(const TypeUsage& type);

    [[nodiscard]] bool empty() const { return callClasses.empty() && types.empty(); }
    [[nodiscard]] bool trivial() const
    {
        return callClasses.empty() &&
                types.size() == 1 && types.front().trivial();
    }
};

struct ModelException : Exception
{
    using Exception::Exception;
};

