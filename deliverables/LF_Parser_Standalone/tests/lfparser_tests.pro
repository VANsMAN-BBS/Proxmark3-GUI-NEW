QT += core testlib
QT -= gui

CONFIG += c++11 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = tst_lfparser

include(../lfparser.pri)

SOURCES += parser_tests/tst_lfparser.cpp

QMAKE_CXXFLAGS += -Wall -Wextra
