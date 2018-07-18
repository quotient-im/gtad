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

#include <algorithm>
#include <locale>

using namespace std;
using namespace std::placeholders;
namespace km = kainjow::mustache;
using km::object;
using km::partial;
using km::lambda2;
using km::renderer;

inline string safeString(const Printer::context_type& data, const string& key,
                         string defaultValue = {})
{
    if (auto value = data.get(key))
    {
        if (value->is_string())
            return value->string_value();
        if (value->is_partial())
            return value->partial_value()();
    }
    return defaultValue;
}

// While C++20 is not around

inline bool startsWith(const string& s, const string::value_type* ss)
{
    for (auto ps = s.begin(); ps != s.end() && ss; ++ps, ++ss);
    return !ss;
}

inline bool endsWith(const string& s, const string& ss)
{
    return equal(ss.rbegin(), ss.rend(), s.rbegin());
}

inline auto make_mustache(const std::string& tmpl)
{
    km::mustache mstch { tmpl };
    mstch.set_custom_escape([](string s) { return s; });
    return mstch;
}

Printer::Printer(context_type&& context, const vector<string>& templateFileNames,
                 const string& inputBasePath, string outputBasePath,
                 const string& outFilesListPath)
    : _context(context)
    , _typeRenderer(make_mustache("{{#scope}}" +
                                      safeString(context, "_scopeRenderer") +
                                  "{{/scope}}{{>name}}"))
    , _quoteChar(safeString(context, "_literalQuote", "\""))
    , _outputBasePath(std::move(outputBasePath))
{
    // Enriching the context with "My Mustache library"
    _context.set("@filePartial", lambda2 { // TODO: Switch to new Mustache's {{>}}
        [inputBasePath](const string& s, const renderer& render) {
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
        ifstream ifs { templateFilePath };
        if (!ifs.good())
            throw Exception(templateFilePath + ": Failed to open");

        string templateContents;
        if (!getline(ifs, templateContents, '\0')) // Won't work on files with NULs
            throw Exception(templateFilePath + ": Failed to read");

        _templates.emplace_back(withoutSuffix(templateFileName, ".mustache"),
                                make_mustache(templateContents));
    }
    if (!outFilesListPath.empty())
    {
        _outFilesList.open(_outputBasePath + outFilesListPath);
        if (!_outFilesList)
            cerr << "No out files list set or cannot write to the file" << endl;
    }
}

inline object wrap(object o)
{
    return o;
}

template <typename T>
inline object wrap(T val)
{
    return object {{ "_", move(val) }};
}

template <typename ObjT, typename ContT, typename FnT>
void setList(ObjT& target, const string& name, const ContT& source, FnT convert)
{
    if (source.empty())
        return; // Don't even bother to add the empty list

    target[name + '?'] = true;
    auto mList = km::list(source.size());
    auto mIt = mList.begin();
    for (auto it = source.begin(); it != source.end();)
    {
        *mIt = wrap(convert(*it));
        mIt->set("@join", ++it != source.end());
        mIt->set("hasMore", it != source.end()); // Swagger compatibility
        ++mIt;
    }
    target[name] = mList;
}

template <typename ObjT, typename ContT>
void setList(ObjT& target, const string& name, const ContT& source)
{
    setList(target, name, source,
        [](const typename ContT::value_type& v) { return v; });
}

template <typename ModelT>
void dumpDescription(object& target, const ModelT& model)
{
    setList(target, "description", km::split(model.description, '\n'));
}

object Printer::renderType(const TypeUsage& tu) const
{
    object values { { "name", partial {[name=tu.name] { return name; }} }
//                  , { "baseName", tu.baseName }
    };
    auto qualifiedValues = values;
    if (!tu.scope.empty())
    {
        qualifiedValues.emplace("scope", tu.scope);
        qualifiedValues.emplace("scopeCamelCase", camelCase(tu.scope));
    }

    // Fill parameters for parameterized types
    setList(values, "types", tu.innerTypes,
            bind(&Printer::renderType, this, _1));
    setList(qualifiedValues, "types", tu.innerTypes,
            bind(&Printer::renderType, this, _1));
    int i = 0;
    for (const auto& t: tu.innerTypes)
    {
        // Substituting {{1}}, {{2}} and so on with actual inner type names
        auto mInnerType = renderType(t);
        values.emplace(to_string(++i), mInnerType["name"]);
        qualifiedValues.emplace(to_string(i), mInnerType["qualifiedName"]);
    }

    return { { "name", _typeRenderer.render(values) }
           , { "qualifiedName", _typeRenderer.render(qualifiedValues) }
//           , { "baseName", tu.baseName }
    };
}

object Printer::dumpField(const VarDecl& field) const
{
    auto paramNameCamelCase = camelCase(field.name);
    paramNameCamelCase.front() =
        tolower(paramNameCamelCase.front(), locale::classic());

    object fieldDef { { "dataType",      renderType(field.type) }
                    , { "baseName",      field.baseName }
                    , { "paramName",     paramNameCamelCase } // Swagger compat
                    , { "nameCamelCase", paramNameCamelCase }
                      // TODO: nameSnakeCase
                    , { "required?",     field.required }
                    , { "required",      field.required } // Swagger compat
    };
    dumpDescription(fieldDef, field);
    if (!field.defaultValue.empty())
        fieldDef.emplace("defaultValue", field.defaultValue);

    for (const auto& attr: field.type.attributes)
        fieldDef.emplace(attr.first, partial {[v=attr.second] { return v; }});

    for (const auto& listAttr: field.type.lists)
    {
        km::list mAttrValue;
        for (const auto& i: listAttr.second)
            mAttrValue.emplace_back(i);
        fieldDef.emplace(listAttr.first, move(mAttrValue));
    }
    return fieldDef;
}

object Printer::dumpAllTypes(const Model::schemas_type& types) const
{
    object mModels;
    setList(mModels, "model", types,
        [this](const ObjectSchema& type)
        {
            auto mType = renderType(TypeUsage(type));
            mType["classname"] = type.name; // Swagger compat
            dumpDescription(mType, type);
            mType["in?"] = (type.inOut & In) != 0;
            mType["out?"] = (type.inOut & Out) != 0;
            if (type.trivial())
            {
                mType["trivial?"] = true;
                mType["parent"] = renderType(type.parentTypes.back());
            }
            setList(mType, "parents", type.parentTypes,
                    bind(&Printer::renderType, this, _1));
            setList(mType, "vars", type.fields,
                [this](const VarDecl& f) {
                    object fieldDef = dumpField(f);
                    fieldDef["name"] = f.name;
                    fieldDef["datatype"] = f.type.name; // Swagger compat
                    return fieldDef;
                });
            return mType;
        });
    return mModels;
}

object Printer::dumpTypes(const Model::schemas_type& types,
                          const string& scope) const
{
    Model::schemas_type selectedTypes;
    copy_if(types.begin(), types.end(), back_inserter(selectedTypes),
            [&](const ObjectSchema& s) { return s.scope == scope; });
    return dumpAllTypes(selectedTypes);
}

bool dumpContentTypes(object& target, const string& keyName, vector<string> types)
{
    const auto& cTypes = types;
    setList(target, keyName, cTypes);
    const bool hasNonJson =
            !all_of(cTypes.begin(), cTypes.end(), bind(endsWith, _1, "/json"));
    target.emplace(keyName + "NonJson?", hasNonJson);
    return hasNonJson;
}

vector<string> Printer::print(const Model& model) const
{
    auto context = _context;
    context.set("filenameBase", model.srcFilename);
    context.set("basePathWithoutHost", model.basePath);
    context.set("basePath", model.hostAddress + model.basePath);
    setList(context, "imports", model.imports);
    auto&& mAllTypes = dumpAllTypes(model.types);
    if (!mAllTypes.empty())
        context.set("allModels", mAllTypes);
    auto&& mTypes = dumpTypes(model.types, "");
    if (!mTypes.empty())
        context.set("models", mTypes);
    if (!model.callClasses.empty())
    {
        const auto& callClass = model.callClasses.back();
        bool globalConsumesNonJson = false, globalProducesNonJson = false;
        object mOperations; // Any attributes should be added after setList
        setList(mOperations, "operation", callClass.calls,
            [&] (const Call& call) {
                object mCall { { "operationId", call.name }
                             , { "camelCaseOperationId", camelCase(call.name) }
                             , { "httpMethod",  call.verb }
                             , { "path", call.path }
                             , { "summary",  call.summary }
                             , { "skipAuth", !call.needsSecurity }
                };
                dumpDescription(mCall, call);

                globalConsumesNonJson |=
                    dumpContentTypes(mCall, "consumes",
                                     call.consumedContentTypes);
                globalProducesNonJson |=
                    dumpContentTypes(mCall, "produces",
                                     call.producedContentTypes);
                mCall.emplace("producesImage?",
                        all_of(call.producedContentTypes.begin(),
                               call.producedContentTypes.end(),
                               bind(startsWith, _1, "image/")));

                auto&& mCallTypes = dumpTypes(model.types, call.name);
                if (!mCallTypes.empty())
                    mCall.emplace("models", mCallTypes);
                setList(mCall, "pathParts", call.path.parts,
                    [this, &call] (const Path::part_type& p) {
                        const string s { call.path, get<0>(p), get<1>(p) };
                        return get<2>(p) == Path::Variable ? s
                               : _quoteChar + s + _quoteChar;
                    });

                using namespace placeholders;
                setList(mCall, "allParams", call.collateParams(),
                        bind(&Printer::dumpField, this, _1));
                for (size_t i = 0; i < Call::paramsBlockNames.size(); ++i)
                    setList(mCall, Call::paramsBlockNames[i] + "Params",
                            call.allParams[i],
                            bind(&Printer::dumpField, this, _1));
                if (call.inlineBody)
                    mCall.emplace("inlineBody",
                                  dumpField(call.bodyParams().front()));

                setList(mCall, "responses", call.responses,
                    [this](const Response& r) {
                        object mResponse { { "code", r.code }
                                         , { "normalResponse?", r.code == "200" }
                        };
                        vector<VarDecl> allProperties;
                        copy(r.headers.begin(), r.headers.end(),
                             back_inserter(allProperties));
                        copy(r.properties.begin(), r.properties.end(),
                             back_inserter(allProperties));
                        setList(mResponse, "allProperties", allProperties,
                                bind(&Printer::dumpField, this, _1));
                        setList(mResponse, "properties", r.properties,
                                bind(&Printer::dumpField, this, _1));
                        setList(mResponse, "headers", r.headers,
                                bind(&Printer::dumpField, this, _1));
                        return mResponse;
                    });
                return mCall;
            });
        if (!mOperations.empty())
        {
            mOperations.emplace("classname", "NOT_IMPLEMENTED");
            mOperations.emplace("consumesNonJson?", globalConsumesNonJson);
            mOperations.emplace("producesNonJson?", globalProducesNonJson);
            context.set("operations", mOperations);
        }
    }
    vector<string> fileNames;
    for (auto fileTemplate: _templates)
    {
        ostringstream fileNameStr;
        fileNameStr << _outputBasePath << model.fileDir;
        fileTemplate.first.render({ "base", model.srcFilename }, fileNameStr);
        if (!fileTemplate.first.error_message().empty())
        {
            throw Exception("Incorrect filename template: " +
                            fileTemplate.first.error_message());
        }
        auto fileName = fileNameStr.str();
        cout << "Printing " << fileName << endl;

        ofstream ofs { fileName };
        if (!ofs.good())
            throw Exception(fileName + ": Couldn't open for writing");

        fileTemplate.second.render(context, ofs);
        if (fileTemplate.second.error_message().empty())
            _outFilesList << fileName << endl;
        else
            clog << fileName << ": "
                 << fileTemplate.second.error_message() << endl;
        fileNames.emplace_back(std::move(fileName));
    }
    return fileNames;
}
