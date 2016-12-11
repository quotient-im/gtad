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
#include <unordered_map>

struct VarDecl
{
    std::string type;
    std::string name;
    bool required = false;

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
    void printTo(std::ostream& s) const;
};

using ResponseType = DataModel;

struct ParamDecl : public VarDecl
{
        enum In { Undefined, Path, Query, Header, Data } in;
        static const char* const in_str[];
        static In inFromStr(const std::string& s);
        static std::string strFromIn(In in)
        {
            return in_str[in];
        }

        ParamDecl(const std::string& t, const std::string& n, In in)
            : VarDecl(t, n)
            , in(in)
        { }
};

struct CallOverload
{
    std::vector<ParamDecl> params;
    std::string quotedPath;
    std::string verb;
    std::unordered_map<std::string, std::string> query;
    std::string data;
    std::string contentType;
    bool needsToken;

};

struct Model;

struct CallConfigModel
{
    const Model& topModel;
    std::string className;
    std::vector<CallOverload> callOverloads;
    VarDecl replyFormatVar;
    ResponseType responseType;

    CallConfigModel(const Model& parent,
                    const std::string& callName,
                    const std::string& responseTypeName,
                    const std::string& replyFormatType = "const QJsonObject&");

    void printTo(std::ostream& hText, std::ostream& cppText) const;

    void printSignatures(std::ostream& hS, std::ostream& cppS,
                         const std::vector<ParamDecl>& params,
                         const std::string& returnType = "") const;
    void printBody(std::ostream& s, const CallOverload& call,
                   bool asFunction = false) const;
    void printOverloads(std::ostream& hS, std::ostream& cppS) const;
};

struct Model
{
    std::string nsName;
    std::vector<std::string> includes;
    std::vector<DataModel> dataModels;
    std::vector<CallConfigModel> callModels;

    explicit Model(const std::string& nameSpace = "") : nsName(nameSpace) { }
    CallOverload& addCall(const std::string& path, const std::string& verb,
                          const std::string& responseTypename);
};

