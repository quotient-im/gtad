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

#include "model.h"
#include "translator.h"
#include "util.h"
#include "yaml.h"

#include <filesystem>
#include <optional>

class Analyzer {
public:
    using string = std::string;
    using fspath = std::filesystem::path;
    // Using string instead of fspath here because of
    // https://cplusplus.github.io/LWG/issue3657
    // TODO: switch string to fspath once the oldest supported libstdc++ has
    //       std::hash<std::filesystem::path>
    using models_t = std::unordered_map<string, Model>;

    explicit Analyzer(const Translator& translator, fspath basePath = {});
    Analyzer(Analyzer&) = delete;
    Analyzer(Analyzer&&) = delete;
    void operator=(Analyzer&) = delete;
    void operator=(Analyzer&&) = delete;

    const Model& loadModel(const string& filePath, InOut inOut);
    static const models_t& allModels() { return _allModels; }

private:
    static models_t _allModels;

    const fspath _baseDir;
    const Translator& _translator;

    struct Context {
        fspath fileDir;
        Model* model;
        const Identifier scope;
    };
    const Context* _context = nullptr;
    size_t _indent = 0;
    friend class ContextOverlay; // defined in analyzer.cpp

    enum IsTopLevel : bool { Inner = false, TopLevel = true };
    enum RefsStrategy : bool { ImportRefs = false, InlineRefs = true };

    [[nodiscard]] const Context& context() const
    {
        if (!_context)
            throw Exception(
                "Internal error: trying to access the context before creation");
        return *_context;
    }
    [[nodiscard]] Model& currentModel() const { return *context().model; }
    [[nodiscard]] const Identifier& currentScope() const { return context().scope; }
    [[nodiscard]] InOut currentRole() const { return currentScope().role; }
    [[nodiscard]] const Call* currentCall() const { return currentScope().call; }

    [[nodiscard]] fspath makeModelKey(const fspath& sourcePath);
    [[nodiscard]] std::pair<const Model&, fspath> loadDependency(
        const string& relPath, const string& overrideTitle,
        bool inlined = false);
    void fillDataModel(Model& m, const YamlMap<>& yaml, const fspath& filename);

    [[nodiscard]] TypeUsage analyzeTypeUsage(const YamlMap<>& node,
                                             IsTopLevel isTopLevel = Inner);
    TypeUsage addSchema(ObjectSchema&& schema);
    [[nodiscard]] TypeUsage analyzeMultitype(const YamlSequence<>& yamlTypes);
    [[nodiscard]] ObjectSchema analyzeSchema(const YamlMap<>& yamlSchema,
                                             RefsStrategy refsStrategy = ImportRefs);
    [[nodiscard]] ObjectSchema analyzeObject(const YamlMap<>& yamlSchema,
                                             RefsStrategy refsStrategy);

    Body analyzeBodySchema(const YamlMap<>& yamlSchema, const string& name,
                           string description, bool required = true);

    ObjectSchema resolveRef(const string& refPath, RefsStrategy refsStrategy);

    [[nodiscard]] ObjectSchema makeEphemeralSchema(TypeUsage&& tu) const;
    [[nodiscard]] std::optional<VarDecl> makeVarDecl(
        TypeUsage type, const string& baseName, const Identifier& scope,
        string description, bool required = false,
        string defaultValue = {}) const;

    void addVarDecl(VarDecls& varList, VarDecl&& v) const;
    void addVarDecl(VarDecls& varList, TypeUsage type, const string& baseName,
                    const Identifier& scope, string description,
                    bool required = false, string defaultValue = {}) const;

    [[nodiscard]] auto logOffset() const { return string(_indent * 2, ' '); }
};
