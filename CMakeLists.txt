cmake_minimum_required(VERSION 3.8) # Older ones don't know about C++17

project(gtad VERSION 0.9 LANGUAGES CXX)

include(CheckCXXCompilerFlag)

# Set a default build type if none was specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Debug' as none was specified")
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build" FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
    "MinSizeRel" "RelWithDebInfo")
endif()

set(CMAKE_CXX_STANDARD 17)

# Setup command line parameters for the compiler and linker
if (MSVC)
    add_compile_options(/EHsc /W3
        /wd4100 /wd4127 /wd4242 /wd4244 /wd4245 /wd4267 /wd4365 /wd4456 /wd4459
        /wd4464 /wd4505 /wd4514 /wd4571 /wd4619 /wd4623 /wd4625 /wd4626 /wd4706
        /wd4710 /wd4774 /wd4820 /wd4946 /wd5026 /wd5027)
else ()
    foreach (FLAG "" all pedantic extra no-unused-parameter
            no-unused-lambda-capture no-shadow-field-in-constructor)
        CHECK_CXX_COMPILER_FLAG("-W${FLAG}" WARN_${FLAG}_SUPPORTED)
        if (WARN_${FLAG}_SUPPORTED AND NOT CMAKE_CXX_FLAGS MATCHES "(^| )-W?${FLAG}($| )")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W${FLAG}")
        endif ()
    endforeach ()
endif ()

find_package(Qt5 5.7 REQUIRED Core)
get_filename_component(Qt5_Prefix "${Qt5_DIR}/../../../.." ABSOLUTE)
set(CMAKE_AUTOMOC OFF)

message( STATUS )
message( STATUS "=============================================================================" )
message( STATUS "                       GTAD Build Information" )
message( STATUS "=============================================================================" )
if (CMAKE_BUILD_TYPE)
    message( STATUS "Build type: ${CMAKE_BUILD_TYPE}")
endif(CMAKE_BUILD_TYPE)
message( STATUS "Using compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}" )
message( STATUS "Using Qt ${Qt5_VERSION} at ${Qt5_Prefix}" )
message( STATUS "=============================================================================" )
message( STATUS )

option(YAML_CPP_BUILD_TESTS "Enable yaml-cpp tests" OFF)
option(YAML_CPP_BUILD_TOOLS "Enable yaml-cpp tools" OFF)
option(YAML_CPP_BUILD_CONTRIB "Enable contrib stuff in library" OFF)
add_subdirectory(yaml-cpp)
#add_subdirectory(mustache) # This is only needed to build mustache tests

list(APPEND SRCS
    main.cpp
    translator.cpp
    analyzer.cpp
    model.cpp
    printer.cpp
    yaml.cpp
    util.cpp
)

add_executable(${CMAKE_PROJECT_NAME} ${SRCS})
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE yaml-cpp/include)
if (CMAKE_CXX_COMPILER_ID STREQUAL GNU AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.1)
    set(std_fs_lib "stdc++fs")
elseif(CMAKE_CXX_COMPILER_ID EQUAL Clang AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    set(std_fs_lib "c++fs")
endif()
target_link_libraries(${CMAKE_PROJECT_NAME} Qt5::Core yaml-cpp ${std_fs_lib})

install(TARGETS ${CMAKE_PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
