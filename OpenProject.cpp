#include"OpenProject.h"
#include"icon_source.h"
#include<QFileDialog>
#include<QMessageBox>
OpenProject::OpenProject(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::OpenProject)
{
    ui->setupUi(this);
    setFixedSize(450, 130);
}

OpenProject::~OpenProject()
{
    delete ui;
}

void OpenProject::on_buttonBox_accepted()
{
    QString filename = ui->PathLine->text();
    if (filename.isEmpty() || !QFileInfo::exists(filename) || !QFileInfo(filename).isFile())
    {
        QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("无法加载工程文件，请检查文件是否损坏或路径是否正确。"));
        return;
    }
    emit projectSelected(filename);
    accept();
}

void OpenProject::on_buttonBox_rejected()
{
    reject();
}

void OpenProject::on_BrowseButton_pressed()
{
    QString filename = QFileDialog::getOpenFileName(this,
        "Path of project",
        "",
        "*.insar");
    if (filename.isEmpty())
    {
        return; // 用户点击取消，直接返回，保留原有值
    }
    ui->PathLine->setText(filename);
}
