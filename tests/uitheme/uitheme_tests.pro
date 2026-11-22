QT += core gui widgets testlib

CONFIG += c++11 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = tst_uitheme

INCLUDEPATH += $$PWD/../../src
INCLUDEPATH += $$OUT_PWD

FORMS += ../../src/ui/mainwindow.ui
RESOURCES += ../../src/qdarkstyle/light/lightstyle.qrc

SOURCES += tst_uitheme.cpp

QMAKE_CXXFLAGS += -Wall -Wextra
