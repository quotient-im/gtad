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

        std::vector<std::string> print(const Model& model) const;

    private:
        context_type _context;
        mutable template_type _typeRenderer;
        std::string _quoteChar;
        templates_type _templates;
        std::string _outputBasePath;
        mutable std::ofstream _outFilesList;

        m_object_type renderType(const TypeUsage& tu) const;
        m_object_type dumpField(const VarDecl& field) const;
        m_object_type dumpAllTypes(const Model::schemas_type& types) const;
        m_object_type dumpTypes(const Model::schemas_type& types,
                                const std::string& scope) const;
};
