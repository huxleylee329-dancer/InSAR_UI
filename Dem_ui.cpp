#include"Dem_ui.h"
#include"icon_source.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
//#include<Utils.h>
//#include<FormatConversion.h>
#include<qmessagebox.h>
#include<QFile>
#include<QDir>
//#ifdef _DEBUG
//#pragma comment(lib, "Utils_d.lib")
//#pragma comment(lib, "FormatConversion_d.lib")
//#endif
//#include<FormatConversion.h>
Dem_ui::Dem_ui(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::Dem_ui)
{
    ui->setupUi(this);
    ui->times_label->setHidden(1);
    ui->times->setHidden(1);
    this->method = 0;
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
    connect(ui->NewtonButton, &QRadioButton::clicked, this, &Dem_ui::Change_Setting);
}
Dem_ui::~Dem_ui()
{
    if (copy)
    {
        for (int i = 0; i < ui->comboBox->count(); i++)
        {
            if (!copy->findItems(ui->comboBox->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
    emit sendCopy(copy);
    Dem_thread = NULL;
}

void Dem_ui::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QString::fromLocal8Bit("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}
void Dem_ui::endProcess()
{
    Dem_thread->thread()->quit();
    Dem_thread->thread()->wait();
    ui->progressBar->hide();
    this->close();
}
void Dem_ui::endThread()
{
    Dem_thread->thread()->quit();
    Dem_thread->thread()->wait();
}
void Dem_ui::StopThread()
{
    if (Dem_thread != NULL)
        if (Dem_thread->thread()->isRunning())
        {
            Dem_thread->thread()->requestInterruption();
            Dem_thread->thread()->quit();
            Dem_thread->thread()->wait();
        }

}
void Dem_ui::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    emit sendCopy(model);
}

void Dem_ui::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox->setDisabled(0);
        ui->comboBox_2->setDisabled(0);
        ui->file_name->setDisabled(0);
        ui->NewtonButton->setDisabled(0);
        ui->times->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
    }
    else
    {
        ui->comboBox->setDisabled(1);
        ui->comboBox_2->setDisabled(1);
        ui->file_name->setDisabled(1);
        ui->NewtonButton->setDisabled(1);
        ui->times->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
    }


}

void Dem_ui::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox->addItem(model->item(i, 0)->text());
        model->item(i, 0)->setStatusTip(IN_PROCESS);
    }
   
    this->save_path = copy->item(0, 1)->text();
    QStandardItem* project = NULL;
    //QList<QStandardItem*> project_list = model->findItems(ui->comboBox->currentText());

    int count = 0;
    for (int i = 0; i < model->rowCount(); i++)
    {
        if (model->item(i, 0)->rowCount() != 0)
        {
            count = model->item(i, 0)->rowCount();
            project = model->item(i, 0);
            ui->comboBox->setCurrentIndex(i);
            break;
        }

    }
    if (count == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("无可处理数据，请先导入数据！"));
        this->deleteLater();
    }
    QModelIndex pro_index = model->indexFromItem(project);
    ui->comboBox_2->clear();
    for (int i = 0; i < count; i++)
    {
        if (model->data(model->index(i, 1, pro_index)).toString().compare("phase-3.0") == 0)
            ui->comboBox_2->addItem(model->data(model->index(i, 0, pro_index)).toString());
    }
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("未检测到可处理数据，请先进行解缠！"));
        this->deleteLater();
    }
    ui->comboBox_2->setCurrentIndex(0);
}
void Dem_ui::on_comboBox_currentIndexChanged()
{
    if (ui->comboBox->count() != 0)
    {
        /*this->save_path = copy->item(ui->comboBox->currentIndex(), 1)->text();*/
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        this->save_path = copy->item(project->row(), 1)->text();
        QModelIndex pro_index = copy->indexFromItem(project);
        int count = project->rowCount();
        ui->comboBox_2->clear();
        //ui->comboBox_2->setMaxCount(count);
        for (int i = 0; i < count; i++)
        {
            if (copy->data(copy->index(i, 1, pro_index)).toString().compare("phase-3.0") == 0)
                ui->comboBox_2->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
        }
        //ui->comboBox_2->setCurrentIndex(0);
    }
}

void Dem_ui::on_comboBox_2_currentIndexChanged()
{
    /*QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (project->child(i, 0)->text() == ui->comboBox_2->currentText())
        {
            QStandardItem* node = project->child(i, 0);
            int count = 0;
            for (int j = 0; j < node->rowCount(); j++)
            {
                if (node->child(j, 0)->toolTip() == "phase")
                    count++;
            }
            this->image_number = count;
            break;
        }
    }*/
}

void Dem_ui::on_buttonBox_accepted()
{
    bool bFlag = false;
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无可处理数据，请先进行解缠或更换工程！"));
        return;
    }
    if (ui->NewtonButton->isChecked()) 
    {
        this->method = 1;
        if (ui->times->text().isEmpty())
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入牛顿法的迭代次数！"));
            return;
        }
        int times = ui->times->text().toInt(&bFlag);
        if (!bFlag)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("迭代次数应为正整数！"));
            return;
        }
        else if(times<=0)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("迭代次数应为正整数！"));
            return;
        }
    }
    if (this->method == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请选择Dem方法!"));
        return;
    }
    if (ui->file_name->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入保存DEM数据的文件夹名称！"));
        return;
    }
    bFlag = ui->file_name->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请注意文件夹名称应当为数字、字母及下划线的组合！"));
        return;
    }
    
    Dem_thread = new MyThread;
    Dem_thread->moveToThread(new QThread(this));
    //this->Process->setAutoClose(true);
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    QList<int> para;
    para.push_back(this->method);
    // para.push_back(this->image_number);
     //this->thread()->msleep(1);
    connect(this, &Dem_ui::operate, Dem_thread, &MyThread::QDem, Qt::QueuedConnection);
    connect(Dem_thread, &MyThread::updateProcess, this, &Dem_ui::updateProcess);
    connect(Dem_thread->thread(), &QThread::finished, Dem_thread, &MyThread::deleteLater);
    connect(Dem_thread, &MyThread::endProcess, this, &Dem_ui::endProcess);
    connect(this, &QWidget::destroyed, this, &Dem_ui::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Dem_ui::StopThread);// , Qt::QueuedConnection);
    connect(Dem_thread, &MyThread::sendModel, this, &Dem_ui::TransitModel);
    Dem_thread->thread()->start();
    ChangeVision(false);
    emit operate(this->method, ui->times->text().toInt(), this->save_path, ui->comboBox->currentText(), ui->comboBox_2->currentText(), ui->file_name->text(), this->copy);
}

void Dem_ui::on_buttonBox_rejected()
{
    this->close();
}

void Dem_ui::Change_Setting()
{
    if (ui->NewtonButton->isChecked())
    {
        ui->times_label->setHidden(0);
        ui->times->setHidden(0);
    }
    else
    {
        ui->times_label->setHidden(1);
        ui->times->setHidden(1);
    }
}