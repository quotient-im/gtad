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

class Analyzer
{
    public:
        Analyzer(std::string filePath, std::string basePath,
                 const Translator& translator);

        Model loadModel(const pair_vector_t<std::string>& substitutions,
                        InOut inOut);

    private:
        std::string fileName;
        std::string baseDir;
        Model model;
        const Translator& translator;

        enum IsTopLevel : bool { Inner = false, TopLevel = true };
        TypeUsage analyzeTypeUsage(const YamlMap& node, InOut inOut,
                              std::string scope, IsTopLevel isTopLevel = Inner);
        ObjectSchema analyzeSchema(const YamlMap& yamlSchema, InOut inOut,
                std::string scope = {}, std::string locus = {});

        void addParamsFromSchema(VarDecls& varList, const Call& call,
                const std::string& baseName, bool required,
                const ObjectSchema& bodyParamSchema);
        template <typename ScopeT, typename... ArgTs>
        VarDecl makeVarDecl(TypeUsage type, const std::string& baseName,
                         const ScopeT& scope, ArgTs&&... args)
        {
            return { std::move(type),
                translator.mapIdentifier(baseName, qualifiedName(scope)),
                baseName, std::forward<ArgTs>(args)... };
        }

        template <typename... ArgTs>
        void addVarDecl(VarDecls& varList, ArgTs&&... varDeclArgs)
        {
            model.addVarDecl(varList,
                makeVarDecl(std::forward<ArgTs>(varDeclArgs)...));
        }
};
