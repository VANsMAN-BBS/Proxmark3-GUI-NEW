#include "terminalpalette.h"

#include <QPalette>
#include <QSettings>
#include <QtMath>

#ifdef QT_WIDGETS_LIB
#include <QApplication>
#endif

namespace
{
QColor colour(const char *value)
{
    return QColor(QString::fromLatin1(value));
}

qreal linearChannel(qreal value)
{
    return value <= 0.03928 ? value / 12.92
                            : qPow((value + 0.055) / 1.055, 2.4);
}
}

TerminalPalette::TerminalPalette()
    : normal(colour("#39FF14"))
    , userCommand(normal)
    , success(normal)
    , info(normal)
    , data(normal)
    , progress(colour("#00D68F"))
    , hardware(normal)
    , protocol(normal)
    , hint(normal)
    , warning(colour("#FFD54F"))
    , error(colour("#FF5252"))
    , hintCommand(normal)
{
}

TerminalPalette TerminalPalette::forDark(bool darkBackground)
{
    TerminalPalette palette;
    if (darkBackground) {
        palette.normal = colour("#39FF14");
        palette.progress = colour("#00D68F");
        palette.warning = colour("#FFD54F");
        palette.error = colour("#FF5252");
    } else {
        palette.normal = colour("#008000");
        palette.progress = colour("#00796B");
        palette.warning = colour("#946000");
        palette.error = colour("#C62828");
    }

    // The classic terminal intentionally collapses these semantic roles into
    // one readable foreground colour.
    palette.userCommand = palette.normal;
    palette.success = palette.normal;
    palette.info = palette.normal;
    palette.data = palette.normal;
    palette.hardware = palette.normal;
    palette.protocol = palette.normal;
    palette.hint = palette.normal;
    palette.hintCommand = palette.normal;
    return palette;
}

bool TerminalPalette::detectDarkBackground()
{
    // The bundled themes are selected before the main window is constructed
    // and applied as a QSS stylesheet, so QPalette alone is not reliable.
    QSettings settings(QStringLiteral("GUIsettings.ini"), QSettings::IniFormat);
    settings.setIniCodec("UTF-8");
    const QString theme = settings.value(QStringLiteral("UI/Theme_Name")).toString();
    if (theme == QLatin1String("qdss_dark")) {
        return true;
    }
    if (theme == QLatin1String("qdss_light")) {
        return false;
    }

#ifdef QT_WIDGETS_LIB
    if (qApp) {
        const QColor base = QApplication::palette().color(QPalette::Base);
        if (base.isValid()) {
            return base.lightness() < 128;
        }
    }
#endif
    return false;
}

qreal TerminalPalette::relativeLuminance(const QColor &colour)
{
    if (!colour.isValid()) {
        return 0.0;
    }
    return 0.2126 * linearChannel(colour.redF())
           + 0.7152 * linearChannel(colour.greenF())
           + 0.0722 * linearChannel(colour.blueF());
}

qreal TerminalPalette::contrastRatio(const QColor &first, const QColor &second)
{
    const qreal light = qMax(relativeLuminance(first), relativeLuminance(second));
    const qreal dark = qMin(relativeLuminance(first), relativeLuminance(second));
    return (light + 0.05) / (dark + 0.05);
}
