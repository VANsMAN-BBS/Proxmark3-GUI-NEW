find_package(Qt5 COMPONENTS Core Test REQUIRED)

add_executable(tst_lfparser
    parser_tests/tst_lfparser.cpp
)

target_include_directories(tst_lfparser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tst_lfparser PRIVATE lfparser Qt5::Test)

add_test(NAME lfparser COMMAND tst_lfparser)
