#pragma once

#include "model.h"

#include "mustache/mustache.hpp"

#include <fstream>

class Printer {
public:
    using context_type = kainjow::mustache::data;
    using template_type = kainjow::mustache::mustache;
    using templates_type = std::vector<std::pair<template_type, template_type>>;
    using m_object_type = kainjow::mustache::object;
    using string = std::string;
    template <typename T> using vector = std::vector<T>;

    Printer(context_type&& contextData, const vector<string>& templateFileNames,
            string inputBasePath, string outputBasePath,
            const string& outFilesListPath);
    Printer(Printer&& p) = default;

    static Printer::template_type makeMustache(const string& tmpl,
                                               string setDelimiter = {});
    vector<string> print(const Model& model) const;

private:
    context_type _contextData;
    string _delimiter;
    mutable template_type _typeRenderer;
    string _leftQuote;
    string _rightQuote;
    templates_type _templates;
    string _inputBasePath;
    string _outputBasePath;
    mutable std::ofstream _outFilesList;

    [[nodiscard]] m_object_type renderType(const TypeUsage& tu) const;
    [[nodiscard]] m_object_type dumpField(const VarDecl& field) const;
    void addList(m_object_type& target, const string& name,
                 const VarDecls& properties) const;
    [[nodiscard]] m_object_type dumpAllTypes(const Model::schemas_type& types) const;
    [[nodiscard]] m_object_type dumpTypes(const Model::schemas_type& types,
                                          const string& scope) const;
};
