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
also briefly announces the GTAD project.

## Contacts
 You can find Kitsune Ral (the author) in Matrix room
 [#QMatrixClient:matrix.org](https://matrix.to/#/#qmatrixclient:matrix.org).

You can also file issues at
[the project's issue tracker](https://github.com/KitsuneRal/gtad/issues).

## Setting up and building
The source code is hosted at [GitHub](https://github.com/KitsuneRal/gtad/). Tags
starting with `v` represent released versions; `rc` mark release candidates.
Do remember to use `--recursive` or update submodules after checking out -
the project has external dependencies taken in as submodules (this may change
in the future).

### Pre-requisites
- a Linux, OSX or Windows system (desktop versions tried; Ubuntu Touch is known
  to work; mobile Windows and iOS might work too but never tried)
  - For Ubuntu flavours - zesty or later (or a derivative) is good enough out
    of the box; older ones will need PPAs at least for a newer Qt; in 
    particular, if you have xenial you're advised to add Kubuntu Backports PPA
    for it
- a Git client to check out this repo
- Qt 5 (either Open Source or Commercial), version 5.7 or higher (to be 
  phased out once the project switches to C++17 - not yet)
- CMake (from your package management system or
  [the official website](https://cmake.org/download/))
- a C++ toolchain supported by your version of Qt (see a link for your platform
  at [the Qt's platform requirements page](http://doc.qt.io/qt-5/gettingstarted.html#platform-requirements))
  - GCC 5 (Windows, Linux, OSX), Clang 5 (Linux), Apple Clang 8.1 (OSX) and
    Visual C++ 2015 (Windows) are the oldest officially supported; Clang 3.8 and
    GCC 4.9.2 are known to still work, maintenance patches for them are accepted
  - any build system that works with CMake and/or qmake should be fine:
    GNU Make, ninja (any platform), NMake, jom (Windows) are known to work.

#### Linux
Just install things from the list above using your preferred package manager.
GTAD only uses a tiny subset of Qt Base so you can install as little of Qt as
possible.

#### OS X
`brew install qt5` should get you a recent Qt5. If you plan to use CMake, you
may need to tell it about the path to Qt by passing
`-DCMAKE_PREFIX_PATH=<where-Qt-installed>`

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

There are no official MinGW-based 64-bit packages for Qt. If you're determined
to build with 64-bit Qt, either use a Visual Studio toolchain or build Qt5
yourself as described in Qt documentation.

### Building
In the root directory of the project sources:
```
mkdir build_dir
cd build_dir
cmake .. # Pass -DCMAKE_PREFIX_PATH and -DCMAKE_INSTALL_PREFIX here if needed
cmake --build . --target all
```
This will produce a gtad binary in `build_dir` inside your project sources.
Installing is not supported yet.

## Usage

GTAD uses 3 inputs to generate "things":
1. Swagger/OpenAPI definition files, further referred to as OpenAPI files or
   OpenAPI definitions. Only
   [OpenAPI 2](https://github.com/OAI/OpenAPI-Specification/blob/master/versions/2.0.md)
   is supported yet (version 3 is in
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
   templating. This may change in the future (Lua looks as a very promising 
   candidate to replace Mustache). GTAD exports the model for the API as
   a Mustache structure; this is covered in the respective section below.

A good example of GTAD usage can be found in
[libQMatrixClient](https://github.com/QMatrixClient/libqmatrixclient/) that has
its network request classes generated from OpenAPI definitions of
[Matrix CS API](https://matrix.org/docs/spec/client_server/unstable.html).
The CMakeLists.txt has a GTAD invocation line, using
[gtad.yaml](https://github.com/QMatrixClient/libqmatrixclient/blob/master/lib/csapi/gtad.yaml)
for the configuration file and
[{{base}}.h.mustache](https://github.com/QMatrixClient/libqmatrixclient/blob/master/lib/csapi/%7B%7Bbase%7D%7D.h.mustache)/
[{{base}}.cpp.mustache](https://github.com/QMatrixClient/libqmatrixclient/blob/master/lib/csapi/%7B%7Bbase%7D%7D.cpp.mustache)
as templates for code generation. See also notes in that project's
[CONTRIBUTING.md](https://github.com/QMatrixClient/libqmatrixclient/blob/master/CONTRIBUTING.md)
and
[CMakeLists.txt](https://github.com/QMatrixClient/libqmatrixclient/blob/master/CMakeLists.txt)
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
above, libQMatrixClient has the (working in production) example of
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
This is a similar (in format) map of more fine-tuned substitutions, only applied
to _identifiers_ encountered in OpenAPI. As of GTAD 0.6, it's only applied
to call parameters and structure fields but not, e.g., call names.

One of the main cases for this is escaping parameter names that clash with
language reserved words (`unsigned` in the example below) or otherwise
undesirable as field/parameter names. If you add, e.g.,
`unsigned: unsignedData` to the `identifiers` section, GTAD will transform
all target parameter names `unsigned` to `unsignedData`. The Mustache
configuration will have both the original (`{{baseName}}`) and the transformed
(`{{paramName}}` or `{{nameCamelCase}}`) names of those parameters so that you
can still use it for JSON key names in actual API payloads in your template
files.

##### `types`
This is the biggest and the most important part of the analyzer configuration,
defining which OpenAPI types and data structures become which target language
types and structures in generated files. Before moving on further I strongly
recommend to open the types map in libQMatrixClient's `gtad.yaml` next to this
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
  optional and is not processed), otherwise it's used as a literal
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
  another type in case a parameter is optional, or which import - a file for
  `#include`, in C/C++ - should be added). To address that, GTAD has a concept
  of _type attributes_: every type can have an arbitrary number of "attributes"
  with arbitrary (except `type`) names, modeled as string-to-string or
  string-to-list mappings. `imports` is an example of a string-to-list mapping.
  At the moment GTAD special-cases this attribute: in addition to just passing
  it along with the type name, it adds its contents to a "global" (per input
  file) deduplicated set, in order to allow painless generation of `#include`
  blocks in C/C++ code. This will be made more generic in the future.

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
    singular `$ref`, without additional parameter definitions; in other words, a
    schema with one base type that stores the same data as this base type).
  
  The list of "_formats_" inside `schema` allows to specify types, for which
  special target types/attributes should be used. Same as for other types,
  _formats_ under `schema` are either regexes if they start with `/`
  or literal case-sensitive strings otherwise. _Formats_ match:
  - the `title` attribute provided within the schema definition of API file;
  - failing that, schema's title as calculated after resolving `$ref`
    attributes (note that some `$ref`'s can be intercepted, as described
    in the next bullet)
    
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
    
- _type_ `object`: this is an entry (without _formats_ underneath) that
  describes the target type to be used when GTAD could not find any schema
  for it and the context implies that there are no restrictions on the type
  (but some data structure is still needed). Notably, this target type is used
  when an input parameter for an API call is described as `schema` without
  any attributes or with a sole `type: object` attribute; this normally means
  that a user can supply _anything_. Either a generic type (`std::any` in
  C++17 or `void*` in C) or a generic JSON object (as long as the API is based
  on JSON structures) such as Qt's `QJsonObject` can be used for this purpose.

- `map`: this is what OpenAPI clumsily calls `additionalProperties` and
  the rest of the world knows as a property map or a property list. 
  `additionalProperties` corresponds to a data structure mapping strings 
  (property names) to structures defined in the API description (property 
  values); the API description file does not define property names, only the 
  mapped type is defined. Similar to arrays, GTAD matches _formats_ under this
  _type_ against the type defined inside `additionalProperties` (type of
  property values). A typical translation of that in static-typed languages
  involves a map from strings to structures; e.g. the current libQMatrixClient
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
    std::variant<{{#types}}{{_}}{{#@join}}, {{/@join}}{{/@types}}>
    imports: <variant>
  ```

##### Advanced type mapping configuration

In case when an element type of an array or a property map is in turn
a container (an array, a map/`additionalProperties` or a variant) _and_
it does not have a name, the _format_ can be specified as follows (no nesting,
`string[][]` is not
supported):
- for arrays: `<elementType>[]`
- for maps aka `additionalProperties`: `string-><elementType>`
- for variants: `<elementType1>,<elementType2>,...` (in the same order as in
the API description)
You should _not_ put any whitespaces in those constructs. The consolidated
example follows:
```yaml
- array:
  # Here's also an example of passing the namespace as a separate type
  # attribute so that you could add or omit it in the code, depending
  # on your `using` context.
- string: { type: vector<string>, ns: std }
- int[]: { type: vector<vector<int>> }
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
    of C++);
- `{{>partial}}` - somewhat similar to `{{variable}}` but the _result_ of
  substitution is treated as a Mustache template itself, meaning that Mustache
  goes over it looking for more tags to substitute.
- `{{^invertedSection}}{{/invertedSection}}` - the same as a _genuine section_
  except that it checks that a particular value is _false_ in the current
  context (i.e. it's either absent or evaluates to false) and only renders
  the inner block if the value is not found. No context overloading occurs as
  there's nothing to overload it with.

Supporting tag types include a `{{!comment}}` and delimiter reassignment
(`{{=<% %>=}}` switches from the `{{`/`}}` pair to `<%`/`%>`)

The printer configuration is stored in the top-level `mustache` node and
includes the following parts:

##### `definitions`
This string-to-mustache map is passed as is to the Mustache generator; 
strings defined here are treated as Mustache _partials_; use them to
factor out often-used Mustache snippets in a manner you would use functions in
a programming language. Using one partial from another is perfectly fine; the
configuration file from libQMatrixClient has several examples of such
inclusion. As of GTAD 0.6, including _partials_ from another file using the
usual Mustache syntax is not supported but GTAD provides a workaround macro
`{{#@filePartial}}` that can be used either from `definitions` or directly
from Mustache templates.

##### `templates`
This is the YAML array of file _templates_ used to generate output files.
For a given language, this is fairly static: in case of C/C++, it's a pair of
entries: `{{base}}.h.mustache` and `{{base}}.cpp.mustache`. As you might have
guessed, template file names are themselves Mustache templates; `{{base}}` is
replaced with the original API description file name (if it has `.yml` or `
.yaml` extension, it will be stripped; other extensions will be preserved).

##### `outFilesList`
This node is not used in libQMatrixClient but is there for convenience and 
possible future use. The value for this key specifies the name of the file that
will have the full list of generated file names upon GTAD completion. This can
further be used, e.g., in a build system to include generated files into the 
build sequence. The target file is not a Mustache template; its contents will
be entirely overwritten on every GTAD run.

##### Mustache tips and tricks

The used implementation of Mustache is very literal with respect to newlines:
it does not try to collapse/eliminate any of them around Mustache tags. This
becomes quite a problem if you try to get the nicely formatted text directly
from GTAD. To save effort on repositioning Mustache tags desperately in order
to get the formatting right (that can be seen in libQMatrixClient's templates)
it's much more labour-efficient to feed GTAD output to clang-format or a similar
code formatter. If you still need some way to eliminate extra newlines, note
that the ending `}}` of any tag can be put on a newline. Hence, the minimal
way to consume a nasty newline is to just put a Mustache comment as follows:
```handlebars
(a lot of text, I really want to break it {{!
}}in two lines but only in the template)
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
- `@filePartial` - allows to load a Mustache template from another file,
  with the relative or absolute path passed as the argument. This augments
  the limitation of older Kainjow's Mustache library that prevented GTAD from
  using the normal `{{>partial}}` syntax for the same case. Subject to
  deprecation once the conventional syntax is supported (as of GTAD 0.6,
  not yet).

GTAD has a few extensions to _lists_ compared to original Mustache:
- on the same level with the list `l` an additional boolean variable with
  the name `l?` (with the question mark appended) is set to `true`. This
  allows you to write templates that get substituted no more than once even
  when a variable is a list (see the example below).
- inside the list, a boolean variable `@join` is set to true for all elements
  except the last one. For compatibility with Mustache templates used in
  swagger-codegen, there's a synonym `hasMore` equal to `@join`.
- because the above variables have to be created under the list context,
  lists of literals are exposed to templates as lists of hashmaps, with the
  original element value saved to `{{_}}`.
  
The following example demonstrates list-related tags coming together.
The template:
```handlebars
The list{{#list?}}: {{#list}}{{_}}{{#@join}}, {{/@join}}{{/list}}{{/list?}}{{!
See the note about newlines in "Tips and tricks" above
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

