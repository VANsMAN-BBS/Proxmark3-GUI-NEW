QT += core testlib
QT -= gui

CONFIG += c++11 console testcase
CONFIG -= app_bundle

TEMPLATE = app
TARGET = tst_mifaregen3

GEN3 = $$PWD/../../src/module/mifare
INCLUDEPATH += $$PWD/../../src

SOURCES += \
    tst_mifaregen3.cpp \
    $$GEN3/mfgen3crypto.cpp \
    $$GEN3/mfnoncestore.cpp \
    $$GEN3/mfgen3service.cpp \
    $$GEN3/mfcardinfoservice.cpp \
    $$GEN3/mfpm3log.cpp

HEADERS += \
    $$GEN3/mfgen3crypto.h \
    $$GEN3/mfgen3types.h \
    $$GEN3/mfnoncestore.h \
    $$GEN3/mfgen3service.h \
    $$GEN3/mfcardinfoservice.h \
    $$GEN3/mfcardtypesel.h \
    $$GEN3/mfpm3log.h \
    $$GEN3/mfcancelgate.h

QMAKE_CXXFLAGS += -Wall -Wextra
