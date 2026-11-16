QT       += core gui serialport network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# LF multi-protocol text parser (Stage 2). Public headers use the <lf/...> prefix.
INCLUDEPATH += $$PWD/lf/parser/include

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
    module/amiibo/amiibodata.cpp \
    module/lfparsertab.cpp \
    module/ntag/ntagmodel.cpp \
    module/ntag/ndefbuilder.cpp \
    module/ntag/ntagmemory.cpp \
    module/felicatab.cpp \
    module/felica/felica_reader.cpp \
    module/felica/cjrc_profile.cpp \
    module/felica/station_table.cpp \
    module/felica/code_tables.cpp \
    module/felica/felica_exporter.cpp \
    module/felica/octopus_parser.cpp \
    module/hidseospage.cpp \
    module/hidseos/seos_codes.cpp \
    module/hidseos/seos_parser.cpp \
    module/hidseos/seos_reader.cpp \
    module/hidseos/seos_exporter.cpp \
    lf/parser/src/lf/lfparsetypes.cpp \
    lf/parser/src/lf/lftextnormalizer.cpp \
    lf/parser/src/lf/lfparserregistry.cpp \
    lf/parser/src/lf/t55xxparser.cpp \
    lf/parser/src/lf/parsers/em4100parser.cpp \
    lf/parser/src/lf/parsers/hidproxparser.cpp \
    lf/parser/src/lf/parsers/awidparser.cpp \
    lf/parser/src/lf/parsers/indalaparser.cpp \
    lf/parser/src/lf/parsers/ioproxparser.cpp \
    lf/parser/src/lf/parsers/fdxbparser.cpp \
    lf/parser/src/lf/parsers/paradoxparser.cpp \
    lf/parser/src/lf/parsers/keriparser.cpp \
    lf/parser/src/lf/parsers/vikingparser.cpp \
    lf/parser/src/lf/parsers/pyramidparser.cpp \
    lf/parser/src/lf/parsers/nexwatchparser.cpp \
    lf/parser/src/lf/parsers/prescoparser.cpp \
    lf/parser/src/lf/parsers/nedapparser.cpp \
    lf/parser/src/lf/parsers/gproxiiparser.cpp \
    lf/parser/src/lf/parsers/gallagherparser.cpp \
    lf/parser/src/lf/parsers/pacparser.cpp \
    lf/parser/src/lf/parsers/noralsyparser.cpp \
    lf/parser/src/lf/parsers/jablotronparser.cpp \
    lf/parser/src/lf/parsers/securakeyparser.cpp \
    lf/parser/src/lf/parsers/visa2000parser.cpp \
    lf/parser/src/lf/parsers/motorolaparser.cpp \
    lf/parser/src/lf/parsers/idteckparser.cpp \
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
    module/amiibo/amiibodata.h \
    module/lfparsertab.h \
    module/ntag/ntagmodel.h \
    module/ntag/ndefbuilder.h \
    module/ntag/ntagmemory.h \
    module/felicatab.h \
    module/felica/transit_types.h \
    module/felica/felica_reader.h \
    module/felica/cjrc_profile.h \
    module/felica/station_table.h \
    module/felica/code_tables.h \
    module/felica/felica_exporter.h \
    module/felica/octopus_parser.h \
    module/felica/felica_chunks.h \
    module/hidseospage.h \
    module/hidseos/seos_types.h \
    module/hidseos/seos_codes.h \
    module/hidseos/seos_parser.h \
    module/hidseos/seos_reader.h \
    module/hidseos/seos_exporter.h \
    lf/parser/include/lf/lfparsetypes.h \
    lf/parser/include/lf/lftextnormalizer.h \
    lf/parser/include/lf/lfparser.h \
    lf/parser/include/lf/lfparserregistry.h \
    lf/parser/include/lf/t55xxparser.h \
    lf/parser/include/lf/parsers/em4100parser.h \
    lf/parser/include/lf/parsers/hidproxparser.h \
    lf/parser/include/lf/parsers/awidparser.h \
    lf/parser/include/lf/parsers/indalaparser.h \
    lf/parser/include/lf/parsers/ioproxparser.h \
    lf/parser/include/lf/parsers/fdxbparser.h \
    lf/parser/include/lf/parsers/paradoxparser.h \
    lf/parser/include/lf/parsers/keriparser.h \
    lf/parser/include/lf/parsers/vikingparser.h \
    lf/parser/include/lf/parsers/pyramidparser.h \
    lf/parser/include/lf/parsers/nexwatchparser.h \
    lf/parser/include/lf/parsers/prescoparser.h \
    lf/parser/include/lf/parsers/nedapparser.h \
    lf/parser/include/lf/parsers/gproxiiparser.h \
    lf/parser/include/lf/parsers/gallagherparser.h \
    lf/parser/include/lf/parsers/pacparser.h \
    lf/parser/include/lf/parsers/noralsyparser.h \
    lf/parser/include/lf/parsers/jablotronparser.h \
    lf/parser/include/lf/parsers/securakeyparser.h \
    lf/parser/include/lf/parsers/visa2000parser.h \
    lf/parser/include/lf/parsers/motorolaparser.h \
    lf/parser/include/lf/parsers/idteckparser.h \
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

VERSION = 0.5.0
QMAKE_TARGET_PRODUCT = "Proxmark3GUI"
QMAKE_TARGET_DESCRIPTION = "A cross-platform GUI for Proxmark3 client"
QMAKE_TARGET_COMPANY = "wh201906"

RESOURCES += \
    ../i18n/language.qrc \
    ../config/config.qrc \
    ../resources/icons/icons.qrc \
    ../resources/felica/felica.qrc \
    qdarkstyle/dark/darkstyle.qrc \
    qdarkstyle/light/lightstyle.qrc

# Windows executable icon (multi-size ICO). qmake generates the .rc so relative
# paths stay correct regardless of the shadow-build directory.
win32: RC_ICONS = ../resources/icons/app-icon.ico
