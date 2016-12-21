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
#include <vector>
#include <array>

struct VarDecl
{
    std::string type;
    std::string name;
    bool required = false;
    bool isRequired() const { return required; }

    VarDecl(const std::string& t, const std::string& n)
        : type(t), name(n)
    { }

    std::string toString(bool withDefault = false) const
    {
        return type + " " + (withDefault && !required ? name + " = {}" : name);
    }
};

struct DataModel
{
    std::string name;
    std::vector<VarDecl> fields;

    explicit DataModel(const std::string& typeName);
};

using ResponseType = DataModel;

class Call
{
    public:
        using params_type = std::vector<VarDecl>;

        Call(const std::string& callPath, const std::string& callVerb,
             bool callNeedsToken)
            : path(callPath), verb(callVerb), needsToken(callNeedsToken)
        { }
        Call(Call&) = delete;
        Call(Call&& other)
            : path(other.path), verb(other.verb), allParams(other.allParams)
            , needsToken(other.needsToken)
        {

        }

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
        std::array<params_type, 4> allParams;
        params_type& pathParams = allParams[0];
        params_type& queryParams = allParams[1];
        params_type& headerParams = allParams[2];
        params_type& bodyParams = allParams[3];
        bool needsToken;
};

struct Model;

struct CallConfigModel
{
    std::string className;
    std::vector<Call> callOverloads;
    VarDecl replyFormatVar;
    ResponseType responseType;

    CallConfigModel(const std::string& callName,
                    const std::string& responseTypeName,
                    const std::string& replyFormatType = "const QJsonObject&");
    Call& addCall(const std::string& path, const std::string& verb,
                  bool needsToken)
    {
        callOverloads.emplace_back(path, verb, needsToken);
        return callOverloads.back();
    }
};

struct Model
{
    std::string nsName;
    std::vector<std::string> includes;
    std::vector<DataModel> dataModels;
    std::vector<CallConfigModel> callModels;

    explicit Model(const std::string& nameSpace = "") : nsName(nameSpace) { }
    Model(Model&) = delete;
    Model(Model&&) = default;
    Call& addCall(const std::string& path, const std::string& verb,
                  bool needsToken, const std::string& responseTypename);
};

