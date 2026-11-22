QT += core gui widgets testlib

CONFIG += c++11 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = tst_mifareui

INCLUDEPATH += $$PWD/../../src
INCLUDEPATH += $$OUT_PWD

FORMS += ../../src/ui/mainwindow.ui

SOURCES += tst_mifareui.cpp

HEADERS += ../../src/module/mifare/mfcancelgate.h ../../src/module/mifare/mfimage.h ../../src/module/mifare/mftablelayout.h

QMAKE_CXXFLAGS += -Wall -Wextra

DEFINES += SRCDIR=\\\"$$PWD\\\"
