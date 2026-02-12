#include"Unwrap_ui.h"
#include"icon_source.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
//#include<Utils.h>
#include<FormatConversion.h>
#include<qmessagebox.h>
#include<QFile>
#include<QDir>
//#ifdef _DEBUG
//#pragma comment(lib, "Utils_d.lib")
//#pragma comment(lib, "FormatConversion_d.lib")
//#endif
//#include<FormatConversion.h>
Unwrap_ui::Unwrap_ui(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::Unwrap_ui)
{
    ui->setupUi(this);
    ui->ct_label->setHidden(1);
    ui->coherence_threshold->setHidden(1);
    this->method = 0;
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
    connect(ui->SPDButton, &QRadioButton::clicked, this, &Unwrap_ui::Change_Setting);
    connect(ui->MCFButton, &QRadioButton::clicked, this, &Unwrap_ui::Change_Setting);
    connect(ui->SnaphuButton, &QRadioButton::clicked, this, &Unwrap_ui::Change_Setting);
    connect(ui->Q_MButton, &QRadioButton::clicked, this, &Unwrap_ui::Change_Setting);
}
Unwrap_ui::~Unwrap_ui()
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
    Unwrap_thread = NULL;
}

void Unwrap_ui::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QString::fromLocal8Bit("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}
void Unwrap_ui::endProcess()
{
    Unwrap_thread->thread()->quit();
    Unwrap_thread->thread()->wait();
    ui->progressBar->hide();
    this->close();
}
void Unwrap_ui::endThread()
{
    Unwrap_thread->thread()->quit();
    Unwrap_thread->thread()->wait();
}
void Unwrap_ui::StopThread()
{
    if (Unwrap_thread != NULL)
        if (Unwrap_thread->thread()->isRunning())
        {
            Unwrap_thread->thread()->requestInterruption();
            Unwrap_thread->thread()->quit();
            Unwrap_thread->thread()->wait();
        }

}
void Unwrap_ui::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    emit sendCopy(model);
}

void Unwrap_ui::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox->setDisabled(0);
        ui->comboBox_2->setDisabled(0);
        ui->file_name->setDisabled(0);
        ui->SPDButton->setDisabled(0);
        ui->MCFButton->setDisabled(0);
        ui->Q_MButton->setDisabled(0);
        ui->SnaphuButton->setDisabled(0);
        ui->coherence_threshold->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
    }
    else
    {
        ui->comboBox->setDisabled(1);
        ui->comboBox_2->setDisabled(1);
        ui->file_name->setDisabled(1);
        ui->SPDButton->setDisabled(1);
        ui->MCFButton->setDisabled(1);
        ui->Q_MButton->setDisabled(1);
        ui->SnaphuButton->setDisabled(1);
        ui->coherence_threshold->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
    }


}

void Unwrap_ui::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox->addItem(model->item(i, 0)->text());
        model->item(i, 0)->setStatusTip(IN_PROCESS);
    }
    
    this->save_path = copy->item(0, 1)->text();
    QStandardItem* project = NULL;  
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
        if (model->data(model->index(i, 1, pro_index)).toString().compare("phase-1.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("phase-2.0") == 0 )
            ui->comboBox_2->addItem(model->data(model->index(i, 0, pro_index)).toString());
    }
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("未检测到可处理数据，请确保已经生成过干涉相位或已完成滤波！"));
        this->deleteLater();
    }
    ui->comboBox_2->setCurrentIndex(0);
}
void Unwrap_ui::on_comboBox_currentIndexChanged()
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
            if (copy->data(copy->index(i, 1, pro_index)).toString().compare("phase-1.0") == 0 ||
                copy->data(copy->index(i, 1, pro_index)).toString().compare("phase-2.0") == 0)
                ui->comboBox_2->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
        }
        //ui->comboBox_2->setCurrentIndex(0);
    }
}

void Unwrap_ui::on_comboBox_2_currentIndexChanged()
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

void Unwrap_ui::on_buttonBox_accepted()
{
    bool bFlag = false;
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无可处理数据，请先进行滤波或更换工程！"));
        return;
    }
    if (ui->SPDButton->isChecked())
    {
        this->method = 1;
    }
    else if (ui->MCFButton->isChecked())
    {
        this->method = 2;
    }
    else if (ui->SnaphuButton->isChecked())
    {
        this->method = 3;
    }
    else if (ui->Q_MButton->isChecked())
    {
        this->method = 4;
        if (ui->coherence_threshold->text().isEmpty())
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入0-1的相干系数阈值!"));
            return;
        }
       
        double threshold = ui->coherence_threshold->text().toDouble(&bFlag);
        if (!bFlag)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请确认相干系数阈值为小数!"));
            return;
        }
        else if (threshold < 0 || threshold >1)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请确认相干系数阈值在0-1!"));
            return;
        }
    } 
    if (this->method == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请选择解缠方法!"));
        return;
    }
    if (ui->file_name->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入保存解缠相位的文件夹名称！"));
        return;
    }
    bFlag = ui->file_name->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请注意文件夹名称应当为数字、字母及下划线的组合！"));
        return;
    }
    Unwrap_thread = new MyThread;
    Unwrap_thread->moveToThread(new QThread(this));
    //this->Process->setAutoClose(true);
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    QList<int> para;
    para.push_back(this->method);
    // para.push_back(this->image_number);
     //this->thread()->msleep(1);
    connect(this, &Unwrap_ui::operate, Unwrap_thread, &MyThread::QUnwrap, Qt::QueuedConnection);
    connect(Unwrap_thread, &MyThread::updateProcess, this, &Unwrap_ui::updateProcess);
    connect(Unwrap_thread->thread(), &QThread::finished, Unwrap_thread, &MyThread::deleteLater);
    connect(Unwrap_thread, &MyThread::endProcess, this, &Unwrap_ui::endProcess);
    connect(this, &QWidget::destroyed, this, &Unwrap_ui::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Unwrap_ui::StopThread);// , Qt::QueuedConnection);
    connect(Unwrap_thread, &MyThread::sendModel, this, &Unwrap_ui::TransitModel);
    Unwrap_thread->thread()->start();
    ChangeVision(false);
    //connect(thread, &MyThread::endProcess, this, &MainWindow::endProcess);
    emit operate(this->method, ui->coherence_threshold->text().toDouble(), this->save_path, ui->comboBox->currentText(), ui->comboBox_2->currentText(), ui->file_name->text(), this->copy);
}

void Unwrap_ui::on_buttonBox_rejected()
{
    this->close();
}

void Unwrap_ui::Change_Setting()
{
    if (ui->SPDButton->isChecked())
    {
        ui->ct_label->setHidden(1);
        ui->coherence_threshold->setHidden(1);
        this->method = 1;
    }
    else if (ui->MCFButton->isChecked())
    {
        ui->ct_label->setHidden(1);
        ui->coherence_threshold->setHidden(1);
        this->method = 2;
    }
    else if (ui->SnaphuButton->isChecked())
    {
        ui->ct_label->setHidden(1);
        ui->coherence_threshold->setHidden(1);
        this->method = 3;
    }
    else if (ui->Q_MButton->isChecked())
    {
        ui->ct_label->setHidden(0);
        ui->coherence_threshold->setHidden(0);
        this->method = 4;
    }
    else
    {
        ui->ct_label->setHidden(1);
        ui->coherence_threshold->setHidden(1);
        this->method = 0;
    }
}