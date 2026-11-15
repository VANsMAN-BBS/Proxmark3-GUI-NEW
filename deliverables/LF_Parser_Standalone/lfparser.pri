# Shared qmake include for the LF Parser library.
# Usage: include(path/to/lfparser.pri)

QT += core

INCLUDEPATH += $$PWD/include
INCLUDEPATH += $$PWD/src

SOURCES += \
    $$PWD/src/lf/lfparsetypes.cpp \
    $$PWD/src/lf/lftextnormalizer.cpp \
    $$PWD/src/lf/lfparserregistry.cpp \
    $$PWD/src/lf/t55xxparser.cpp \
    $$PWD/src/lf/parsers/em4100parser.cpp \
    $$PWD/src/lf/parsers/hidproxparser.cpp \
    $$PWD/src/lf/parsers/awidparser.cpp \
    $$PWD/src/lf/parsers/indalaparser.cpp \
    $$PWD/src/lf/parsers/ioproxparser.cpp \
    $$PWD/src/lf/parsers/fdxbparser.cpp \
    $$PWD/src/lf/parsers/paradoxparser.cpp \
    $$PWD/src/lf/parsers/keriparser.cpp \
    $$PWD/src/lf/parsers/vikingparser.cpp \
    $$PWD/src/lf/parsers/pyramidparser.cpp \
    $$PWD/src/lf/parsers/nexwatchparser.cpp \
    $$PWD/src/lf/parsers/prescoparser.cpp \
    $$PWD/src/lf/parsers/nedapparser.cpp \
    $$PWD/src/lf/parsers/gproxiiparser.cpp \
    $$PWD/src/lf/parsers/gallagherparser.cpp \
    $$PWD/src/lf/parsers/pacparser.cpp \
    $$PWD/src/lf/parsers/noralsyparser.cpp \
    $$PWD/src/lf/parsers/jablotronparser.cpp \
    $$PWD/src/lf/parsers/securakeyparser.cpp \
    $$PWD/src/lf/parsers/visa2000parser.cpp \
    $$PWD/src/lf/parsers/motorolaparser.cpp \
    $$PWD/src/lf/parsers/idteckparser.cpp

HEADERS += \
    $$PWD/include/lf/lfparsetypes.h \
    $$PWD/include/lf/lftextnormalizer.h \
    $$PWD/include/lf/lfparser.h \
    $$PWD/include/lf/lfparserregistry.h \
    $$PWD/include/lf/t55xxparser.h
