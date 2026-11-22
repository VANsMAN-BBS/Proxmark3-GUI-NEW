QT += core testlib
QT -= gui

CONFIG += c++11 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = tst_session

INCLUDEPATH += $$PWD/../../src

SOURCES += \
    tst_session.cpp \
    ../../src/core/clientsession.cpp \
    ../../src/core/logger.cpp \
    ../../src/module/mifare/mfpm3log.cpp

HEADERS += \
    ../../src/core/clientsession.h \
    ../../src/core/clientinfo.h \
    ../../src/core/logger.h \
    ../../src/module/mifare/mfpm3log.h

QMAKE_CXXFLAGS += -Wall -Wextra
