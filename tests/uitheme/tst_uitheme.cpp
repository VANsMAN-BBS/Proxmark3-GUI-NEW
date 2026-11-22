#include <QtTest>
#include <QApplication>
#include <QDialog>
#include <QMainWindow>
#include <QDockWidget>
#include <QFile>
#include <QLabel>
#include <QListView>
#include <QSizePolicy>
#include <QStyle>
#include <QTabBar>
#include <QWidget>
#include <QFontMetricsF>

#include "ui_mainwindow.h"
#include "ui/docktitlemetrics.h"

class TestUiTheme : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void themeUiHasNoRestartText();
    void themeTooltipsDescribeLiveSwitching();
    void applicationStyleSheetReachesEveryWindow();
    void centralWidgetKeepsCompactVerticalPolicy();
    void mainWindowHasNoTitleDerivedMinimumWidth();
    void dockTitleBarAlwaysFitsFontMetrics();
    void dockTabTitlesAreVisible();
    void mainWindowLabelsFitAtAllScales();
};

static QStringList scaleLabels()
{
    return QStringList() << QStringLiteral("100%") << QStringLiteral("125%")
                         << QStringLiteral("150%") << QStringLiteral("175%")
                         << QStringLiteral("200%");
}

void TestUiTheme::initTestCase()
{
    // Load the real light theme so PM_DockWidgetTitleMargin, the title
    // padding and the text alignment are the ones used by the application.
    QFile qss(QStringLiteral(":/qdarkstyle/light/lightstyle.qss"));
    QVERIFY(qss.open(QFile::ReadOnly));
    qApp->setStyleSheet(QString::fromUtf8(qss.readAll()));

    QDockWidget dock(QStringLiteral("Terminal"));
    QCOMPARE(dock.style()->pixelMetric(QStyle::PM_DockWidgetTitleMargin, nullptr, &dock),
             DockTitleMetrics::PaddingTop);
}

void TestUiTheme::themeUiHasNoRestartText()
{
    QMainWindow window;
    Ui::MainWindow ui;
    ui.setupUi(&window);

    QVERIFY(window.findChild<QLabel *>(QStringLiteral("label_themeRestart")) == nullptr);

    for (QLabel *label : window.findChildren<QLabel *>()) {
        QVERIFY2(!label->text().contains(QStringLiteral("Restart this app to use new theme")),
                 qPrintable(label->objectName()));
    }
}

void TestUiTheme::themeTooltipsDescribeLiveSwitching()
{
    QMainWindow window;
    Ui::MainWindow ui;
    ui.setupUi(&window);

    QVERIFY(ui.Set_UI_Theme_nameBox->toolTip().contains(QStringLiteral("immediately")));
    QVERIFY(ui.Set_UI_Theme_setButton->toolTip().contains(QStringLiteral("no restart")));
}

void TestUiTheme::applicationStyleSheetReachesEveryWindow()
{
    QWidget main;
    QListView view(&main);
    QDialog dialog;
    QDockWidget dock(QStringLiteral("Command Browser"));
    dock.setFloating(true);
    main.show();
    dialog.show();
    dock.show();

    struct Counter : QObject {
        int changes = 0;
        bool eventFilter(QObject *, QEvent *event) override
        {
            if (event->type() == QEvent::StyleChange) {
                ++changes;
            }
            return false;
        }
    } mainCounter, viewCounter, dialogCounter, dockCounter;
    main.installEventFilter(&mainCounter);
    view.installEventFilter(&viewCounter);
    dialog.installEventFilter(&dialogCounter);
    dock.installEventFilter(&dockCounter);

    const QString before = qApp->styleSheet();
    qApp->setStyleSheet(QStringLiteral("QWidget { background-color: #19232D; color: #39FF14; }"));
    QApplication::processEvents();

    QVERIFY(qApp->styleSheet() != before);
    QVERIFY2(mainCounter.changes > 0, "main window did not receive the style change");
    QVERIFY2(viewCounter.changes > 0, "command browser child did not receive the style change");
    QVERIFY2(dialogCounter.changes > 0, "dialog did not receive the style change");
    QVERIFY2(dockCounter.changes > 0, "floating dock did not receive the style change");

    qApp->setStyleSheet(before);
}

void TestUiTheme::centralWidgetKeepsCompactVerticalPolicy()
{
    // After dockInit() the connection row is the only visible central child.
    // A Preferred/Fixed policy keeps that row compact when the window is
    // maximised; Preferred/Preferred made QMainWindow hand all spare height to
    // the central widget and produced a large blank strip above it.
    QMainWindow window;
    Ui::MainWindow ui;
    ui.setupUi(&window);
    ui.funcTab->setVisible(false);

    QDockWidget *dock = new QDockWidget(QStringLiteral("MIFARE"), &window);
    dock->setWidget(new QWidget);
    window.addDockWidget(Qt::BottomDockWidgetArea, dock);

    window.resize(1600, 1000);
    window.show();
    QApplication::processEvents();
    QApplication::processEvents();

    QCOMPARE(ui.centralwidget->sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
    const int compact = ui.centralwidget->sizeHint().height();
    QVERIFY2(ui.centralwidget->height() <= compact + 8,
             qPrintable(QStringLiteral("central widget %1 px, hint %2 px")
                                .arg(ui.centralwidget->height())
                                .arg(compact)));
    window.hide();
}

void TestUiTheme::mainWindowHasNoTitleDerivedMinimumWidth()
{
    QMainWindow window;
    Ui::MainWindow ui;
    ui.setupUi(&window);
    QCOMPARE(window.minimumWidth(), 800);

    // A dock keeps the explicit content constraint and is not widened by the
    // removed title-text compensation.
    QDockWidget dock(QStringLiteral("Command Browser"));
    dock.setMinimumWidth(320);
    dock.show();
    QApplication::processEvents();
    QCOMPARE(dock.minimumWidth(), 320);
    dock.hide();
}

void TestUiTheme::dockTitleBarAlwaysFitsFontMetrics()
{
    const QFont base = QApplication::font();
    const QStringList labels = scaleLabels();
    const QStringList titles = { QStringLiteral("Terminal"),
                                 QStringLiteral("Command Browser"),
                                 QStringLiteral("RawCommand") };
    QStringList failures;
    int previousHeight = 0;
    for (int i = 0; i < labels.size(); ++i) {
        const qreal factor = 1.0 + 0.25 * i;
        QFont font = base;
        font.setPointSizeF(base.pointSizeF() * factor);
        QApplication::setFont(font);

        for (const QString &title : titles) {
            QDockWidget dock(title);
            QFontMetricsF metrics(dock.font());
            const int bar = DockTitleMetrics::titleBarHeight(&dock);
            const int paint = DockTitleMetrics::textRectHeight(&dock);
            const int text = qRound(metrics.ascent() + metrics.descent());
            const int margin = dock.style()->pixelMetric(QStyle::PM_DockWidgetTitleMargin,
                                                         nullptr, &dock);

            if (margin != DockTitleMetrics::PaddingTop) {
                failures << QStringLiteral("%1 at %2: margin %3")
                                .arg(title, labels.at(i)).arg(margin);
            }
            if (paint < text + DockTitleMetrics::MinimumTextSlack) {
                failures << QStringLiteral("%1 at %2: paint rect %3 < glyphs %4 + slack")
                                .arg(title, labels.at(i)).arg(paint).arg(text);
            }
            if (!DockTitleMetrics::textFits(&dock)) {
                failures << QStringLiteral("%1 at %2: textFits() false").arg(title, labels.at(i));
            }
            if (i > 0 && bar < previousHeight) {
                failures << QStringLiteral("%1 shrank at %2").arg(title, labels.at(i));
            }
            previousHeight = bar;
        }
    }
    QApplication::setFont(base);

    QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral("; "))));
}

void TestUiTheme::dockTabTitlesAreVisible()
{
    const QFont base = QApplication::font();
    const QStringList labels = scaleLabels();
    for (int i = 0; i < labels.size(); ++i) {
        const qreal factor = 1.0 + 0.25 * i;
        QFont font = base;
        font.setPointSizeF(base.pointSizeF() * factor);
        QApplication::setFont(font);

        QMainWindow window;
        window.setDockNestingEnabled(true);
        QList<QDockWidget *> docks;
        const QStringList titles = {
            QStringLiteral("MIFARE"), QStringLiteral("LF"), QStringLiteral("NTAG/UL"),
            QStringLiteral("eMRTD"), QStringLiteral("RawCommand"), QStringLiteral("Settings")
        };
        for (int d = 0; d < titles.size(); ++d) {
            QDockWidget *dock = new QDockWidget(titles.at(d), &window);
            dock->setObjectName(QStringLiteral("dock%1").arg(d));
            window.addDockWidget(Qt::BottomDockWidgetArea, dock);
            if (!docks.isEmpty()) {
                window.tabifyDockWidget(docks.first(), dock);
            }
            docks.append(dock);
        }
        docks.first()->raise();
        window.resize(900, 600);
        window.show();
        QApplication::processEvents();

        QTabBar *tabBar = nullptr;
        for (QTabBar *candidate : window.findChildren<QTabBar *>()) {
            if (candidate->count() == titles.size()) {
                tabBar = candidate;
                break;
            }
        }
        QVERIFY2(tabBar != nullptr, qPrintable(labels.at(i)));
        QCOMPARE(tabBar->count(), titles.size());
        QVERIFY2(tabBar->height() >= tabBar->fontMetrics().height(),
                 qPrintable(QStringLiteral("tab bar %1 px, font %2 px at %3")
                                .arg(tabBar->height())
                                .arg(tabBar->fontMetrics().height())
                                .arg(labels.at(i))));
        for (int t = 0; t < titles.size(); ++t) {
            const QRect rect = tabBar->tabRect(t);
            QVERIFY2(rect.width() >= tabBar->fontMetrics().horizontalAdvance(titles.at(t)),
                     qPrintable(QStringLiteral("%1 tab %2 px, text %3 px at %4")
                                    .arg(titles.at(t))
                                    .arg(rect.width())
                                    .arg(tabBar->fontMetrics().horizontalAdvance(titles.at(t)))
                                    .arg(labels.at(i))));
        }
        window.hide();
    }
    QApplication::setFont(base);
}

void TestUiTheme::mainWindowLabelsFitAtAllScales()
{
    const QFont base = QApplication::font();
    const QStringList labels = scaleLabels();
    QStringList clipped;
    int previousMinimum = 0;
    for (int i = 0; i < labels.size(); ++i) {
        QApplication::setFont(base);
        const qreal factor = 1.0 + 0.25 * i;
        QFont font = base;
        font.setPointSizeF(base.pointSizeF() * factor);
        QApplication::setFont(font);

        QMainWindow window;
        Ui::MainWindow ui;
        ui.setupUi(&window);

        const QFontMetrics metrics(font);
        for (QWidget *widget : window.findChildren<QWidget *>()) {
            if (widget->objectName().isEmpty()) {
                continue;
            }
            QString text;
            if (auto *label = qobject_cast<QLabel *>(widget)) {
                text = label->text();
            } else if (auto *button = qobject_cast<QAbstractButton *>(widget)) {
                text = button->text();
            } else if (auto *group = qobject_cast<QGroupBox *>(widget)) {
                text = group->title();
            } else {
                continue;
            }
            if (text.isEmpty() || text.contains(QLatin1Char('\n'))) {
                continue;
            }
            const int textWidth = metrics.horizontalAdvance(text);
            // A hard minimum width below the text width forces the layout to
            // clip the caption even when there is room.  Zero means "use
            // sizeHint", which is what we want.
            if (widget->minimumWidth() > 0 && widget->minimumWidth() + 2 < textWidth) {
                clipped << QStringLiteral("%1 at %2: min %3 < text %4")
                               .arg(widget->objectName(), labels.at(i))
                               .arg(widget->minimumWidth())
                               .arg(textWidth);
            }
        }

        // The layout must be allowed to grow with the font (no hard 800 px
        // clamp that would clip the MIFARE/LF pages at large text sizes).
        const int minimum = window.minimumSizeHint().width();
        if (i > 0 && minimum < previousMinimum) {
            clipped << QStringLiteral("minimum width shrank at %1").arg(labels.at(i));
        }
        previousMinimum = minimum;
    }
    QApplication::setFont(base);

    QVERIFY2(clipped.isEmpty(), qPrintable(clipped.join(QStringLiteral("; "))));
}

int main(int argc, char *argv[])
{
    // Use the real Windows platform so font metrics match the application.
    // (The offscreen plugin cannot load the system fonts on this host.)
    QApplication app(argc, argv);
    TestUiTheme test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_uitheme.moc"
