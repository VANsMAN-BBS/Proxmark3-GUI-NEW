QT += core gui widgets testlib

CONFIG += c++11 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = tst_iso14443a

MODULE = $$PWD/../../src/module/iso14443a
INCLUDEPATH += $$PWD/../../src $$MODULE

SOURCES += \
    tst_iso14443a.cpp \
    $$MODULE/iso14443aruntime.cpp \
    $$MODULE/iso14443aservice.cpp \
    $$MODULE/iso14443apage.cpp

HEADERS += \
    $$MODULE/iso14443aruntime.h \
    $$MODULE/iso14443aservice.h \
    $$MODULE/iso14443apage.h

RESOURCES += $$PWD/../../resources/iso14443a/iso14443a.qrc \
    $$PWD/../../i18n/language.qrc

QMAKE_CXXFLAGS += -Wall -Wextra
