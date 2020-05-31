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

#include <stack>
#include <filesystem>

class YamlNode;
class YamlMap;
class YamlSequence;

class Analyzer
{
public:
    using string = std::string;
    using fspath = std::filesystem::path;
    using models_t = std::unordered_map<string, Model>;

    explicit Analyzer(const Translator& translator, fspath basePath = {});

    const Model& loadModel(const string& filePath, InOut inOut);
    static const models_t& allModels() { return _allModels; }

private:
    static models_t _allModels;

    const fspath _baseDir;
    const Translator& _translator;

    struct WorkItem {
        fspath fileDir;
        Model* model;
    };
    std::stack<WorkItem, std::vector<WorkItem>> _workStack;

    enum IsTopLevel : bool { Inner = false, TopLevel = true };
    enum SubschemasStrategy : bool { ImportSubschemas = false,
                                     InlineSubschemas = true };

    [[nodiscard]] Model& curModel() const { return *_workStack.top().model; }

    [[nodiscard]] std::pair<const Model&, string>
    loadDependency(const string& relPath, InOut inOut = In | Out);
    void fillDataModel(Model& m, const YamlNode& yaml, const string& filename,
                       InOut inOut = In | Out);

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
        curModel().addVarDecl(varList,
                              makeVarDecl(std::forward<ArgTs>(varDeclArgs)...));
    }
};
