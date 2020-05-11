/******************************************************************************
 * Copyright (C) 2018 Kitsune Ral <kitsune-ral@users.sf.net>
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

#include "translator.h"
#include "model.h"
#include "util.h"

#include <filesystem>

class YamlNode;
class YamlMap;
class YamlSequence;

class Analyzer
{
public:
    using string = std::string;
    using fspath = std::filesystem::path;

    Analyzer(std::string filePath, fspath basePath,
             const Translator& translator);

    Model&& loadModel(const pair_vector_t<std::string>& substitutions,
                      InOut inOut);

private:
    string fileName;
    fspath _baseDir;
    Model model;
    const Translator& _translator;

    enum IsTopLevel : bool { Inner = false, TopLevel = true };
    enum SubschemasStrategy : bool { ImportSubschemas = false,
                                     InlineSubschemas = true };

    [[nodiscard]] TypeUsage analyzeTypeUsage(const YamlMap& node, InOut inOut,
                                             const Call* scope,
                                             IsTopLevel isTopLevel = Inner);
    [[nodiscard]] TypeUsage analyzeMultitype(const YamlSequence& yamlTypes,
                                             InOut inOut, const Call* scope);
    [[nodiscard]] ObjectSchema
    analyzeSchema(const YamlMap& yamlSchema, InOut inOut,
                  const Call* scope = {}, const string& locus = {},
                  SubschemasStrategy subschemasStrategy = ImportSubschemas);

    void mergeFromSchema(ObjectSchema& target, const ObjectSchema& sourceSchema,
                         const string& baseName = {}, bool required = true);

    template <typename... ArgTs>
    [[nodiscard]] VarDecl makeVarDecl(TypeUsage type, const string& baseName,
                                      const Identifier& scope, ArgTs&&... args)
    {
        return { std::move(type),
                 _translator.mapIdentifier(baseName, scope.qualifiedName()),
                 baseName, std::forward<ArgTs>(args)... };
    }

    template <typename... ArgTs>
    void addVarDecl(VarDecls& varList, ArgTs&&... varDeclArgs)
    {
        model.addVarDecl(varList,
                         makeVarDecl(std::forward<ArgTs>(varDeclArgs)...));
    }
};
