QT += core testlib
QT -= gui

CONFIG += c++11 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = tst_ntagmodel

MODEL = $$PWD/../../src/module/ntag
INCLUDEPATH += $$PWD/../../src/module

SOURCES += \
    tst_ntagmodel.cpp \
    $$MODEL/ntagmodel.cpp \
    $$MODEL/ndefbuilder.cpp \
    $$MODEL/ntagmemory.cpp

HEADERS += \
    $$MODEL/ntagmodel.h \
    $$MODEL/ndefbuilder.h \
    $$MODEL/ntagmemory.h

QMAKE_CXXFLAGS += -Wall -Wextra
