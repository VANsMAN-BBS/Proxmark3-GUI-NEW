QT += core testlib
QT -= gui

CONFIG += c++11 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = tst_felica

MODULE = $$PWD/../../src/module/felica
INCLUDEPATH += $$MODULE

SOURCES += \
    tst_felica.cpp \
    $$MODULE/cjrc_profile.cpp \
    $$MODULE/station_table.cpp \
    $$MODULE/code_tables.cpp \
    $$MODULE/felica_exporter.cpp

HEADERS += \
    $$MODULE/cjrc_profile.h \
    $$MODULE/station_table.h \
    $$MODULE/code_tables.h \
    $$MODULE/transit_types.h \
    $$MODULE/felica_chunks.h \
    $$MODULE/felica_exporter.h

RESOURCES += $$PWD/../../resources/felica/felica.qrc

QMAKE_CXXFLAGS += -Wall -Wextra
