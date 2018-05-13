# GTAD

[![license](https://img.shields.io/github/license/KitsuneRal/gtad.svg)](https://github.com/KitsuneRal/gtad/blob/master/LICENSE)
![status](https://img.shields.io/badge/status-beta-yellow.svg)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg?style=flat-square)](http://makeapullrequest.com)

GTAD (Generate Things from an API Description) is a work-in-progress generator of code from a Swagger/OpenAPI specification. Initially made to generate marshalling/unmarshalling C++ code for [Matrix CS API](https://matrix.org/docs/spec/client_server/unstable.html), it can be extended to support other API descriptions (possibly even spreading to other API description languages, such as RAML) and other programming languages with static type checking.

A brief introduction to the topic of API description languages (ADLs) can be found in [the talk at Qt World Summit 2017](https://youtu.be/W5TmRozH-rg) that also briefly announces the GTAD project.

## Contacts
 You can find Kitsune Ral (the author) at [#QMatrixClient:matrix.org](https://matrix.to/#/#qmatrixclient:matrix.org), the Matrix room.

You can also file issues at [the project's issue tracker](https://github.com/KitsuneRal/gtad/issues).

## Setting up and building
The source code is hosted at [GitHub](https://github.com/KitsuneRal/gtad/). Tags starting with `v` represent released versions; `rc` mark release candidates.

### Pre-requisites
- a Linux, OSX or Windows system (desktop versions tried; Ubuntu Touch is known to work; mobile Windows and iOS might work too but never tried)
  - For Ubuntu flavours - zesty or later (or a derivative) is good enough out of the box; older ones will need PPAs at least for a newer Qt; in particular, if you have xenial you're advised to add Kubuntu Backports PPA for it
- a Git client to check out this repo
- Qt 5 (either Open Source or Commercial), version 5.7 or higher
- CMake (from your package management system or [the official website](https://cmake.org/download/))
- a C++ toolchain supported by your version of Qt (see a link for your platform at [the Qt's platform requirements page](http://doc.qt.io/qt-5/gettingstarted.html#platform-requirements))
  - GCC 5 (Windows, Linux, OSX), Clang 5 (Linux), Apple Clang 8.1 (OSX) and Visual C++ 2015 (Windows) are the oldest officially supported; Clang 3.8 and GCC 4.9.2 are known to still work, maintenance patches for them are accepted
  - any build system that works with CMake and/or qmake should be fine: GNU Make, ninja (any platform), NMake, jom (Windows) are known to work.

#### Linux
Just install things from the list above using your preferred package manager. GTAD only uses a tiny subset of Qt Base so you can install as little of Qt as possible.

#### OS X
`brew install qt5` should get you a recent Qt5. If you plan to use CMake, you may need to tell it about the path to Qt by passing `-DCMAKE_PREFIX_PATH=<where-Qt-installed>`

#### Windows
1. Install Qt5 and CMake.
1. The commands in further sections imply that cmake is in your PATH - otherwise you have to prepend those commands with actual paths. As an option, it's a good idea to run a `qtenv2.bat` script that can be found in `C:\Qt\<Qt version>\<toolchain>\bin` (assuming you installed Qt to `C:\Qt`); the only thing it does is adding necessary paths to PATH. You might not want to run that script on system startup but it's very handy to setup the environment before building. Setting `CMAKE_PREFIX_PATH` in the same way as for OS X (see above) is fine too.

There are no official MinGW-based 64-bit packages for Qt. If you're determined to build with 64-bit Qt, either use a Visual Studio toolchain or build Qt5 yourself as described in Qt documentation.

### Building
In the root directory of the project sources:
```
mkdir build_dir
cd build_dir
cmake .. # Pass -DCMAKE_PREFIX_PATH and -DCMAKE_INSTALL_PREFIX here if needed
cmake --build . --target all
```
This will produce a gtad binary in `build_dir` inside your project sources. Installing is not supported yet.

## Usage

GTAD uses 3 inputs to generate "things":
1. Swagger/OpenAPI definition files, further referred to as OpenAPI files or OpenAPI definitions. Only [OpenAPI 2](https://github.com/OAI/OpenAPI-Specification/blob/master/versions/2.0.md) is supported yet (version 3 is in [the roadmap](https://github.com/KitsuneRal/gtad/projects/1#column-526169)). Each file is treated as a separate source. Notably, the referenced (via `$ref`) files are parsed independently from the referring ones; the generated code is supposed to import the files produced from the referenced OpenAPI definitions.
2. A configuration file in YAML. One GTAD invocation always uses one configuration file (but you can invoke GTAD separately for different OpenAPI files). The format of this file is described in detail below.
3. Source code template files. As of now, GTAD uses [Kainjow's Mustache implementation](https://github.com/kainjow/Mustache) for templating. This may change in the future (Lua looks as a very promising candidate to replace Mustache). GTAD exports the model for the API as a Mustache structure; this is covered in the respective section below.

A good example of GTAD usage can be found in [libQMatrixClient](https://github.com/QMatrixClient/libqmatrixclient/) that has its network request classes generated from OpenAPI definitions of [Matrix CS API](https://matrix.org/docs/spec/client_server/unstable.html). The CMakeLists.txt has a GTAD invocation line, using [gtad.yaml](https://github.com/QMatrixClient/libqmatrixclient/blob/master/lib/csapi/gtad.yaml) for the configuration file and [{{base}}.h.mustache](https://github.com/QMatrixClient/libqmatrixclient/blob/master/lib/csapi/%7B%7Bbase%7D%7D.h.mustache)/[{{base}}.cpp.mustache](https://github.com/QMatrixClient/libqmatrixclient/blob/master/lib/csapi/%7B%7Bbase%7D%7D.cpp.mustache) as templates for code generation. See also notes in that project's [CONTRIBUTING.md](https://github.com/QMatrixClient/libqmatrixclient/blob/master/CONTRIBUTING.md) and [CMakeLists.txt](https://github.com/QMatrixClient/libqmatrixclient/blob/master/CMakeLists.txt) for an idea how to integrate GTAD in your project.

### Invocation

GTAD is a command-line application; assuming that the `gtad` binary is in your PATH, the invocation line looks as follows:
```
gtad --config <configfile> --out <outdir> <files/dirs...>

```
The options are:
- `<configfile>` - the path to GTAD configuration file (see the next section)
- `<outdir>` - the (top-level) directory where the generated files (possibly a tree of them) will be put. Must exist before runnning GTAD.
- `<files/dirs...>` - a list of OpenAPI files or directories with those files to process. A hyphen appended to the filename means that the file must be skipped (allows to select a directory with files and then explicitly disable some files in it).

#### Dealing with referenced files

If a processed OpenAPI file has a `$ref` value referring to relative paths, the referred file will be added to the processing list (even if they were disabled in the command line as described above). The respective relative path will be created in the output directory, so if an OpenAPI file has `"$ref": "definitions/events.yml"`, the `<outdir>/definitions` directory will be created and the file(s) generated from `definitions/events.yml` will be put in there. Note that if `definitions/events.yml` has `"$ref": events/base.yml`, the `events` directory will be searched under input `definitions` directory, and a respective `<outdir>/definitions/events` directory will be made for output files from `base.yml` processing.

### GTAD configuration file

GTAD uses its configuration file to customise specific type mapping and file generating details. The file is made in YAML and consists, as of GTAD 0.5, of 2 main nodes: `analyzer` and `mustache`. As mentioned above, libQMatrixClient has the (working in production) example of a configuration file.

#### Analyzer configuration

Analyzer configuration, as of GTAD 0.5, includes the following parts.

##### `subst`
A regex-to-pattern map of substitutions that should be applied before any processing takes place - the effect is the same as if a `s/old/new/` regex were applied to the input file (assuming `old: new` entry in YAML). Be careful with such substitutions, as they ignore YAML/JSON structure of the input file; a careless regex can easily render the input invalid.
##### `identifiers`
This is a similar (in format) map of more fine-tuned substitutions, only applied to _identifiers_ encountered in OpenAPI. As of GTAD 0.5, it's only applied to call parameters and structure fields but not, e.g., call names.

One of the main cases for this is escaping parameter names that clash with language reserved words (`unsigned` in the example below) or otherwise undesirable as field/parameter names. The way this works is that you add, e.g., an `unsigned: unsignedData` line into `identifiers` section, and GTAD will transform all target parameter Mustache entries (such as `{{nameCamelCase}}`, see below in Data model exposed to Mustache) with the name `unsigned` to `unsignedData` while `{{baseName}}` of those parameters will remain intact so that you can still use it for JSON key names in actual API payloads in your template files.
##### `types`
This is the most important part of the analyzer configuration, defining which OpenAPI types and data structures turn to which target language types and structures in generated files. The format of this section is as follows:
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
where `targetTypeSpec` is either the target type literal (such as `double`) or, in turn,
```yaml
type: <target type literal>
imports: <filename> or [ <filenames...> ]
<custom key-value pairs>
```
`swaggerType` and `swaggerFormat`/`swaggerFormatRegEx` are matched against the OpenAPI _type_ and _format_ respectively (the [OpenAPI 2 specification](https://github.com/OAI/OpenAPI-Specification/blob/master/versions/2.0.md#TODO:find_a_good_anchor) defines standard _types_ and _formats_). On top of these GTAD understands additional "types" and "formats" necessary for good types mapping:
- Under type `object`: while the specification doesn't use _format_ with `object` type, GTAD uses the next level under object to further distinguish objects by their actual base type, which is contents either of `title` or, if any, `$ref`. This, in particular, allows you to override specific types that you don't want to generate automatically (in libQMatrixClient, e.g., these are `Event` and derived classes).
- Under type `array`: the next level is used to designate the element type; this way you can, e.g., special case an array of strings as `QStringList` while use `QVector<>` for arrays of all other types (including objects and other arrays). To enable passing the parameter type GTAD assumes that the contents of `type` key are themselves a template (in Mustache parlance - partials) and that parameter types stand for `{{1}}`, `{{2}}` etc. (actually, only case with `{{1}}` is actually applicable at the moment; there's nothing to fill even `{{2}}` with). Therefore, to use `QVector<>` to store arrays one has to use `QVector<{{1}}>` in the types map.
- A non-standard type `map`: this is what OpenAPI clumsily calls `additionalProperties` and the rest of the world knows as `property map` or `property list`. The trick here is that _names_ of properties are not defined by the API description file; only the mapped type is defined. Similar to arrays, GTAD considers "formats" under this "type" to be the types defined inside `additionalProperties`, in other words, mapped types. A typical translation of that into static-typed languages normally involves a map from strings to structures defined in the API description file; e.g. the current libQMatrixClient uses (mostly but not always) `QHash<QString, {{1}}>` for the purpose (other options include `QVariantHash` for a generic map with no specific type) and `std::unordered_map<QString, {{1}}>` when contained objects are uncopyable.
- A non-standard type `variant` is not (yet) supported by GTAD 0.5 and reserved for future use. It basically means: whatever type the value has, it can be covered by `QVariant`. This is a case of variant types and multitypes (and also the case where `{{2}}` etc. will be applicable).
- A non-standard "type" `schema`: this must not normally include the translated `type` inside - rather, it's a collection (map) of additional type attributes (see below) that should be passed along with types defined in-place in the API description file (rather than in a separate file and a `$ref` for it). This type may potentially be merged with _type_ `object`/_format_ `//` but stays for the time being.

Each `targetTypeSpec` (except the one for `schema`, see above) must unambiguously define the target type to be used - either inline or in its `type` property, as described above. However it may be needed to pass more attributes with type (e.g., whether the type is copyable); also, usage of most types requires importing those types; the import name should be passed along with the type. To address that, GTAD has a concept of "type attributes": every type can have an arbitrary number of arbitrary (except `type`) "attributes", which are modeled as string-to-string or string-to-list maps. At the moment GTAD special-cases the `imports` attribute, making a consolidated set of strings out of all imports (besides storing them with every type). This will be made more generic in the future.

#### Printer configuration

TODO

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

