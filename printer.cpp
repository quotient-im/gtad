/******************************************************************************
 * Copyright (C) 2016-2017 Kitsune Ral <kitsune-ral@users.sf.net>
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

#include "printer.h"

#include "exception.h"

#include <fstream>
#include <algorithm>
#include <locale>

enum {
    CannotReadTemplateFile = PrinterCodes, CannotWriteToFile,
};

using namespace std;
using namespace kainjow::mustache;

Printer::Printer(context_type&& context, const vector<string>& templateFileNames,
                 const string& inputBasePath)
    : _context(context)
{
    // Enriching the context with "My Mustache library"
    _context.set("@filePartial", lambda3 {
        [inputBasePath, this](const string& s, const renderer2& render) {
            ifstream ifs { inputBasePath + s };
            if (!ifs.good())
            {
                cerr << "Failed to open file for a partial: "
                     << inputBasePath + s << endl;
                return "/* Failed to open " + inputBasePath + s + " */";
            }
            string embeddedTemplate;
            getline(ifs, embeddedTemplate, '\0'); // Won't work on files with NULs
            return render(embeddedTemplate, false);
        }
    });
    _context.set("@cap", lambda2 {
        [](const string& s, const basic_renderer<string>& render) {
            return capitalizedCopy(render(s));
        }
    });
    _context.set("@toupper", lambda2 {
        [](string s, const basic_renderer<string>& render) {
            s = render(s);
            transform(s.begin(), s.end(), s.begin(),
                      [] (char c) { return toupper(c, locale::classic()); });
            return s;
        }
    });
    _context.set("@tolower", lambda2 {
        [](string s, const basic_renderer<string>& render) {
            s = render(s);
            transform(s.begin(), s.end(), s.begin(),
                      [] (char c) { return tolower(c, locale::classic()); });
            return s;
        }
    });
    for (const auto& templateFileName: templateFileNames)
    {
        auto templateFilePath = inputBasePath + templateFileName;
        cout << "Opening " << templateFilePath << endl;
        ifstream ifs { templateFilePath };
        if (!ifs.good())
        {
            cerr << "Failed to open " << templateFilePath << std::endl;
            fail(CannotReadTemplateFile);
        }
        string templateContents;
        if (!getline(ifs, templateContents, '\0')) // Won't work on files with NULs
        {
            cerr << "Failed to read from " << templateFilePath << std::endl;
            fail(CannotReadTemplateFile);
        }
        mustache fileTemplate { templateContents };
        fileTemplate.set_custom_escape([](const string& s) { return s; });
        _templates.emplace_back(dropSuffix(templateFileName, ".mustache"),
                                std::move(fileTemplate));
    }
}

template <typename ObjT>
inline void setList(ObjT* object, const string& name, list&& list)
{
    if (list.empty())
        (*object)[name + '?'] = true;
    else
    {
        using namespace placeholders;
        (*object)[name + '?'] = false;
        for_each(list.begin(), list.end() - 1,
                 bind(&data::set, _1, "hasMore", true));
    }
    (*object)[name] = list;
}

void Printer::print(const Model& model, const string& outputBasePath) const
{
    auto context = _context;
    context.set("filenameBase", model.filename);
    {
        list mImports;
        for (const auto& import: model.imports)
            mImports.emplace_back(import);
        setList(&context, "imports", std::move(mImports));
    }
    {
        list mClasses;
        for (const auto& callClass: model.callClasses)
        {
            for (const auto& call: callClass.callOverloads)
            {
                object mClass { { "operationId", callClass.operationId }
                              , { "httpMethod",  call.verb }
                              , { "path", call.path }
                              };
                for (const auto& pp: {
                    make_pair("pathParams", call.pathParams),
                    make_pair("queryParams", call.queryParams),
                    make_pair("headerParams", call.headerParams),
                    make_pair("bodyParams", call.bodyParams),
                    make_pair("allParams", call.collateParams())
                })
                {
                    list mParams;
                    for (const auto& param: pp.second)
                    {
                        object&& mParam { { "dataType", param.type }
                                        , { "baseName", param.name }
                                        , { "paramName", param.name } };
                        mParams.emplace_back(mParam);
                    }
                    setList(&mClass, pp.first, move(mParams));
                }
                mClasses.emplace_back(move(mClass));
            }
        }
        context.set("operations",
            object { { "className", "!!!TODO:undefined!!!" },
                     { "operation", mClasses } }
        );
    }
    for (auto fileTemplate: _templates)
    {
        ostringstream fileNameStr;
        fileNameStr << outputBasePath << model.fileDir;
        fileTemplate.first.render({ "base", model.filename }, fileNameStr);
        if (!fileTemplate.first.error_message().empty())
            cerr << "When rendering the filename:" << endl
                 << fileTemplate.first.error_message() << endl;
        const auto fileName = fileNameStr.str();
        cout << "Printing " << fileName << endl;

        ofstream ofs { fileName };
        if (!ofs.good())
        {
            cerr << "Couldn't open " << fileName << " for writing" << endl;
            fail(CannotWriteToFile);
        }
        fileTemplate.second.set_custom_escape([](const string& s) { return s; });
        fileTemplate.second.render(context, ofs);
        if (!fileTemplate.second.error_message().empty())
            cerr << "When rendering the file:" << endl
                 << fileTemplate.second.error_message() << endl;
    }
}
