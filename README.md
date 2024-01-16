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
  - for Ubuntu flavours - bionic or newer (or a derivative) is good enough
  - [macOS 10.15 SDK or later is needed](https://developer.apple.com/documentation/xcode_release_notes/xcode_11_release_notes)
    due to `std::filesystem` dependency
- a Git client to check out this repo
- Qt 6 (either Open Source or Commercial)
- CMake 3.20 or newer (from your package management system or
  [the official website](https://cmake.org/download/))
- a C++ toolchain with solid C++20 support (concepts and `std::source_location`,
  in particular), that is: GCC 12 (Windows, Linux, OSX), Clang 14 (Linux),
  Xcode 15 (macOS 13), Visual C++ 19.30 (aka VS 2022 17.0), or newer
- any build system that works with CMake and/or qmake should be fine:
  GNU Make, ninja (any platform), NMake, jom (Windows) are known to work.
- for the actual invocation - clang-format in your PATH or CLANG_FORMAT variable
  having a full path to clang-format.

#### Linux
Just install things from the list above using your preferred package manager.
GTAD only uses a tiny subset of Qt Base so you can install as little of Qt as
possible.

#### OS X
`brew install qt5` should get you a recent Qt5. You may need to tell CMake
about the path to Qt by passing `-DCMAKE_PREFIX_PATH=<where-Qt-installed>`.

#### Windows
1. Install Qt5 and CMake.
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

Scope matching is especially useful to adjust the parameter name for
`additionalProperties` (see further in this document) in different schemas
and the "packed" response body name (by default it's always `data`) in
different calls. A "packed" body is a case when the entire JSON in
the request or response body is treated as a single piece (parameter or
returned value, respectively). In the opposite, "unpacked" case
the top-level JSON object in the request body or response body is
destructured to a series of parameters.

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
      type: <target type literal> # mandatory except for schema and $ref
      imports: <filename> or [ <filenames...> ] # optional
      ... # key-value pairs for custom type attributes, optional
  ```
  Each `<targetTypeSpec>` (except those in `schema` and `$ref`, see below)
  must unambiguously specify the target type to be used - either as a string
  (`bool`) or as an object with `type` property (`{ type: bool }`). For
  the purpose of proper rendering you will likely need to pass (and use in
  your Mustache templates) additional information about the mapped type -
  e.g., whether the type is copyable, whether it should be wrapped up in
  another type in case a parameter is optional, or which import - for C/C++
  it's a file for `#include` - should be added). To address that, GTAD has
  a concept of _type attributes_: every type can have an arbitrary number of
  "attributes" with arbitrary (except `type`) names, modeled as string-to-string
  or string-to-list mappings. `imports` is an example of a string-to-list
  mapping.
  
  At the moment GTAD special-cases `imports`: in addition to just passing this
  attribute along with the type name, it adds its contents to a "global" (per
  input file) deduplicated set, to simplify generation of import/include
  blocks. Since some of imports come from `$ref` keys in API descriptions
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
    - $ref: # ...
    - schema: # ...
  ```
Note that you should only specify any particular _type_/_format_ combination
no more than once. The lookup will stop on the first match, even if it only
specifies attributes, without a type.

###### Supported types and formats
As mentioned above, `swaggerType` and `swaggerFormat`/`swaggerFormatRegEx` are
matched against _type_ and _format_ specified in API description. The
[OpenAPI 2 specification](https://github.com/OAI/OpenAPI-Specification/blob/master/versions/2.0.md#user-content-data-types)
defines standard _types_ and _formats_; on top of these you can use
the following non-standard _types_/_formats_ in the GTAD configuration file
(but _not_ in the API descriptions):

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

- `schema`: this matches all types defined within the API definition as
  _schema_ structures, as long as these structures are not _trivial_.
  - a trivial schema is a wrapper around another type, such as `string` or a
    singular `$ref`, without additional parameter definitions; in other words,
    a schema with one base type that stores the same data as this base type).
    Such a schema will be inlined (=substituted with that type) in every
    possible occasion; so there's little sense in handling it.
  
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
    
- `$ref` (supported from GTAD 0.6): this matches types defined in a file 
  referenced by `$ref` and is used either to override some of those types
  with another type entirely or to add type attributes - similar to `schema`.
  "_Formats_" under `$ref` define patterns (regexes or literal strings) for
  referenced filenames, as they appear in the API description file. A `type`
  key provided for a certain pattern means that a file matching this pattern
  shall be entirely ignored and the target type provided under `type` shall
  be used instead. This allows to completely skip files generation for
  unwanted definitions. For example:
  ```yaml
  $ref:
    # Coerce all types from files that have the path ending with "event.yaml"
    # to type EventPtr; the files won't be opened at all
    /event.yaml$/: { type: EventPtr, imports: '"events.h"' }
    # For all other ref'ed types, resolve references as usual and set the
    # 'referenced' attribute
    //: { referenced: }
  ```
  Beware: supplying `type` in a catch-all (`//`) node in this section
  (e.g., `$ref: { //: { type: Ref } }`) will lead to coercion of _all_
  not explicitly mentioned $ref'ed types to the type specified in this node.
  
  Since GTAD 0.7, two special type attributes can be used in `$ref` blocks:
  - `title: newName` will override the name of the main (top-level) referenced
    structure with `newName`, disregarding any `title` it has in
    the API description.
  - `_inline: true` will attempt to "inline" the top-level structure.
    
    As described in the very beginning of the `Usage` section, GTAD usually
    represents files included via `$ref` as separate type definitions
    in separate target files. There's one exception to that: if the loaded
    model turns out to be "trivial" - containing exactly one schema that
    has an exactly one parent, basically, an alias for another type -
    GTAD will replace usages of this schema with usages of the original type
    and eliminate the schema from the generated files entirely (both for
    definition and for usage).
    
    If `_inline: true` is specified for a given `$ref` GTAD will attempt
    to apply the same optimisation even if the loaded schema is non-trivial.
    This is useful in cases where an additional level of indirection
    complicates the code without bringing value, e.g., to unroll the top-level
    response object to a series of response parameters instead. Not all schemas
    can be inlined; if the $ref'ed schema itself consists of a `$ref` to yet 
    another schema _and_ parameters on top of that (more generally: if there's
    an `allOf` instance with more than one list entry in the API description)
    such schema will be "imported" as usual regardless of the `_inline` value -
    that is, the type will be defined in a separate file and this type will
    be used at the place of reference, with imports added
    in the referring file.
    
    As of GTAD 0.7, `_inline: false` does nothing; eventually it may force
    generating a full-fledged type for a schema even when the schema is
    trivial.
    
- _type_ `object`: this is an entry (without _formats_ underneath) that
  describes the target type to be used when GTAD could not find any schema
  for it and the context implies that there are no restrictions on the type
  (but some data structure is still needed). Notably, this target type is used
  when an input parameter for an API call is described as `schema` without
  any attributes or with a sole `type: object` attribute; this normally means
  that a user can supply _anything_. Either a generic type (`std::any` in
  C++17 or `void*` in C) or a generic JSON object such as Qt's `QJsonObject`
  (as long as the API is based on JSON structures) can be used for this purpose.

- `map`: this is what OpenAPI clumsily calls `additionalProperties` but 
  a better name would probably be "property map" or "property list". 
  `additionalProperties` corresponds to a data structure mapping strings 
  (property names) to structures defined in the API description (property 
  values); the API description file does not define property names, only the 
  mapped type is defined. Similar to arrays, GTAD matches _formats_ under this
  _type_ against the type defined inside `additionalProperties` (type of
  property values). A typical translation of that in static-typed languages
  involves a map from strings to structures; e.g. the current libQuotient
  uses `QHash<QString, {{1}}>` as the default data structure for
  `additionalProperties` when the mapped data type is defined, `QVariantHash`
  for a generic map with no specific type, and, as a special case,
  `std::unordered_map<QString, {{1}}>` when the contained schema's title is 
  `RoomState` because that type is uncopyable.

- `variant` (supported from GTAD 0.6): this is a case of variant types or
  multitypes. Similar to `map` and others, you can override certain type
  combinations and use a dedicated type for them. The _format_ pattern under
  `variant` should list the types separated by `&` in _exactly
  the same order as in the API description_ (`string&object` and
  `object&string` are distinct sequences). Also, beware that `null` is a
  reserved keyword in JSON/YAML, so OpenAPI's `null` type should be escaped
  with quotes (e.g., `"string&null"`).
  
  The list of types (for cases when the target type delimits the stored types,
  such `std::variant<>` from C++17) is stored in `{{types}}` variable:
  e.g. mapping to C++17 `std::variant<>` might look like:
  ```yaml
  variant:
    type: std::variant<{{#types}}{{_}}{{#_join}}, {{/_join}}{{/types}}>
    imports: <variant>
  ```

##### Import renderers

Since GTAD 0.8, it is possible (and, actually, necessary) to define the way
imports will look in generated files. An import renderer is called in
the context where a given import is stored in two forms: in complete but
possibly half-baked (we'll get to that in a minute) form (in `{{_}}` - think of
an element inside the `{{imports}}` Mustache list), and in a split form, as
a Mustache list of path components comprising it (in `{{#segments}}` list).
To give an example, if the original import was `events/event_loader.h`,
the Mustache context would be:
```yaml
{{_}}: 'events/event_loader.h' # without quotes
{{#segments}}: [ 'events', 'event_loader.h' ]
```
For simple cases when an import is provided verbatim in `gtad.yaml`, the default
import renderer - that looks like `{{_}}` - works just fine. However, when it
comes to imports produced from `$ref` nodes in the API description, `{{_}}` will
store something like `csapi/definitions/filter` (base output directory and the
path _stem_ - that is, a path to a file without its extension. To convert that
to something usable in C/C++, one needs to override the import renderer by
supplying `_importRenderer` type attribute - for example:
```yaml
types:
- +set:
    _importRenderer: '"{{#segments}}{{_}}{{#_join}}/{{/_join}}{{/segments}}.h"'
  +on:
  - $ref:
    - # ...
```
You can use Mustache constants and partials defined in `gtad.yaml` within
import renderers.

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

#### Printer configuration

The printer is essentially a Mustache generator (see above) that receives a 
certain context (very much resembling JSON structure) produced from the model
made by the analyzer and from additional definitions, as described below.
For that reason, it's essential that you get acquainted with Mustache language
and its vocabulary;
[the entire Mustache specification](https://mustache.github.io/mustache.5.html)
is a 5-minute read but the following section gives a quick overview of what's
available. Originally Mustache has been made to render HTML but GTAD
reconfigures the generator for C++ instead (so you don't need to worry about
`&`-escaping etc.).

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
GTAD provides a small "library" of predefined Mustache tags. Most of them
start with `@` to avoid clashes with ordinary context values.
- `@cap` - capitalises the passed text; `{{#@cap}}text{{/@cap}}` is rendered
  as `Text`.
- `@toupper` and `@tolower` - do what you expect them to do to each letter of
  the passed text, e.g. `{{#@toupper}}tExt{{/@toupper}}` becomes `TEXT`.
- before GTAD 0.7, `@filePartial` allowed to load a Mustache template from
  another file, with the relative or absolute path passed as the argument.
  This is no more needed now that GTAD is on Kainjow Mustache v4.0 that 
  allows to use the normal Mustache `{{>partial}}` syntax to load a partial
  either from the context (`gtad.yaml`) or, failing that, from a file.

GTAD has a few extensions to _lists_ compared to original Mustache:
- on the same level with the list `l` an additional boolean variable with
  the name `l?` (with the question mark appended) is set to `true`. This
  allows you to write templates that get substituted no more than once even
  when a variable is a list (see the example below). Unfortunately, because
  false behaves as _null_ in this Mustache implementation, you cannot rely
  on nested lists with the same name `l` to have the correct `l?` value:
  the following snippet
  ```handlebars
  {{#l?}}{{#l}}{{#a}}
    {{!the following is supposed to be on the `l?` nested in `a`;
       but it checks the upper level `l?` when inner `l?` is empty or false}}
    {{^l?}}something{{/l?}}
  {{/a}}{{/l}}{{/l?}}
  ```
  will not emit `something` even if the inner `l` list is empty. Fixing this is
  [on the roadmap](https://github.com/KitsuneRal/gtad/issues/50).
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
  
Text-manipulating lambdas do not support locales since the main intention of
GTAD is to generate code (written in Latin script). This may change in
the future.

##### API data model

TODO

## Troubleshooting

#### Building fails

If `cmake` fails with...
```
CMake Warning at CMakeLists.txt:11 (find_package):
  By not providing "FindQt5Core.cmake" in CMAKE_MODULE_PATH this project
  has asked CMake to find a package configuration file provided by
  "Qt5Core", but CMake did not find one.
```
...then you need to set the right `-DCMAKE_PREFIX_PATH` variable, see above.

