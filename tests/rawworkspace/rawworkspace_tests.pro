QT += core gui widgets testlib

CONFIG += c++11 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = tst_rawworkspace

INCLUDEPATH += $$PWD/../../src

SOURCES += \
    tst_rawworkspace.cpp \
    ../../src/ui/rawworkspace.cpp \
    ../../src/ui/commandcatalogdelegate.cpp

HEADERS += \
    ../../src/ui/rawworkspace.h \
    ../../src/ui/commandcatalogdelegate.h

QMAKE_CXXFLAGS += -Wall -Wextra
