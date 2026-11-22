QT += core gui testlib
QT -= widgets

CONFIG += c++11 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = tst_terminal

INCLUDEPATH += $$PWD/../../src

SOURCES += \
    tst_terminal.cpp \
    ../../src/ui/terminalclassifier.cpp \
    ../../src/ui/terminalhighlighter.cpp \
    ../../src/ui/terminalpalette.cpp

HEADERS += \
    ../../src/ui/terminalclassifier.h \
    ../../src/ui/terminalhighlighter.h \
    ../../src/ui/terminalpalette.h

RESOURCES += ../../resources/commands/commands.qrc

QMAKE_CXXFLAGS += -Wall -Wextra
