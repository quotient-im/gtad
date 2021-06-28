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

#include <filesystem>
#include <memory>

class Printer;

enum class Verbosity { Quiet = 0, Basic, Debug };

class Translator
{
public:
    using string = std::string;
    using path = std::filesystem::path;
    using output_config_t = std::vector<std::pair<path, string>>;

    Translator(const path& configFilePath, path outputDirPath,
               Verbosity verbosity);
    ~Translator();

    [[nodiscard]] const pair_vector_t<string>& substitutions() const
    {
        return _substitutions;
    }
    [[nodiscard]] const path& outputBaseDir() const { return _outputDirPath; }
    [[nodiscard]] Printer& printer() const { return *_printer; }

    [[nodiscard]] output_config_t outputConfig(const path& filePathBase,
                                               const Model& model) const;

    [[nodiscard]] string mapImport(const string& importBaseName) const;
    [[nodiscard]] TypeUsage mapType(const string& swaggerType,
                                    const string& swaggerFormat = {},
                                    const string& baseName = {}) const;
    [[nodiscard]] string mapIdentifier(const string& baseName,
                                       const Identifier* scope,
                                       bool required) const;

private:
    Verbosity _verbosity;
    pair_vector_t<string> _substitutions;
    pair_vector_t<string> _identifiers;
    /// In JSON/YAML, the below looks like:
    /// <swaggerType>: { <swaggerFormat>: <TypeUsage>, ... }, ...
    pair_vector_t<pair_vector_t<TypeUsage>> _typesMap;
    /// Mapping of file extensions to mustache templates
    pair_vector_t<string> _dataTemplates, _apiTemplates;
    path _outputDirPath;
    std::unique_ptr<Printer> _printer;
};
