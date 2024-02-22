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

#include <QtCore/QUrl>

#include <algorithm>
#include <iostream>

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
                   Identifier newScope)
        : _analyzer(a)
        , _prevContext(_analyzer._context)
        , _thisContext{std::move(newFileDir), newModel, std::move(newScope)}
    {
        _analyzer._context = &_thisContext;
        ++_analyzer._indent;
    }
    ContextOverlay(Analyzer& a, fs::path newFileDir, Model* newModel, InOut role)
        : ContextOverlay(a, std::move(newFileDir), newModel, {{}, role})
    {}
    ContextOverlay(Analyzer& a, Identifier newScope)
        : ContextOverlay(a, a.context().fileDir, a.context().model, newScope)
    { }
    ~ContextOverlay()
    {
        --_analyzer._indent;
        _analyzer._context = _prevContext;
    }
};

Analyzer::models_t Analyzer::_allModels {};

Analyzer::Analyzer(const Translator& translator, fspath basePath)
    : _baseDir(std::move(basePath))
    , _translator(translator)
{
    if (!fs::is_directory(_baseDir))
        throw Exception("Base path " + _baseDir.string() + " is not a directory");
    cout << "Using " << _baseDir << " as a base directory for YAML/JSON files"
         << endl;
}

TypeUsage Analyzer::analyzeTypeUsage(const YamlMap<>& node)
{
    auto yamlTypeNode = node["type"];

    if (yamlTypeNode && yamlTypeNode->IsSequence())
        return analyzeMultitype(yamlTypeNode->as<YamlSequence<>>());

    auto yamlType = yamlTypeNode ? yamlTypeNode->as<string>() : "object"s;
    if (yamlType == "array")
    {
        if (auto yamlElemType = node.maybeGet<YamlMap<>>("items"); !yamlElemType.empty()) {
            auto&& elemType = analyzeTypeUsage(*yamlElemType);
            const auto& protoType =
                _translator.mapType("array", elemType.baseName,
                                    camelCase(node.get<string>("title", elemType.baseName + "[]")));
            return protoType.specialize({std::move(elemType)});
        }

        return _translator.mapType("array");
    }
    if (yamlType == "object")
    {
        auto schema = analyzeSchema(node);
        if (schema.maxProperties == 0)
            return {}; // 'void'

        // If the schema is trivial it is treated as an alias for another type.
        // NB: if the found name or top-level $ref for the schema has any
        // type substitution configured in gtad.yaml, the schema will also
        // be trivial with the substituting type in parentTypes.front()
        if (schema.trivial())
            return schema.parentTypes.front();

        if (!schema.name.empty()) // Only ever filled for non-empty schemas
            return addSchema(std::move(schema)); // Wrap `schema` in a TypeUsage

        // An empty object is schemaless but existing, mapType("object")
        // Also, a nameless non-empty schema is treated as a generic
        // mapType("object") for now. TODO, low priority: ad-hoc typing (via tuples?)
    }
    if (const auto tu = _translator.mapType(yamlType, node.get<string_view>("format", {}));
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
    currentModel().addSchema(std::move(schema));
    return tu;
}

TypeUsage Analyzer::analyzeMultitype(const YamlSequence<>& yamlTypes)
{
    vector<TypeUsage> tus;
    for (const auto& yamlType: yamlTypes)
        tus.emplace_back(yamlType.IsScalar() ? _translator.mapType(yamlType.as<string>())
                                             : analyzeTypeUsage(yamlType.as<YamlMap<>>()));

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
    return protoType.specialize(std::move(tus));
}

ObjectSchema Analyzer::analyzeSchema(const YamlMap<>& yamlSchema,
                                     RefsStrategy refsStrategy)
{
    if (const auto yamlRef = yamlSchema.maybeGet<string>("$ref")) {
        // https://tools.ietf.org/html/draft-pbryan-zyp-json-ref-03#section-3
        if (yamlSchema.size() > 1)
            clog << yamlSchema.location() << ": Warning: "
                    "members next to $ref in the same map will be ignored"
                 << endl;
        return resolveRef(*yamlRef, refsStrategy);
    }

    const auto schema = yamlSchema.get<string>("type", "object"s) == "object"
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

ObjectSchema Analyzer::analyzeObject(const YamlMap<>& yamlSchema, RefsStrategy refsStrategy)
{
    ObjectSchema schema{currentRole(), currentCall(), yamlSchema.get<string>("description", {})};

    // The name is taken from: the schema's "title" property; failing that,
    // the _inline_ schema(s) "title" in allOf (with the last one winning,
    // if there are several); and failing that, parent schemas (i.e., taken
    // in oneOf or $ref'ed in allOf).
    string name;

    // To check for substitutions, calculate the name without resolving
    // references first, to avoid generating unused schemas.

    auto yamlAllOf = yamlSchema.maybeGet<YamlSequence<YamlMap<>>>("allOf");
    for (const auto& yamlEntry : yamlAllOf) {
        yamlEntry.maybeLoad("title", &name);
    }

    yamlSchema.maybeLoad("title", &name);

    if (!name.empty()) {
        // Now that we have a good idea of the schema identity we can check if
        // the configuration has anything to substitute this schema with.
        if (auto&& tu = _translator.mapType("schema", name); !tu.empty())
            return makeEphemeralSchema(std::move(tu));
    }

    yamlSchema.maybeLoad("maxProperties", &schema.maxProperties);
    // Don't bother parsing parents and properties if the schema is known to be empty
    if (schema.maxProperties == 0)
        return schema;

    if (auto yamlOneOf = yamlSchema.maybeGet<YamlSequence<>>("oneOf"))
        schema.parentTypes.emplace_back(analyzeMultitype(*yamlOneOf));

    for (const auto& yamlEntry : yamlAllOf) {
        auto&& innerSchema = analyzeSchema(yamlEntry, refsStrategy);
        // NB: If the schema is loaded from $ref, it ends up in
        // innerSchema.parentType; its name won't be in innerSchema.name
        if (!innerSchema.name.empty())
            name = innerSchema.name;
        std::copy(innerSchema.parentTypes.begin(), innerSchema.parentTypes.end(),
                  std::back_inserter(schema.parentTypes));
        if (!innerSchema.description.empty())
            schema.description = innerSchema.description;
        for (auto&& f: innerSchema.fields) {
            // Re-map the identifier name using the current schema as scope
            // (f has been produced with innerSchema as scope)
            f.name = _translator.mapIdentifier(f.baseName, &schema, f.required);
            if (!f.name.empty())
                addVarDecl(schema.fields, std::move(f));
        }

        if (innerSchema.hasPropertyMap()) {
            auto&& pm = innerSchema.propertyMap;
            if (schema.hasPropertyMap() && schema.propertyMap.type != pm.type)
                throw YamlException(yamlEntry, "Conflicting property map types when "
                                               "merging properties to the main schema");

            pm.name = _translator.mapIdentifier(pm.baseName, &schema, pm.required);
            if (!pm.name.empty())
                schema.propertyMap = std::move(pm);
        }
    }

    // Last resort: pick the name from the parent (i.e. $ref'ed) schema but only
    // if the current schema is trivial (i.e. has no extra fields on top of
    // what $ref'ed schema defines).
    if (name.empty() && schema.trivial())
        name = schema.parentTypes.back().name;

    if (!name.empty())
        name = _translator.mapIdentifier(name, &currentScope(), false);

    auto properties = yamlSchema.maybeGet<YamlMap<YamlMap<>>>("properties");
    auto additionalProperties = yamlSchema["additionalProperties"];

    if (properties || additionalProperties
        || (!schema.empty() && !schema.trivial())) {
        // If the schema is not just an alias for another type, name it.
        schema.name = camelCase(name);
    }

    if (properties) {
        const auto requiredList = yamlSchema.maybeGet<YamlSequence<string>>("required");
        for (const auto& [baseName, details] : *properties) {
            const auto required = ranges::contains(*requiredList, baseName);
            addVarDecl(schema.fields, analyzeTypeUsage(details), baseName, schema,
                       details.get<string>("description", {}), required,
                       details.get<string>("default", {}));
        }
    }
    if (additionalProperties) {
        TypeUsage tu;
        string description;
        switch (additionalProperties->Type()) {
        case YAML::NodeType::Map: {
            const auto propertiesMap = additionalProperties->as<YamlMap<>>();
            auto elemType = analyzeTypeUsage(propertiesMap);
            const auto& protoType =
                _translator.mapType("map", elemType.baseName,
                                    "string->" + elemType.baseName);
            tu = protoType.specialize({std::move(elemType)});
            description = propertiesMap.get<string>("description", {});
            break;
        }
        case YAML::NodeType::Scalar: // Generic map
            if (additionalProperties->as<bool>())
                tu = _translator.mapType("map");
            break;
        default:
            throw YamlException(*additionalProperties,
                                "additionalProperties should be either a boolean or a map");
        }

        if (!tu.empty()) {
            if (schema.empty())
                return makeEphemeralSchema(std::move(tu));

            if (auto&& v = makeVarDecl(std::move(tu), "additionalProperties",
                                       schema, std::move(description)))
                schema.propertyMap = *v;
        }
    }
    return schema;
}

Body Analyzer::analyzeBody(const YamlMap<>& contentYaml, string description,
                           const string& contentType, bool required, std::string_view name)
{
    if (currentRole() == InAndOut)
        throw YamlException(contentYaml, "Internal error, role must be either OnlyIn or OnlyOut");
    if (currentModel().apiSpecVersion < 20 || currentModel().apiSpecVersion >= 40)
        throw YamlException(
            contentYaml, "Internal error, trying to call analyzeBody on non-OpenAPI description");

    const Identifier location{{}, currentRole(), currentCall()};
    auto packedType = _translator.mapType("schema", location.qualifiedName());
    if (packedType.empty()) {
        const auto isOldSwagger = context().model->apiSpecVersion < 30;
        auto&& bodySchema =
            isOldSwagger ? analyzeSchema(contentYaml) : [this, &contentYaml, &contentType] {
                // TODO: the below sort of hardwires having `schema` to application/json content
                //       type; other structured content types (application/yaml, application/xml)
                //       could certainly have a schema too. Maybe make it configurable via gtad.yaml
                if (contentType == "application/json") [[likely]]
                    if (const auto schemaYaml = contentYaml.maybeGet<YamlMap<>>("schema"))
                        return analyzeSchema(*schemaYaml);
                // Emulate type definitions used with Swagger 2 so that users don't have
                // to rewrite their configuration files
                return makeEphemeralSchema(_translator.mapType("string"s, "binary"s));
            }();

        if (description.empty())
            description = bodySchema.description;

        // For description of packed vs. unpacked bodies cf. the documentation
        // at Body definition
        if (bodySchema.maxProperties == 0) // same as at the end of analyzeTypeUsage()
            return {}; // Empty object, effectively void or monostate
        if (bodySchema.empty()) {
            // Empty schema without maxProperties: 0 is treated as a freeform object
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
            packedType = addSchema(std::move(bodySchema));
        } else {
            // No parents, non-empty - unpack the schema to body properties
            currentModel().addImportsFrom(bodySchema);
            // NOLINTNEXTLINE(cppcoreguidelines-slicing): no parents to lose
            return FlatSchema {bodySchema};
        }
    }
    if (auto&& v = makeVarDecl(std::move(packedType), name, location,
                               std::move(description), required))
    {
        cout << logOffset() << contentYaml.location() << ": substituting the " << location
             << " body definition with '" << v->type.qualifiedName() << ' ' << v->name << "'\n";
        return *v;
    }
    cout << logOffset() << contentYaml.location() << location
         << " body definition has been nullified by configuration\n";
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

    return makeEphemeralSchema(std::move(tu));
}

ObjectSchema Analyzer::makeEphemeralSchema(TypeUsage&& tu) const
{
    ObjectSchema result{currentRole()};
    if (!tu.empty())
        result.parentTypes.emplace_back(std::move(tu));
    return result;
}

optional<VarDecl> Analyzer::makeVarDecl(TypeUsage type, string_view baseName,
                                        const Identifier& scope,
                                        string description, bool required,
                                        string defaultValue) const
{
    auto&& id = _translator.mapIdentifier(baseName, &scope, required);
    if (id.empty())
        return {}; // Skip the variable

    return VarDecl{std::move(type),        std::move(id), string(baseName),
                   std::move(description), required,      std::move(defaultValue)};
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
    if (auto&& v = makeVarDecl(std::move(type), baseName, scope,
                               std::move(description), required,
                               std::move(defaultValue)))
        addVarDecl(varList, std::move(*v));
}

inline string withoutSuffix(const string& path, string_view suffix)
{
    return path.substr(0, path.find(suffix, path.size() - suffix.size()));
}

inline fs::path Analyzer::makeModelKey(const fs::path& sourcePath)
{
    return (_translator.outputBaseDir() / withoutSuffix(sourcePath, ".yaml"))
        .lexically_normal();
}

Server resolveOas3Server(const YamlMap<>& yamlServer)
{
    auto urlPattern = QString::fromStdString(yamlServer.get<string>("url"));
    // Replace all {variable} occurrences in `url` with default values taken from the variables
    // dictionary. The conversions between std::string and QString are unfortunate but make the code
    // much simpler, and we have to use QUrl anyway, because of its damn good user input parsing.
    for (const auto& [varName, values] : yamlServer.maybeGet<YamlMap<YamlMap<string>>>("variables"))
        urlPattern.replace(u'{' + QString::fromStdString(varName) + u'}',
                           QString::fromStdString(values.get("default")));

    return {urlPattern, yamlServer.get<string>("description", {})};
}

vector<string> loadSwaggerContentTypes(const YamlMap<>& yaml, const char* keyName)
{
    if (auto yamlTypes = yaml.maybeGet<YamlSequence<string>>(keyName))
        return {yamlTypes->begin(), yamlTypes->end()};
    return {};
}

const Model& Analyzer::loadModel(const string& filePath, InOut inOut)
{
    cout << "Loading from " << filePath << endl;
    const auto yaml =
        YamlNode::fromFile(_baseDir / filePath, _translator.substitutions()).as<YamlMap<>>();
    if (_allModels.contains(filePath)) {
        clog << "Warning: the model has been loaded from " << filePath
             << " but will be reloaded again" << endl;
        _allModels.erase(filePath);
    }
    auto&& model = _allModels[makeModelKey(filePath)];
    const ContextOverlay _modelContext(*this, fspath(filePath).parent_path(), &model, inOut);

    // Detect which file we have: API description or data definition
    // Using YamlGenericMap so that YamlException could be used on the map key
    const auto paths = yaml.maybeGet<YamlGenericMap>("paths");
    if (!paths) {
        // XXX: this branch is yet unused; one day it will load event schemas
        fillDataModel(model, yaml, fspath(filePath).filename());
        return model;
    }

    // The rest is exclusive to API descriptions
    vector<string> defaultConsumed, defaultProduced;
    const auto isOpenApi3 = yaml.get<string_view>("openapi", {}).starts_with("3.1");
    if (isOpenApi3) {
        model.apiSpec = ApiSpec::OpenAPI3;
        model.apiSpecVersion = 31;
        for (const auto serverSpec : yaml.maybeGet<YamlSequence<YamlMap<>>>("servers"))
            model.defaultServers.emplace_back(resolveOas3Server(serverSpec));
    } else if (yaml.get<string_view>("swagger", {}).starts_with("2.0")) {
        model.apiSpec = ApiSpec::Swagger;
        model.apiSpecVersion = 20; // Swagger/OpenAPI 2.0
        auto schemesYaml = yaml.maybeGet<YamlSequence<string>>("schemes");
        model.defaultServers.emplace_back(schemesYaml.empty() ? string{} : schemesYaml->front(),
                                          yaml.get<string>("host", {}),
                                          yaml.get<string>("basePath", {}));
        defaultConsumed = loadSwaggerContentTypes(yaml, "consumes");
        defaultProduced = loadSwaggerContentTypes(yaml, "produces");
    } else
        throw Exception("This software only supports Swagger 2.0 or OpenAPI 3.1.x");

    // Either Swagger 2 or OpenAPI 3 from here; isOpenApi3 cached which one we have

    for (const auto& yaml_path: *paths)
        try {
            const Path path { yaml_path.first.as<string>() };

            for (auto [verb, yamlCall] : yaml_path.second.as<YamlMap<YamlMap<>>>()) {
                auto&& operationId = yamlCall.get<string>("operationId");

                bool needsSecurity = false;
                if (const auto security = yamlCall.maybeGet<YamlSequence<>>("security"))
                    needsSecurity = security->front()["accessToken"].IsDefined();

                cout << logOffset() << yamlCall.location() << ": Found operation " << operationId
                     << " (" << path << ", " << verb << ")\n";

                Call& call =
                    model.addCall(path, std::move(verb), std::move(operationId), needsSecurity);

                yamlCall.maybeLoad("summary", &call.summary);
                yamlCall.maybeLoad("description", &call.description);
                if (auto&& yamlExternalDocs = yamlCall.maybeGet<YamlMap<string>>("externalDocs"))
                    call.externalDocs = {yamlExternalDocs->get("description", {}),
                                         yamlExternalDocs->get("url")};
                if (isOpenApi3) {
                    if (auto&& yamlBody = yamlCall.maybeGet<YamlMap<>>("requestBody")) {
                        const ContextOverlay _inContext(*this, {"(requestBody)", OnlyIn, &call});

                        if (call.verb == "get" || call.verb == "head" || call.verb == "delete")
                            clog << logOffset() << yamlBody.location()
                                 << ": warning: RFC7231 does not allow requestBody in '"
                                 << call.verb << "' operations\n";

                        // Only one content type in requestBody is supported
                        const auto& [contentType, contentData] =
                            yamlBody->get<YamlMap<YamlMap<>>>("content").front();
                        call.consumedContentTypes.emplace_back(contentType);
                        call.body =
                            analyzeBody(contentData, yamlBody->get<string>("description", {}),
                                        contentType, yamlBody->get<bool>("required", false));
                    }
                } else {
                    call.consumedContentTypes = loadSwaggerContentTypes(yamlCall, "consumes");
                    if (call.consumedContentTypes.empty())
                        call.consumedContentTypes = defaultConsumed;
                }

                const auto yamlParams = yamlCall.maybeGet<YamlSequence<YamlMap<>>>("parameters");
                for (const auto& yamlParam: yamlParams) {
                    const auto& name = yamlParam.get<string>("name");

                    const ContextOverlay _inContext(*this, {name, OnlyIn, &call});

                    auto&& in = yamlParam.get<string>("in");
                    auto required = yamlParam.get<bool>("required", false);
                    if (!required && in == "path") {
                        cout << logOffset() << yamlParam.location() << ": warning: '" << name
                             << "' is in path but has no 'required' attribute"
                                " - treating as required anyway\n";
                        required = true;
                    }

                    auto&& description = yamlParam.get<string>("description", {});
                    if (in == "body") {
                        if (isOpenApi3)
                            throw YamlException(
                                yamlParam, "OpenAPI 3 definitions cannot have 'body' parameters");
                        call.body =
                            analyzeBody(yamlParam, std::move(description), {}, required, name);
                    } else {
                        const auto& typeYaml =
                            isOpenApi3 ? yamlParam.get<YamlMap<>>("schema") : yamlParam;
                        addVarDecl(call.getParamsBlock(in), analyzeTypeUsage(typeYaml), name, call,
                                   std::move(description), required,
                                   typeYaml.get<string>("default", {}));
                    }
                }
                const auto& yamlResponses = yamlCall.get<YamlMap<YamlMap<>>>("responses");
                for (const auto& [responseCode, responseData] : yamlResponses)
                    if (responseCode.starts_with('2')) {
                        // Only handling the first 2xx response for now
                        Response response{responseCode, responseData.get<string>("description")};
                        const ContextOverlay _outContext(*this, {responseCode, OnlyOut, &call});
                        for (const auto& [headerName, headerYaml] :
                             responseData.maybeGet<YamlMap<YamlMap<>>>("headers"))
                            addVarDecl(response.headers,
                                       analyzeTypeUsage(isOpenApi3
                                                            ? headerYaml.get<YamlMap<>>("schema")
                                                            : headerYaml),
                                       headerName, call, headerYaml.get<string>("description", {}));

                        if (isOpenApi3) {
                            if (const auto& yamlContent =
                                responseData.maybeGet<YamlMap<YamlMap<>>>("content"))
                            {
                                for (const auto& [contentType, contentTypeYaml] : yamlContent) {
                                    response.contentTypes.emplace_back(contentType);
                                    if (empty(response.body))
                                        response.body = analyzeBody(
                                            contentTypeYaml, response.description, contentType);
                                    else
                                        clog << logOffset() << contentTypeYaml.location()
                                             << ": warning: No support for more than one non-empty "
                                                "content schema, subsequent schemas will be "
                                                "skipped\n";
                                }
                            }
                        } else {
                            response.contentTypes = loadSwaggerContentTypes(yamlCall, "produces");
                            if (response.contentTypes.empty())
                                response.contentTypes = defaultProduced;
                            response.body = analyzeBody(responseData, response.description);
                        }

                        call.responses.emplace_back(std::move(response));
                        break;
                    }

                if (call.responses.empty()
                    || std::none_of(call.responses.cbegin(), call.responses.cend(),
                                    [](const Response& r) {
                                        return r.code.starts_with('2') || r.code.starts_with('3');
                                    }))
                    clog << logOffset() << yamlResponses.location()
                         << ": warning: all responses seem to describe errors - possibly "
                            "incomplete API description\n";
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
    const auto stem = makeModelKey(fullPath);
    const auto [mIt, unseen] = _allModels.try_emplace(stem);
    auto& model = mIt->second;
    const pair result {cref(model), stem};

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
        YamlNode::fromFile(_baseDir / fullPath, _translator.substitutions()).as<YamlMap<>>();
    const ContextOverlay _modelContext(*this, fullPath.parent_path(), &model, modelRole);
    fillDataModel(model, yaml, stem.filename());
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

void Analyzer::fillDataModel(Model& m, const YamlMap<>& yaml, const fs::path& filename)
{
    m.apiSpec = ApiSpec::JSONSchema;
    m.apiSpecVersion = 201909; // Only JSON Schema 2019-09 is targeted for now
    auto&& s = analyzeSchema(yaml);
    if (s.name.empty())
        s.name = camelCase(filename.string());
    m.addSchema(std::move(s));
}
