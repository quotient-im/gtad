#pragma once

#include "model.h"

#include "mustache/mustache.hpp"

#include <filesystem>
#include <fstream>

class Translator;

kainjow::mustache::partial makePartial(std::string s,
                                       const std::string& delimiter);

class Printer {
public:
    using context_type = kainjow::mustache::object;
    using template_type = kainjow::mustache::mustache;
    using templates_type = std::vector<std::pair<template_type, template_type>>;
    using m_object_type = kainjow::mustache::object;
    using string = std::string;
    using fspath = std::filesystem::path;

    Printer(context_type&& contextObj, fspath inputBasePath,
            const fspath& outFilesListPath, string delimiter,
            const Translator& translator);
    Printer(Printer&& p) = default;

    Printer::template_type makeMustache(const string& tmpl) const;
    std::vector<std::string> print(const fspath& filePathBase,
                                   const Model& model) const;

private:
    const Translator& _translator;
    kainjow::mustache::data _contextData;
    string _delimiter;
    template_type _typeRenderer;
    string _leftQuote;
    string _rightQuote;
    fspath _inputBasePath;
    mutable std::ofstream _outFilesList;

    [[nodiscard]] m_object_type renderType(const TypeUsage& tu) const;
    [[nodiscard]] m_object_type dumpField(const VarDecl& field) const;
    void addList(m_object_type& target, const string& name,
                 const VarDecls& properties) const;
    [[nodiscard]] m_object_type dumpAllTypes(const Model::schemas_type& types) const;
    [[nodiscard]] m_object_type dumpTypes(const Model::schemas_type& types,
                                          const Call* scope = {}) const;
};
