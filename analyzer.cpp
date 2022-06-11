/******************************************************************************
 * Copyright (C) 2018 Kitsune Ral <kitsune-ral@users.sf.net>
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

#include "analyzer.h"

#include "translator.h"
#include "yaml.h"

#include <algorithm>

using namespace std;
namespace fs = filesystem;

/// This class helps to save the context and restore it on C++ scope exit.
/// Previous context lifecycle remains the caller's responsibility.
class ContextOverlay {
private:
    Analyzer& _analyzer;
    const Analyzer::Context* _prevContext;
    Analyzer::Context _thisContext;
    const Identifier _scope;

public:
    ContextOverlay(Analyzer& a, fs::path newFileDir, Model* newModel,
                   Identifier newScope = {})
        : _analyzer(a)
        , _prevContext(_analyzer._context)
        , _thisContext{std::move(newFileDir), newModel, move(newScope)}
    {
        _analyzer._context = &_thisContext;
        ++_analyzer._indent;
    }
    template <typename... ArgTs>
    ContextOverlay(Analyzer& a, ArgTs&&... scopeArgs)
        : ContextOverlay(a, a.context().fileDir, a.context().model,
                         {std::forward<ArgTs>(scopeArgs)...})
    { }
    ~ContextOverlay()
    {
        --_analyzer._indent;
        _analyzer._context = _prevContext;
    }
};

Analyzer::models_t Analyzer::_allModels {};

Analyzer::Analyzer(const Translator& translator, fspath basePath)
    : _baseDir(move(basePath))
    , _translator(translator)
{
    if (!fs::is_directory(_baseDir))
        throw Exception("Base path " + _baseDir.string() + " is not a directory");
    cout << "Using " << _baseDir << " as a base directory for YAML/JSON files"
         << endl;
}

TypeUsage Analyzer::analyzeTypeUsage(const YamlMap& node, IsTopLevel isTopLevel)
{
    auto yamlTypeNode = node["type"];

    if (yamlTypeNode && yamlTypeNode.IsSequence())
        return analyzeMultitype(yamlTypeNode.asSequence());

    auto yamlType = yamlTypeNode.as<string>("object");
    if (yamlType == "array")
    {
        if (auto yamlElemType = node["items"].asMap(); !yamlElemType.empty()) {
            auto&& elemType = analyzeTypeUsage(yamlElemType, TopLevel);
            const auto& protoType = _translator.mapType(
                "array", elemType.baseName,
                camelCase(node["title"].as<string>(elemType.baseName + "[]")));
            return protoType.specialize({move(elemType)});
        }

        return _translator.mapType("array");
    }
    if (yamlType == "object")
    {
        auto schema = analyzeSchema(node);
        if (isTopLevel && schema.empty() && currentRole() == OnlyOut)
            return {}; // The type returned by this API is void

        // If the schema is trivial it is treated as an alias for another type.
        // NB: if the found name or top-level $ref for the schema has any
        // type substitution configured in gtad.yaml, the schema will also
        // be trivial with the substituting type in parentTypes.front()
        if (schema.trivial())
            return schema.parentTypes.front();

        if (!schema.name.empty()) // Only ever filled for non-empty schemas
            return addSchema(move(schema)); // Wrap the schema in a TypeUsage

        // An OnlyIn empty object is schemaless but existing, mapType("object")
        // Also, a nameless non-empty schema is now treated as a generic
        // mapType("object"). TODO, low priority: ad-hoc typing (via tuples?)
    }
    if (const auto tu = _translator.mapType(yamlType, node["format"].as<string>(""));
        !tu.empty())
        return tu;

    throw YamlException(node, "Unknown type: " + yamlType);
}

TypeUsage Analyzer::addSchema(ObjectSchema&& schema)
{
    // This is just to enrich the type usage with attributes, not
    // for type substitution
    auto tu = _translator.mapType("schema", schema.name);
    tu.name = schema.name;
    tu.call = schema.call;
    currentModel().addSchema(move(schema));
    return tu;
}

TypeUsage Analyzer::analyzeMultitype(const YamlSequence& yamlTypes)
{
    vector<TypeUsage> tus;
    for (const auto& yamlType: yamlTypes)
        tus.emplace_back(yamlType.IsScalar()
                         ? _translator.mapType(yamlType.as<string>())
                         : analyzeTypeUsage(yamlType));

    string baseTypes;
    for (const auto& t: tus)
    {
        if (!baseTypes.empty())
            baseTypes.push_back(',');
        baseTypes += t.baseName;
    }

    const auto& protoType = _translator.mapType("variant", baseTypes, baseTypes);
    cout << logOffset() << "Using " << protoType.qualifiedName()
         << " for a multitype: " << baseTypes << endl;
    return protoType.specialize(move(tus));
}

ObjectSchema Analyzer::analyzeSchema(const YamlMap& yamlSchema,
                                     RefsStrategy refsStrategy)
{
    if (const auto yamlRef = yamlSchema["$ref"]) {
        // https://tools.ietf.org/html/draft-pbryan-zyp-json-ref-03#section-3
        if (yamlSchema.size() > 1)
            clog << yamlSchema.location() << ": Warning: "
                    "members next to $ref in the same map will be ignored"
                 << endl;
        return resolveRef(yamlRef.as<string>(), refsStrategy);
    }

    const auto schema = yamlSchema["type"].as<string>("object") == "object"
                            ? analyzeObject(yamlSchema, refsStrategy)
                            : makeEphemeralSchema(analyzeTypeUsage(yamlSchema));

    if (!schema.empty()) {
        cout << logOffset() << yamlSchema.location() << ": schema for "
             << schema;
        if (const auto& scopeName = currentScope().name; !scopeName.empty())
            cout << '/' << scopeName;
        if (schema.trivial())
            cout << " mapped to " << schema.parentTypes.front().qualifiedName();
        else {
            cout << " (parent(s): " << schema.parentTypes.size()
                 << ", field(s): " << schema.fields.size();
            if (!schema.propertyMap.type.empty())
                cout << " and a property map";
            cout << ")";
        }
        cout << endl;
    }
    return schema;
}

ObjectSchema Analyzer::analyzeObject(const YamlMap& yamlSchema,
                                     RefsStrategy refsStrategy)
{
    ObjectSchema schema{currentRole(), currentCall(),
                        yamlSchema["description"].as<string>("")};

    // The name is taken from: the schema's "title" property; failing that,
    // the _inline_ schema(s) "title" in allOf (with the last one winning,
    // if there are several); and failing that, parent schemas (i.e., taken
    // in oneOf or $ref'ed in allOf).
    string name;

    // To check for substitions, calculate the name without resolving
    // references first, to avoid generating unused schemas.

    auto yamlAllOf = yamlSchema["allOf"].asSequence();
    if (yamlAllOf)
        for (const auto& yamlEntry : yamlAllOf) {
            name = yamlEntry["title"].as<string>(name);
        }

    name = yamlSchema["title"].as<string>(name);

    if (!name.empty()) {
        // Now that we have a good idea of the schema identity we can check if
        // the configuration has anything to substitute this schema with.
        if (auto&& tu = _translator.mapType("schema", name); !tu.empty())
            return makeEphemeralSchema(move(tu));
    }

    if (auto yamlOneOf = yamlSchema["oneOf"].asSequence())
        schema.parentTypes.emplace_back(analyzeMultitype(yamlOneOf));

    if (yamlAllOf)
        for (const auto& yamlEntry : yamlAllOf) {
            auto&& innerSchema = analyzeSchema(yamlEntry, refsStrategy);
            // NB: If the schema is loaded from $ref, it ends up in
            // innerSchema.parentType; its name won't be in innerSchema.name
            if (!innerSchema.name.empty())
                name = innerSchema.name;
            std::copy(innerSchema.parentTypes.begin(),
                      innerSchema.parentTypes.end(),
                      std::back_inserter(schema.parentTypes));
            if (!innerSchema.description.empty())
                schema.description = innerSchema.description;
            for (auto&& f: innerSchema.fields) {
                // Re-map the identifier name using the current schema as scope
                // (f has been produced with innerSchema as scope)
                f.name =
                    _translator.mapIdentifier(f.baseName, &schema, f.required);
                if (!f.name.empty())
                    addVarDecl(schema.fields, move(f));
            }

            if (innerSchema.hasPropertyMap()) {
                auto&& pm = innerSchema.propertyMap;
                if (schema.hasPropertyMap() && schema.propertyMap.type != pm.type)
                    throw YamlException(
                        yamlEntry, "Conflicting property map types when "
                                   "merging properties to the main schema");

                pm.name = _translator.mapIdentifier(pm.baseName, &schema,
                                                    pm.required);
                if (!pm.name.empty())
                    schema.propertyMap = move(pm);
            }
        }

    // Last resort: pick the name from the parent (i.e. $ref'ed) schema but only
    // if the current schema is trivial (i.e. has no extra fields on top of
    // what $ref'ed schema defines).
    if (name.empty() && schema.trivial())
        name = schema.parentTypes.back().name;

    if (!name.empty())
        name = _translator.mapIdentifier(name, &currentScope(), false);

    auto properties = yamlSchema["properties"].asMap();
    auto additionalProperties = yamlSchema["additionalProperties"];

    if (properties || additionalProperties
        || (!schema.empty() && !schema.trivial())) {
        // If the schema is not just an alias for another type, name it.
        schema.name = camelCase(name);
    }

    if (properties) {
        const auto requiredList = yamlSchema["required"].asSequence();
        for (const auto& property: properties) {
            auto&& baseName = property.first.as<string>();
            auto required = any_of(requiredList.begin(), requiredList.end(),
                                   [&baseName](const YamlNode& n) {
                                       return baseName == n.as<string>();
                                   });
            addVarDecl(schema.fields, analyzeTypeUsage(property.second),
                       baseName, schema,
                       property.second["description"].as<string>(""), required,
                       property.second["default"].as<string>(""));
        }
    }
    if (additionalProperties) {
        TypeUsage tu;
        string description;
        switch (additionalProperties.Type()) {
        case YAML::NodeType::Map: {
            auto elemType = analyzeTypeUsage(additionalProperties.asMap());
            const auto& protoType =
                _translator.mapType("map", elemType.baseName,
                                    "string->" + elemType.baseName);
            tu = protoType.specialize({move(elemType)});
            description = additionalProperties["description"].as<string>("");
            break;
        }
        case YAML::NodeType::Scalar: // Generic map
            if (additionalProperties.as<bool>())
                tu = _translator.mapType("map");
            break;
        default:
            throw YamlException(additionalProperties,
                "additionalProperties should be either a boolean or a map");
        }

        if (!tu.empty()) {
            if (schema.empty())
                return makeEphemeralSchema(move(tu));

            if (auto&& v = makeVarDecl(move(tu), "additionalProperties", schema,
                                       move(description)))
                schema.propertyMap = *v;
        }
    }
    return schema;
}

Body Analyzer::analyzeBodySchema(const YamlMap& yamlSchema, const string& name,
                                 string description, bool required)
{
    if (currentRole() == InAndOut)
        throw YamlException(yamlSchema,
                "Internal error, role must be either OnlyIn or OnlyOut");

    const Identifier location {"", currentRole(), currentCall() };
    auto packedType = _translator.mapType("schema", location.qualifiedName());
    if (packedType.empty()) {
        auto&& bodySchema = analyzeSchema(yamlSchema);

        if (description.empty())
            description = bodySchema.description;

        // For description of packed vs. unpacked bodies cf. the documentation
        // at Body definition
        if (bodySchema.empty()) {
            if (currentRole() == OnlyOut) // cf. the end of analyzeType() :-/
                return {}; // An empty schema for _response_ means "ignore body"

            // An empty schema for _request_ means a freeform object
            required = false;
            packedType = _translator.mapType("object");
        } else if (bodySchema.trivial()) {
            // If the schema consists of a single parent type, inline that type
            packedType = bodySchema.parentTypes.front();
            currentModel().addImportsFrom(packedType);
        } else if (bodySchema.hasParents()) {
            // If the schema is complex (=with both parents and properties),
            // add a definition for it and make a single parameter with
            // the type of the schema.
            packedType = addSchema(move(bodySchema));
        } else {
            // No parents, non-empty - unpack the schema to body properties
            currentModel().addImportsFrom(bodySchema);
            // NOLINTNEXTLINE(cppcoreguidelines-slicing): no parents to lose
            return FlatSchema {bodySchema};
        }
    }
    if (auto&& v = makeVarDecl(move(packedType), name, location,
                               move(description), required)) {
        cout << logOffset() << yamlSchema.location() << ": substituting the "
             << location << " schema with a '" << v->type.qualifiedName() << ' '
             << v->name << "' parameter" << endl;
        return *v;
    }
    cout << logOffset() << yamlSchema.location() << location
         << " schema has been nullified by configuration" << endl;
    return {};
}

ObjectSchema Analyzer::resolveRef(const string& refPath,
                                  RefsStrategy refsStrategy)
{
    // First try to resolve refPath in types map
    auto&& tu = _translator.mapType("$ref", refPath);
    if (tu.empty()) {
        // No type shortcut in the types map (but tu may have some attributes
        // loaded by mapType() above)
        const auto titleIt = tu.attributes.find("title");
        const auto& overrideTitle =
            titleIt != tu.attributes.cend() ? titleIt->second : string();
        // Take the configuration override into account
        if (auto inlineAttrIt = tu.attributes.find("_inline");
            inlineAttrIt != tu.attributes.end() && inlineAttrIt->second == "true")
            refsStrategy = InlineRefs;
        auto&& [refModel, importPath] =
            loadDependency(refPath, overrideTitle, refsStrategy == InlineRefs);
        if (refModel.types.empty())
            throw Exception(refPath + " has no schemas");

        tu.addImport(importPath.string());
        auto&& refSchema = refModel.types.back();

        if (refsStrategy == InlineRefs || refModel.trivial()) {
            if (refModel.trivial())
                cout << logOffset() << "The model at " << refPath
                     << " is trivial (see the mapping above) and";
            else
                cout << logOffset() << "The main schema from " << refPath;
            cout << " will be inlined" << endl;

            currentModel().imports.insert(refModel.imports.begin(),
                                          refModel.imports.end());
            // If the model is non-trivial the inlined main schema likely
            // depends on other definitions from the same file; but it's
            // not always practical to inline dependencies as well
            if (refModel.types.size() > 1) {
                cout << "The dependencies will still be imported from "
                     << importPath << endl;
                currentModel().addImportsFrom(tu); // One import actually
            }
            return refSchema;
        }
        tu.name = refSchema.name;
        tu.baseName = tu.name.empty() ? refPath : tu.name;
    }
    cout << logOffset() << "Resolved $ref: " << refPath << " to type usage "
         << tu.name << endl;

    return makeEphemeralSchema(move(tu));
}

ObjectSchema Analyzer::makeEphemeralSchema(TypeUsage&& tu) const
{
    ObjectSchema result{currentRole()};
    if (!tu.empty())
        result.parentTypes.emplace_back(move(tu));
    return result;
}

optional<VarDecl> Analyzer::makeVarDecl(TypeUsage type, const string& baseName,
                                        const Identifier& scope,
                                        string description, bool required,
                                        string defaultValue) const
{
    auto&& id = _translator.mapIdentifier(baseName, &scope, required);
    if (id.empty())
        return {}; // Skip the variable

    return VarDecl {move(type),        move(id), baseName,
                    move(description), required, move(defaultValue)};
}

void Analyzer::addVarDecl(VarDecls &varList, VarDecl &&v) const
{
    varList.erase(remove_if(varList.begin(), varList.end(),
                            [&v, this](const VarDecl& vv) {
                                if (v.name != vv.name)
                                    return false;
                                cout << logOffset() << "Re-defining field "
                                     << vv << endl;
                                return true;
                            }),
                  varList.end());
    varList.emplace_back(v);
}

void Analyzer::addVarDecl(VarDecls& varList, TypeUsage type,
                          const string& baseName, const Identifier& scope,
                          string description, bool required,
                          string defaultValue) const
{
    if (auto&& v = makeVarDecl(move(type), baseName, scope, move(description),
                               required, move(defaultValue)))
        addVarDecl(varList, move(*v));
}

inline auto makeModelKey(const string& filePath)
{
    return withoutSuffix(filePath, ".yaml");
}

vector<string> loadContentTypes(const YamlMap& yaml, const char* keyName)
{
    if (auto yamlTypes = yaml[keyName].asSequence())
        return yamlTypes.asStrings();
    return {};
}

const Model& Analyzer::loadModel(const string& filePath, InOut inOut)
{
    cout << "Loading from " << filePath << endl;
    const auto yaml =
        YamlMap::loadFromFile(_baseDir / filePath, _translator.substitutions());
    if (_allModels.count(filePath) > 0) {
        clog << "Warning: the model has been loaded from " << filePath
             << " but will be reloaded again" << endl;
        _allModels.erase(filePath);
    }
    auto&& model = _allModels[makeModelKey(filePath)];
    ContextOverlay _modelContext(*this, fspath(filePath).parent_path(), &model);

    // Detect which file we have: API description or data definition
    if (!yaml["paths"]) {
        // XXX: this branch is yet unused; one day it will load event schemas
        fillDataModel(model, yaml, fspath(filePath).filename());
        return model;
    }

    // The rest is exclusive to API descriptions
    const auto paths = yaml.get("paths", true).asMap();
    if (yaml["swagger"].as<string>("") != "2.0")
        throw Exception(
                "This software only supports swagger version 2.0 for now");

    model.apiSpec = ApiSpec::Swagger;
    model.apiSpecVersion = 20; // Swagger/OpenAPI 2.0

    auto defaultConsumed = loadContentTypes(yaml, "consumes");
    auto defaultProduced = loadContentTypes(yaml, "produces");
    model.hostAddress = yaml["host"].as<string>("");
    model.basePath = yaml["basePath"].as<string>("");

    for (const YamlNodePair& yaml_path: paths)
        try {
            const Path path { yaml_path.first.as<string>() };

            for (const YamlNodePair& yaml_call_pair: yaml_path.second.asMap()) {
                auto verb = yaml_call_pair.first.as<string>();
                const YamlMap yamlCall { yaml_call_pair.second };
                auto operationId = yamlCall.get("operationId").as<string>();

                bool needsSecurity = false;
                if (const auto security = yamlCall["security"].asSequence())
                    needsSecurity = security[0]["accessToken"].IsDefined();

                cout << logOffset() << yamlCall.location()
                     << ": Found operation " << operationId
                     << " (" << path << ", " << verb << ')' << endl;

                Call& call = model.addCall(path, move(verb), move(operationId),
                                           needsSecurity);

                if (auto&& yamlSummary = yamlCall["summary"])
                    call.summary = yamlSummary.as<string>();
                if (auto&& yamlDescription = yamlCall["description"])
                    call.description = yamlDescription.as<string>();
                if (YamlMap yamlExternalDocs = yamlCall["externalDocs"])
                    call.externalDocs = {
                        yamlExternalDocs["description"].as<string>(""),
                        yamlExternalDocs.get("url").as<string>("")
                    };
                call.consumedContentTypes =
                        loadContentTypes(yamlCall, "consumes");
                if (call.consumedContentTypes.empty())
                    call.consumedContentTypes = defaultConsumed;
                call.producedContentTypes =
                        loadContentTypes(yamlCall, "produces");
                if (call.producedContentTypes.empty())
                    call.producedContentTypes = defaultProduced;

                const auto yamlParams = yamlCall["parameters"].asSequence();
                for (const YamlMap yamlParam: yamlParams) {
                    const auto& name = yamlParam.get("name").as<string>();

                    ContextOverlay _inContext(*this, name, OnlyIn, &call);

                    auto&& in = yamlParam.get("in").as<string>();
                    auto required = yamlParam["required"].as<bool>(false);
                    if (!required && in == "path") {
                        cout << logOffset() << yamlParam.location()
                        	 << ": warning: '" << name
                             << "' is in path but has no 'required' attribute"
                             << " - treating as required anyway" << endl;
                        required = true;
                    }
//                    cout << "Parameter: " << name << endl;
//                    for (const YamlNodePair p: yamlParam)
//                        cout << "  At " << p.first.location() << ": "
//                                        << p.first.as<string>() << endl;

                    auto&& description =
                        yamlParam["description"].as<string>("");
                    if (in != "body") {
                        addVarDecl(call.getParamsBlock(in),
                                   analyzeTypeUsage(yamlParam, TopLevel),
                                   name, call, move(description), required,
                                   yamlParam["default"].as<string>(""));
                        continue;
                    }

                    call.body = analyzeBodySchema(yamlParam.get("schema"), name,
                                                  move(description), required);
                }
                const auto yamlResponses = yamlCall.get("responses").asMap();
                if (const auto yamlResponse = yamlResponses["200"].asMap()) {
                    Response response{
                        "200", yamlResponse.get("description").as<string>()};
                    ContextOverlay _outContext(*this, response.code, OnlyOut,
                                               &call);
                    if (auto yamlHeaders = yamlResponse["headers"])
                        for (const auto& yamlHeader: yamlHeaders.asMap())
                            addVarDecl(response.headers,
                                analyzeTypeUsage(yamlHeader.second, TopLevel),
                                yamlHeader.first.as<string>(), call,
                                yamlHeader.second["description"].as<string>(""));

                    if (const auto& yamlBody = yamlResponse["schema"])
                        response.body = analyzeBodySchema(yamlBody, "data"s,
                                                          response.description,
                                                          false);

                    call.responses.emplace_back(move(response));
                }
            }
        } catch (ModelException& me) {
            throw YamlException(yaml_path.first, me.message);
        }
    return model;
}

pair<const Model&, fs::path>
Analyzer::loadDependency(const string& relPath, const string& overrideTitle,
                         bool inlined)
{
    const auto& fullPath = context().fileDir / relPath;
    const auto fullPathBase = makeModelKey(fullPath.string());
    const auto [mIt, unseen] = _allModels.try_emplace(fullPathBase);
    auto& model = mIt->second;
    const pair result {cref(model), _translator.outputBaseDir() / fullPathBase};

    // If there is a matching model just return it
    auto modelRole = InAndOut;
    if (!unseen) {
        if (model.apiSpec != ApiSpec::JSONSchema)
            throw Exception("Dependency model for " + relPath
                            + " is found in the cache but doesn't seem to be"
                              " for a data schema (format " + model.apiSpec
                            + ")");
        if (!model.callClasses.empty())
            throw Exception(
                "Internal error: a JSON Schema model has API definitions");

        if (!model.types.empty()) {
            modelRole = model.types.back().role;
            if (modelRole == InAndOut || modelRole == currentRole()) {
                cout << logOffset() << "Reusing already loaded model for "
                     << relPath << " with role " << modelRole << endl;
                return result;
            }
            cout << logOffset()
                 << "Found existing data model generated for role " << modelRole
                 << "; the model will be reloaded for all roles" << endl;
            modelRole = InAndOut;
            model.clear();
        } else {
            cout << logOffset() << "Warning: empty data model for " << relPath
                 << " has been found in the cache; reloading" << endl;
            modelRole = currentRole();
        }
    }

    cout << logOffset() << "Loading data schema from " << relPath
         << " with role " << modelRole << endl;
    const auto yaml =
        YamlMap::loadFromFile(_baseDir / fullPath, _translator.substitutions());
    ContextOverlay _modelContext(*this, fullPath.parent_path(), &model,
                                 Identifier{{}, modelRole});
    fillDataModel(model, yaml, fspath(fullPathBase).filename());
    auto& mainSchema = model.types.back();
    if (!overrideTitle.empty() && !model.types.empty())
        mainSchema.name = overrideTitle;
    if (mainSchema.hasParents()
        && (!mainSchema.fields.empty() || mainSchema.hasPropertyMap()))
        cout << logOffset() << "Inlining suppressed due to model complexity"
             << endl;
    else
        model.inlineMainSchema = (unseen || model.inlineMainSchema) && inlined;
    return result;
}

void Analyzer::fillDataModel(Model& m, const YamlNode& yaml,
                             const fs::path& filename)
{
    m.apiSpec = ApiSpec::JSONSchema;
    m.apiSpecVersion = 201909; // Only JSON Schema 2019-09 is targeted for now
    auto&& s = analyzeSchema(yaml);
    if (s.name.empty())
        s.name = camelCase(filename.string());
    m.addSchema(move(s));
}
