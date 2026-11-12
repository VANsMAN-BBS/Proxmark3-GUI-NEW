QT       += core gui serialport network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    core/clientsession.cpp \
    core/logger.cpp \
    common/myeventfilter.cpp \
    main.cpp \
    common/util.cpp \
    module/lf.cpp \
    module/mifare.cpp \
    module/t55xx.cpp \
    module/ntagtab.cpp \
    module/emrtd.cpp \
    module/amiibopage.cpp \
    ui/mf_trailerdecoderdialog.cpp \
    ui/mf_sim_simdialog.cpp \
    ui/mf_uid_parameterdialog.cpp \
    ui/mainwindow.cpp \
    ui/mf_attack_hardnesteddialog.cpp \
    ui/waveformwidget.cpp \
    ui/waveformdialog.cpp \

HEADERS += \
    core/clientinfo.h \
    core/clientsession.h \
    core/logger.h \
    common/myeventfilter.h \
    common/util.h \
    module/lf.h \
    module/mifare.h \
    module/t55xx.h \
    module/ntagtab.h \
    module/emrtd.h \
    module/amiibopage.h \
    ui/mf_trailerdecoderdialog.h \
    ui/mf_sim_simdialog.h \
    ui/mf_uid_parameterdialog.h \
    ui/mainwindow.h \
    ui/mf_attack_hardnesteddialog.h \
    ui/waveformwidget.h \
    ui/waveformdialog.h \

FORMS += \
    ui/mf_trailerdecoderdialog.ui \
    ui/mf_sim_simdialog.ui \
    ui/mf_uid_parameterdialog.ui \
    ui/mainwindow.ui \
    ui/mf_attack_hardnesteddialog.ui

TRANSLATIONS += \
    ../i18n/zh_CN.ts \
    ../i18n/en_US.ts

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

VERSION = 0.4.0
QMAKE_TARGET_PRODUCT = "Proxmark3GUI"
QMAKE_TARGET_DESCRIPTION = "A cross-platform GUI for Proxmark3 client"
QMAKE_TARGET_COMPANY = "wh201906"

RESOURCES += \
    ../i18n/language.qrc \
    ../config/config.qrc \
    ../resources/icons/icons.qrc \
    qdarkstyle/dark/darkstyle.qrc \
    qdarkstyle/light/lightstyle.qrc

# Windows executable icon (multi-size ICO). qmake generates the .rc so relative
# paths stay correct regardless of the shadow-build directory.
win32: RC_ICONS = ../resources/icons/app-icon.ico
