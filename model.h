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

#include <string>
#include <utility>
#include <vector>
#include <array>
#include <unordered_set>

std::string convertMultiword(std::string s);

struct VarDecl
{
    std::string type;
    std::string name;
    bool required;
    std::string defaultValue;

    static std::string setupDefault(std::string type,
                                    std::string defaultValue);

    VarDecl(std::string type, std::string name,
            bool required = true, std::string defaultValue = {})
        : type(std::move(type)), name(std::move(name)), required(required)
        , defaultValue(setupDefault(type, std::move(defaultValue)))
    { }

    bool isRequired() const { return required; }

    std::string toString(bool withDefault = false) const
    {
        return type + " " +
                (withDefault && !required ? name + " = " + defaultValue : name);
    }
};

struct TypeUsage
{
    using imports_type = std::vector<std::string>;

    std::string name;
    imports_type imports;

    explicit TypeUsage(std::string typeName,
                       imports_type requiredImports = {},
                       std::string appendImport = {})
        : name(std::move(typeName)), imports(std::move(requiredImports))
    {
        if (!appendImport.empty())
            imports.emplace_back(std::move(appendImport));
    }
    TypeUsage(const std::string& typeName, const std::string& import)
        : TypeUsage(typeName, imports_type(1, import))
    { }
};

struct StructDef
{
    std::string name;
    std::vector<VarDecl> fields;

    explicit StructDef(std::string typeName) : name(std::move(typeName)) { }
};

using ResponseType = TypeUsage;

struct Call
{
    using params_type = std::vector<VarDecl>;

    Call(std::string callPath, std::string callVerb, std::string callName,
         bool callNeedsToken)
        : path(std::move(callPath)), verb(std::move(callVerb))
        , name(std::move(callName)), needsToken(callNeedsToken)
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
    params_type::size_type paramsTotalSize() const
    {
        params_type::size_type s = 0;
        for (const auto& p: allParams) s += p.size();
        return s;
    }
    params_type collateParams() const;

    std::string path;
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
    std::string className;
    std::vector<Call> callOverloads;
    VarDecl replyFormatVar;
    ResponseType responseType;

    CallClass(std::string callName,
              std::string responseTypeName,
              std::string replyFormatType = "const QJsonObject&")
        : className(std::move(callName))
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
    std::string nsName;
    imports_type imports;
    std::vector<StructDef> types;
    std::vector<CallClass> callClasses;

    Model(std::string fileDir, std::string fileName, std::string nameSpace = "")
        : fileDir(std::move(fileDir)), filename(std::move(fileName))
        , nsName(std::move(nameSpace))
    { }
    ~Model() = default;
    Model(Model&) = delete;
    Model operator=(Model&) = delete;
    Model(Model&&) = default;
    Model& operator=(Model&&) = delete;
    Call& addCall(std::string path, std::string verb, std::string name, bool needsToken,
                  std::string responseTypename);
    void addCallParam(Call& call, const TypeUsage& type, const std::string& name,
                      bool required, const std::string& in)
    {
        call.addParam(VarDecl(type.name, name, required), in);
        imports.insert(type.imports.begin(), type.imports.end());
    }
};


