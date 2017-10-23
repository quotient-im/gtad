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

#include <algorithm>
#include <locale>

enum {
    CannotReadTemplateFile = PrinterCodes, CannotWriteToFile,
};

using namespace std;
using namespace kainjow::mustache;

Printer::Printer(context_type&& context, const vector<string>& templateFileNames,
                 const string& inputBasePath, string outputBasePath,
                 const string& outFilesListPath)
    : _context(context), _outputBasePath(std::move(outputBasePath))
{
    // Enriching the context with "My Mustache library"
    _context.set("@filePartial", lambda2 {
        [inputBasePath, this](const string& s, const renderer& render) {
            ifstream ifs { inputBasePath + s };
            if (!ifs.good())
            {
                ifs.open(inputBasePath + s + ".mustache");
                if (!ifs.good())
                {
                    cerr << "Failed to open file for a partial: "
                         << inputBasePath + s << endl;
                    // FIXME: Figure a better error reporting mechanism
                    return "/* Failed to open " + inputBasePath + s + " */";
                }
            }
            string embeddedTemplate;
            getline(ifs, embeddedTemplate, '\0'); // Won't work on files with NULs
            return render(embeddedTemplate, false);
        }
    });
    _context.set("@cap", lambda2 {
        [](const string& s, const renderer& render)
        {
            return capitalizedCopy(render(s, false));
        }
    });
    _context.set("@toupper", lambda2 {
        [](string s, const renderer& render) {
            s = render(s, false);
            transform(s.begin(), s.end(), s.begin(),
                      [] (char c) { return toupper(c, locale::classic()); });
            return s;
        }
    });
    _context.set("@tolower", lambda2 {
        [](string s, const renderer& render) {
            s = render(s, false);
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
    if (!outFilesListPath.empty())
    {
        cout << "Opening " << _outputBasePath << outFilesListPath << endl;
        _outFilesList.open(_outputBasePath + outFilesListPath);
        if (!_outFilesList)
            cerr << "No out files list set or cannot write to the file" << endl;
    }
}

template <typename ObjT>
inline void setList(ObjT* object, const string& name, list&& list)
{
    (*object)[name + '?'] = !list.empty();
    if (!list.empty())
    {
        using namespace placeholders;
        for_each(list.begin(), list.end() - 1,
                 bind(&data::set, _1, "hasMore", true));
        list.back().set("last?", true);
    }
    (*object)[name] = list;
}

string renderType(const TypeUsage& tu)
{
    if (tu.innerTypes.empty())
        return tu.name;

    // Template type
    mustache m { tu.name };
    object mInnerTypes;
    int i = 0;
    for (const auto& t: tu.innerTypes)
        mInnerTypes.emplace(to_string(++i), renderType(t)); // {{1}}, {{2}} and so on

    return m.render(mInnerTypes);
}

void dumpFieldAttrs(const VarDecl& param, object& fieldDef)
{
    fieldDef["required?"] = param.required;
    fieldDef["required"] = param.required; // Swagger compatibility
    fieldDef["defaultValue"] = param.defaultValue;
    for (const auto& attr: param.type.attributes)
        fieldDef.emplace(attr);

    for (const auto& listAttr: param.type.lists)
    {
        list mAttrValue;
        for (const auto& i: listAttr.second)
            mAttrValue.emplace_back(i);
        fieldDef.emplace(listAttr.first, move(mAttrValue));
    }
}

vector<string> Printer::print(const Model& model) const
{
    auto context = _context;
    context.set("filenameBase", model.filename);
    {
        // Imports
        list mImports;
        for (const auto& import: model.imports)
            mImports.emplace_back(import);
        setList(&context, "imports", std::move(mImports));
    }
    {
        // Data definitions
        list mTypes;
        for (const auto& type: model.types)
        {
            object mType { { "classname", type.first } };
            {
                list mParents;
                for (const auto& t: type.second.parentTypes)
                    mParents.emplace_back(renderType(t));
                setList(&mType, "parents", move(mParents));
            }
            {
                list mFields;
                for (const auto& f: type.second.fields)
                {
                    object fieldDef { { "name",     f.name },
                                      { "datatype", renderType(f.type) } };
                    dumpFieldAttrs(f, fieldDef);
                    mFields.emplace_back(move(fieldDef));
                }
                setList(&mType, "vars", move(mFields));
            }
            mTypes.emplace_back(object { { "model", move(mType) } });
        }
        if (!mTypes.empty())
            context.set("models", mTypes);
    }
    {
        // Operations
        list mClasses;
        for (const auto& callClass: model.callClasses)
        {
            for (const auto& call: callClass.callOverloads)
            {
                object mClass { { "operationId", call.name }
                              , { "camelCaseOperationId", camelCase(call.name) }
                              , { "httpMethod",  call.verb }
                              , { "path", call.path }
                              , { "skipAuth", !call.needsSecurity }
                };
                list mPathParts;
                for (const auto& pp: call.pathParts)
                    mPathParts.emplace_back(object { { "part", pp } });
                setList(&mClass, "pathParts", move(mPathParts));

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
                        object mParam { { "dataType", renderType(param.type) }
                                      , { "baseName", param.name }
                                      , { "paramName", param.name }
                        };
                        dumpFieldAttrs(param, mParam);
                        mParams.emplace_back(move(mParam));
                    }
                    setList(&mClass, pp.first, move(mParams));
                }
                {
                    list mResponses;
                    for (const auto& response: call.responses)
                    {
                        object mResponse { { "code", response.code }
                                         , { "normalResponse?",
                                                response.code == "200" }
                        };
                        list mProperties;
                        for (const auto& p: response.properties)
                        {
                            object mProperty { { "dataType", renderType(p.type) }
                                             , { "paramName", p.name }
                            };
                            dumpFieldAttrs(p, mProperty);
                            mProperties.emplace_back(move(mProperty));
                        }
                        setList(&mResponse, "properties", move(mProperties));
                        mResponses.emplace_back(move(mResponse));
                    }
                    setList(&mClass, "responses", move(mResponses));
                }
                mClasses.emplace_back(move(mClass));
            }
        }
        if (!mClasses.empty())
            context.set("operations",
                        object { { "className", "NOT_IMPLEMENTED" }
                               , { "operation", mClasses }
                        });
        context.set("basePathWithoutHost", model.basePath);
        context.set("basePath", model.hostAddress + model.basePath);
    }
    vector<string> fileNames;
    for (auto fileTemplate: _templates)
    {
        ostringstream fileNameStr;
        fileNameStr << _outputBasePath << model.fileDir;
        fileTemplate.first.render({ "base", model.filename }, fileNameStr);
        if (!fileTemplate.first.error_message().empty())
        {
            cerr << "When rendering the filename:" << endl
                 << fileTemplate.first.error_message() << endl;
            continue; // FIXME: should be fail()
        }
        auto fileName = fileNameStr.str();
        cout << "Printing " << fileName << endl;

        ofstream ofs { fileName };
        if (!ofs.good())
        {
            cerr << "Couldn't open " << fileName << " for writing" << endl;
            fail(CannotWriteToFile);
        }
        fileTemplate.second.set_custom_escape([](const string& s) { return s; });
        fileTemplate.second.render(context, ofs);
        if (fileTemplate.second.error_message().empty())
            _outFilesList << fileName << endl;
        else
            cerr << "When rendering the file:" << endl
                 << fileTemplate.second.error_message() << endl;
        fileNames.emplace_back(std::move(fileName));
    }
    return fileNames;
}
