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

#include "translator.h"

#include <algorithm>
#include <locale>
#include <regex>

using namespace std;
using namespace std::placeholders;
namespace km = kainjow::mustache;
using km::object;
using km::partial;

inline string safeString(const Printer::context_type& contextObj,
                         const string& key, string defaultValue = {})
{
    if (const auto it = contextObj.find(key); it != contextObj.end()) {
        const auto& value = it->second;
        if (value.is_string())
            return value.string_value();
        if (value.is_partial())
            return value.partial_value()();
    }
    return defaultValue;
}

// While C++20 is not around

inline bool startsWith(const string_view s, const string_view ss)
{
    return s.size() >= ss.size() && equal (ss.begin(), ss.end(), s.begin());
}

inline bool endsWith(const string_view s, const string_view ss)
{
    return s.size() >= ss.size() && equal(ss.rbegin(), ss.rend(), s.rbegin());
}

inline Printer::template_type Printer::makeMustache(const string& tmpl) const
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

        GtadContext(Printer::fspath inputBasePath, const data* d)
            : context(d), inputBasePath(move(inputBasePath))
        {}

        const data* get_partial(const string& name) const override
        {
            if (const auto* result = context::get_partial(name))
                return result;

            auto it = filePartialsCache.find(name);
            if (it != filePartialsCache.end())
                return &it->second;

            auto srcFileName = inputBasePath / name;
            ifstream ifs { srcFileName };
            if (!ifs.good())
            {
                srcFileName += ".mustache";
                ifs.open(srcFileName);
                if (!ifs.good())
                    throw Exception("Failed to open file for a partial " + name
                                    + ", tried "
                                    + (inputBasePath / name).string() + " and "
                                    + srcFileName.string());
            }

            string fileContents;
            getline(ifs, fileContents, '\0'); // Won't work on files with NULs
            return &filePartialsCache.insert(
                        make_pair(name,
                            partial([fileContents] { return fileContents; })))
                    .first->second;
        }

    private:
        Printer::fspath inputBasePath;
        mutable unordered_map<string, data> filePartialsCache;
};

template <typename StringT>
class ContextOverlay {
public:
    ContextOverlay(km::context<StringT>& context,
                   const km::basic_object<StringT>& overlayObj)
        : _context(context)
        , overlay(overlayObj)
    {
        _context.push(&overlay);
    }
    ContextOverlay(const ContextOverlay&) = delete;
    ContextOverlay(ContextOverlay&&) = delete;
    void operator=(const ContextOverlay&) = delete;
    void operator=(ContextOverlay&&) = delete;
    ~ContextOverlay()
    {
        _context.pop();
    }

private:
    km::context<StringT>& _context;
    km::basic_data<StringT> overlay;
};
template <typename StringT>
ContextOverlay(km::context<StringT>& context,
               const km::basic_object<StringT>& overlayObj)
    -> ContextOverlay<StringT>;

template <typename StringT>
inline auto renderWithOverlay(const km::basic_mustache<StringT>& tmpl,
                              km::context<StringT>& ctx,
                              const km::basic_object<StringT>& overlay)
{
    ContextOverlay ctxOverlay(ctx, overlay);
    return tmpl.render(ctx);
}

Printer::Printer(context_type&& contextObj, fspath inputBasePath,
                 const fspath& outFilesListPath, string delimiter,
                 const Translator& translator)
    : _translator(translator)
    , _contextData(contextObj)
    , _delimiter(move(delimiter))
    , _typeRenderer(
          makeMustache(safeString(contextObj, "_typeRenderer", "{{>name}}")))
    , _leftQuote(safeString(contextObj, "_leftQuote",
                            safeString(contextObj, "_quote", "\"")))
    , _rightQuote(safeString(contextObj, "_rightQuote",
                             safeString(contextObj, "_quote", "\"")))
    , _inputBasePath(move(inputBasePath))
{
    using km::lambda2;
    using km::renderer;
    // Enriching the context with "My Mustache library"
    contextObj.emplace("_cap", lambda2 {
        [](const string& s, renderer render)
        {
            return capitalizedCopy(render(s, false));
        }
    });
    contextObj.emplace("_toupper", lambda2 {
        [](string s, renderer render) {
            s = render(s, false);
            transform(s.begin(), s.end(), s.begin(),
                      [] (char c) { return toupper(c, locale::classic()); });
            return s;
        }
    });
    contextObj.emplace("_tolower", lambda2 {
        [](string s, renderer render) {
            s = render(s, false);
            transform(s.begin(), s.end(), s.begin(),
                      [] (char c) { return tolower(c, locale::classic()); });
            return s;
        }
    });
    if (!outFilesListPath.empty())
    {
        _outFilesList.open(_translator.outputBaseDir() / outFilesListPath);
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

inline object wrap(const filesystem::path& p)
{
    return wrap(p.string());
}

template <typename ObjT, typename ContT, typename FnT>
void setList(ObjT& target, const string& name, const ContT& source, FnT convert)
{
    if (source.empty())
        return; // Don't even bother to add the empty list

    target[name + '?'] = true;
    km::list mList;
    auto it = source.begin();
    for (bool hasMore = it != source.end(); hasMore;) {
        auto elementObj = wrap(convert(*it));
        hasMore = ++it != source.end();
        elementObj.emplace("_join", hasMore);
        elementObj.emplace("hasMore", hasMore); // Swagger compatibility
        mList.emplace_back(move(elementObj));
    }
    target[name] = mList;
}

template <typename ObjT, typename ContT>
void setList(ObjT& target, const string& name, const ContT& source)
{
    setList(target, name, source, [](auto element) { return element; });
}

template <typename ModelT>
void dumpDescription(object& target, const ModelT& model)
{
    vector<string> lines{};
    if (!model.description.empty()) {
        regex re{"\\n"};
        lines = {sregex_token_iterator{model.description.cbegin(),
                                       model.description.cend(), re, -1},
                 sregex_token_iterator{}};
    }
    setList(target, "description", lines);
}

object Printer::renderType(const TypeUsage& tu) const
{
    // This method first produces two contexts: one to render a non-qualified
    // name (in `values`), the other to do a qualified name
    // (in `qualifiedValues`). These contexts are filled in, in particular,
    // with pre-rendered (non-qualified and qualified, respectively)
    // inner type names in {{1}}, {{2}} etc. Then, using these two contexts,
    // a Mustache object for the current type is made, fully rendering
    // the bare type name in `name` and the qualified name in `qualifiedName`.
    object values { { "name", partial {[name=tu.name] { return name; }} }
                  , { "baseName", tu.baseName }
    };
    auto qualifiedValues = values;
    if (tu.call)
    {
        // Not using call->qualifiedName() because:
        // 1) we don't have nested calls as a thing
        // 2) we qualify types with call names, not calls (think of referring
        //    to another type within the same call)
        qualifiedValues.emplace("scope", tu.call->name);
        qualifiedValues.emplace("scopeCamelCase", camelCase(tu.call->name));
    }

    // Fill parameters for parameterized types
    setList(values, "types", tu.paramTypes,
            bind(&Printer::renderType, this, _1));
    setList(qualifiedValues, "types", tu.paramTypes,
            bind(&Printer::renderType, this, _1));
    int i = 0;
    for (const auto& t: tu.paramTypes)
    {
        // Substituting {{1}}, {{2}} and so on with actual inner type names
        auto mParamType = renderType(t);
        values.emplace(to_string(++i), mParamType["name"]);
        qualifiedValues.emplace(to_string(i), mParamType["qualifiedName"]);
    }

    GtadContext context {_inputBasePath, &_contextData};
    return {{"name", renderWithOverlay(_typeRenderer, context, values)}
           ,{"qualifiedName",
             renderWithOverlay(_typeRenderer, context, qualifiedValues)}
           ,{"baseName", tu.baseName}
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
            mType["in?"] = type.role != OnlyOut;
            mType["out?"] = type.role != OnlyIn;
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
            if (type.hasPropertyMap())
                mType["propertyMap"] = dumpField(type.propertyMap);
            return mType;
        });
    return mModels;
}

object Printer::dumpTypes(const Model::schemas_type& types,
                          const Call* scope) const
{
    Model::schemas_type selectedTypes;
    copy_if(types.begin(), types.end(), back_inserter(selectedTypes),
            [&](const ObjectSchema& s) { return s.call == scope; });
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

void Printer::print(const fspath& filePathBase, const Model& model) const
{
    if (model.empty()) {
        clog << "Empty model, no files will be emitted" << endl;
        return;
    }

    GtadContext context{_inputBasePath, &_contextData};

    object payloadObj {{"filenameBase", filePathBase.filename().string()}
                      ,{"basePathWithoutHost", model.basePath}
                      ,{"basePath", model.hostAddress + model.basePath}
                      };
    setList(payloadObj, "imports", model.imports,
            [this, &context](const pair<string, string>& import) -> string {
                if (import.first.empty() || import.second.empty()) {
                    cerr << "Warning: empty import, the emitted code will "
                            "likely be invalid"
                         << endl;
                    return {};
                }
                static unordered_map<string, template_type> tmplCache {};
                auto tmplIt = tmplCache.find(import.second);
                if (tmplIt == tmplCache.end())
                    tmplIt =
                        tmplCache
                            .emplace(import.second, makeMustache(import.second))
                            .first;

                object importContextObj {{"_", import.first}};
                setList(importContextObj, "segments", fspath(import.first));
                // This is where the import as collected from the API
                // description is actually transformed to the language-specific
                // import target (such as a C++ header file)
                return renderWithOverlay(tmplIt->second, context,
                                         importContextObj);
            });

    // Unnamed schemas are only saved in the model to enable inlining
    // but cannot be used to emit valid code (not in C++ at least).
    decltype(model.types) namedSchemas;
    for (const auto& schema: model.types)
        if (!schema.name.empty())
            namedSchemas.emplace_back(schema);

    auto&& mAllTypes = dumpAllTypes(namedSchemas); // Back-comp w/swagger
    if (!mAllTypes.empty())
        payloadObj.emplace("allModels", mAllTypes);

    auto&& mTypes = dumpTypes(namedSchemas);
    if (!mTypes.empty())
        payloadObj.emplace("models", mTypes);

    object mOperations;
    if (!model.callClasses.empty()) {
        const auto& callClass = model.callClasses.back();
        bool globalConsumesNonJson = false, globalProducesNonJson = false;
        // Any attributes should be added after setList
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

            auto&& mCallTypes = dumpTypes(model.types, &call);
            if (!mCallTypes.empty())
                mCall.emplace("models", mCallTypes);
            setList(mCall, "pathParts", call.path.parts,
                    [this, &call](const Path::part_type& p) {
                        const string s{call.path, get<0>(p), get<1>(p)};
                        return get<2>(p) == Path::Variable ? s
                               : _leftQuote + s + _rightQuote;
                    });

            addList(mCall, "allParams", call.collateParams());
            for (size_t i = 0; i < Call::ParamGroups.size(); ++i)
                addList(mCall, Call::ParamGroups[i] + "Params",
                        call.params[i]);

            dispatchVisit(
                call.body,
                [this, &mCall](const FlatSchema& unpackedBody) {
                    addList(mCall, "bodyParams", unpackedBody.fields);
                    if (unpackedBody.hasPropertyMap())
                        mCall["propertyMap"] =
                            dumpField(unpackedBody.propertyMap);
                },
                [this, &mCall](const VarDecl& packedBody) {
                    mCall.emplace("inlineBody", dumpField(packedBody));
                },
                [](monostate) {});
            mCall["hasBody?"] = !holds_alternative<monostate>(call.body);

            setList(mCall, "responses", call.responses, [this](const Response& r) {
                object mResponse{{"code", r.code},
                                 {"normalResponse?", r.code == "200"}};

                VarDecls allProperties;
                copy(r.headers.begin(), r.headers.end(),
                     back_inserter(allProperties));

                dispatchVisit(
                    r.body,
                    [this, &mResponse,
                     &allProperties](const FlatSchema& unpackedBody) {
                        addList(mResponse, "properties", unpackedBody.fields);
                        copy(unpackedBody.fields.begin(),
                             unpackedBody.fields.end(),
                             back_inserter(allProperties));
                        if (unpackedBody.hasPropertyMap()) {
                            allProperties.emplace_back(unpackedBody.propertyMap);
                            mResponse["propertyMap"] =
                                dumpField(unpackedBody.propertyMap);
                        }
                    },
                    [this, &mResponse,
                     &allProperties](const VarDecl& packedBody) {
                        mResponse.emplace("inlineResponse",
                                          dumpField(packedBody));
                        allProperties.emplace_back(packedBody);
                    },
                    [](monostate) {});
                for (const auto& src: {{"allProperties", allProperties},
                                       pair{"headers", r.headers}})
                    addList(mResponse, src.first, src.second);

                return mResponse;
            });

            return mCall;
        });

        if (!mOperations.empty()) {
            mOperations.emplace("classname", "NOT_IMPLEMENTED");
            mOperations.emplace("consumesNonJson?", globalConsumesNonJson);
            mOperations.emplace("producesNonJson?", globalProducesNonJson);
            payloadObj.emplace("operations", mOperations);
        }
    }
    if (mTypes.empty() && mOperations.empty()) {
        clog << "Warning: no emittable contents found in the model for "
             << filePathBase << ".*" << endl;
        return;
    }

    ContextOverlay overlay(context, payloadObj);
    for (const auto& [fPath, fTemplate]:
         _translator.outputConfig(filePathBase, model)) {
        ofstream ofs{fPath};
        if (!ofs.good())
            throw Exception(fPath.string() + ": Couldn't open for writing");

        cout << "Emitting " << fPath.string() << endl;
        auto fullTemplate = makeMustache(fTemplate);
        fullTemplate.render(context, ofs);
        if (fullTemplate.error_message().empty())
            _outFilesList << fPath.string() << endl;
        else
            clog << fPath << ": " << fullTemplate.error_message() << endl;
    }
}
