#pragma once

#include "model.h"

#include "mustache/mustache.hpp"

#include <fstream>

class Printer
{
    public:
        using context_type = kainjow::mustache::data;
        using template_type = kainjow::mustache::mustache;
        using templates_type = std::vector<std::pair<template_type, template_type>>;
        using m_object_type = kainjow::mustache::object;

        Printer(context_type&& context,
                const std::vector<std::string>& templateFileNames,
                const std::string& inputBasePath, std::string outputBasePath,
                const std::string& outFilesListPath);
        Printer(Printer&& p) = default;

        Printer::template_type makeMustache(const std::string& tmpl,
                                            std::string setDelimiter = {}) const;
        std::vector<std::string> print(const Model& model) const;

    private:
        context_type _context;
        std::string _delimiter;
        mutable template_type _typeRenderer;
        std::string _leftQuote;
        std::string _rightQuote;
        templates_type _templates;
        std::string _outputBasePath;
        mutable std::ofstream _outFilesList;

        m_object_type renderType(const TypeUsage& tu) const;
        m_object_type dumpField(const VarDecl& field) const;
        m_object_type dumpAllTypes(const Model::schemas_type& types) const;
        m_object_type dumpTypes(const Model::schemas_type& types,
                                const std::string& scope) const;
};
