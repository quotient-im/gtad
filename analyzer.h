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

class YamlNode;
class YamlMap;
class YamlSequence;

class Analyzer
{
public:
    using string = std::string;

    Analyzer(std::string filePath, std::string basePath,
             const Translator& translator);

    Model loadModel(const pair_vector_t<std::string>& substitutions,
                    InOut inOut);

private:
    string fileName;
    string _baseDir;
    Model model;
    const Translator& _translator;

    enum IsTopLevel : bool { Inner = false, TopLevel = true };
    enum SubschemasStrategy : bool { ImportSubschemas = false,
                                     InlineSubschemas = true };

    [[nodiscard]] TypeUsage analyzeTypeUsage(const YamlMap& node, InOut inOut,
                                             string scope,
                                             IsTopLevel isTopLevel = Inner);
    [[nodiscard]] TypeUsage analyzeMultitype(const YamlSequence& yamlTypes,
                                             InOut inOut,
                                             const string& scope);
    [[nodiscard]] ObjectSchema
    analyzeSchema(const YamlMap& yamlSchema, InOut inOut,
                  string scope = {}, const string& locus = {},
                  SubschemasStrategy subschemasStrategy = ImportSubschemas);

    void addParamsFromSchema(VarDecls& varList, const Scope& scope,
                             const string& baseName, bool required,
                             const ObjectSchema& bodyParamSchema);

    template <typename... ArgTs>
    [[nodiscard]] VarDecl makeVarDecl(TypeUsage type,
                                      const string& baseName,
                                      const Scope& scope, ArgTs&&... args)
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
