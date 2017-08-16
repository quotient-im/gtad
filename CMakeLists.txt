cmake_minimum_required(VERSION 2.8.11) # Maybe works with even older versions

project(api-generator)
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
message( STATUS "                       api-generator Build Information" )
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
add_subdirectory(mustache)

list(APPEND api_generator_SRCS
        main.cpp
        translator.cpp
        analyzer.cpp
        formatting.cpp
        model.cpp
        exception.cpp
        printer.cpp
        yaml.cpp
)

add_executable(api-generator ${api_generator_SRCS})
target_include_directories(api-generator PRIVATE yaml-cpp/include)
target_link_libraries(api-generator Qt5::Core yaml-cpp)
