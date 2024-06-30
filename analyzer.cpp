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

    ContextOverlay(Analyzer& a, fs::path newFileDir, Model* newModel,
                   Identifier newScope)
        : _analyzer(a)
        , _prevContext(_analyzer._context)
        , _thisContext{std::move(newFileDir), newModel, std::move(newScope)}
    {
        _analyzer._context = &_thisContext;
        ++_analyzer._indent;
    }
public:
    ContextOverlay(Analyzer& a, fs::path newFileDir, Model* newModel, InOut role)
        : ContextOverlay(a, std::move(newFileDir), newModel, {{}, role})
    {}
    ContextOverlay(Analyzer& a, Identifier newScope)
        : ContextOverlay(a, a.context().fileDir, a.context().model, std::move(newScope))
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

    auto yamlType = yamlTypeNode ? yamlTypeNode->as<string>() : ""s;
    if (yamlType == "array")
    {
        if (auto yamlElemType = node.maybeGet<YamlMap<>>("items"); !yamlElemType.empty()) {
            auto&& elemType = analyzeTypeUsage(*yamlElemType);
            const auto& protoType =
                _translator.mapType("array", elemType.baseName,
                                    titleCased(node.get<string>("title", elemType.baseName + "[]")));
            return protoType.specialize({std::move(elemType)});
        }

        return _translator.mapType("array");
    }
    if (yamlType.empty() || yamlType == "object")
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

        // An empty type is schemaless but existing, while a nameless non-empty schema is treated
        // as a generic mapType("object") for now. TODO, low priority: ad-hoc typing (via tuples?)
    }
    return _translator.mapType(yamlType, node.get<string_view>("format", {}));
}

TypeUsage Analyzer::addSchema(ObjectSchema&& schema)
{
    // This is just to enrich the type usage with attributes, not
    // for type substitution
    auto tu = _translator.mapType("schema", schema.name);
    if (const auto titleAttrIt = tu.attributes.find("title"s); titleAttrIt != tu.attributes.end())
        schema.name = titleAttrIt->second;
    tu.name = schema.name;
    tu.call = schema.call;
    currentModel().addSchema(std::move(schema), tu);
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

ObjectSchema Analyzer::analyzeSchema(const YamlMap<>& schemaYaml, RefsStrategy refsStrategy)
{
    if (const auto yamlRef = schemaYaml.maybeGet<string>("$ref"))
        return analyzeRefObject(schemaYaml, refsStrategy);

    auto&& schema = schemaYaml.get<string>("type", "object"s) == "object"
                        ? analyzeObject(schemaYaml, refsStrategy)
                        : makeTrivialSchema(analyzeTypeUsage(schemaYaml));

    if (!schema.empty()) {
        cout << logOffset() << schemaYaml.location() << ": schema for "
             << schema;
        if (const auto& scopeName = currentScope().name; !scopeName.empty())
            cout << '/' << scopeName;
        if (schema.trivial())
            cout << " mapped to " << schema.parentTypes.front().qualifiedName();
        else {
            cout << " (parent(s): " << schema.parentTypes.size()
                 << ", field(s): " << schema.fields.size();
            if (!schema.additionalProperties.type.empty())
                cout << ", and additional properties";
            cout << ")";
        }
        cout << '\n';
    }
    return std::move(schema);
}

std::pair<TypeUsage, string> Analyzer::analyzePropertiesMap(const YamlMap<>& propertyYaml)
{
    auto keyType =
        _translator.mapType("string", propertyYaml.get<string_view>("x-pattern-format", {}));
    auto elemType = analyzeTypeUsage(propertyYaml);

    const auto& protoType =
        _translator.mapType("map", elemType.baseName, "string->" + elemType.baseName);
    return std::pair{protoType.specialize({std::move(keyType), std::move(elemType)}),
                     propertyYaml.get<string>("description", {})};
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
            return makeTrivialSchema(std::move(tu));
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

        if (auto&& aps = innerSchema.additionalProperties; !aps.type.empty()) {
            if (schema.additionalProperties.type != aps.type)
                throw YamlException(yamlEntry, "Conflicting property map types when "
                                               "merging properties to the main schema");

            aps.name = _translator.mapIdentifier(aps.baseName, &schema, aps.required);
            if (!aps.name.empty())
                schema.additionalProperties = std::move(aps);
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
    auto patternPropertiesYaml = yamlSchema.maybeGet<YamlMap<YamlMap<>>>("patternProperties");
    auto additionalProperties = yamlSchema["additionalProperties"];

    if (properties || patternPropertiesYaml || additionalProperties
        || (!schema.empty() && !schema.trivial())) {
        // If the schema is not just an alias for another type, name it.
        schema.name = titleCased(name);
    }

    if (properties) {
        const auto requiredList = yamlSchema.maybeGet<YamlSequence<string>>("required");
        for (const auto& [baseName, details] : *properties) {
            const auto required = ranges::contains(requiredList, baseName);
            addVarDecl(schema.fields, analyzeTypeUsage(details), baseName, schema,
                       details.get<string>("description", {}), required,
                       details.get<string>("default", {}));
        }
    }
    TypeUsage tu;
    string description;
    if (patternPropertiesYaml) {
        if (additionalProperties)
            throw YamlException(yamlSchema, "Using patternProperties and additionalProperties in "
                                            "the same object is not supported at the moment");
        if (patternPropertiesYaml->size() > 1)
            throw YamlException(*patternPropertiesYaml,
                                "Multiple pattern properties are not supported at the moment");
        const auto& [pattern, propertyYaml] = patternPropertiesYaml->front();
        std::tie(tu, description) = analyzePropertiesMap(propertyYaml);
        if (!tu.empty() && !schema.empty())
            schema.additionalPropertiesPattern = pattern;
    } else if (additionalProperties) {
        switch (additionalProperties->Type()) {
        case YAML::NodeType::Map:
            std::tie(tu, description) = analyzePropertiesMap(additionalProperties->as<YamlMap<>>());
            break;
        case YAML::NodeType::Scalar: // Generic map
            if (additionalProperties->as<bool>())
                tu = _translator.mapType("map");
            break;
        default:
            throw YamlException(*additionalProperties,
                                "additionalProperties should be either a boolean or a map");
        }
    }
    if (!tu.empty()) {
        if (schema.empty())
            return makeTrivialSchema(std::move(tu));

        if (auto&& v = makeVarDecl(std::move(tu), "additionalProperties",
                                   schema, std::move(description)))
            schema.additionalProperties = std::move(*v);
    }
    return schema;
}

Body Analyzer::analyzeBody(const YamlMap<>& contentYaml, string description,
                           const string& contentType, bool required, std::string_view name)
{
    if (currentRole() == InAndOut)
        throw YamlException(contentYaml, "Internal error, role must be either OnlyIn or OnlyOut");
    if (currentModel().apiSpec != ApiSpec::Swagger && currentModel().apiSpec != ApiSpec::OpenAPI3)
        throw YamlException(
            contentYaml, "Internal error, trying to call analyzeBody on non-OpenAPI description");

    const Identifier location{{}, currentRole(), currentCall()};
    auto packedType = _translator.mapType("schema", location.qualifiedName());
    if (packedType.empty()) {
        const auto isOldSwagger = context().model->apiSpec == ApiSpec::Swagger;
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
                return makeTrivialSchema(_translator.mapType("string"s, "binary"s));
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
            if (bodySchema.name.empty())
                bodySchema.name = titleCased(string(name));
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

ObjectSchema Analyzer::analyzeRefObject(const YamlMap<>& refObjectYaml, RefsStrategy refsStrategy)
{
    const auto refPath = refObjectYaml.get<string>("$ref");
    const auto refPathForMapping =
        refPath.starts_with('#') ? refObjectYaml.fileName() + refPath : refPath;

    // First try to resolve refPath in types map
    auto&& tu = _translator.mapReference(refPathForMapping);
    if (!tu.empty()) {
        cout << logOffset() << "Mapped $ref: " << refPath << " to type usage " << tu.name
             << " from the configuration\n";
        return makeTrivialSchema(std::move(tu));
    }

    // No type shortcut in the references map, load schema from the $ref value

    // Check whether the configuration requests inlining; also, if there's an overriding description
    // in the $ref object the resulting schema has to be inlined as it will be distinct from
    // the $ref'ed one anyway.
    if (_translator.isRefInlined(refPathForMapping) || refObjectYaml.size() > 1)
        refsStrategy = InlineRefs;

    if (refPath.starts_with('#')) {
        // TODO: merge with similar code in loadSchemaFromRef(); and maybe even move the whole
        // branch there
        const ContextOverlay _schemaContext(
            *this,
            {refPath,
             InAndOut /* TODO: see loadSchemaFromRef() - we have to have similar stuff here */,
             nullptr}); // NB: schemas in localRefs are considered common, not belonging to any call
        if (auto sIt = currentModel().localRefs.find(refPath);
            sIt != currentModel().localRefs.end())
        {
            if (refsStrategy != InlineRefs) {
                tu = sIt->second;
                cout << logOffset() << "Reusing already loaded mapping " << refPath
                     << " -> " << tu.name << " with role " << currentRole() << '\n';
                return makeTrivialSchema(std::move(tu));
            }
            cout << logOffset() << refObjectYaml.location() << ": forced inlining of saved schema "
                 << sIt->second << '\n';
        }
        auto&& s = analyzeSchema(refObjectYaml.resolveRef(), refsStrategy);
        s.preferInlining = refsStrategy == InlineRefs;
        if (s.inlined())
            return std::move(s);

        if (s.name.empty()) // Use the $ref's last segment as a fallback for the name
            s.name =
                titleCased({find(refPath.crbegin(), refPath.crend(), '/').base(), refPath.cend()});
        tu = addSchema(std::move(s));
        currentModel().localRefs.emplace(refPath, tu);
        return makeTrivialSchema(std::move(tu));
    }

    auto&& [schemaOrTu, importPath, hasExtraDeps] =
        loadSchemaFromRef(refPath, refsStrategy == InlineRefs);

    return dispatchVisit(
        std::move(schemaOrTu),
        [this, refPath, &tu, importPath](TypeUsage&& refTu) {
            cout << logOffset() << "Resolved $ref: " << refPath << " to type usage " << refTu.name
                 << '\n';
            if (!importPath.empty())
                refTu.addImport(importPath.string());
            refTu.importRenderer = tu.importRenderer;

            return makeTrivialSchema(std::move(refTu));
        },
        [this, &tu, &refObjectYaml, importPath, hasExtraDeps](ObjectSchema&& s) {
            refObjectYaml.maybeLoad("description", &s.description);

            if (!importPath.empty())
                tu.addImport(importPath.string());

            // If the model is non-trivial the inlined main schema likely
            // depends on other definitions from the same file; but it's
            // not always practical to inline dependencies as well
            if (hasExtraDeps) {
                cout << logOffset() << "The dependencies will still be imported from " << importPath
                     << '\n';
                currentModel().addImportsFrom(tu); // Usually one, unless mapType() added more
            }
            return std::move(s);
        });
}

ObjectSchema Analyzer::makeTrivialSchema(TypeUsage&& tu) const
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
    if (auto&& id = _translator.mapIdentifier(baseName, &scope, required); !id.empty())
        return VarDecl{std::move(type),        std::move(id), string(baseName),
                       std::move(description), required,      std::move(defaultValue)};
    return {}; // Skip the variable
}

void Analyzer::addVarDecl(VarDecls &varList, VarDecl &&v) const
{
    const auto vIt = ranges::find(varList, v.name, &VarDecl::name);
    if (vIt == varList.end()) {
        varList.emplace_back(std::move(v));
        return;
    }
    // throw Exception("Attempt to overwrite field " + v.name);
    cout << logOffset() << "Warning: re-defining field " << *vIt
         << ", make sure its schema is inlined or standalone to avoid aliasing\n";
    *vIt = std::move(v);
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
        for (const auto serverSpec : yaml.maybeGet<YamlSequence<YamlMap<>>>("servers"))
            model.defaultServers.emplace_back(resolveOas3Server(serverSpec));
    } else if (yaml.get<string_view>("swagger", {}).starts_with("2.0")) {
        model.apiSpec = ApiSpec::Swagger;
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

                // NB: accessToken and accessTokenBearer are Matrix-specific schemes that stand for,
                // roughly, `{ type: apiKey, scheme: bearer }` in OpenAPI. GTAD only supports these
                // for now, and even for these, only passes a flag that a given call needs a token.
                bool needsSecurity = false;
                for (const auto securityScheme :
                     yamlCall.maybeGet<YamlSequence<YamlMap<>>>("security"))
                    switch (securityScheme.size()) {
                    case 0: break; // Empty security scheme == no token needed
                    case 1:
                        needsSecurity |= securityScheme.front().first.starts_with("accessToken");
                        break;
                    default:
                        throw YamlException(securityScheme,
                                            "Malformed security scheme: each security scheme "
                                            "should be a map with exactly one pair in it");
                    }

                cout << logOffset() << yamlCall.location() << ": Found operation " << operationId
                     << " (" << path << ", " << verb << ")\n";

                Call& call = model.addCall(path, std::move(verb), std::move(operationId),
                                           yamlCall.get<bool>("deprecated", false), needsSecurity);

                yamlCall.maybeLoad("summary", &call.summary);
                yamlCall.maybeLoad("description", &call.description);
                if (auto&& yamlExternalDocs = yamlCall.maybeGet<YamlMap<string>>("externalDocs"))
                    call.externalDocs = {yamlExternalDocs->get("description", {}),
                                         yamlExternalDocs->get("url")};
                if (isOpenApi3) {
                    if (auto&& unresolvedYamlBody = yamlCall.maybeGet<YamlMap<>>("requestBody")) {
                        const ContextOverlay _inContext(*this, {"(requestBody)", OnlyIn, &call});

                        if (call.verb == "get" || call.verb == "head" || call.verb == "delete")
                            clog << logOffset() << unresolvedYamlBody.location()
                                 << ": warning: RFC7231 does not allow requestBody in '"
                                 << call.verb << "' operations\n";

                        const auto& yamlBody = unresolvedYamlBody->resolveRef();
                        // Only one content type in requestBody is supported
                        const auto& [contentType, contentData] =
                            yamlBody.get<YamlMap<YamlMap<>>>("content").front();
                        call.consumedContentTypes.emplace_back(contentType);
                        call.body =
                            analyzeBody(contentData, yamlBody.get<string>("description", {}),
                                        contentType, yamlBody.get<bool>("required", false));
                    }
                } else {
                    call.consumedContentTypes = loadSwaggerContentTypes(yamlCall, "consumes");
                    if (call.consumedContentTypes.empty())
                        call.consumedContentTypes = defaultConsumed;
                }

                const auto yamlParams = yamlCall.maybeGet<YamlSequence<YamlMap<>>>("parameters");
                for (const auto& yamlParam : yamlParams | resolveRefs) {
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
                for (const auto& [responseCode, responseData] : yamlResponses | resolveRefs)
                    if (responseCode.starts_with('2')) {
                        // Only handling the first 2xx response for now
                        Response response{responseCode, responseData.get<string>("description")};
                        const ContextOverlay _outContext(*this, {responseCode, OnlyOut, &call});
                        for (const auto& [headerName, headerYaml] :
                             responseData.maybeGet<YamlMap<YamlMap<>>>("headers") | resolveRefs)
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

Analyzer::ImportedSchemaData Analyzer::loadSchemaFromRef(string_view refPath, bool preferInlining)
{
    const auto& fullPath = context().fileDir / refPath;
    const auto stem = makeModelKey(fullPath);
    const auto [mIt, unseen] = _allModels.try_emplace(stem);
    auto& model = mIt->second;

    // If there is a matching model just return it
    auto modelRole = InAndOut;
    if (!unseen) {
        if (model.apiSpec != ApiSpec::JSONSchema)
            throw Exception(
                string("Dependency model for ")
                    .append(refPath)
                    .append(" is found in the cache but doesn't seem to be for a data structure"));
        if (!model.callClasses.empty())
            throw Exception(
                "Internal error: a JSON Schema model has API definitions");

        if (!model.types.empty()) {
            const auto& mainSchema = model.types.back().first;
            modelRole = mainSchema->role;
            if (modelRole == InAndOut || modelRole == currentRole()) {
                cout << logOffset() << "Reusing already loaded model for " << refPath
                     << " with role " << modelRole << '\n';
                if (!mainSchema->inlined()) {
                    if (!preferInlining)
                        return {model.types.back().second, stem};
                    cout << logOffset() << "Forced inlining of schema " << mainSchema
                         << " $ref'ed as " << refPath << '\n';
                }
                return {mainSchema->cloneForInlining(), stem, model.types.size() > 1};
            }
            cout << logOffset()
                 << "Found existing data model generated for role " << modelRole
                 << "; the model will be reloaded for all roles\n";
            modelRole = InAndOut;
            model.clear();
        } else {
            cout << logOffset() << "Warning: empty data model for " << refPath
                 << " has been found in the cache; reloading\n";
            modelRole = currentRole();
        }
    }

    cout << logOffset() << "Loading data schema from " << refPath
         << " with role " << modelRole << '\n';
    const auto yaml =
        YamlNode::fromFile(_baseDir / fullPath, _translator.substitutions()).as<YamlMap<>>();
    const ContextOverlay _modelContext(*this, fullPath.parent_path(), &model, modelRole);
    auto tu = fillDataModel(model, yaml, stem.filename());
    const auto& mainSchema = model.types.back().first;
    if (mainSchema->hasParents() && (!mainSchema->fields.empty() || mainSchema->hasAdditionalProperties())) {
        cout << logOffset() << "Inlining suppressed due to model complexity\n";
        return {tu, stem};
    }
    mainSchema->preferInlining = (unseen || mainSchema->preferInlining) && preferInlining;
    if (mainSchema->inlined()) {
        cout << logOffset() << "The main schema from " << refPath;
        if (mainSchema->trivial())
            cout << " is trivial (see the mapping above) and";
        cout << " will be inlined\n";

        currentModel().imports.insert(model.imports.begin(), model.imports.end());
        return {mainSchema->cloneForInlining(), stem, model.types.size() > 1};
    }
    return {tu, stem};
}

TypeUsage Analyzer::fillDataModel(Model& m, const YamlMap<>& yaml, const fs::path& filename)
{
    m.apiSpec = ApiSpec::JSONSchema;
    auto&& s = analyzeSchema(yaml);
    if (s.name.empty())
        s.name = titleCased(filename.string());
    return addSchema(std::move(s));
}
