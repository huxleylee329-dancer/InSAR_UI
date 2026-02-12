#include "NewProject.h"
#include <qfiledialog.h>
#include<qmessagebox.h>
#include<qsettings.h>
#include"qregularexpression.h"
#include<opencv2/opencv.hpp>
#include<opencv2/highgui.hpp>
//#include<FormatConversion.h>
//#ifdef _DEBUG
//#pragma comment(lib, "Utils_d.lib")
//#pragma comment(lib, "FormatConversion_d.lib")
//#endif
using namespace cv;
NewProject::NewProject(QWidget* parent) :
	QWidget(parent),
	ui(new Ui::NewProject)
{
	ui->setupUi(this);
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
    //QString dir = QFileDialog::getExistingDirectory(this, "choose a folder", "");
    //ui->savelineEdit->setText(dir + QString("/") + ui->NamelineEdit->text());
    QString name = ui->NamelineEdit->text();
    QString folder;
    folder = QFileDialog::getExistingDirectory(this, "Path of project", "");
    this->save = folder;
    if (!name.isEmpty())
    {
        ui->savelineEdit->setText(folder + QString("/") + name);
    }
    else
    {
        name = QString("default");
        ui->NamelineEdit->setText(name);
        ui->savelineEdit->setText(folder + QString("/") + name);
    }
}
void NewProject::saveProjectSettings()
{
    QSettings settings(QString("Config.ini").arg(ui->savelineEdit->text()), QSettings::IniFormat);
    settings.beginGroup("Project");
    settings.setValue("projectname", ui->NamelineEdit->text());
    settings.setValue("SavePath", ui->savelineEdit->text());
    settings.endGroup();
}

void NewProject::saveSystemSettings()
{
    QSettings *settings=new QSettings("Config.ini", QSettings::IniFormat);
    settings->beginGroup("Project");
    settings->setValue("SavePath", ui->savelineEdit->text());
    settings->endGroup();
}
void NewProject::on_buttonBox_accepted()
{
    if (ui->NamelineEdit->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入新建工程名称（该名称应为数字、字母及下划线的组合）！"));
        return;
    }
    bool bFlag = ui->NamelineEdit->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请注意工程名称应当为数字、字母及下划线的组合！"));
        return;
    }
    if (ui->savelineEdit->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请浏览或输入保存工程路径！"));
        return;
    }
    bFlag = ui->savelineEdit->text().contains(QRegularExpression("^[\\n\\w:.\\()-/]+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请注意工程路径不应包含中文或特殊字符！"));
        return;
    }

    //工程防重名检查
    if (copy_model)
    {
        if (copy_model->findItems(ui->NamelineEdit->text() + ".insar").size() != 0) return;
    }

    //saveProjectSettings();
    //saveSystemSettings();
    QFileInfo info(this->save);
    if (!info.isDir()) return;
    QDir dir(this->save);
    this->project = ui->NamelineEdit->text();
    if (!dir.exists(this->project))
        dir.mkdir(this->project);
    /*this->save = ui->savelineEdit->text();*/
    emit sendPath(ui->NamelineEdit->text(),/*ui->masterlineEdit->text(), ui->slavelineEdit->text(),*/ ui->savelineEdit->text());
    close();
}
void NewProject::on_buttonBox_rejected()
{
    close();
}

void NewProject::ReceiveModel(QStandardItemModel* model)
{
    copy_model = model;
}

