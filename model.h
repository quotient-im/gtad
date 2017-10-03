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

struct TypeUsage
{
    using imports_type = std::vector<std::string>;

    std::string name;
    std::unordered_map<std::string, std::string> attributes;
    std::unordered_map<std::string, std::vector<std::string>> lists;
    std::vector<TypeUsage> innerTypes;

    explicit TypeUsage(std::string typeName) : name(std::move(typeName)) { }
    TypeUsage(std::string typeName, std::string import)
        : TypeUsage(move(typeName))
    {
        lists.emplace("imports", imports_type { move(import) });
    }

    TypeUsage operator()(const TypeUsage& innerType) const;
};

struct VarDecl
{
    TypeUsage type;
    std::string name;
    bool required;
    std::string defaultValue;

    static std::string setupDefault(TypeUsage type, std::string defaultValue);

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

struct StructDef
{
    std::string name;
    std::vector<VarDecl> fields;

    explicit StructDef(std::string typeName) : name(std::move(typeName)) { }
};

using ResponseType = TypeUsage;

std::vector<std::string> splitPath(const std::string& path);

struct Call
{
    using params_type = std::vector<VarDecl>;

    Call(std::string callPath, std::string callVerb, std::string callName,
         bool callNeedsToken)
        : path(std::move(callPath)), pathParts(splitPath(path))
        , verb(std::move(callVerb)), name(std::move(callName))
        , needsToken(callNeedsToken)
    { }
    ~Call() = default;
    Call(Call&) = delete;
    Call operator=(Call&) = delete;
    Call(Call&& other) noexcept
        : path(std::move(other.path)), verb(std::move(other.verb))
        , allParams(std::move(other.allParams)), needsToken(other.needsToken)
    { }
    Call operator=(Call&&) = delete;

    void addParam(const VarDecl& param, const std::string& in);
    params_type collateParams() const;

    std::string path;
    std::vector<std::string> pathParts;
    std::string verb;
    std::string name;
    std::array<params_type, 4> allParams;
    params_type& pathParams = allParams[0];
    params_type& queryParams = allParams[1];
    params_type& headerParams = allParams[2];
    params_type& bodyParams = allParams[3];
    bool needsToken;
};

struct Model;

struct CallClass
{
    std::string operationId;
    std::vector<Call> callOverloads;
    VarDecl replyFormatVar;
    ResponseType responseType;

    CallClass(std::string operationId,
              std::string responseTypeName,
              TypeUsage replyFormatType = TypeUsage("const QJsonObject&"))
        : operationId(std::move(operationId))
        , replyFormatVar (std::move(replyFormatType), "reply", true)
        , responseType(std::move(responseTypeName))
    { }
    Call& addCall(std::string path, std::string verb, std::string name,
                  bool needsToken)
    {
        callOverloads.emplace_back(path, verb, name, needsToken);
        return callOverloads.back();
    }
};

struct Model
{
    using imports_type = std::unordered_set<std::string>;

    const std::string fileDir;
    const std::string filename;

    std::string hostAddress;
    std::string basePath;
    imports_type imports;
    std::vector<StructDef> types;
    std::vector<CallClass> callClasses;

    Model(std::string fileDir, std::string fileName)
        : fileDir(std::move(fileDir)), filename(std::move(fileName))
    { }
    ~Model() = default;
    Model(Model&) = delete;
    Model operator=(Model&) = delete;
    Model(Model&&) = default;
    Model& operator=(Model&&) = delete;
    Call& addCall(std::string path, std::string verb, std::string operationId,
                  bool needsToken, std::string responseTypename);
    void addCallParam(Call& call, const TypeUsage& type, const std::string& name,
                      bool required, const std::string& in);
};


