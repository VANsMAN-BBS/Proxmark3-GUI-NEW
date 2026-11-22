#include <QtTest>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include "ui_mainwindow.h"
#include "ui/settingslayout.h"

class TestSettingsLayout : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void everySettingWidgetStillExists();
    void everyControlIsEnabled();
    void scrollAreaIsResponsive();
    void pathFieldsExpand();
    void numericFieldsAreCompact();
    void inlineLabelsShareOneColumn();
    void checkboxRowsAreAligned();
    void longCaptionsUseSecondaryRole();
    void groupSpacingIsUniform();
    void contentsHaveNoFixedGeometry();
    void applyIsIdempotent();
    void fontChangeRefreshesMetrics();

private:
    Ui::MainWindow *ui = nullptr;
    QMainWindow *window = nullptr;
};

void TestSettingsLayout::init()
{
    window = new QMainWindow;
    ui = new Ui::MainWindow;
    ui->setupUi(window);
    refineSettingsLayout(ui->settingsTab);
}

void TestSettingsLayout::cleanup()
{
    delete window;
    window = nullptr;
    delete ui;
    ui = nullptr;
}

void TestSettingsLayout::everySettingWidgetStillExists()
{
    const QStringList names = {
        QStringLiteral("Set_scrollArea"),
        QStringLiteral("Set_clientGroupBox"),
        QStringLiteral("Set_Client_GUIWorkingDirLabel"),
        QStringLiteral("Set_Client_envScriptEdit"),
        QStringLiteral("Set_Client_workingDirEdit"),
        QStringLiteral("Set_Client_startArgsEdit"),
        QStringLiteral("Set_Client_configFileBox"),
        QStringLiteral("Set_Client_configPathEdit"),
        QStringLiteral("Set_Client_forceEnabledBox"),
        QStringLiteral("Set_Client_keepClientActiveBox"),
        QStringLiteral("groupBox_2"),
        QStringLiteral("Set_UI_setLanguageButton"),
        QStringLiteral("Set_UI_Opacity_slider"),
        QStringLiteral("Set_UI_Opacity_Box"),
        QStringLiteral("Set_UI_Theme_nameBox"),
        QStringLiteral("Set_UI_Theme_setButton"),
        QStringLiteral("Set_UI_Font_nameBox"),
        QStringLiteral("Set_UI_Font_sizeBox"),
        QStringLiteral("Set_UI_Font_setButton"),
        QStringLiteral("Set_UI_DataFont_nameBox"),
        QStringLiteral("Set_UI_DataFont_sizeBox"),
        QStringLiteral("Set_UI_DataFont_setButton"),
        QStringLiteral("Set_UI_CMDFont_nameBox"),
        QStringLiteral("Set_UI_CMDFont_sizeBox"),
        QStringLiteral("Set_UI_CMDFont_setButton"),
        QStringLiteral("Set_About_projectLink"),
        QStringLiteral("Set_About_descriptionLabel")
    };
    for (const QString &name : names) {
        QVERIFY2(ui->settingsTab->findChild<QWidget *>(name) != nullptr, qPrintable(name));
    }
}

void TestSettingsLayout::everyControlIsEnabled()
{
    for (QWidget *control : ui->settingsTab->findChildren<QWidget *>()) {
        if (qobject_cast<QLineEdit *>(control) || qobject_cast<QComboBox *>(control)
                || qobject_cast<QAbstractSpinBox *>(control)
                || qobject_cast<QPushButton *>(control)) {
            QVERIFY2(control->isEnabled(), qPrintable(control->objectName()));
        }
    }
}

void TestSettingsLayout::scrollAreaIsResponsive()
{
    QScrollArea *scroll = ui->Set_scrollArea;
    QVERIFY(scroll->widgetResizable());
    QCOMPARE(scroll->horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    QCOMPARE(scroll->verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    QVERIFY(scroll->widget() != nullptr);
    QCOMPARE(scroll->widget()->minimumWidth(), 0);

    QLayout *layout = scroll->widget()->layout();
    QVERIFY(layout != nullptr);
    QCOMPARE(layout->contentsMargins(), QMargins(16, 16, 16, 16));
    QHBoxLayout *columns = qobject_cast<QHBoxLayout *>(layout);
    QVERIFY(columns != nullptr);
    QCOMPARE(columns->stretch(0), 1);
}

void TestSettingsLayout::pathFieldsExpand()
{
    const QStringList names = {
        QStringLiteral("Set_Client_envScriptEdit"),
        QStringLiteral("Set_Client_workingDirEdit"),
        QStringLiteral("Set_Client_startArgsEdit"),
        QStringLiteral("Set_Client_configPathEdit")
    };
    for (const QString &name : names) {
        QLineEdit *edit = ui->settingsTab->findChild<QLineEdit *>(name);
        QVERIFY2(edit != nullptr, qPrintable(name));
        QCOMPARE(edit->sizePolicy().horizontalPolicy(), QSizePolicy::Expanding);
        QVERIFY2(edit->minimumWidth() >= 200, qPrintable(name));
    }
}

void TestSettingsLayout::numericFieldsAreCompact()
{
    const QList<QSpinBox *> boxes = ui->settingsTab->findChildren<QSpinBox *>();
    QVERIFY(boxes.size() >= 4);
    const QFontMetrics metrics(window->font());
    const int limit = metrics.horizontalAdvance(QStringLiteral("000000")) + 40;
    for (QSpinBox *box : boxes) {
        QVERIFY2(box->maximumWidth() <= limit, qPrintable(box->objectName()));
        QVERIFY2(box->maximumWidth() > 0, qPrintable(box->objectName()));
        QCOMPARE(box->sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
    }
}

void TestSettingsLayout::inlineLabelsShareOneColumn()
{
    const QStringList labels = {
        QStringLiteral("label_16"), QStringLiteral("label_70"),
        QStringLiteral("label_71"), QStringLiteral("label_72"),
        QStringLiteral("label_73"), QStringLiteral("label_74")
    };
    int column = -1;
    for (const QString &name : labels) {
        QLabel *label = ui->settingsTab->findChild<QLabel *>(name);
        QVERIFY2(label != nullptr, qPrintable(name));
        if (column < 0) {
            column = label->minimumWidth();
        }
        QCOMPARE(label->minimumWidth(), column);
        QVERIFY(label->minimumWidth() >= 96);
    }
}

void TestSettingsLayout::checkboxRowsAreAligned()
{
    const QStringList names = {
        QStringLiteral("Set_Client_forceEnabledBox"),
        QStringLiteral("Set_Client_keepClientActiveBox")
    };
    for (const QString &name : names) {
        QCheckBox *box = ui->settingsTab->findChild<QCheckBox *>(name);
        QVERIFY2(box != nullptr, qPrintable(name));
        QVERIFY(box->text().isEmpty());
        QLayout *row = nullptr;
        for (QLayout *candidate : box->parentWidget()->findChildren<QLayout *>()) {
            if (candidate->indexOf(box) >= 0) {
                row = candidate;
                break;
            }
        }
        QVERIFY2(row != nullptr, qPrintable(name));
        QCOMPARE(row->spacing(), 6);
        QCOMPARE(row->contentsMargins(), QMargins(0, 0, 0, 0));
        const int index = row->indexOf(box);
        QVERIFY(index >= 0);
        QVERIFY(row->itemAt(index)->alignment() & Qt::AlignTop);
        bool captionMarked = false;
        for (QLabel *label : box->parentWidget()->findChildren<QLabel *>()) {
            if (label->wordWrap() && label->text().size() >= 40) {
                QVERIFY2(label->property("settingsDescription").toBool(),
                         qPrintable(label->text()));
                captionMarked = true;
            }
        }
        QVERIFY(captionMarked);
    }
}

void TestSettingsLayout::longCaptionsUseSecondaryRole()
{
    int descriptions = 0;
    for (QLabel *label : ui->settingsTab->findChildren<QLabel *>()) {
        if (label->property("settingsDescription").toBool()) {
            QVERIFY(label->wordWrap());
            ++descriptions;
        }
    }
    QVERIFY2(descriptions >= 5, qPrintable(QString::number(descriptions)));

    for (const QString &name : { QStringLiteral("label_13"), QStringLiteral("label_20"),
                                 QStringLiteral("label_14"), QStringLiteral("label_64"),
                                 QStringLiteral("Set_About_descriptionLabel") }) {
        QLabel *label = ui->settingsTab->findChild<QLabel *>(name);
        QVERIFY2(label != nullptr, qPrintable(name));
        QVERIFY2(label->property("settingsDescription").toBool(), qPrintable(name));
    }
}

void TestSettingsLayout::groupSpacingIsUniform()
{
    for (QGroupBox *group : ui->settingsTab->findChildren<QGroupBox *>()) {
        QVERIFY2(group->layout() != nullptr, qPrintable(group->objectName()));
        QVERIFY2(group->layout()->spacing() == 8, qPrintable(group->objectName()
                 + QStringLiteral(" spacing %1").arg(group->layout()->spacing())));
        QCOMPARE(group->layout()->contentsMargins(), QMargins(12, 16, 12, 12));
    }
    QVBoxLayout *column = ui->settingsTab->findChild<QVBoxLayout *>(QStringLiteral("verticalLayout_12"));
    QVERIFY(column != nullptr);
    QCOMPARE(column->spacing(), 12);
}

void TestSettingsLayout::contentsHaveNoFixedGeometry()
{
    QWidget *contents = ui->Set_scrollArea->widget();
    QVERIFY(contents != nullptr);
    QVERIFY(contents->layout() != nullptr);
    QCOMPARE(contents->minimumHeight(), 0);
    QCOMPARE(contents->maximumWidth(), QWIDGETSIZE_MAX);
    for (QWidget *control : ui->settingsTab->findChildren<QWidget *>()) {
        if (control->objectName().isEmpty()
                || control->objectName().startsWith(QStringLiteral("qt_"))) {
            continue;
        }
        if (qobject_cast<QLineEdit *>(control) || qobject_cast<QComboBox *>(control)
                || qobject_cast<QAbstractSpinBox *>(control)
                || qobject_cast<QPushButton *>(control)) {
            QVERIFY2(control->parentWidget() != nullptr, qPrintable(control->objectName()));
            QVERIFY2(control->parentWidget()->layout() != nullptr,
                     qPrintable(control->objectName()));
        }
    }
}

void TestSettingsLayout::applyIsIdempotent()
{
    const int column = ui->label_72->minimumWidth();
    const int spinWidth = ui->Set_UI_Font_sizeBox->maximumWidth();
    refineSettingsLayout(ui->settingsTab);
    refineSettingsLayout(ui->settingsTab);
    QCOMPARE(ui->label_72->minimumWidth(), column);
    QCOMPARE(ui->Set_UI_Font_sizeBox->maximumWidth(), spinWidth);
    QCOMPARE(ui->Set_scrollArea->widget()->layout()->contentsMargins(),
             QMargins(16, 16, 16, 16));
}

void TestSettingsLayout::fontChangeRefreshesMetrics()
{
    const QFont base = QApplication::font();
    const int before = ui->Set_UI_Font_sizeBox->maximumWidth();
    QFont font = base;
    font.setPointSizeF(base.pointSizeF() * 1.5);
    QApplication::setFont(font);
    refineSettingsLayout(ui->settingsTab);
    QVERIFY2(ui->Set_UI_Font_sizeBox->maximumWidth() > before,
             qPrintable(QStringLiteral("%1 -> %2")
                                .arg(before).arg(ui->Set_UI_Font_sizeBox->maximumWidth())));
    QVERIFY(ui->label_72->minimumWidth() >= 96);
    QApplication::setFont(base);
    refineSettingsLayout(ui->settingsTab);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    TestSettingsLayout test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_settingslayout.moc"
