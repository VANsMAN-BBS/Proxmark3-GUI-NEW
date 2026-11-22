QT += core testlib
QT -= gui

CONFIG += c++11 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = tst_commandcatalog

INCLUDEPATH += $$PWD/../../src

SOURCES += \
    tst_commandcatalog.cpp \
    ../../src/ui/commandcatalogmodel.cpp

HEADERS += \
    ../../src/ui/commandcatalogmodel.h

RESOURCES += ../../resources/commands/commands.qrc \
    ../../i18n/language.qrc

DEFINES += PROJECT_ROOT=\\\"$$PWD/../..\\\"

QMAKE_CXXFLAGS += -Wall -Wextra
