# GTAD

[![license](https://img.shields.io/github/license/KitsuneRal/gtad.svg)](https://github.com/KitsuneRal/gtad/blob/master/LICENSE)
![status](https://img.shields.io/badge/status-beta-yellow.svg)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg?style=flat-square)](http://makeapullrequest.com)

GTAD (Generate Things from an API Description) is a work-in-progress 
generator of code from a Swagger/OpenAPI specification. Initially made to 
generate marshalling/unmarshalling C++ code for
[Matrix CS API](https://matrix.org/docs/spec/client_server/unstable.html),
it can be extended to support other API descriptions (possibly even spreading
to other API description languages, such as RAML) and other programming
languages with static type checking.

A brief introduction to the topic of API description languages (ADLs) can be
found in [the talk at Qt World Summit 2017](https://youtu.be/W5TmRozH-rg) that
also announces the GTAD project.

## Contacts
Matrix room: [#gtad:matrix.org](https://matrix.to/#/#gtad:matrix.org).

You can also file issues at
[the project's issue tracker](https://github.com/KitsuneRal/gtad/issues).

## Setting up and building
The source code is hosted at [GitHub](https://github.com/KitsuneRal/gtad/). Tags
starting with `v` represent released versions; `rc` mark release candidates.
Do remember to use `--recursive` or update submodules after checking out -
the project has external dependencies taken in as submodules (this may change
in the future).

### Pre-requisites
- a recent Linux, Windows or macOS system
- a Git client to check out this repo
- Qt 6 (either Open Source or Commercial)
- CMake 3.20 or newer (from your package management system or
  [the official website](https://cmake.org/download/))
- a C++ toolchain with solid C++20 and at least some C++23 support (ranges, in particular), that is:
  GCC 13 (Windows, Linux, OSX), Clang 16 (Linux), Xcode 15 (macOS 13),
  Visual C++ 19.30 (aka VS 2022 17.0), or newer
- any build system that works with CMake and/or qmake should be fine:
  GNU Make, ninja (any platform), NMake, jom (Windows) are known to work.
- for the actual invocation - clang-format in your PATH or CLANG_FORMAT variable
  having a full path to clang-format.

#### Linux
Just install things from the list above using your preferred package manager.
GTAD only uses a tiny subset of Qt Base so you can install as little of Qt as
possible.

#### OS X
`brew install qt` should get you a recent Qt. You may need to tell CMake
about the path to Qt by passing `-DCMAKE_PREFIX_PATH=<where-Qt-installed>`.

#### Windows
1. Install Qt and CMake.
1. The commands in further sections imply that cmake is in your PATH - otherwise
   you have to prepend those commands with actual paths. As an option, it's a 
   good idea to run a `qtenv2.bat` script that can be found in
   `C:\Qt\<Qt version>\<toolchain>\bin` (assuming you installed Qt to `C:\Qt`);
   the only thing it does is adding necessary paths to PATH. You might not want
   to run that script on system startup but it's very handy to setup
   the environment before building. Setting `CMAKE_PREFIX_PATH` in the same way
   as for OS X (see above) is fine too.

### Building
In the root directory of the project sources:
```
mkdir build_dir
cd build_dir
cmake .. # Pass -DCMAKE_PREFIX_PATH and -DCMAKE_INSTALL_PREFIX here if needed
cmake --build . --target all
```
This will produce a gtad binary in `build_dir` inside your project sources.
Installing is not generally supported yet; `cmake --build . --target install`
installs a single executable with no dependencies and/or documentation.

## Usage

GTAD uses 3 inputs to generate "things":
1. Swagger/OpenAPI definition files, further referred to as OpenAPI files or
   OpenAPI definitions. Only
   [OpenAPI 2](https://github.com/OAI/OpenAPI-Specification/blob/master/versions/2.0.md)
   is supported for now (version 3 is in
   [the roadmap](https://github.com/KitsuneRal/gtad/projects/1#column-526169)).
   Each file is treated as a separate source. Notably, the referenced
   (via `$ref`) files are parsed independently from the referring ones;
   the generated code is supposed to import the files produced from
   the referenced OpenAPI definitions.
2. A configuration file in YAML. One GTAD invocation always uses one
   configuration file (but you can invoke GTAD separately for different OpenAPI
   files). The format of this file is described in detail below.
3. Source code template files. As of now, GTAD uses
   [Kainjow's Mustache implementation](https://github.com/kainjow/Mustache) for
   templating. GTAD exports the model for the API as
   a Mustache structure; this is covered in the respective section below.

A good example of GTAD usage can be found in
[libQuotient](https://github.com/quotient-im/libQuotient/) that has
its network request classes generated from OpenAPI definitions of
[Matrix CS API](https://matrix.org/docs/spec/client_server/unstable.html).
The CMakeLists.txt has a GTAD invocation line, using
[gtad.yaml](https://github.com/quotient-im/libQuotient/blob/master/gtad/gtad.yaml)
for the configuration file and a few Mustache templates for code generation
next to it. See also notes in that project's
[CONTRIBUTING.md](https://github.com/quotient-im/libQuotient/blob/master/CONTRIBUTING.md)
and
[CMakeLists.txt](https://github.com/quotient-im/libQuotient/blob/master/CMakeLists.txt)
for an idea how to integrate GTAD in your project.

### Invocation

GTAD is a command-line application; assuming that the `gtad` binary is in your
`PATH`, the invocation line looks as follows:
```
gtad --config <configfile> --out <outdir> <files/dirs...>
```
The options are:
- `<configfile>` - the path to GTAD configuration file (see the next section)
- `<outdir>` - the (top-level) directory where the generated files (possibly
  a tree of them) will be put. Must exist before runnning GTAD.
- `<files/dirs...>` - a list of OpenAPI files or directories with those files
  to process. A hyphen appended to the filename means that the file must be 
  skipped (allows to select a directory with files and then explicitly disable
  some files in it).

Since version 0.9 GTAD uses clang-format at the last stage of files generation
to format the emitted files. For that to work, a binary that can be called
as `clang-format` (that is, `clang-format` for POSIX systems and
`clang-format.exe` for Windows) should reside in `PATH`. Alternatively, you can
pass the full path in the `CLANG_FORMAT` environment variable. While the target
format is normally specified in a `.clang-format` file, you can override that
by passing Clang-format command-line options in `CLANG_FORMAT_ARGS` - notably,
if you prefer to skip formatting for whatever reason, you can set
`CLANG_FORMAT_ARGS="-n"` (dry-run mode) before invoking GTAD.

#### Dealing with referenced files

If a processed OpenAPI file has a `$ref` value referring to relative paths,
the referred file will be added to the processing list (even if they were
disabled in the command line as described above). The respective relative path
will be created in the output directory, so if an OpenAPI file has
`"$ref": "definitions/events.yml"`, the `<outdir>/definitions` directory will
be created and the file(s) generated from `definitions/events.yml` will be put
in there. Note that if `definitions/events.yml` has `"$ref": events/base.yml`,
the `events` directory will be searched under input `definitions` directory, and
a respective `<outdir>/definitions/events` directory will be made for output
files from `base.yml` processing.

Since version 0.11, GTAD also supports local `$ref` objects (those with `$ref` starting with `#`).
Schemas loaded from the local `$ref` will be emitted as a part of the current model, rather than
put in their own files.

### GTAD configuration file

GTAD uses a configuration file written in YAML to customise type mapping and
files generation. The configuration consists of 2 main parts: `analyzer`
(Analyzer configuration) and `mustache` (Printer configuration). As mentioned
above, libQuotient has the (working in production) example of
a configuration file.

#### Analyzer configuration

Analyzer configuration is a YAML object that includes the following parts.

##### `subst`
A regex-to-pattern map of substitutions that should be applied before any
processing takes place - for each `old: new` entry the effect will be the same
as if a regex replacement (`s/old/new/`) were applied to the entire input.
Be careful with such substitutions, as they ignore YAML/JSON structure of
the API description; a careless regex can easily render the input invalid.

##### `identifiers`
(Since GTAD 0.6) This is a map of more fine-tuned substitutions compared to
`subst`, only applied to _names_ (identifiers) encountered in OpenAPI. For now
it's only applied to names of call parameters, schemas and schema properties
but not, notably, to call names (operationIds).

There are two ways to specify a match. By default, the names are matched
sensitive to case and literally; but if the match string starts with a `/`
the rest of the string until the trailing optional `/` becomes a regular
expression as described at https://en.cppreference.com/w/cpp/regex/ecmascript.
When using the regular expression for matching, the substitution string can
include `$1`,`$2`,... to reference the submatches; or `$&` to reference
the entire match. Except from the first and the last position, `/` has
no special meaning and should not be escaped.

One of the main cases for `identifiers` is to change names that clash
with reserved words of the target language (`unsigned` in the example below)
or otherwise undesirable as field/parameter names. If you add, e.g.,
`unsigned: unsignedData` to the `identifiers` section, GTAD will transform
all target parameter names `unsigned` to `unsignedData`, unbreaking C++ code
that otherwise would be invalid. The Mustache configuration will have both
the original (`{{baseName}}`) and the transformed (`{{paramName}}` or
`{{nameCamelCase}}`) names of those parameters so that you can still use
the original name for JSON key names in actual API payloads and
a transformed one to name C++ identifiers in your template files.

Since GTAD 0.7 you can use the scope (schema or call name; see below for
the caveat on the call name syntax) to match specific occurence(s) of
the identifier. The scope name is matched in the original form as it appears
in the API description file but is not represented in the substituting string
in any way; it cannot be rewritten. The separating character is `/` (unescaped, as mentioned).
For calls, you should use the name provided in the `operationId` field
followed by either `>` for identifiers in the request or `<` for those
in the response. If you need to cover both directions, you're likely also
covering several calls using a regex; just put a full stop (`.`)
instead of the direction character.

Scope matching is especially useful to adjust the parameter name for `patternProperties` and
`additionalProperties` (see further in this document) in different schemas and the "packed" response
body name (by default it's always `data`) in different calls. A "packed" body is a case when
the entire JSON in the request or response body is treated as a single piece (parameter or
returned value, respectively). In the opposite, "unpacked" case the top-level JSON object in
the request body or response body is "destructured" to several parameters/accessors.

Also since GTAD 0.7 you can skip the entire field by renaming it to an empty
string. This is useful to prune deprecated fields from your generated code
without touching the original API description. To protect you from shooting
yourself in the foot GTAD will error if an attempt is made to remove a field 
that has `required: true` in the API description.

Example (the first line works in GTAD 0.6, the rest since GTAD 0.7): 
```yaml
default: isDefault                                # 1
AuthenticationData/additionalProperties: authInfo # 2
/^requestTokenTo.*</data/: response               # 3
/requestOpenIdToken</(.*)/: token$1               # 4
setAccountData>/additionalProperties: accountData # 5
getProtocols</data: protocols                     # 6
login>/medium: "" # The quotes are mandatory here # 7
```
This will:
1. All parameters named `default` in the source API will be named `isDefault`
   in the generated code.
2. Rename the `additionalProperties` field of `AuthenticationData`
   schema to `authInfo`. The schema name will be kept intact, only
   the identifier will be renamed.
3. For any call with the name starting with `requestTokenTo`, rename the `data`
   parameter (likely, but not necessarily, representing the "packed" response
   body) occuring in the call's response (`<`) to `response`. The call name
   itself is not changed.
4. For a call named `requestOpenIdToken`, prepend every parameter in its
   response with `token`. This line can be abbreviated to
   `/requestOpenIdToken</: token`; this is not recommended though, as it relies
   quite heavily on the specific way GTAD makes a replacement and may break
   once this logic changes.
5. Rename the `additionalProperties` field occuring in the top level of
   the `setAccountData` call's request body to `accountData`.
6. Rename the `data` field (likely a "packed" response body) in
   the `getProtocols` call's response body to `protocols`.
7. Completely remove the `medium` parameter from the `login` request definition,
   as long as this parameter is not marked with `required: true` in the API
   description file. 

##### `types`
This is the biggest and the most important part of the analyzer configuration,
defining which OpenAPI types and data structures become which target language
types and structures in generated files. Before moving on further I strongly
recommend to open the types map in libQuotient's `gtad.yaml` next to this
file: it's one of those cases when an example can better explain the matter
than a thousand words.

This section is a list (YAML array) of entries; each entry is either
of the following:
```yaml
- <swaggerType>: <targetTypeSpec>
```
or
```yaml
- <swaggerType>:
  - <swaggerFormat>: <targetTypeSpec>
  - /<swaggerFormatRegEx>/:
      <targetTypeSpec>
  - //: <targetTypeSpec> # default, if the format doesn't mach anything above
```
or
```yaml
- +set: { <attributes> }
  +on: [ <typesMap> ]
```
In the above,
- `<swaggerType>` and `<swaggerFormat>`/`<swaggerFormatRegEx>` are matched
  against _type_ and _format_ in the API description (see below on extensions
  to OpenAPI _types_ and _formats_). If the _format_ key starts with a `/`
  (forward slash) it is treated as a regular expression (the trailing slash is
  optional and is not processed - if you need it to be the last character of
  the regex, just add one more `/`), otherwise it's used as a literal
  case-sensitive string.
  
- `<targetTypeSpec>` is either the target type literal string (such as
  `double`) or, in turn, a YAML object:
  ```yaml
      type: <target type literal> # mandatory except for 'schema' and mappings in 'references'
      imports: <filename> or [ <filenames...> ] # optional
      ... # key-value pairs for custom type attributes, optional
  ```
  Each `<targetTypeSpec>` (except those in `schema`, see below)
  must unambiguously specify the target type to be used - either as a string
  (`bool`) or as an object with `type` property (`{ type: bool }`). For
  the purpose of proper rendering you will likely need to pass (and use in
  your Mustache templates) additional information about the mapped type -
  e.g., whether the type is copyable, whether it should be wrapped up in
  another type in case a parameter is optional, or which import - for C/C++
  it's a file for `#include` - should be added. To address that, GTAD has
  a concept of _type attributes_: every type can have an arbitrary number of
  "attributes" with arbitrary (except `type`) names, modeled as string-to-string
  or string-to-list mappings. `imports` is an example of a string-to-list
  mapping.
  
  At the moment GTAD special-cases `imports`: in addition to just passing this
  attribute along with the type name, it adds its contents to a "global" (per
  input file) deduplicated set, to simplify generation of import/include
  blocks. Since some of imports come from `$ref` objects in API descriptions
  (see below), GTAD also translates the original `$ref` path to a form
  suitable to import the respective data structure in the target language.
  Before GTAD 0.8, that was really hardcoded to C/C++; GTAD used the first
  extension in a given (`data` or `api`) subsection of `templates` to append
  to the relative path and added quotes to paths that didn't have it.
  GTAD 0.8 introduced a concept of _import renderers_, Mustache templates
  that allow some basic configuration of the import transformation. This is
  discussed in a dedicated section below.

- `+set/+on` statement allows you to apply type attributes to several mappings
  at once:
  ```yaml
  - +set: { avoidCopy: } # Add 'avoidCopy' attribute...
    +on: # ...to anything matched by the list below
    - object: # ...
    - string: string
    - schema: # ...
  ```
Note that you should only specify any particular _type_/_format_ combination
no more than once. The lookup will stop on the first match, even if it only
specifies attributes, without a type.

###### Supported types and formats
As mentioned above, `swaggerType` and `swaggerFormat`/`swaggerFormatRegEx` are matched against
_type_ and _format_ specified in API description.
[OpenAPI 2](https://github.com/OAI/OpenAPI-Specification/blob/master/versions/2.0.md#user-content-data-types)
and [OpenAPI 3](https://github.com/OAI/OpenAPI-Specification/blob/main/versions/3.1.0.md#data-types)
define standard _types_ and _formats_; on top of these you can use the following non-standard
_types_/_formats_ in the GTAD configuration file (but _not_ in the API descriptions):

- "_formats_" under _type_ `array` are used to match arrays with elements
  of a certain element type. This way you can, e.g., special case an array
  of strings as `QStringList` and still use `QVector<>` for arrays of all other
  types (including objects and other arrays). To render parameterised types
  GTAD assumes that strings under the `type` key in `<targetTypeSpec>` are
  themselves Mustache templates (in Mustache parlance - _partials_) and passes
  the parameter type as value `{{1}}`. Therefore, to use `QVector<>` for
  arrays you should write something like this:
  ```yaml
  - array:
      type: "QVector<{{1}}>"
      imports: <QVector>
  ```
  or, if you prefer the "flow" style of YAML,
  ```yaml
  - array: { type: "QVector<{{1}}>", imports: <QVector> }
  ```

- `schema`: this matches all types defined within the API definition as _schema_ structures,
  as long as these structures are not _trivial_. (A trivial schema is a wrapper around another type,
  such as `string` or a singular `$ref`, without additional parameter definitions; technically,
  it can be represented as a data structure that derives from a single base type, with fields added.
  Such a schema will be inlined (=substituted with that type) on every possible occasion; normally
  you should never see it in generated code.)
  
  The list of "_formats_" inside `schema` allows to specify types, for which
  special target types/attributes should be used. Same as for other types,
  _formats_ under `schema` are either regexes if they start with `/`
  or literal case-sensitive strings otherwise. _Formats_ match:
  - the `title` attribute provided within the schema definition of API file;
  - failing that, schema's title as calculated after resolving `$ref`
    attributes (note that some `$ref`'s can be intercepted, as described
    in the next bullet)
  - (since GTAD 0.7) for top-level request/response schemas _format_
    also matches the operation's `operationId` with a `>` appended
    for requests, `<` for responses (in the same vein as for `identifiers`
    entries) - e.g., `getTurnServers<` matches the top-level schema for
    the response body of `getTurnServers`.

  In case of schemas, `<targetTypeSpec>` may omit `type` entirely and only supply additional
  attributes that will be added to the Mustache context at each usage of the given type, while
  the original schema is used to define the type. In GTAD 0.11, the `title` attribute has
  a special meaning in this context: it preserves the original type definition but overrides
  the type name (as if the respective `title` were provided in the API description instead). In
  versions 0.7 through 0.10.x, the same effect could be achieved by supplying `_title` attribute
  under `$ref`; this didn't allow to rename schemas not involved in reference objects, hence
  the new mechanism.

- `$ref` - this is a historical key supported by GTAD versions from 0.6 to 0.10.x; it was moved out
  to its own `references` section under `analyzer` in GTAD 0.11, see below.
    
- _type_ `object`: this is an entry (without _formats_ underneath) that
  describes the target type to be used when GTAD could not find any schema
  for it and the context implies that there are no restrictions on the type
  (but some data structure is still needed). Notably, this target type is used
  when an input parameter for an API call is described as `schema` without
  any attributes or with a sole `type: object` attribute; this normally means
  that a user can supply _anything_. Either a generic type (`std::any` in
  C++17 or `void*` in C) or a generic JSON object such as Qt's `QJsonObject`
  (as long as the API is based on JSON structures) can be used for this purpose.

- `map`: this corresponds to OpenAPI's `patternProperties` (since GTAD 0.11) and
  `additionalProperties`. In terms of actual API these define an open list of properties without
  saying which names those properties must have: the API description file does not define property
  names, but only the mapped type. Similar to arrays, GTAD matches _formats_ under this _type_
  against the type defined inside `patternProperties`/`additionalProperties` (type of property
  values). A typical translation of that in static-typed languages involves a map from strings
  to structures; e.g. the current libQuotient uses `QHash<QString, {{1}}>` (`QHash<{{1}}, {{2}}`
  since GTAD 0.11, see the next paragraph) as the default data structure for `map` when the mapped
  data type is defined, `QVariantHash` for a generic map with no specific type, and as a special
  case, `std::unordered_map<QString, {{1}}>` (`std::unordered_map<{{1}}, {{2}}>` since GTAD 0.11,
  see the next paragraph) when the contained schema's title is `RoomState` because that type is
  uncopyable and therefore cannot be stored in a `QHash`.

  With support for `patternProperties` added in GTAD 0.11, it also became possible to specify
  the mapping for the property name type, thanks to an OpenAPI extension used in Matrix.org
  API definitions. The extension is a key-value pair with the key named `x-pattern-format` added
  for the given pattern, next to the definition of the property value type, e.g.:
  ```yaml
  patternProperties:
    "^@":
      type: object
      x-pattern-format: mx-user-id
      additionalProperties:
        type: number
  ```
  By default, the property name type is `string`, mapped to the target type according to usual
  rules. `x-pattern-format` allows to override that. To map `mx-user-id` to some other type than
  whatever `string` is mapped to, just add an `mx-user-id` entry to the list of formats under
  `string` type. E.g., the following configuration in `gtad.yaml`:
  ```yaml
  types:
    number: float
    string:
    - mx-user-id: UserId
    # ...
    map:
    - /.+/: "QHash<{{1}}, {{2}}>"
  ```
  would cause GTAD to translate the above `patternProperties` block into an additional `data` field
  with the type `QHash<UserId, float>`.

  Be mindful that GTAD pre-0.11 only used one parameter for target types in `map` and would ignore
  `patternProperties` entirely, only processing `additionalProperties`.

- `variant` (supported from GTAD 0.6): this is a case of variant types or
  multitypes. Similar to `map` and others, you can override certain type
  combinations and use a dedicated type for them. The _format_ pattern under
  `variant` should list the types separated by `,` (comma) in _exactly
  the same order as in the API description_ (`string,object` and
  `object,string` are distinct sequences). Also, beware that `null` is a
  reserved keyword in JSON/YAML, so OpenAPI's `null` type should be escaped
  with quotes (e.g., `"string,null"`).
  
  The list of types (for cases when the target type delimits the stored types,
  such `std::variant<>` from C++17) is stored in `{{types}}` variable:
  e.g. mapping to C++17 `std::variant<>` might look like:
  ```yaml
  variant:
    type: std::variant<{{#types}}{{_}}{{#_join}}, {{/_join}}{{/types}}>
    imports: <variant>
  ```

##### Advanced type mapping configuration

In case when an element type of an array or a property map is in turn
a container (an array, a map/`additionalProperties` or a variant) _and_ it
does not have a name, the _format_ can be specified as follows (no nesting,
`string[][]` is not supported):
- for arrays: `<elementType>[]`;
- for maps aka `additionalProperties`: `string-><elementType>`;
- for variants: `<elementType1>,<elementType2>,...` (exactly in the same
  order as in the API description).

You should _not_ put any whitespaces in those constructs. The consolidated
example follows:
```yaml
- array:
  # Here's also an example of passing the namespace as a custom `ns` type
  # attribute so that you could add or omit it in the code, depending
  # on your `using` context.
  - string: { type: vector<string>, ns: std }
  - int[]: vector<vector<int>> # A shortcut for { type: ... }
  - /string->.+/: # Any additionalProperties map with non-empty structure
      type: "QVector<QHash<QString, {{1}}>>"
      # You can use imports either as a string or as a list attribute in
      # configuration; here's the example of using it as a list.
      imports: [ <QVector>, <QHash> ]
  - /^string,null|null,string$/:
      QStringList # Because QString can assume null values
```
Generally though it's better (more readable) to assign a name to such
an element type and use this name instead - but you have to change the API
description for that.

It's not possible to use the same shortcut on the _type_ (top) level:
```yaml
- int[]: # will not work
- array:
  - int: # correct
```

##### `references`

This is a section where you configure the behaviour of the analyzer with respect to
[reference objects](https://github.com/OAI/OpenAPI-Specification/blob/main/versions/3.1.0.md#reference-object).
Before GTAD 0.11, there was a special `$ref` "type" that used to work in a way similar to
`references.replace` subsection.

Where value matching is performed under this configuration section, configuratin entries match
relative paths contained in `$ref` values as follows:
- if the `$ref` value in the API description is an external ref (contains a path leading to another
  API description file), it is matched as is;
- if the `$ref` value is a local reference, i.e. _starts_ with `#`, it is prepended by the relative
  path of the current API description file, based off the root path of the API description (passed
  to GTAD at invocation).

As in other match locations, an entry key has to either match the value in the API description
entirely byte by byte, or be a regular expression enclosed in `/`.

The `references` section includes the following (all optional) keys.

###### `inline`

This subsection is a list of patterns (strings or regexes) for schemas that must always be inlined.
Before GTAD 0.11, adding `_inline: true` to the type entry served the same purpose.

As described in the very beginning of the `Usage` section, GTAD usually represents files
included via `$ref` as separate type definitions in separate target files. Aside from the case
of local `$ref`s there's one more exception to that, when the loaded model turns out to be
"trivial" - containing exactly one schema that has an exactly one parent. In terms of OpenAPI,
its data definition either consists of a bare `type`, or itself is a reference object - basically,
an alias for another type. In that case, GTAD will replace usages of this schema with usages of
the original type and eliminate the schema from the generated files entirely (both
the definition and all usages).

If a given `$ref` matches any entry in the `inline` list, GTAD will attempt to apply the same
optimisation even if the loaded schema is non-trivial. This is useful in cases where an additional
level of indirection complicates the code without bringing value; e.g., you can unroll a top-level
response object to a series of response parameters this way. Not all schemas can be inlined;
if the $ref'ed schema itself consists of a `$ref` object _and_ parameters on top of that
(more generally: if there's an `allOf` instance with more than one list entry in the API
description) such schema will be imported as usual regardless of the `_inline` value - that is,
the type will be defined separately and used at the place of reference, with imports added
in the referring file.

As of GTAD 0.11 (and before), there's no way to force generation of a full-fledged definition for
a trivial schema (pre-0.11, `_inline: false` did nothing).

###### `replace`

This is a dictionary from patterns to `<targetTypeSpec>` entries (see `types` section above). If
a matching entry in this section is found for a given `$ref` value, it is used either to override
some of those types with another type entirely or to decorate usages  of the target schema with
additional attributes - similar to the effect of `types.schema` but applied before any resolution
takes place. A `type` key provided for a matching pattern means that the referenced schema in
the API description shall be entirely ignored and the target type provided under `type` (with
additional attributes, if any are defined next to it) shall be used instead. This allows to skip
generation of type definitions (and even whole files containing those), using a type defined
in the target language/SDK instead.
For example:
```yaml
references:
  # Coerce all types from files that have the path ending with "event.yaml" to type EventPtr
  # imported from "events.h"; the target API description files won't be opened at all
  /event.yaml$/: { type: EventPtr, imports: '"events.h"' }
  # For all other ref'ed types, resolve references as usual and set the 'referenced' attribute
  //: { referenced: }
```

Beware: supplying `type` in a catch-all (`//`) node in this section (e.g., `- //: { type: Ref }`)
will lead to substitution of _all_ not explicitly mentioned reference objects with the type
specified in this node. This is most likely not what you would want to do.

###### `importRenderer`

The import renderer mechanism has been introduced back in GTAD 0.8 but was configured by supplying
`_importRenderer` attribute for each type that needs it (effectively, for each externally defined
`schema`). Since GTAD 0.11, one renderer is configured for all imports that are not spelled out
explicitly in the configuration (i.e. if there's an `import` attribute then `importRenderer`
is not used for it).

An import renderer is a Mustache template called in the context where a given import is stored
in two forms: in complete but possibly half-baked (we'll get to that in a minute) form, and in a split form, as a Mustache list of path components comprising it. These two forms are placed in
`{{_}}` and `{{#segments}}` respectively. To give an example, if the import path is
`events/event_loader.h`, the Mustache context would be:
```yaml
{{_}}: 'events/event_loader.h' # without quotes
{{#segments}}: [ 'events', 'event_loader.h' ]
```
For simple cases when an import is provided verbatim in `gtad.yaml`, the default import renderer
(that simply inserts the contents of `{{_}}`) works just fine. However, when it comes to imports
produced from reference objects in the API description, `{{_}}` will store something like
`csapi/definitions/filter` (base output directory and the path _stem_ - that is, a path to a file
without its extension). To convert that to something usable, one would almost always need
to override the import renderer, for example:
```yaml
reference:
  importRenderer: '"{{#segments}}{{_}}{{#_join}}/{{/_join}}{{/segments}}.h"'
  # ...
```
`{{#join}}` in the example above is a predefined partial coming in any list context (see "Data
model exposed to Mustache" below); you can also define your own Mustache constants and partials
in `gtad.yaml` and use them within import renderers.

#### Printer configuration

The printer is essentially a Mustache generator that receives a certain context (mostly resembling
JSON structure) produced from the model made by the analyzer and from additional definitions,
as described below. For that reason, it's essential that you get acquainted with Mustache language
and its vocabulary; [the entire Mustache specification](https://mustache.github.io/mustache.5.html)
is a 5-minute read but the following section gives a quick overview of what's available. Originally
Mustache was made to render HTML but GTAD reconfigures the generator for C++ instead (so you
don't need to worry about `&`-escaping etc.).

##### Quick introduction into Mustache
Mustache template syntax boils down to 4 tag types:
- `{{variable}}` - direct substitution for a literal or result of a predefined
  0-argument function (a lambda) stored in the context under that name.
- `{{#section}}text{{/section}}` - the value stored in the context under that
  name is applied to a block between the opening and closing tags. Depending
  on the type of that value, it can be:
  - a genuine _section_, if the context has a hashmap for that name, or
    a literal or 0-argument function evaluating to non-false; the inner block
    is printed with substitution according to the "derived" context, which is
    the previously effective (inherited) context overloaded with this hashmap
    (if the section corresponds to a variable, no overloading occurs);
  - iteration over a _list_ - if the context has a list for that name,
    the inner block is rendered once for each element of the list, with
    substitutions done according to the "derived" context (see above) for that
    element of the list;
  - a 1-argument _lambda_ - if the context has a (predefined, in case of GTAD)
    function with that name, the function gets the inner block and renders it,
    optionally using the current context (definition of new lambdas without
    code rebuilding is unfortunately impossible due to the compiled nature
    of C++ and the lack of dynamically loaded plugins framework in GTAD);
- `{{>partial}}` - somewhat similar to `{{variable}}` but the _result_ of
  substitution is treated as a Mustache template itself, meaning that Mustache
  goes through it looking for more tags to substitute.
- `{{^invertedSection}}{{/invertedSection}}` - the same as a _genuine section_
  except that it checks that a particular value is _false_ in the current
  context (i.e. it's either absent or evaluates to false) and only renders
  the inner block if the value is not found. No context overloading occurs as
  there's nothing to overload it with.

Auxiliary tag types include a `{{!comment}}` tag and delimiter reassignment
(e.g., `{{=<% %>=}}` switches from the `{{`/`}}` pair to `<%`/`%>`)

As of GTAD 0.7, the printer configuration is stored in the top-level
`mustache` node and includes the following parts:

##### `delimiter`
Since GTAD 0.8, Mustache delimiters can be reassigned to a different pair. This
may be necessary to comfortably work with languages like Julia that use braces
for template specialisation (see #42). The value must be a string, with the
opening and closing sequences separated by a whitespace; e.g.:
`delimiter: '%| |%'
replaces `{{` with `%|` and `}}` with `|%`.

P.S. Previous versions accepted `_delimiter` under `constants` - unfortunately,
     it never could practically serve the intended purpose is it had to be
     defined in the beginning of every single Mustache snippet (file, constant
     or partial). Due to a significant change in behavior, the old location
     is _not_ supported any more.

##### `constants`
This is a string-to-string map that forms a part of the context for the Mustache
templating engine. Strings provided as keys correspond to Mustache _variables_
and values, respectively, are values of those variables. No further
interpolation of Mustache constructs takes place. Using of a constant `name` in
Mustache code is as simple as `{{name}}`.

##### `partials`
This string-to-mustache map is passed as is to the Mustache generator;
strings defined here are treated as Mustache _partials_; use them to
factor out often-used Mustache snippets in a manner you would use functions in
a programming language. Using one partial from another is perfectly fine; the
configuration file from libQuotient has several examples of such
inclusion. The standard Mustache syntax is used: to use a partial with the name
`myPartial` put `{{>myPartial}}` into your Mustache code and define
`myPartial: '(definition)'` in the configuration file. Since GTAD 0.7 you can
also include a partial defined in another file using the same syntax:
`{{>path/to/file}}` includes (and interpolates as Mustache code in its turn)
a file with the path `path/to/file` or, if that is not found,
`path/to/file.mustache`.

##### `templates`
This consists of two maps of extensions for generated files to Mustache
_templates_ used to generate each file: one map under `api` for API operation
descriptions and another one under `data` for data schemas. For a given
language, this is fairly static: in case of C/C++, it's likely to look like:
```yaml
templates:
  data:
    .h: "{{>data.h.mustache}}"
    .cpp: "{{>data.cpp.mustache}}" # if needed
  api:
    .h: "{{>operation.h.mustache}}"
    .cpp: "{{>operation.cpp.mustache}}" # if needed
```
This instructs GTAD to take the original file (API or data), strip `.yml` or
`.yaml` extension if it has one (other extensions will be preserved) and
generate two files (with `.h` and `.cpp` extensions respectively) for each
kind, using the respective Mustache template (that boils down to including
a partial from the respective `.mustache` files).

##### `outFilesList`
This node is not used in libQuotient but is there for convenience and 
possible future use. The value for this key specifies the name of the file that
will have the full list of generated file names upon GTAD completion. This can
further be used, e.g., in a build system to include generated files into the 
build sequence. The target file is not a Mustache template; its contents will
be entirely overwritten on every GTAD run.

##### Mustache tips and tricks

Mustache does not know anything about the target language and only does minimal
work to collapse/eliminate linebreaks (basically - if there's nothing on the
line except Mustache tags the extra linebreak will be eliminated). To relieve
the developer from having to position Mustache tags in a very specific way
only to get the formatting right GTAD 0.9 calls clang-format on the generated
files as the final stage (GTAD 0.8 and before did not do that but it was
still possible to achieve the same effect by calling clang-format after GTAD
- libQuotient used to do that in its CMakeLists.txt, in particular). If you
still need some way to eliminate extra linebreaks not removed by clang-format
note that the ending `}}` of any tag can be put on a new line. The minimal
way to consume a nasty linebreak is to just put a Mustache comment as follows:
```handlebars
{{!
}}
```

TODO: more tips and tricks

#### Data model exposed to Mustache

##### Predefined 
For now, GTAD provides one predefined Mustache tag. There might be more, eventually.
- `_titleCase` - does what you expect it to do to the passed text, e.g.
`{{#_titleCase}}plain_text{{/_titleCase}}` becomes `PlainText`. It does not support locales for now
but this may change in the future.

GTAD versions before 0.11 had `_cap`, `_toupper` and `_tolower` tags that are no more used and were
therefore discontinued. Also, `@filePartial` that allowed to load a Mustache template from another
file before GTAD 0.7, was removed as external files inclusion now works with the native partial
syntax: `{{>name}}` would first try to load a partial from the context (`gtad.yaml`); failing
that, from the file named `name`; and as a last resort, from the file named `name.mustache`.

GTAD has a few extensions to _lists_ compared to original Mustache:
- on the same level with the list `l` an additional boolean variable with the name `l?` (with
  the question mark suffix) is set to `true`. This allows you to write templates that get
  substituted no more than once even when a variable is a list (see the example below). Before
  version 0.10.2 GTAD had [a bug](https://github.com/KitsuneRal/gtad/issues/50) not resetting
  `l?` to false when lists with the same name were nested; 0.10.2 and later versions allow to nest
  lists with the same name without side effects.
- inside the list, a boolean variable `_join` is set to true for all elements
  except the last one. For (eventual) compatibility with Mustache templates
  used in swagger-codegen, there's a synonym `hasMore` equal to `_join`.
- because the above variables have to be created under the list context,
  lists of literals are exposed to templates as lists of hashmaps, with the
  original element value accessed at `{{_}}` instead of `{{.}}` that is
  usual for Mustache.
  
The following example demonstrates list-related tags coming together.
The template:
```handlebars
The list{{#list?}}: {{#list}}{{_}}{{#_join}}, {{/_join}}{{/list}}{{/list?
}}{{!See the note about linebreaks in "Tips and tricks" above
}}{{^list?}} is empty{{/list?}}
```
For the context: `{ "list": [1, 2, 3] }` the output will be:
`The list: 1, 2, 3`; for the empty context, it will be: `The list is empty`.
  

##### API data model

TODO
