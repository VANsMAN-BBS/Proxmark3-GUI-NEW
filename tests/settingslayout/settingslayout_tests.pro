QT += core gui widgets testlib

CONFIG += c++11 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = tst_settingslayout

INCLUDEPATH += $$PWD/../../src
INCLUDEPATH += $$OUT_PWD

FORMS += ../../src/ui/mainwindow.ui

SOURCES += \
    tst_settingslayout.cpp \
    ../../src/ui/settingslayout.cpp

HEADERS += ../../src/ui/settingslayout.h

QMAKE_CXXFLAGS += -Wall -Wextra
