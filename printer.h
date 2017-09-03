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

        Printer(context_type&& context,
                const std::vector<std::string>& templateFileNames,
                const std::string& inputBasePath, std::string outputBasePath,
                const std::string& outFilesListPath);
        Printer(Printer&& p) = default;

        void print(const Model& model) const;

    private:
        context_type _context;
        templates_type _templates;
        std::string _outputBasePath;
        mutable std::ofstream _outFilesList;
};
