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

#include "model.h"
#include "util.h"

#include <string>

class Translator;
class YamlNode;
class YamlMap;

class Analyzer
{
    public:
        Analyzer(const std::string& filePath, const std::string& basePath,
                 const Translator& translator);

        Model loadModel(const pair_vector_t<std::string>& substitutions);

    private:
        std::string fileName;
        std::string baseDir;
        Model model;
        const Translator& translator;

        enum InOut { In, Out };
        TypeUsage analyzeType(const YamlMap& node,
                              InOut inOut, std::string scope);

        ObjectSchema analyzeSchema(const YamlMap& yamlSchema, std::string scope);
        ObjectSchema tryResolveRefs(const YamlMap& yamlSchema);
        void addParamsFromSchema(VarDecls& varList, std::string name,
                     bool required, const ObjectSchema& bodyParamSchema);
};
