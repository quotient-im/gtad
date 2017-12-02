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

template <typename ObjT, typename ContT, typename FnT>
void setList(ObjT* object, const string& name, const ContT& source,
             FnT convert)
{
    if (source.empty())
        return; // Don't even bother to add the empty list

    (*object)[name + '?'] = true;
    auto mList = list(source.size());
    auto mIt = mList.begin();
    auto it = source.begin();
    while (it != source.end())
    {
        *mIt = convert(*it);
        mIt->set("hasMore", ++it != source.end()); // Swagger compatibility
        ++mIt;
    }
    mList.back().set("last?", true);
    (*object)[name] = mList;

}

template <typename ObjT, typename ContT>
void setList(ObjT* object, const string& name, const ContT& source)
{
    setList(object, name, source,
            [](const typename ContT::value_type& v) { return v; });
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

object prepareField(const VarDecl& field)
{
    auto paramNameCamelCase = camelCase(field.name);
    paramNameCamelCase.front() =
        tolower(paramNameCamelCase.front(), locale::classic());

    auto dataType = renderType(field.type);
    object fieldDef { { "dataType",           dataType }
                    , { "baseName",           field.name }
                    , { "paramName",          paramNameCamelCase } // Swagger compat
                    , { "paramNameCamelCase", paramNameCamelCase }
                      // TODO: paramNameSnakeCase
                    , { "required?",          field.required }
                    , { "required",           field.required } // Swagger compatibility
                    , { "defaultValue",       field.defaultValue }
    };

    for (const auto& attr: field.type.attributes)
        fieldDef.emplace(attr);

    for (const auto& listAttr: field.type.lists)
    {
        list mAttrValue;
        for (const auto& i: listAttr.second)
            mAttrValue.emplace_back(i);
        fieldDef.emplace(listAttr.first, move(mAttrValue));
    }
    return fieldDef;
}

vector<string> Printer::print(const Model& model) const
{
    auto context = _context;
    context.set("filenameBase", model.filename);
    setList(&context, "imports", model.imports);
    {
        // Data definitions
        list mTypes;
        for (const auto& type: model.types)
        {
            object mType { { "classname", type.first } };
            setList(&mType, "parents", type.second.parentTypes, renderType);
            setList(&mType, "vars", type.second.fields,
                [](const VarDecl& f) {
                    object fieldDef = prepareField(f);
                    fieldDef["name"] = f.name;
                    fieldDef["datatype"] = renderType(f.type); // Swagger compat
                    return fieldDef;
                });
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
                setList(&mClass, "pathParts", call.pathParts);

                for (string blockName: Call::paramsBlockNames)
                    setList(&mClass, blockName + "Params",
                            call.getParamsBlock(blockName), prepareField);

                setList(&mClass, "allParams", call.collateParams(), prepareField);
                setList(&mClass, "responses", call.responses,
                    [](const Response& r) {
                        object mResponse { { "code", r.code }
                                         , { "normalResponse?", r.code == "200" }
                        };
                        setList(&mResponse, "properties",
                                r.properties, prepareField);
                        return mResponse;
                    });
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
