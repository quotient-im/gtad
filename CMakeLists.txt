cmake_minimum_required(VERSION 2.8.11) # Maybe works with even older versions

project(matrix-csapi-generator)
enable_language(CXX)

include(CheckCXXCompilerFlag)

# Set a default build type if none was specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Debug' as none was specified")
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build" FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
    "MinSizeRel" "RelWithDebInfo")
endif()

if ( CMAKE_VERSION VERSION_LESS "3.1" )
    CHECK_CXX_COMPILER_FLAG("-std=c++11" STD_FLAG_SUPPORTED)
    if ( STD_FLAG_SUPPORTED )
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
    endif ( STD_FLAG_SUPPORTED )
else ( CMAKE_VERSION VERSION_LESS "3.1" )
    set(CMAKE_CXX_STANDARD 11)
endif ( CMAKE_VERSION VERSION_LESS "3.1" )

find_package(Qt5 5.2.1 REQUIRED Core)
get_filename_component(Qt5_Prefix "${Qt5_DIR}/../../../.." ABSOLUTE)
set(CMAKE_AUTOMOC OFF)

message( STATUS )
message( STATUS "=============================================================================" )
message( STATUS "                       matrix-csapi-generator Build Information" )
message( STATUS "=============================================================================" )
if (CMAKE_BUILD_TYPE)
    message( STATUS "Build type: ${CMAKE_BUILD_TYPE}")
endif(CMAKE_BUILD_TYPE)
message( STATUS "Using compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}" )
message( STATUS "Using Qt ${Qt5_VERSION} at ${Qt5_Prefix}" )
message( STATUS "=============================================================================" )
message( STATUS )

option(YAML_CPP_BUILD_TOOLS "Enable testing and parse tools" OFF)
option(YAML_CPP_BUILD_CONTRIB "Enable contrib stuff in library" OFF)
add_subdirectory(yaml-cpp)

list(APPEND api_generator_SRCS
    main.cpp
        generator.cpp
    swagger_analyzer.cpp
    scope.cpp
)

add_executable(api-generator ${api_generator_SRCS})
target_include_directories(api-generator PRIVATE yaml-cpp/include)
target_link_libraries(api-generator Qt5::Core yaml-cpp)

set (API_YAML_DIR "${PROJECT_SOURCE_DIR}/matrix-doc/api/client-server"
    CACHE STRING "Path to CS API files in Swagger format")
set (GENERATED_API_DIR "${PROJECT_BINARY_DIR}/api"
    CACHE STRING "Path to directory with generated C++ API files")

file(GLOB API_YAML_FILES RELATIVE "${PROJECT_SOURCE_DIR}" "${API_YAML_DIR}/*.yaml")
list(FILTER API_YAML_FILES EXCLUDE REGEX "/(old_sync|room_initial_sync)\\.")
list(LENGTH API_YAML_FILES API_SIZE)

foreach(_yaml_file ${API_YAML_FILES})
    get_filename_component(_f ${_yaml_file} NAME_WE)
    set(_cppfile ${GENERATED_API_DIR}/${_f}.cpp)
    add_custom_command(
        OUTPUT ${_cppfile}
        COMMAND api-generator --out ${GENERATED_API_DIR} ${_yaml_file}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        MAIN_DEPENDENCY ${_yaml_file}
        DEPENDS api-generator
        COMMENT Generating C++ stubs for ${_yaml_file}
    )

    list(APPEND generated_SRCS ${_cppfile})
endforeach()
add_custom_target(api DEPENDS ${generated_SRCS})
