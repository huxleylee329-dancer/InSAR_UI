#include "NewProject.h"
#include <qfiledialog.h>
#include<qmessagebox.h>
#include<qsettings.h>
#include "NodeUtils.h"
#include"qregularexpression.h"
#include<opencv2/opencv.hpp>
#include<opencv2/highgui.hpp>
using namespace cv;
NewProject::NewProject(QWidget* parent) :
	QDialog(parent),
	ui(new Ui::NewProject)
{
	ui->setupUi(this);
	setFixedSize(450, 130);
    copy_model = NULL;
    //QSettings settings("Config.ini", QSettings::IniFormat);
    //settings.beginGroup("Project");
    //this->save = settings.value("SavePath").toString();
    //settings.endGroup();
}

NewProject::~NewProject()
{

}

void NewProject::on_savepushButton_pressed()
{
    QString folder = QFileDialog::getExistingDirectory(this, "Path of project", "");
    if (folder.isEmpty())
    {
        return; // 用户点击取消，直接返回，保留原有值
    }
    this->save = folder;

    QString name = ui->NamelineEdit->text();
    if (!name.isEmpty())
    {
        ui->savelineEdit->setText(QDir(folder).filePath(name));
    }
    else
    {
        name = QString("default");
        ui->NamelineEdit->setText(name);
        ui->savelineEdit->setText(QDir(folder).filePath(name));
    }
}
void NewProject::saveProjectSettings()
{
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    settings.beginGroup("Project");
    settings.setValue("projectname", ui->NamelineEdit->text());
    settings.setValue("SavePath", ui->savelineEdit->text());
    settings.endGroup();
}

void NewProject::saveSystemSettings()
{
    QSettings *settings=new QSettings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    settings->beginGroup("Project");
    settings->setValue("SavePath", ui->savelineEdit->text());
    settings->endGroup();
}
void NewProject::on_buttonBox_accepted()
{
    if (ui->NamelineEdit->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请输入新建工程名称（该名称应为数字、字母及下划线的组合）！"));
        return;
    }
    bool bFlag = ui->NamelineEdit->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请注意工程名称应当为数字、字母及下划线的组合！"));
        return;
    }
    if (ui->savelineEdit->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请浏览或输入保存工程路径！"));
        return;
    }
    bFlag = ui->savelineEdit->text().contains(QRegularExpression("^[\\n\\w:.\\()-/]+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请注意工程路径不应包含中文或特殊字符！"));
        return;
    }

    //工程防重名检查
    if (copy_model)
    {
        if (copy_model->findItems(ui->NamelineEdit->text() + ".insar").size() != 0) return;
    }

    //saveProjectSettings();
    //saveSystemSettings();
    this->project = ui->NamelineEdit->text();
    this->save = ui->savelineEdit->text();

    QDir dir;
    if (!dir.exists(this->save))
    {
        if (!dir.mkpath(this->save))
        {
            QMessageBox::warning(this, "Warning!", QStringLiteral("无法创建目标工程目录，请检查路径权限或合法性！"));
            return;
        }
    }
    emit sendPath(this->project, this->save);
    accept();
}
void NewProject::on_buttonBox_rejected()
{
    reject();
}

void NewProject::ReceiveModel(QStandardItemModel* model)
{
    copy_model = model;
}

