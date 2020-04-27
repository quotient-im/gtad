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

inline string safeString(const Printer::context_type& data, const string& key,
                         string defaultValue = {})
{
    if (const auto* value = data.get(key))
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

Printer::template_type Printer::makeMustache(const string& tmpl) const
{
    km::mustache mstch{
        _delimiter.empty() ? tmpl : "{{=" + _delimiter + "=}}" + tmpl};
    mstch.set_custom_escape([](string s) { return s; });
    return mstch;
}

class GtadContext : public km::context<string>
{
    public:
        using data = km::data;

        GtadContext(string inputBasePath, const data* d)
            : context(d), inputBasePath(std::move(inputBasePath))
        { }

        const data* get_partial(const string& name) const override
        {
            if (auto result = context::get_partial(name))
                return result;

            auto it = filePartialsCache.find(name);
            if (it != filePartialsCache.end())
                return &it->second;

            auto srcFileName = inputBasePath + name;
            ifstream ifs { srcFileName };
            if (!ifs.good())
            {
                srcFileName += ".mustache";
                ifs.open(srcFileName);
                if (!ifs.good())
                    cerr << "Failed to open file for a partial: "
                         << inputBasePath + name << endl;
            }

            string fileContents;
            if (ifs.good())
                getline(ifs, fileContents, '\0'); // Won't work on files with NULs
            else
                fileContents =
                    "{{_comment}} Failed to open " + inputBasePath + name + "\n";
            return &filePartialsCache.insert(
                        make_pair(name,
                            partial([fileContents] { return fileContents; })))
                    .first->second;
        }

    private:
        string inputBasePath;
        mutable unordered_map<string, data> filePartialsCache;
};

Printer::Printer(context_type&& contextData,
                 const vector<string>& templateFileNames,
                 string inputBasePath, string outputBasePath,
                 const string& outFilesListPath)
    : _contextData(std::move(contextData))
    , _delimiter(safeString(_contextData, "_delimiter"))
    , _typeRenderer(makeMustache(
          safeString(_contextData, "_typeRenderer", "{{>name}}")))
    , _leftQuote(safeString(_contextData, "_leftQuote",
                            safeString(_contextData, "_quote", "\"")))
    , _rightQuote(safeString(_contextData, "_rightQuote",
                             safeString(_contextData, "_quote", "\"")))
    , _inputBasePath(std::move(inputBasePath))
    , _outputBasePath(std::move(outputBasePath))
    // _outFilesList is initialised further below
{
    using km::lambda2;
    using km::renderer;
    // Enriching the context with "My Mustache library"
    _contextData.set("_cap", lambda2 {
        [](const string& s, const renderer& render)
        {
            return capitalizedCopy(render(s, false));
        }
    });
    _contextData.set("_toupper", lambda2 {
        [](string s, const renderer& render) {
            s = render(s, false);
            transform(s.begin(), s.end(), s.begin(),
                      [] (char c) { return toupper(c, locale::classic()); });
            return s;
        }
    });
    _contextData.set("_tolower", lambda2 {
        [](string s, const renderer& render) {
            s = render(s, false);
            transform(s.begin(), s.end(), s.begin(),
                      [] (char c) { return tolower(c, locale::classic()); });
            return s;
        }
    });
    for (const auto& templateFileName: templateFileNames)
    {
        auto templateFilePath = _inputBasePath + templateFileName;
        ifstream ifs { templateFilePath };
        if (!ifs.good())
            throw Exception(templateFilePath + ": Failed to open");

        string templateContents;
        if (!getline(ifs, templateContents, '\0')) // Won't work on files with NULs
            throw Exception(templateFilePath + ": Failed to read");

        _templates.emplace_back(
            makeMustache(withoutSuffix(templateFileName, ".mustache")),
            makeMustache(templateContents));
    }
    if (!outFilesListPath.empty())
    {
        _outFilesList.open(_outputBasePath + outFilesListPath);
        if (!_outFilesList)
            clog << "No out files list set or cannot write to the file" << endl;
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
        mIt->set("_join", ++it != source.end());
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
    // This method first produces two contexts: one to render a non-qualified
    // name (in `values`), the other to do a qualified name
    // (in `qualifiedValues`). These contexts are filled in, in particular,
    // with pre-rendered (non-qualified and qualifed, respectively)
    // inner type names in {{1}}, {{2}} etc. Then, using these two contexts,
    // a Mustache object for the current type is made, fully rendering
    // the bare type name in `name` and the qualified name in `qualifiedName`.
    object values { { "name", partial {[name=tu.name] { return name; }} }
                  , { "baseName", tu.baseName }
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
           , { "baseName", tu.baseName }
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

void Printer::addList(object& target, const string& name,
                      const VarDecls& properties) const
{
    setList(target, name, properties, bind(&Printer::dumpField, this, _1));
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
            if (!type.propertyMap.type.empty())
                mType["propertyMap"] = dumpField(type.propertyMap);
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
    setList(target, keyName, types);
    const bool hasNonJson =
            !all_of(types.begin(), types.end(), bind(endsWith, _1, "/json"));
    target.emplace(keyName + "NonJson?", hasNonJson);
    return hasNonJson;
}

vector<string> Printer::print(const Model& model) const
{
    if (model.empty()) {
        clog << "Empty model, no files will be emitted" << endl;
        return {};
    }

    auto contextData = _contextData;
    contextData.set("filenameBase", model.srcFilename);
    contextData.set("basePathWithoutHost", model.basePath);
    contextData.set("basePath", model.hostAddress + model.basePath);
    setList(contextData, "imports", model.imports);
    auto&& mAllTypes = dumpAllTypes(model.types);
    if (!mAllTypes.empty())
        contextData.set("allModels", mAllTypes);
    auto&& mTypes = dumpTypes(model.types, "");
    if (!mTypes.empty())
        contextData.set("models", mTypes);

    if (!model.callClasses.empty()) {
        const auto& callClass = model.callClasses.back();
        bool globalConsumesNonJson = false, globalProducesNonJson = false;
        object mOperations; // Any attributes should be added after setList
        setList(mOperations, "operation", callClass.calls, [&](const Call& call) {
            // clang-format off
            object mCall { { "operationId", call.name }
                         , { "camelCaseOperationId", camelCase(call.name) }
                         , { "httpMethod", call.verb }
                         , { "path", call.path }
                         , { "summary", call.summary }
                         , { "skipAuth", !call.needsSecurity } };
            // clang-format on
            dumpDescription(mCall, call);

            globalConsumesNonJson |=
                dumpContentTypes(mCall, "consumes", call.consumedContentTypes);
            globalProducesNonJson |=
                dumpContentTypes(mCall, "produces", call.producedContentTypes);
            mCall.emplace("producesImage?",
                        all_of(call.producedContentTypes.begin(),
                               call.producedContentTypes.end(),
                               bind(startsWith, _1, "image/")));

            auto&& mCallTypes = dumpTypes(model.types, call.name);
            if (!mCallTypes.empty())
                mCall.emplace("models", mCallTypes);
            setList(mCall, "pathParts", call.path.parts,
                    [this, &call](const Path::part_type& p) {
                        const string s{call.path, get<0>(p), get<1>(p)};
                        return get<2>(p) == Path::Variable ? s
                               : _leftQuote + s + _rightQuote;
                    });

            using namespace placeholders;
            addList(mCall, "allParams", call.collateParams());
            for (size_t i = 0; i < Call::ParamGroups.size(); ++i)
                addList(mCall, Call::ParamGroups[i] + "Params",
                        call.params[i]);
            if (call.inlineBody)
                mCall.emplace("inlineBody",
                              dumpField(call.params[InBody].front()));

            setList(mCall, "responses", call.responses, [this](const Response& r) {
                object mResponse{{"code", r.code},
                                 {"normalResponse?", r.code == "200"}};
                vector<VarDecl> allProperties;
                for (const auto& src: {r.headers, r.properties})
                    copy(src.begin(), src.end(), back_inserter(allProperties));

                for (const auto& src: {{"allProperties", allProperties},
                                       {"properties", r.properties},
                                       pair{"headers", r.headers}})
                    addList(mResponse, src.first, src.second);

                return mResponse;
            });
            if (call.inlineResponse && !call.responses.empty()
                && !call.responses.front().properties.empty())
                mCall.emplace("inlineResponse",
                              dumpField(
                                  call.responses.front().properties.front()));

            return mCall;
        });

        if (!mOperations.empty()) {
            mOperations.emplace("classname", "NOT_IMPLEMENTED");
            mOperations.emplace("consumesNonJson?", globalConsumesNonJson);
            mOperations.emplace("producesNonJson?", globalProducesNonJson);
            contextData.set("operations", mOperations);
        }
    }
    vector<string> fileNames;
    for (auto fileTemplate: _templates)
    {
        auto fileName = _outputBasePath;
        fileName.append(model.fileDir)
            .append(fileTemplate.first.render({ "base", model.srcFilename }));
        if (!fileTemplate.first.error_message().empty())
        {
            throw Exception("Incorrect filename template: " +
                            fileTemplate.first.error_message());
        }
        cout << "Printing " << fileName << endl;

        ofstream ofs { fileName };
        if (!ofs.good())
            throw Exception(fileName + ": Couldn't open for writing");

        GtadContext c { _inputBasePath, &contextData };
        fileTemplate.second.render(c, ofs);
        if (fileTemplate.second.error_message().empty())
            _outFilesList << fileName << endl;
        else
            clog << fileName << ": "
                 << fileTemplate.second.error_message() << endl;
        fileNames.emplace_back(std::move(fileName));
    }
    return fileNames;
}
