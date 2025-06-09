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

#include <QtCore/QUrl>

#include <array>
#include <cstdint>
#include <limits>
#include <list>
#include <unordered_map>
#include <variant>

std::string titleCased(std::string s);

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
    std::string importRenderer = "{{_}}";

    TypeUsage() = default;
    explicit TypeUsage(std::string typeName)
        : Identifier {std::move(typeName)}
    {}
    explicit TypeUsage(const ObjectSchema& schema);
    void assignName(std::string setName, std::string setBaseName = {});

    [[nodiscard]] TypeUsage specialize(std::vector<TypeUsage>&& params) const;

    [[nodiscard]] bool empty() const { return name.empty(); }

    void addImport(imports_type::value_type importName)
    {
        lists["imports"].emplace_back(std::move(importName));
    }

    std::string_view getAttributeValue(const std::string& attrName) const
    {
        const auto attrIt = attributes.find(attrName);
        return attrIt != attributes.end() ? attrIt->second : std::string_view{};
    }

    [[nodiscard]] bool operator==(const TypeUsage& other) const
    {
        return name == other.name && call == other.call
               && baseName == other.baseName && attributes == other.attributes
               && lists == other.lists && paramTypes == other.paramTypes;
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
    // uint16_t because that's as much as JSON accepts for integers
    uint16_t maxProperties = std::numeric_limits<uint16_t>::max();
    VarDecls fields;
    VarDecl additionalProperties;
    std::string additionalPropertiesPattern = {};

    [[nodiscard]] bool hasAdditionalProperties() const
    {
        return !additionalProperties.type.empty();
    }
};

struct ObjectSchema : FlatSchema {
    std::string description;
    std::vector<TypeUsage> parentTypes;
    mutable bool preferInlining = false;

    explicit ObjectSchema(InOut inOut, const Call* scope = nullptr,
                          std::string description = {})
        : FlatSchema(inOut, scope), description(std::move(description))
    { }
    ~ObjectSchema() = default; // Just to satisfy Rule of 5
    ObjectSchema(ObjectSchema&&) = default;
    void operator=(const ObjectSchema&) = delete;
    void operator=(ObjectSchema&&) = delete;

    [[nodiscard]] bool empty() const
    {
        return parentTypes.empty() && fields.empty();
        // If parentTypes and fields are empty, propertyMap must not exist
    }
    [[nodiscard]] bool trivial() const
    {
        return parentTypes.size() == 1 && fields.empty() && additionalProperties.name.empty();
    }
    [[nodiscard]] bool hasParents() const { return !parentTypes.empty(); }
    ObjectSchema cloneForInlining() const
    {
        auto clone = *this;
        clone.preferInlining = true;
        return clone;
    }
    bool inlined() const { return trivial() || preferInlining; }

private: // I know, doesn't belong to structs, but it's the simplest and most readable
    ObjectSchema(const ObjectSchema&) = default;
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

class Server {
public:
    Server(const QString& urlString, std::string description = {})
        : url(QUrl::fromUserInput(urlString)), desc(std::move(description))
    {}
    Server(const std::string& scheme, const std::string& host, const std::string& basePath,
           std::string description = {})
        : url(QString::fromStdString(scheme) + QStringLiteral("://") + QString::fromStdString(host)
              + QString::fromStdString(basePath))
        , desc(std::move(description))
    {}

    std::string toString() const { return url.toString().toStdString(); }
    std::string scheme() const { return url.scheme().toStdString(); }
    std::string host() const { return url.host().toStdString(); }
    std::string basePath() const { return url.path().toStdString(); }
    std::string description() const { return desc; }

private:
    QUrl url;
    std::string desc;
};

struct Path : public std::string
{
    explicit Path(std::string path);

    struct PartType {
        size_type from;
        size_type to;
        enum { Literal, Variable } kind;
    };

    std::vector<PartType> parts;
    std::vector<Server> overrideServers;
};

struct Response
{
    explicit Response(std::string code, std::string description = {}) :
        code(std::move(code)), description(std::move(description))
    { }
    std::string code;
    std::string description;
    VarDecls headers;
    std::vector<std::string> contentTypes;
    Body body;
};

struct ExternalDocs
{
    std::string description;
    std::string url;
};

enum Location : size_t { InPath = 0, InQuery = 1, InHeaders = 2 };

using types_t = std::vector<std::pair<std::unique_ptr<const ObjectSchema>, TypeUsage>>;

struct Call : Identifier {
    using params_type = VarDecls;
    using string = std::string;

    Call(Path callPath, string callVerb, string callName, bool deprecated, bool callNeedsSecurity)
        : Identifier{std::move(callName)}
        , path(std::move(callPath))
        , verb(std::move(callVerb))
        , deprecated(deprecated)
        , needsSecurity(callNeedsSecurity)
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
    std::vector<Server> serverOverride;
    string verb;
    string summary;
    string description;
    bool deprecated;
    ExternalDocs externalDocs;
    types_t localSchemas;
    static const std::array<string, 3> ParamGroups;
    std::array<params_type, 3> params;
    Body body;
    // TODO: Embed proper securityDefinitions representation.
    bool needsSecurity;

    std::vector<string> consumedContentTypes;
    std::vector<Response> responses;
};

struct CallClass
{
    // Using std::list because it doesn't move the storage around
    std::list<Call> calls;
};

enum class ApiSpec { Undefined = 0, Swagger = 20, OpenAPI3 = 31, JSONSchema = 201909 };

struct Model {
    using string = std::string;
    /// Map from the included path (in API description) to the import renderer
    using imports_type = std::unordered_map<string, string>;

    ApiSpec apiSpec;

    imports_type imports;
    types_t globalSchemas;
    std::unordered_map<std::string, TypeUsage> localRefs;

    std::vector<Server> defaultServers;
    std::list<CallClass> callClasses;

    void clear();

    Call& addCall(Path path, string verb, string operationId, bool deprecated, bool needsToken);
    void addSchema(ObjectSchema&& schema, const TypeUsage &tu);
    void addImportsFrom(const ObjectSchema& type);
    void addImportsFrom(const FlatSchema& type);
    void addImportsFrom(const TypeUsage& type);

    [[nodiscard]] bool empty() const
    {
        return callClasses.empty() && globalSchemas.empty();
    }
    [[nodiscard]] bool trivial() const
    {
        return callClasses.empty() &&
               globalSchemas.size() == 1 && globalSchemas.front().first->trivial();
    }

private:
    types_t& typesForScope(const Call* s);
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
