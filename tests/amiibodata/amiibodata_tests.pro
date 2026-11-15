QT += core testlib
QT -= gui

CONFIG += c++11 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = tst_amiibodata

MODEL = $$PWD/../../src/module
INCLUDEPATH += $$MODEL

SOURCES += \
    tst_amiibodata.cpp \
    $$MODEL/amiibo/amiibodata.cpp

HEADERS += \
    $$MODEL/amiibo/amiibodata.h

QMAKE_CXXFLAGS += -Wall -Wextra
