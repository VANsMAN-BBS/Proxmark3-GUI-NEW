cmake_minimum_required(VERSION 3.16)

project(LfParserStandalone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt5 COMPONENTS Core REQUIRED)

set(LF_PARSER_SOURCES
    src/lf/lfparsetypes.cpp
    src/lf/lftextnormalizer.cpp
    src/lf/lfparserregistry.cpp
    src/lf/t55xxparser.cpp
    src/lf/parsers/em4100parser.cpp
    src/lf/parsers/hidproxparser.cpp
    src/lf/parsers/awidparser.cpp
    src/lf/parsers/indalaparser.cpp
    src/lf/parsers/ioproxparser.cpp
    src/lf/parsers/fdxbparser.cpp
    src/lf/parsers/paradoxparser.cpp
    src/lf/parsers/keriparser.cpp
    src/lf/parsers/vikingparser.cpp
    src/lf/parsers/pyramidparser.cpp
    src/lf/parsers/nexwatchparser.cpp
    src/lf/parsers/prescoparser.cpp
    src/lf/parsers/nedapparser.cpp
    src/lf/parsers/gproxiiparser.cpp
    src/lf/parsers/gallagherparser.cpp
    src/lf/parsers/pacparser.cpp
    src/lf/parsers/noralsyparser.cpp
    src/lf/parsers/jablotronparser.cpp
    src/lf/parsers/securakeyparser.cpp
    src/lf/parsers/visa2000parser.cpp
    src/lf/parsers/motorolaparser.cpp
    src/lf/parsers/idteckparser.cpp
)

add_library(lfparser STATIC ${LF_PARSER_SOURCES})

target_include_directories(lfparser
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(lfparser PUBLIC Qt5::Core)

option(LF_PARSER_BUILD_TESTS "Build the QtTest suite" ON)
option(LF_PARSER_BUILD_EXAMPLES "Build the example program" ON)

if(LF_PARSER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(LF_PARSER_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
