QT += core testlib
QT -= gui

CONFIG += c++11 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = tst_emv

MODULE = $$PWD/../../src/module/emv
INCLUDEPATH += $$MODULE $$PWD/../../src

SOURCES += \
    tst_emv.cpp \
    $$MODULE/emvruntime.cpp \
    $$MODULE/emvworker.cpp \
    $$MODULE/emvexport.cpp

HEADERS += \
    $$MODULE/emvtypes.h \
    $$MODULE/emvruntime.h \
    $$MODULE/emvworker.h \
    $$MODULE/emvexport.h

RESOURCES += $$PWD/../../resources/emv/emv.qrc

QMAKE_CXXFLAGS += -Wall -Wextra
