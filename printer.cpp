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

inline auto assignDelimiter(const string& delimiter, string tmpl)
{
    if (!delimiter.empty())
        tmpl.insert(0, "{{=" + delimiter + "=}}");
    return tmpl;
}

km::partial makePartial(string s, const string& delimiter)
{
    return km::partial {
        [tmpl = assignDelimiter(delimiter, std::move(s))] { return tmpl; }};
}

inline Printer::template_type Printer::makeMustache(const string& tmpl) const
{
    km::mustache mstch {assignDelimiter(_delimiter, tmpl)};
    mstch.set_custom_escape([](string s) { return s; });
    return mstch;
}

class GtadContext : public km::context<string>
{
    public:
        using data = km::data;

        GtadContext(Printer::fspath inputBasePath, string delimiter,
                    const data* d)
            : context(d)
            , inputBasePath(std::move(inputBasePath))
            , delimiter(std::move(delimiter))
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
            return &filePartialsCache
                        .emplace(name,
                                 makePartial(std::move(fileContents), delimiter))
                        .first->second;
        }

    private:
        Printer::fspath inputBasePath;
        string delimiter;
        mutable unordered_map<string, data> filePartialsCache;
};

template <typename StringT>
class ContextOverlay {
public:
    ContextOverlay(km::context<StringT>& context,
                   const km::basic_object<StringT>& overlayObj)
        : overlay(overlayObj), ctx(context), ctxPusher(ctx, &overlay)
    {}

private:
    km::basic_data<StringT> overlay;
    km::context_internal<StringT> ctx;
    km::context_pusher<StringT> ctxPusher;
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
    const ContextOverlay ctxOverlay(ctx, overlay);
    return tmpl.render(ctx);
}

/// Enrich the context with "My Mustache library"
inline Printer::context_type addLibrary(Printer::context_type contextObj)
{
    using km::lambda2;
    using km::renderer;
    contextObj.emplace("_titleCase", lambda2{[](const string& s, renderer render) {
                           return titleCased(render(s, false));
                       }});
    return contextObj;
}

Printer::Printer(context_type&& contextObj, fspath inputBasePath,
                 const fspath& outFilesListPath, string delimiter,
                 const Translator& translator)
    : _translator(translator)
    , _contextData(addLibrary(contextObj))
    , _delimiter(std::move(delimiter))
    , _typeRenderer(makeMustache(safeString(contextObj, "_typeRenderer", "{{>name}}")))
    , _leftQuote(safeString(contextObj, "_leftQuote", safeString(contextObj, "_quote", "\"")))
    , _rightQuote(safeString(contextObj, "_rightQuote", safeString(contextObj, "_quote", "\"")))
    , _inputBasePath(std::move(inputBasePath))
{
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

inline object wrap(auto val)
{
    return object {{ "_", std::move(val) }};
}

inline object wrap(const filesystem::path& p)
{
    return wrap(p.string());
}

void setList(object& target, const string& name, const auto& source, auto convert)
{
    target[name + '?'] = !source.empty();
    km::list mList;
    auto it = source.begin();
    for (bool hasMore = it != source.end(); hasMore;) {
        auto elementObj = wrap(convert(*it));
        hasMore = ++it != source.end();
        elementObj.emplace("_join", hasMore);
        elementObj.emplace("hasMore", hasMore); // Swagger compatibility
        mList.emplace_back(std::move(elementObj));
    }
    target[name] = mList;
}

void setList(object& target, const string& name, const auto& source)
{
    setList(target, name, source, [](auto element) { return element; });
}

void dumpDescription(object& target, const auto& model)
{
    vector<string> lines{};
    if (!model.description.empty()) {
        static const regex re{"\\n"};
        lines = {
            sregex_token_iterator{model.description.cbegin(), model.description.cend(), re, -1},
            sregex_token_iterator{}
        };
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
        // 2) we qualify types, not calls, with call names (think of referring
        //    to another type within the same call)
        qualifiedValues.emplace("scope", tu.call->name);
    }

    // Fill parameters for parameterized types
    setList(values, "types", tu.paramTypes, bind_front(&Printer::renderType, this));
    setList(qualifiedValues, "types", tu.paramTypes, bind_front(&Printer::renderType, this));
    int i = 0;
    for (const auto& t: tu.paramTypes)
    {
        // Substituting {{1}}, {{2}} and so on with actual inner type names
        auto mParamType = renderType(t);
        values.emplace(to_string(++i), mParamType["name"]);
        qualifiedValues.emplace(to_string(i), mParamType["qualifiedName"]);
    }

    GtadContext context {_inputBasePath, _delimiter, &_contextData};
    return {{"name", renderWithOverlay(_typeRenderer, context, values)}
           ,{"qualifiedName",
             renderWithOverlay(_typeRenderer, context, qualifiedValues)}
           ,{"baseName", tu.baseName}
           };
}

object Printer::dumpField(const VarDecl& field) const
{
    auto paramNameCamelCase = titleCased(field.name);
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
        fieldDef.emplace(listAttr.first, std::move(mAttrValue));
    }
    return fieldDef;
}

void Printer::addList(object& target, const string& name,
                      const VarDecls& properties) const
{
    setList(target, name, properties, bind_front(&Printer::dumpField, this));
}

inline auto copyPartitionedByRequired(std::vector<VarDecl> vars)
{
    ranges::stable_partition(vars, &VarDecl::required);
    return vars;
}

bool Printer::dumpAdditionalProperties(m_object_type& target, const FlatSchema& s) const
{
    if (!s.hasAdditionalProperties())
        return false;

    target.insert_or_assign("propertyMap", dumpField(s.additionalProperties));
    if (const auto& p = s.additionalPropertiesPattern; !p.empty())
        target.insert_or_assign("extraPropertiesPattern", p);
    return true;
}

object Printer::dumpAllTypes(const Model::schemaptrs_type& types) const
{
    object mModels;
    if (!types.empty())
        setList(
            mModels, "model", types, [this](const std::pair<const ObjectSchema*, TypeUsage>& type) {
                auto mType = renderType(type.second);
                mType["classname"] = type.first->name; // Swagger compat
                dumpDescription(mType, *type.first);
                mType["in?"] = type.first->role != OnlyOut;
                mType["out?"] = type.first->role != OnlyIn;
                if (type.first->trivial())
                {
                    mType["trivial?"] = true;
                    mType["parent"] = renderType(type.first->parentTypes.back());
                }
                setList(mType, "parents", type.first->parentTypes,
                        bind_front(&Printer::renderType, this));
                setList(mType, "vars", copyPartitionedByRequired(type.first->fields),
                    [this](const VarDecl& f) {
                        object fieldDef = dumpField(f);
                        fieldDef["name"] = f.name;
                        fieldDef["datatype"] = f.type.name; // Swagger compat
                        return fieldDef;
                    });
                dumpAdditionalProperties(mType, *type.first);
                return mType;
            });
    return mModels;
}

template <typename SchemaPtrT>
object Printer::dumpTypes(const std::vector<std::pair<SchemaPtrT, TypeUsage>>& types,
                          const Call* scope) const
{
    Model::schemaptrs_type selectedTypes;
    for (const auto& s : types)
        if (s.first->call == scope)
            selectedTypes.emplace_back(std::to_address(s.first), s.second);
    return dumpAllTypes(selectedTypes);
}

bool dumpContentTypes(object& target, const string& keyName, vector<string> types)
{
    setList(target, keyName, types);
    const bool hasNonJson =
        !all_of(types.begin(), types.end(),
                [](const string& s) { return s.ends_with("/json"); });
    target.emplace(keyName + "NonJson?", hasNonJson);
    return hasNonJson;
}

vector<string> Printer::print(const fspath& filePathBase,
                              const Model& model) const
{
    if (model.empty()) {
        clog << "Empty model, no files will be emitted" << endl;
        return {};
    }

    GtadContext context{_inputBasePath, _delimiter, &_contextData};

    object payloadObj{
        {"filenameBase"s, filePathBase.filename().string()}
    };
    if (!model.defaultServers.empty()) {
        // NB: For now, only support one server
        const auto& firstServer = model.defaultServers.front();
        payloadObj.emplace("basePathWithoutHost"s, firstServer.basePath());
        payloadObj.emplace("basePath"s, firstServer.toString());
    }
    setList(payloadObj, "imports", model.imports,
            [this, &context](const pair<string, string>& import) -> string {
                if (import.first.empty() || import.second.empty()) {
                    cerr << "Warning: empty import, the emitted code will "
                            "likely be invalid"
                         << endl;
                    return {};
                }
                static unordered_map<string, template_type> tmplCache {};
                const auto tmplIt =
                    tmplCache
                        .try_emplace(import.second, makeMustache(import.second))
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
    std::vector<std::pair<const ObjectSchema*, TypeUsage>> namedSchemas;
    for (const auto& schema: model.types)
        if (!schema.first->name.empty() && !schema.first->inlined())
            namedSchemas.emplace_back(schema.first.get(), schema.second);

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
            object mCall{
                {"operationId", call.name          },
                {"httpMethod",  call.verb          },
                {"path",        call.path          },
                {"summary",     call.summary       },
                {"deprecated?", call.deprecated    },
                {"skipAuth",    !call.needsSecurity}
            };
            dumpDescription(mCall, call);

            globalConsumesNonJson |=
                dumpContentTypes(mCall, "consumes", call.consumedContentTypes);
            if (!call.responses.empty()) {
                const auto& producedContentTypes = call.responses.front().contentTypes;
                globalProducesNonJson |= dumpContentTypes(mCall, "produces", producedContentTypes);
                mCall.emplace("producesImage?",
                              ranges::all_of(producedContentTypes, [](const string& s) {
                                  return s.starts_with("image/");
                              }));
            }

            auto&& mCallTypes = dumpTypes(model.types, &call);
            if (!mCallTypes.empty())
                mCall.emplace("models", mCallTypes);
            setList(mCall, "pathParts", call.path.parts,
                    [this, &call](const Path::part_type& p) {
                        const string s{call.path, get<0>(p), get<1>(p)};
                        return get<2>(p) == Path::Variable ? s
                               : _leftQuote + s + _rightQuote;
                    });

            addList(mCall, "allParams",
                    copyPartitionedByRequired(call.collateParams()));
            for (size_t i = 0; i < Call::ParamGroups.size(); ++i)
                addList(mCall, Call::ParamGroups[i] + "Params",
                        call.params[i]);

            dispatchVisit(
                call.body,
                [this, &mCall](const FlatSchema& unpackedBody) {
                    addList(mCall, "bodyParams", unpackedBody.fields);
                    dumpAdditionalProperties(mCall, unpackedBody);
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

                dispatchVisit(r.body,
                    [this, &mResponse, &allProperties](const FlatSchema& unpackedBody) {
                        addList(mResponse, "properties", unpackedBody.fields);
                        ranges::copy(unpackedBody.fields, back_inserter(allProperties));
                        if (!dumpAdditionalProperties(mResponse, unpackedBody)
                            && unpackedBody.fields.size() == 1)
                            mResponse["singleValue?"] = true;
                    },
                    [this, &mResponse, &allProperties](const VarDecl& packedBody) {
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
        clog << "No emittable contents found in the model for " << filePathBase.string()
             << ".*, skipping\n";
        return {};
    }

    const ContextOverlay overlay(context, payloadObj);
    const auto outputs = _translator.outputConfig(filePathBase, model);
    vector<string> emittedFilenames;
    emittedFilenames.reserve(outputs.size());
    for (const auto& [fPath, fTemplate]: outputs) {
        const auto& fPathString = fPath.string();
        ofstream ofs{fPath};
        if (!ofs.good())
            throw Exception(fPathString + ": Couldn't open for writing");

        cout << "Emitting " << fPathString << endl;
        auto fullTemplate = makeMustache(fTemplate);
        fullTemplate.render(context, ofs);
        if (fullTemplate.error_message().empty()) {
            _outFilesList << fPathString << endl;
            emittedFilenames.push_back(fPathString);
        } else
            clog << fPath << ": " << fullTemplate.error_message() << endl;
    }
    return emittedFilenames;
}
