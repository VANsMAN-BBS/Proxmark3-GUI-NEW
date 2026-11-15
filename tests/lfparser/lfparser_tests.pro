QT += core testlib
QT -= gui

CONFIG += c++11 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = tst_lfparser

# Stage 1 regression suite, kept and pointed at the integrated Stage 2 sources.
# LFParserRegistry registers all 22 parsers, so the full source set is linked.
PARSER = $$PWD/../../src/lf/parser
INCLUDEPATH += $$PARSER/include
INCLUDEPATH += $$PARSER/include/lf

SOURCES += \
    tst_lfparser.cpp \
    $$PARSER/src/lf/lfparsetypes.cpp \
    $$PARSER/src/lf/lftextnormalizer.cpp \
    $$PARSER/src/lf/lfparserregistry.cpp \
    $$PARSER/src/lf/t55xxparser.cpp \
    $$PARSER/src/lf/parsers/em4100parser.cpp \
    $$PARSER/src/lf/parsers/hidproxparser.cpp \
    $$PARSER/src/lf/parsers/awidparser.cpp \
    $$PARSER/src/lf/parsers/indalaparser.cpp \
    $$PARSER/src/lf/parsers/ioproxparser.cpp \
    $$PARSER/src/lf/parsers/fdxbparser.cpp \
    $$PARSER/src/lf/parsers/paradoxparser.cpp \
    $$PARSER/src/lf/parsers/keriparser.cpp \
    $$PARSER/src/lf/parsers/vikingparser.cpp \
    $$PARSER/src/lf/parsers/pyramidparser.cpp \
    $$PARSER/src/lf/parsers/nexwatchparser.cpp \
    $$PARSER/src/lf/parsers/prescoparser.cpp \
    $$PARSER/src/lf/parsers/nedapparser.cpp \
    $$PARSER/src/lf/parsers/gproxiiparser.cpp \
    $$PARSER/src/lf/parsers/gallagherparser.cpp \
    $$PARSER/src/lf/parsers/pacparser.cpp \
    $$PARSER/src/lf/parsers/noralsyparser.cpp \
    $$PARSER/src/lf/parsers/jablotronparser.cpp \
    $$PARSER/src/lf/parsers/securakeyparser.cpp \
    $$PARSER/src/lf/parsers/visa2000parser.cpp \
    $$PARSER/src/lf/parsers/motorolaparser.cpp \
    $$PARSER/src/lf/parsers/idteckparser.cpp

HEADERS += \
    $$PARSER/include/lf/lfparsetypes.h \
    $$PARSER/include/lf/lftextnormalizer.h \
    $$PARSER/include/lf/lfparser.h \
    $$PARSER/include/lf/lfparserregistry.h \
    $$PARSER/include/lf/t55xxparser.h

# The module must stay warning free under the project's compiler.
QMAKE_CXXFLAGS += -Wall -Wextra
