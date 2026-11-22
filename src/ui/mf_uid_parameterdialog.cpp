#include "mf_uid_parameterdialog.h"
#include "ui_mf_uid_parameterdialog.h"
#include <QMessageBox>
#include <QRegularExpression>

MF_UID_parameterDialog::MF_UID_parameterDialog(const QString& uid, const QString& atqa, const QString& sak, const QVariantMap& config, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MF_UID_parameterDialog)
{
    ui->setupUi(this);
    ui->UIDLineEdit->setText(uid);
    ui->ATQALineEdit->setText(atqa);
    ui->SAKLineEdit->setText(sak);
    this->config = config;
}

MF_UID_parameterDialog::~MF_UID_parameterDialog()
{
    delete ui;
}

void MF_UID_parameterDialog::on_buttonBox_accepted()
{
    const QString uid = ui->UIDLineEdit->text().remove(QRegularExpression("\\s")).toUpper();
    const QString atqa = ui->ATQALineEdit->text().remove(QRegularExpression("\\s")).toUpper();
    const QString sak = ui->SAKLineEdit->text().remove(QRegularExpression("\\s")).toUpper();
    const QRegularExpression hex("^[0-9A-F]+$");

    if(!hex.match(uid).hasMatch() || (uid.size() != 8 && uid.size() != 14 && uid.size() != 20))
    {
        QMessageBox::information(this, tr("Info"), tr("UID must be 4, 7 or 10 bytes of hex."));
        return;
    }
    if(!hex.match(atqa).hasMatch() || atqa.size() != 4)
    {
        QMessageBox::information(this, tr("Info"), tr("ATQA must be 2 bytes of hex."));
        return;
    }
    if(!hex.match(sak).hasMatch() || sak.size() != 2)
    {
        QMessageBox::information(this, tr("Info"), tr("SAK must be 1 byte of hex."));
        return;
    }

    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this, tr("Set Magic Card Parameters"),
        tr("This writes UID / ATQA / SAK to a Chinese Magic Card.\n\nContinue?"),
        QMessageBox::Yes | QMessageBox::No);
    if(choice != QMessageBox::Yes)
        return;

    QString cmd = config["cmd"].toString();
    cmd.replace("<uid>", uid);
    cmd.replace("<atqa>", atqa);
    cmd.replace("<sak>", sak);
    emit sendCMD(cmd);
}
