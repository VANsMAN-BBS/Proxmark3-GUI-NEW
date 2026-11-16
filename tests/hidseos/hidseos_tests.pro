QT += core testlib
QT -= gui

CONFIG += c++11 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = tst_hidseos

MODULE = $$PWD/../../src/module/hidseos
INCLUDEPATH += $$MODULE $$PWD/../../src

SOURCES += \
    tst_hidseos.cpp \
    $$MODULE/seos_parser.cpp \
    $$MODULE/seos_codes.cpp \
    $$MODULE/seos_exporter.cpp \
    $$MODULE/seos_reader.cpp \
    $$PWD/../../src/core/clientsession.cpp \
    $$PWD/../../src/core/logger.cpp

HEADERS += \
    $$MODULE/seos_types.h \
    $$MODULE/seos_parser.h \
    $$MODULE/seos_codes.h \
    $$MODULE/seos_exporter.h \
    $$MODULE/seos_reader.h \
    $$PWD/../../src/core/clientsession.h \
    $$PWD/../../src/core/logger.h

QMAKE_CXXFLAGS += -Wall -Wextra
