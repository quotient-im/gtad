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

GTAD uses its configuration file to customise specific type mapping and file
generating details. The file is made in YAML and consists, as of GTAD 0.5, of
2 main nodes: `analyzer` (Analyzer configuration) and `mustache`
(Printer configuration). As mentioned above, libQMatrixClient has the (working
in production) example of a configuration file.

#### Analyzer configuration

Analyzer configuration, as of GTAD 0.5, includes the following parts.

##### `subst`
A regex-to-pattern map of substitutions that should be applied before any
processing takes place - the effect is the same as if a `s/old/new/` regex were
applied to the input file (assuming `old: new` entry in YAML). Be careful with
such substitutions, as they ignore YAML/JSON structure of the input file;
a careless regex can easily render the input invalid.
##### `identifiers`
This is a similar (in format) map of more fine-tuned substitutions, only applied
to _identifiers_ encountered in OpenAPI. As of GTAD 0.5, it's only applied
to call parameters and structure fields but not, e.g., call names.

One of the main cases for this is escaping parameter names that clash with
language reserved words (`unsigned` in the example below) or otherwise
undesirable as field/parameter names. The way this works is that you add, e.g.,
an `unsigned: unsignedData` line into `identifiers` section, and GTAD will
transform all target parameter Mustache entries (such as `{{nameCamelCase}}`,
see below in Data model exposed to Mustache) with the name `unsigned` to
`unsignedData` while `{{baseName}}` of those parameters will remain intact so
that you can still use it for JSON key names in actual API payloads in your
template files.
##### `types`
This is the most important part of the analyzer configuration, defining which
OpenAPI types and data structures turn to which target language types and
structures in generated files. Before reading further I strongly recommend
to look at the types map in libQMatrixClient's `gtad.yaml`: it's one of those
cases when an example can better explain the matter than a close-to-formal
definition.

The format of this section is as follows:
```yaml
<swaggerType>: <targetTypeSpec>
```
or
```yaml
<swaggerType>:
  - <swaggerFormat>: <targetTypeSpec>
  - /<swaggerFormatRegEx>/: <targetTypeSpec>
  - //: <targetTypeSpec> # default, if the format doesn't mach anything above
```
where `targetTypeSpec` is either the target type literal (such as `double`) or,
in turn,
```yaml
type: <target type literal>
imports: <filename> or [ <filenames...> ]
<custom key-value pairs>
```
`swaggerType` and `swaggerFormat`/`swaggerFormatRegEx` are matched against
the OpenAPI _type_ and _format_ respectively (the
[OpenAPI 2 specification](https://github.com/OAI/OpenAPI-Specification/blob/master/versions/2.0.md#user-content-data-types)
defines standard _types_ and _formats_). On top of these GTAD understands
additional non-standard _types_ and _formats_:
- Under _type_ `object`: while the specification doesn't use _format_ with
  the `object` type, GTAD uses the next level under `object` type to further 
  distinguish objects by their actual "base type", which is defined as the value
  either of `title` or, if any, `$ref`. This, in particular, allows you to
  override specific types that you don't want to generate automatically (in
  libQMatrixClient, e.g., these are `EventPtr` and similar classes).
- Under _type_ `array`: in this case the next level is used to designate
  the element type; this way you can, e.g., special case an array of strings 
  as `QStringList` and use `QVector<>` for arrays of all other types (including
  objects and other arrays). To enable passing the parameter type GTAD assumes
  that the contents of `type` key are themselves a template (in Mustache
  parlance - _partials_) and that parameter types stand for `{{1}}`, `{{2}}`
  etc. (actually, only case with `{{1}}` is applicable at the moment; there's
  nothing to fill even `{{2}}` with). Therefore, to use `QVector<>` to store
  arrays one should use `QVector<{{1}}>` in the types map.
  - In case when the element type is in turn a container (an array or a map -
    see below), the _format_ can be specified in the following way (these 
    cannot be nested, `[[elementType]]` won't work):
    - for arrays: `[elementType]` (e.g. _type_ `array` with _format_
      `[string]` means an array of arrays of strings)
    - for maps: `{string:elementType}` (e.g. _type_ `map` with _format_ 
      `{string:double}` means `additionalProperties` (see below) with
      an element type `double`, which can be translated to a hashmap of
      strings to doubles)
- `map` _type_: this is what OpenAPI clumsily calls `additionalProperties` and
  the rest of the world knows as `property map` or `property list`. 
  `additionalProperties` corresponds to a data structure mapping strings 
  (property names) to structures defined in the API description (property 
  values); the API description file does not define property names; only the 
  mapped type is defined. Similar to arrays, GTAD matches _formats_ under this
  _type_ against the types defined inside `additionalProperties`, in other
  words, mapped types. A typical translation of that in static-typed languages
  involves a map from strings to structures; e.g. the current libQMatrixClient
  uses `QHash<QString, {{1}}>` as the default data structure for
  `additionalProperties` when the mapped data type is defined, `QVariantHash`
  for a generic map with no specific type, and, as a special-case,
  `std::unordered_map<QString, {{1}}>` when the contained schema's title is 
  `RoomState` because that type is uncopyable.
- `variant` _type_ is not (yet) supported by GTAD 0.5 and reserved for future
  use. In the current libQMatrixClient it basically means: whichever of 
  several types the value has, it will be stored in a `QVariant`. This is a case
  of variant types or multitypes.
- `schema` _type_: this stands for all types defined directly in the API 
  definition as _schema_ structures. As of now GTAD doesn't consider the 
  property `type` inside this node and only loads additional type attributes
  (see below) that should be passed along with such types defined in-place
  (rather than in a separate file with a corresponding `$ref`). In the 
  future this _type_ may be obsoleted in favor of using _type_ `object` and
  _format_ `//`.

Each `targetTypeSpec` (except the one for `schema`, see above) must
unambiguously define the target type to be used - either inline
(`boolean: bool`) or in its `type` property, as described above
(`boolean: { type: bool }`). For the purpose of proper rendering by the 
printer you will likely need to pass additional information with the mapped type
(e.g., whether the type is copyable, or which import - a file for `#include`,
in C/C++ - should be added). To address that, GTAD has a concept of
_type attributes_: every type can have an arbitrary number of 
"attributes" with arbitrary (except `type`) names, modeled as string-to-string
or string-to-list maps. At the moment GTAD special-cases the `imports`
attribute, making a deduplicated set of strings out of all imports (besides
storing them with every type). This will be made more generic in the future.

#### Printer configuration

The printer is essentially a Mustache generator (see above) that receives a 
certain context produced from the model made by the analyzer and additional 
definitions as described below. The printer configuration is stored in
the top-level `mustache` node and includes the following parts:

##### `definitions`
This string-to-mustache map is passed as is to the Mustache generator; 
strings defined here are treated as Mustache _partials_; use them to
factor out often-used Mustache snippets in a manner you would use functions in
a programming language. Using one partial from another is perfectly fine; the
configuration file from libQMatrixClient has several examples of such inclusion.

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

### Data model exposed to Mustache

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

