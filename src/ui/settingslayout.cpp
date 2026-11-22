#include "settingslayout.h"

#include <QAbstractSpinBox>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
const int kPageMargin = 16;
const int kColumnSpacing = 12;
const int kGroupSpacing = 8;
const int kGroupMargin = 12;
const int kMinLabelColumn = 96;
const int kMaxLabelColumn = 240;

bool isInputWidget(const QWidget *widget)
{
    return qobject_cast<const QLineEdit *>(widget)
            || qobject_cast<const QComboBox *>(widget)
            || qobject_cast<const QAbstractSpinBox *>(widget);
}

QFontMetrics pageMetrics(const QWidget *page)
{
    return QFontMetrics(page->font());
}

void markDescription(QLabel *label)
{
    if (!label || label->text().isEmpty()) {
        return;
    }
    label->setProperty("settingsDescription", true);
    label->style()->unpolish(label);
    label->style()->polish(label);
}

QLayout *layoutContaining(QWidget *child)
{
    if (!child || !child->parentWidget()) {
        return nullptr;
    }
    for (QLayout *layout : child->parentWidget()->findChildren<QLayout *>()) {
        if (layout->indexOf(child) >= 0) {
            return layout;
        }
    }
    return nullptr;
}

void alignCheckBoxRow(QCheckBox *box)
{
    if (!box || !box->text().isEmpty()) {
        return;
    }
    QLayout *row = layoutContaining(box);
    if (!row) {
        return;
    }
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    const int index = row->indexOf(box);
    if (QLayoutItem *item = row->itemAt(index)) {
        item->setAlignment(Qt::AlignTop);
    }
    box->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    for (int i = 0; i < row->count(); ++i) {
        QWidget *child = row->itemAt(i)->widget();
        if (QLabel *label = qobject_cast<QLabel *>(child)) {
            // Only the vertical alignment is fixed; the caption must keep the
            // full remaining row width so word wrap stays readable.
            row->setAlignment(label, Qt::AlignTop);
            if (QBoxLayout *boxLayout = qobject_cast<QBoxLayout *>(row)) {
                boxLayout->setStretch(i, 1);
            }
            label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            label->setWordWrap(true);
            if (label->text().size() >= 40) {
                markDescription(label);
            }
        }
    }
}
} // namespace

void refineSettingsLayout(QWidget *settingsPage)
{
    if (!settingsPage) {
        return;
    }

    QScrollArea *scroll = settingsPage->findChild<QScrollArea *>(QStringLiteral("Set_scrollArea"));
    if (scroll) {
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        if (QWidget *contents = scroll->widget()) {
            contents->setMinimumWidth(0);
            contents->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            if (QLayout *contentLayout = contents->layout()) {
                contentLayout->setContentsMargins(kPageMargin, kPageMargin,
                                                  kPageMargin, kPageMargin);
                if (QHBoxLayout *columns = qobject_cast<QHBoxLayout *>(contentLayout)) {
                    // Let the single content column own the available width;
                    // the trailing spacer only absorbs leftover pixels.
                    columns->setStretch(0, 1);
                    if (columns->count() > 1) {
                        columns->setStretch(columns->count() - 1, 0);
                    }
                }
            }
        }
    }

    const QFontMetrics metrics = pageMetrics(settingsPage);
    const int controlHeight = metrics.height() + 8;

    // Group boxes: uniform inner rhythm and stack at their preferred height.
    const QList<QGroupBox *> groups = settingsPage->findChildren<QGroupBox *>();
    int maxRowLabel = kMinLabelColumn;
    for (QGroupBox *group : groups) {
        group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        if (QLayout *layout = group->layout()) {
            layout->setContentsMargins(kGroupMargin, kGroupMargin + 4,
                                       kGroupMargin, kGroupMargin);
            layout->setSpacing(kGroupSpacing);
        }
    }

    // A single label column for rows that place a caption beside a control.
    for (QHBoxLayout *row : settingsPage->findChildren<QHBoxLayout *>()) {
        QLabel *caption = nullptr;
        bool hasInput = false;
        for (int i = 0; i < row->count(); ++i) {
            QWidget *child = row->itemAt(i)->widget();
            if (!caption) {
                caption = qobject_cast<QLabel *>(child);
            }
            if (isInputWidget(child) || qobject_cast<QSlider *>(child)
                    || qobject_cast<QPushButton *>(child)) {
                hasInput = true;
            }
        }
        if (!caption || !hasInput || caption->text().isEmpty()
                || Qt::mightBeRichText(caption->text())) {
            continue;
        }
        const int needed = metrics.horizontalAdvance(caption->text()) + 12;
        maxRowLabel = qMax(maxRowLabel, needed);
        row->setSpacing(8);
    }
    maxRowLabel = qBound(kMinLabelColumn, maxRowLabel, kMaxLabelColumn);

    for (QHBoxLayout *row : settingsPage->findChildren<QHBoxLayout *>()) {
        QLabel *caption = nullptr;
        bool hasInput = false;
        for (int i = 0; i < row->count(); ++i) {
            QWidget *child = row->itemAt(i)->widget();
            if (!caption) {
                caption = qobject_cast<QLabel *>(child);
            }
            if (isInputWidget(child) || qobject_cast<QSlider *>(child)
                    || qobject_cast<QPushButton *>(child)) {
                hasInput = true;
            }
        }
        if (!caption || !hasInput || caption->text().isEmpty()
                || Qt::mightBeRichText(caption->text())) {
            continue;
        }
        caption->setFixedWidth(maxRowLabel);
        caption->setWordWrap(true);
        caption->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    }

    // Common control height and vertical policy.
    for (QWidget *control : settingsPage->findChildren<QWidget *>()) {
        const bool input = qobject_cast<QLineEdit *>(control)
                || qobject_cast<QComboBox *>(control)
                || qobject_cast<QAbstractSpinBox *>(control)
                || qobject_cast<QPushButton *>(control);
        if (!input) {
            continue;
        }
        control->setMinimumHeight(qMax(control->minimumHeight(), controlHeight));
        QSizePolicy policy = control->sizePolicy();
        policy.setVerticalPolicy(QSizePolicy::Fixed);
        control->setSizePolicy(policy);
    }

    // Path-like fields expand; numeric fields stay compact.
    const QStringList pathNames = {
        QStringLiteral("Set_Client_envScriptEdit"),
        QStringLiteral("Set_Client_workingDirEdit"),
        QStringLiteral("Set_Client_startArgsEdit"),
        QStringLiteral("Set_Client_configPathEdit")
    };
    for (const QString &name : pathNames) {
        if (QLineEdit *edit = settingsPage->findChild<QLineEdit *>(name)) {
            edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            edit->setMinimumWidth(qMax(edit->minimumWidth(),
                                       metrics.horizontalAdvance(QStringLiteral("MMMMMMMMMM")) * 3));
        }
    }

    if (QLineEdit *configPath = settingsPage->findChild<QLineEdit *>(
            QStringLiteral("Set_Client_configPathEdit"))) {
        configPath->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    if (QComboBox *configBox = settingsPage->findChild<QComboBox *>(
            QStringLiteral("Set_Client_configFileBox"))) {
        configBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        for (QHBoxLayout *row : settingsPage->findChildren<QHBoxLayout *>()) {
            const int index = row->indexOf(configBox);
            if (index < 0) {
                continue;
            }
            for (int i = 0; i < row->count(); ++i) {
                row->setStretch(i, i == index ? 1 : 0);
            }
            break;
        }
    }

    const int numericWidth = metrics.horizontalAdvance(QStringLiteral("00000")) + 34;
    const QList<QSpinBox *> spinBoxes = settingsPage->findChildren<QSpinBox *>();
    for (QSpinBox *box : spinBoxes) {
        box->setMinimumWidth(qMin(numericWidth, 96));
        box->setMaximumWidth(numericWidth);
        box->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    if (QComboBox *theme = settingsPage->findChild<QComboBox *>(
            QStringLiteral("Set_UI_Theme_nameBox"))) {
        theme->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    for (QFontComboBox *fontBox : settingsPage->findChildren<QFontComboBox *>()) {
        fontBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    if (QSlider *opacity = settingsPage->findChild<QSlider *>(
            QStringLiteral("Set_UI_Opacity_slider"))) {
        opacity->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    // Buttons that only carry a short verb share one width so the rows line up.
    QHash<QString, int> buttonWidths;
    const QList<QPushButton *> buttons = settingsPage->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        const int width = metrics.horizontalAdvance(button->text()) + 28;
        buttonWidths[button->text()] = qMax(buttonWidths.value(button->text()), width);
    }
    for (QPushButton *button : buttons) {
        button->setMinimumWidth(buttonWidths.value(button->text()));
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    // Checkbox rows use an empty box plus a wrapped caption; keep them aligned.
    for (QCheckBox *box : settingsPage->findChildren<QCheckBox *>()) {
        alignCheckBoxRow(box);
    }

    // Long explanatory captions form the third hierarchy level.
    for (QLabel *label : settingsPage->findChildren<QLabel *>()) {
        if (!label->wordWrap() || label->text().isEmpty()) {
            continue;
        }
        if (Qt::mightBeRichText(label->text())
                || label->textInteractionFlags().testFlag(Qt::TextSelectableByMouse)) {
            continue;
        }
        if (label->text().size() >= 60) {
            markDescription(label);
        }
    }

    // Consistent spacing between the group boxes in the content column.
    if (QWidget *contents = scroll ? scroll->widget() : nullptr) {
        if (QVBoxLayout *column = contents->findChild<QVBoxLayout *>(
                QStringLiteral("verticalLayout_12"))) {
            column->setSpacing(kColumnSpacing);
        }
    }
}
