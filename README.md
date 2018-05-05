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

A good introduction to GTAD usage can be found in [libQMatrixClient](https://github.com/QMatrixClient/libqmatrixclient/) that has its network request classes generated from OpenAPI definitions of [Matrix CS API](https://matrix.org/docs/spec/client_server/unstable.html). The CMakeLists.txt has a typical GTAD invocation line, using [gtad.yaml](https://github.com/QMatrixClient/libqmatrixclient/blob/master/lib/csapi/gtad.yaml) for the configuration file and [{{base}}.h.mustache](https://github.com/QMatrixClient/libqmatrixclient/blob/master/lib/csapi/%7B%7Bbase%7D%7D.h.mustache)/[{{base}}.cpp.mustache](https://github.com/QMatrixClient/libqmatrixclient/blob/master/lib/csapi/%7B%7Bbase%7D%7D.cpp.mustache) as templates for code generation. See also notes in that project's [CONTRIBUTING.md](https://github.com/QMatrixClient/libqmatrixclient/blob/master/CONTRIBUTING.md) and [CMakeLists.txt](https://github.com/QMatrixClient/libqmatrixclient/blob/master/CMakeLists.txt) for an idea how to integrate GTAD in your project.

### Invocation

TODO

### GTAD configuration file

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

