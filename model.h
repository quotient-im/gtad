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

#include <string>
#include <utility>
#include <vector>
#include <array>
#include <unordered_set>
#include <unordered_map>

std::string capitalizedCopy(std::string s);
std::string camelCase(std::string s);
void eraseSuffix(std::string* path, const std::string& suffix);
std::string dropSuffix(std::string path, const std::string& suffix);

struct ObjectSchema;

struct TypeUsage
{
    using imports_type = std::vector<std::string>;

    std::string scope;
    std::string name;
    std::string baseName;
    std::unordered_map<std::string, std::string> attributes;
    std::unordered_map<std::string, std::vector<std::string>> lists;
    std::vector<TypeUsage> innerTypes;

    explicit TypeUsage(std::string typeName) : name(std::move(typeName)) { }
    explicit TypeUsage(const ObjectSchema& schema);
    TypeUsage(std::string typeName, std::string import)
        : TypeUsage(move(typeName))
    {
        lists.emplace("imports", imports_type { move(import) });
    }

    TypeUsage instantiate(TypeUsage&& innerType) const;
    bool empty() const { return name.empty(); }
};

struct VarDecl
{
    TypeUsage type;
    std::string name;
    bool required;
    std::string defaultValue;

    static std::string setupDefault(const TypeUsage& type,
                                    std::string defaultValue);

    VarDecl(TypeUsage type, std::string name,
            bool required = true, std::string defaultValue = {})
        : type(std::move(type)), name(std::move(name)), required(required)
        , defaultValue(setupDefault(type, std::move(defaultValue)))
    { }

    bool isRequired() const { return required; }

    std::string toString(bool withDefault = false) const
    {
        return type.name + " " +
                (withDefault && !required ? name + " = " + defaultValue : name);
    }
};

struct ObjectSchema
{
    std::string scope; // Either empty (top-level) or a Call name
    std::string name;
    std::vector<TypeUsage> parentTypes;
    std::vector<VarDecl> fields;

    bool empty() const { return parentTypes.empty() && fields.empty(); }
    bool trivial() const { return parentTypes.size() == 1 && fields.empty(); }
};

using VarDecls = std::vector<VarDecl>;

struct Response
{
    std::string code;
    VarDecls properties;
};

std::vector<std::string> splitPath(const std::string& path);

struct Call
{
    using params_type = VarDecls;

    Call(std::string callPath, std::string callVerb, std::string callName,
         bool callNeedsSecurity)
        : path(std::move(callPath)), pathParts(splitPath(path))
        , verb(std::move(callVerb)), name(std::move(callName))
        , needsSecurity(callNeedsSecurity)
    { }
    ~Call() = default;
    Call(Call&) = delete;
    Call operator=(Call&) = delete;
    Call(Call&&) = default;
    Call operator=(Call&&) = delete;

    static const std::array<std::string, 4> paramsBlockNames;
    const Call::params_type& getParamsBlock(const std::string& name) const;
    Call::params_type& getParamsBlock(const std::string& name);
    params_type collateParams() const;

    std::string path;
    std::vector<std::string> pathParts;
    std::string verb;
    std::string name;
    std::array<params_type, 4> allParams;
    params_type& pathParams() { return allParams[0]; }
    params_type& queryParams() { return allParams[1]; }
    params_type& headerParams() { return allParams[2]; }
    params_type& bodyParams() { return allParams[3]; }
    const params_type& bodyParams() const { return allParams[3]; }
    // TODO: Embed proper securityDefinitions representation.
    bool needsSecurity;
    bool inlineBody = false;
    std::vector<Response> responses;
};

struct CallClass
{
    std::vector<Call> calls;
};

struct Model
{
    using imports_type = std::unordered_set<std::string>;
    using schemas_type = std::vector<ObjectSchema>;

    const std::string fileDir;
    const std::string filename;

    std::string hostAddress;
    std::string basePath;
    imports_type imports;
    schemas_type types;
    std::vector<CallClass> callClasses;

    Model(std::string fileDir, std::string fileName)
        : fileDir(std::move(fileDir)), filename(std::move(fileName))
    { }
    ~Model() = default;
    Model(const Model&) = delete;
    Model operator=(const Model&) = delete;
    Model(Model&&) = default;
    Model& operator=(Model&&) = delete;
    Call& addCall(std::string path, std::string verb, std::string operationId,
                  bool needsToken);
    void addVarDecl(VarDecls& varList, VarDecl var);
    void addSchema(const ObjectSchema& schema);
    void addImports(const TypeUsage& type);
};


