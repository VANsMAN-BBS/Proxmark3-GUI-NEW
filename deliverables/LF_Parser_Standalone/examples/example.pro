QT += core
QT -= gui

CONFIG += c++11 console
CONFIG -= app_bundle

TEMPLATE = app
TARGET = lf_parse_example

include(../lfparser.pri)

SOURCES += parse_text_example.cpp

QMAKE_CXXFLAGS += -Wall -Wextra
