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

#include <array>
#include <list>
#include <unordered_set>
#include <unordered_map>

std::string capitalizedCopy(std::string s);
std::string camelCase(std::string s);
std::string withoutSuffix(const std::string& path,
                          const std::string_view& suffix);

enum InOut : unsigned char { InAndOut = 0, OnlyIn, OnlyOut };

constexpr inline char roleToChar(InOut r)
{
    return r == OnlyIn ? '>' : r == OnlyOut ? '<' : '.';
}

template <typename StreamT>
inline StreamT& operator<<(StreamT& s, const InOut& v)
{
    return s << (v == OnlyIn ? "In" : v == OnlyOut ? "Out" : "In/Out");
}

struct Call;

struct Identifier
{
    /// As transformed for the generated code, not what's in YAML
    std::string name;
    InOut role = InAndOut;
    /// Always empty for Calls as they cannot be scoped (as yet)
    const Call* call = nullptr;
    // NB: When Calls do get scoped, the scope will likely be a namespace
    // so the above field will become const Identifier* or smth

    [[nodiscard]] std::string qualifiedName() const;
};

struct ObjectSchema;

struct TypeUsage : Identifier
{
    using imports_type = std::vector<std::string>;

    std::string baseName; //< As used in the API definition
    std::unordered_map<std::string, std::string> attributes;
    std::unordered_map<std::string, std::vector<std::string>> lists;
    std::vector<TypeUsage> paramTypes; //< Parameter types for type templates

    TypeUsage() = default;
    explicit TypeUsage(std::string typeName) : Identifier{move(typeName)} { }
    explicit TypeUsage(const ObjectSchema& schema);

    [[nodiscard]] TypeUsage specialize(std::vector<TypeUsage>&& params) const;

    [[nodiscard]] bool empty() const { return name.empty(); }

    void addImport(imports_type::value_type importName)
    {
        lists["imports"].emplace_back(move(importName));
    }

    [[nodiscard]] bool operator==(const TypeUsage& other) const
    {
        return name == other.name && call == other.call
               && baseName == other.baseName && attributes == other.attributes
               && lists == other.lists && paramTypes == other.paramTypes;
    }
    [[nodiscard]] bool operator!=(const TypeUsage& other) const
    {
        return !operator==(other);
    }
};

struct VarDecl : Identifier
{
    TypeUsage type;
    std::string baseName; //< Identifier as used in the API
    std::string description;
    bool required = false; // For the default constructor
    std::string defaultValue;

    VarDecl() = default;
    VarDecl(TypeUsage type, std::string name, std::string baseName,
            std::string description, bool required = false,
            std::string defaultValue = {})
        : Identifier{move(name)}, type(std::move(type))
        , baseName(move(baseName)), description(move(description))
        , required(required), defaultValue(move(defaultValue))
    { }

    [[nodiscard]] std::string toString(bool withDefault = false) const;
};

using VarDecls = std::vector<VarDecl>;

struct ObjectSchema : Identifier
{
    std::string description;
    std::vector<TypeUsage> parentTypes;
    std::vector<VarDecl> fields;
    VarDecl propertyMap;

    explicit ObjectSchema(InOut inOut, const Call* scope = nullptr,
                          std::string description = {})
        : Identifier{"", inOut, scope}, description(move(description))
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
        code(move(code)), description(move(description)), body(OnlyOut)
    { }
    std::string code;
    std::string description;
    VarDecls headers;
    ObjectSchema body;
};

struct ExternalDocs
{
    std::string description;
    std::string url;
};

enum Location : size_t { InPath = 0, InQuery = 1, InHeaders = 2 };

struct Call : Identifier {
    using params_type = VarDecls;
    using string = std::string;

    Call(Path callPath, string callVerb, string callName,
         bool callNeedsSecurity)
        : Identifier{move(callName)}, path(move(callPath))
        , verb(move(callVerb)), needsSecurity(callNeedsSecurity)
    { }
    ~Call() = default;
    Call(Call&) = delete;
    Call operator=(Call&) = delete;
    // Moving is not allowed because Call is used by pointer for scoping
    Call(Call&&) = delete;
    Call operator=(Call&&) = delete;

    [[nodiscard]] params_type& getParamsBlock(const string& blockName);
    [[nodiscard]] params_type collateParams() const;

    Path path;
    string verb;
    string summary;
    string description;
    ExternalDocs externalDocs;
    static const std::array<string, 3> ParamGroups;
    std::array<params_type, 3> params;
    ObjectSchema body{OnlyIn};
    // TODO: Embed proper securityDefinitions representation.
    bool needsSecurity;
    bool inlineBody = false;
    bool inlineResponse = false;

    std::vector<string> producedContentTypes;
    std::vector<string> consumedContentTypes;
    std::vector<Response> responses;
};

struct CallClass
{
    // Using std::list because it doesn't move the storage around
    std::list<Call> calls;
};

namespace ApiSpec {
using namespace std::string_literals;
static inline const auto Swagger = "swagger"s;
static inline const auto JSONSchema = "json-schema"s;
// static inline const auto OpenAPI3 = "openapi"s;
// static inline const auto RAML = "raml"s;
}

struct Model {
    using string = std::string;
    using imports_type = std::unordered_set<string>;
    using schemas_type = std::vector<ObjectSchema>;

    string apiSpec;
    /// Spec version liberally encoded in an int, e.g. 20 for Swagger 2.0
    /// or 201909 for JSON Schema 2019-09
    int apiSpecVersion = 0;

    imports_type imports;
    schemas_type types;

    string hostAddress;
    string basePath;
    std::list<CallClass> callClasses;

    void clear();

    Call& addCall(Path path, string verb, string operationId, bool needsToken);
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

template <typename StreamT>
inline StreamT& operator<<(StreamT& s, const Identifier& id)
{
    if (id.call)
        s << id.call->name;
    if (id.call || id.role != InAndOut)
        s << roleToChar(id.role);
    return s << (id.name.empty() && !id.call ? "(anonymous)" : id.name);
}
