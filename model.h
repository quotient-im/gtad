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
#include <unordered_map>
#include <variant>

std::string capitalizedCopy(std::string s);
std::string camelCase(std::string s);

enum InOut : unsigned char { InAndOut = 0, OnlyIn, OnlyOut };

constexpr inline char roleToChar(InOut r)
{
    return r == OnlyIn ? '>' : r == OnlyOut ? '<' : '.';
}

inline auto& operator<<(auto& s, const InOut& v)
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
    // NB: When Calls get scoped, the scope will likely be a namespace
    // so the above field will become const Identifier* or smth

    [[nodiscard]] std::string qualifiedName() const;
};

struct ObjectSchema;

struct TypeUsage : Identifier
{
    using imports_type = std::vector<std::string>;

    std::string baseName; ///< As used in the API definition
    std::unordered_map<std::string, std::string> attributes;
    std::unordered_map<std::string, std::vector<std::string>> lists;
    std::vector<TypeUsage> paramTypes; ///< Parameter types for type templates

    TypeUsage() = default;
    explicit TypeUsage(std::string typeName)
        : Identifier {std::move(typeName)}
    {}
    explicit TypeUsage(const ObjectSchema& schema);

    [[nodiscard]] TypeUsage specialize(std::vector<TypeUsage>&& params) const;

    [[nodiscard]] bool empty() const { return name.empty(); }

    void addImport(imports_type::value_type importName)
    {
        lists["imports"].emplace_back(std::move(importName));
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

struct VarDecl : Identifier {
    using string = std::string;
    TypeUsage type;
    string baseName; ///< Identifier as used in the API
    string description;
    bool required = false;
    string defaultValue;

    VarDecl() = default;
    VarDecl(TypeUsage type, string varName, string baseName, string description,
            bool required = false, string defaultValue = {})
        : Identifier{std::move(varName)}, type(std::move(type))
        , baseName(std::move(baseName)), description(std::move(description))
        , required(required), defaultValue(std::move(defaultValue))
    {}

    [[nodiscard]] std::string toString(bool withDefault = false) const;
};

using VarDecls = std::vector<VarDecl>;

struct FlatSchema : Identifier {
    explicit FlatSchema(InOut inOut, const Call* scope = nullptr)
        : Identifier{"", inOut, scope}
    { }
    VarDecls fields;
    VarDecl propertyMap;

    [[nodiscard]] bool hasPropertyMap() const
    {
        return !propertyMap.type.empty();
    }
};

struct ObjectSchema : FlatSchema {
    std::string description;
    std::vector<TypeUsage> parentTypes;

    explicit ObjectSchema(InOut inOut, const Call* scope = nullptr,
                          std::string description = {})
        : FlatSchema(inOut, scope), description(std::move(description))
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
    [[nodiscard]] bool hasParents() const { return !parentTypes.empty(); }
};

/** @brief A structure to store request and response bodies
 *
 * The first option in this variant corresponds to the case when the body
 * is expected to be empty. No parameters are exposed in the internal API
 * (for the client app).
 *
 * The second option ("unpacked body") corresponds to the case where
 * the top level of the body schema does not involve parent types.
 * In that case it is feasible and convenient for the generated code
 * to use top level properties of the schema for internal (for the client app)
 * API parameters, instead of wrapping the whole body schema into a single
 * parameter.
 *
 * The third option ("packed body") is for complex schemas with both
 * parent types and properties involved (trivial schemas with a single
 * parent type and no properties are inlined and the same criteria is applied
 * to the inlined contents instead). Unpacking properties is not feasible
 * in that case so the generated API will have a single body parameter.
 */
using Body = std::variant<std::monostate, FlatSchema, VarDecl>;

[[nodiscard]] inline bool empty(const Body& body)
{
    return std::holds_alternative<std::monostate>(body);
}

/** \brief Helper type to visit Body objects
 *
 * Taken from https://en.cppreference.com/w/cpp/utility/variant/visit
 */
template <class... Ts>
struct overloadedVisitor : Ts... { using Ts::operator()...; };

/** Convenience wrapper around std::visit */
template <typename VariantT, typename... VisitorTs>
inline auto dispatchVisit(VariantT&& var, VisitorTs&&... visitors)
{
    return std::visit(overloadedVisitor{std::forward<VisitorTs>(visitors)...},
                      std::forward<VariantT>(var));
}

struct Path : public std::string
{
    explicit Path(std::string path);
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
        code(std::move(code)), description(std::move(description))
    { }
    std::string code;
    std::string description;
    VarDecls headers;
    Body body;
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
        : Identifier{std::move(callName)}, path(std::move(callPath))
        , verb(std::move(callVerb)), needsSecurity(callNeedsSecurity)
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
    Body body;
    // TODO: Embed proper securityDefinitions representation.
    bool needsSecurity;

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
static inline constexpr auto Swagger = "swagger"s;
static inline constexpr auto JSONSchema = "json-schema"s;
static inline constexpr auto OpenAPI3 = "openapi3"s;
// static inline const auto RAML = "raml"s;
}

struct Model {
    using string = std::string;
    /// Map from the included path (in API description) to the import renderer
    using imports_type = std::unordered_map<string, string>;
    using schemas_type = std::vector<ObjectSchema>;

    string apiSpec;
    /// Spec version liberally encoded in an int, e.g. 20 for Swagger 2.0
    /// or 201909 for JSON Schema 2019-09
    int apiSpecVersion = 0;
    bool inlineMainSchema = false;

    imports_type imports;
    schemas_type types;

    string hostAddress;
    string basePath;
    std::list<CallClass> callClasses;

    void clear();

    Call& addCall(Path path, string verb, string operationId, bool needsToken);
    void addSchema(ObjectSchema&& schema);
    void addImportsFrom(const ObjectSchema& type);
    void addImportsFrom(const FlatSchema& type);
    void addImportsFrom(const TypeUsage& type);

    [[nodiscard]] bool empty() const
    {
        return callClasses.empty() && types.empty();
    }
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

inline auto& operator<<(auto& s, const Identifier& id)
{
    if (id.call)
        s << id.call->name;
    if (id.call || id.role != InAndOut)
        s << roleToChar(id.role);
    return s << (id.name.empty() && !id.call ? "(anonymous)" : id.name);
}
