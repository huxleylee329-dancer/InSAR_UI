#include"Interferometric_Formation.h"
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
Interferometric_Formation::Interferometric_Formation(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::Interferometric_Formation)
{
    ui->setupUi(this);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
    ui->win_w_label->setHidden(1);
    ui->win_w->setHidden(1);
    ui->win_h_label->setHidden(1);
    ui->win_h->setHidden(1);
    connect(ui->iscoherence, &QCheckBox::stateChanged, this, &Interferometric_Formation::ChangeSetting);
}
Interferometric_Formation::~Interferometric_Formation()
{
    Interferometric_Formation_thread = NULL;
    if (copy)
    {
        for (int i = 0; i < ui->comboBox->count(); i++)
        {
            if (!copy->findItems(ui->comboBox->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
}

void Interferometric_Formation::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QString::fromLocal8Bit("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}
void Interferometric_Formation::endProcess()
{
    Interferometric_Formation_thread->thread()->quit();
    Interferometric_Formation_thread->thread()->wait();
    ui->progressBar->hide();
    this->close();
}
void Interferometric_Formation::endThread()
{
    Interferometric_Formation_thread->thread()->quit();
    Interferometric_Formation_thread->thread()->wait();
}
void Interferometric_Formation::StopThread()
{
    if(Interferometric_Formation_thread != NULL)
        if (Interferometric_Formation_thread->thread()->isRunning())
        {
            Interferometric_Formation_thread->thread()->requestInterruption();
            Interferometric_Formation_thread->thread()->quit();
            Interferometric_Formation_thread->thread()->wait();
        }
   
}
void Interferometric_Formation::TransitModel(QStandardItemModel* model)
{
    emit sendCopy(model);
}

void Interferometric_Formation::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox->setDisabled(0);
        ui->comboBox_2->setDisabled(0);
        ui->comboBox_3->setDisabled(0);
        ui->file_name->setDisabled(0);
        ui->iscoherence->setDisabled(0);
        ui->Isdeflat->setDisabled(0);
        ui->Istopo_removal->setDisabled(0);
        ui->win_h->setDisabled(0);
        ui->win_w->setDisabled(0);
        ui->multilook_az->setDisabled(0);
        ui->multilook_rg->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
    }
    else
    {
        ui->comboBox->setDisabled(1);
        ui->comboBox_2->setDisabled(1);
        ui->comboBox_3->setDisabled(1);
        ui->file_name->setDisabled(1);
        ui->iscoherence->setDisabled(1);
        ui->Isdeflat->setDisabled(1);
        ui->Istopo_removal->setDisabled(1);
        ui->win_h->setDisabled(1);
        ui->win_w->setDisabled(1);
        ui->multilook_az->setDisabled(1);
        ui->multilook_rg->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
    }


}

void Interferometric_Formation::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox->addItem(model->item(i, 0)->text());
        model->item(i, 0)->setStatusTip(IN_PROCESS);
    }
    this->save_path = copy->item(0, 1)->text();
    QStandardItem* project = NULL;
    QStandardItem* node = NULL;
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
        if (project->child(i, 1)->text().compare("complex-2.0") == 0 ||
            project->child(i, 1)->text().compare("complex-3.0") == 0)
        {
            ui->comboBox_2->addItem(project->child(i, 0)->text());
            if (!node)
                node = project->child(i, 0);
        }
            
    }
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("未检测到可处理数据，请先进行配准！"));
        this->deleteLater();
    }
    ui->comboBox_2->setCurrentIndex(0);
    ui->comboBox_3->clear();
    if (node != NULL)
    {
        int children = node->rowCount();
        for (int i = 0; i < children; i++)
        {
            ui->comboBox_3->addItem(node->child(i, 0)->text());
        }
    }
    else
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("无配准后图像用于生成干涉相位，请先进行配准!"));
        this->deleteLater();
    }
    ui->comboBox_3->setCurrentIndex(0);
}

void Interferometric_Formation::ChangeSetting()
{
    if (ui->iscoherence->isChecked())
    {
        ui->win_h->setHidden(0);
        ui->win_w->setHidden(0);
        ui->win_w_label->setHidden(0);
        ui->win_h_label->setHidden(0);
    }
    else
    {
        ui->win_w_label->setHidden(1);
        ui->win_w->setHidden(1);
        ui->win_h_label->setHidden(1);
        ui->win_h->setHidden(1);
    }
}

void Interferometric_Formation::on_comboBox_currentIndexChanged()
{
    if (ui->comboBox->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        this->save_path = copy->item(project->row(), 1)->text();
        int count = project->rowCount();
        if (count != 0)
        {
            //ui->comboBox_2->blockSignals(true);
            ui->comboBox_2->clear();
            QStandardItem* node = NULL;
            //ui->comboBox_2->setMaxCount(count);
            for (int i = 0 ; i < count; i++)
            {
                if (project->child(i, 1)->text().compare("complex-2.0") == 0 ||
                    project->child(i, 1)->text().compare("complex-3.0") == 0
                    )
                {
                    ui->comboBox_2->addItem(project->child(i, 0)->text());
                    if (!node)
                        node = project->child(i, 0);
                }
                    
            }
            //ui->comboBox_2->blockSignals(false);
            //ui->comboBox_2->setCurrentIndex(0);
            if (node != NULL)
            {
                int children = node->rowCount();
                ui->comboBox_3->clear();
                for (int i = 0; i < children; i++)
                {
                    ui->comboBox_3->addItem(node->child(i, 0)->text());
                }
            }
            else
            {
                ui->comboBox_3->clear();
            }
            
        }
        else
        {
            ui->comboBox_2->clear();
            ui->comboBox_3->clear();
        }
    }


    //ui->comboBox->blockSignals(false);
    //ui->comboBox_3->setCurrentIndex(0);
}

void Interferometric_Formation::on_comboBox_2_currentIndexChanged()
{
    if (ui->comboBox_2->count()!=0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        //QString name = ui->comboBox_2->currentText();
        QStandardItem* node = NULL;
        int count = project->rowCount();
        int i = 0;
        for (i = 0; i < count; i++)
        {
            if (project->child(i, 0)->text() == ui->comboBox_2->currentText())
            {
                this->image_number = project->child(i, 0)->rowCount();
                node = project->child(i, 0);
                break;
            }

        }
        if (node != NULL)
        {
            ui->comboBox_3->clear();
            //int children = node.at(i)->rowCount();
            for (int j = 0; j < this->image_number; j++)
            {
                ui->comboBox_3->addItem(node->child(j, 0)->text());
            }
        }
        else
        {
            ui->comboBox_3->clear();
        }
    }

    //ui->comboBox_2->blockSignals(true);
    
   // ui->comboBox_2->blockSignals(false);
    //ui->comboBox_3->setCurrentIndex(0);
}

void Interferometric_Formation::on_buttonBox_accepted()
{
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无可处理数据，请先进行配准或更换工程！"));
        return;
    }
    if (ui->comboBox_3->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该节点下无可处理数据，请先进行配准或更换节点！"));
        return;
    }
    int win_width, win_height;
    bool bFlag = false;
    bool bFlag2 = false;
    if (ui->Isdeflat->isChecked())
    {

    }
    if (ui->Istopo_removal->isChecked())
    {

    }
    if (ui->iscoherence->isChecked())
    {
        if (ui->win_w->text().isEmpty() || ui->win_h->text().isEmpty())
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入估计相干系数窗口参数！"));
            return;
        }
        win_width = ui->win_w->text().toInt(&bFlag);
        win_height = ui->win_h->text().toInt(&bFlag2);
        if (bFlag == false || bFlag2 == false)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("相干系数窗口尺寸应为大于0的奇数！"));
            return;
        }
        else if (win_width <= 0 || win_height <= 0 || win_width % 2 == 0 || win_height % 2 == 0)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("相干系数窗口尺寸应为大于0的奇数！"));
            return;
        }
    }
    else
    {
        win_width = 0;
        win_height = 0;
    }
    if (ui->multilook_az->text().isEmpty() || ui->multilook_rg->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入多视倍数(应为正整数，无需要可输入1)！"));
        return;
    }
    int multi_az = ui->multilook_az->text().toInt(&bFlag);
    int multi_rg = ui->multilook_rg->text().toInt(&bFlag2);
    if (bFlag == false || bFlag2 == false)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("多视倍数应为正整数！"));
        return;
    }
    else if (multi_az <= 0 || multi_rg <= 0 )
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("多视倍数应为正整数！"));
        return;
    }
    if (ui->file_name->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入保存干涉相位的文件夹名称！"));
        return;
    }
    bFlag = ui->file_name->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请注意文件夹名称应当为数字、字母及下划线的组合！"));
        return;
    }
    Interferometric_Formation_thread = new MyThread;
    Interferometric_Formation_thread->moveToThread(new QThread(this));
    //this->Process->setAutoClose(true);
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    QList<int> para;
    //.push_back(ui->indexedit->text().toInt());
    //para.push_back(ui->interpedit->text().toInt());
    //para.push_back(ui->blockedit->text().toInt());
    //para.push_back(this->image_number);
    //this->thread()->msleep(1);
    connect(this, &Interferometric_Formation::operate, Interferometric_Formation_thread, &MyThread::Interferometric, Qt::QueuedConnection);
    connect(Interferometric_Formation_thread, &MyThread::updateProcess, this, &Interferometric_Formation::updateProcess);
    connect(Interferometric_Formation_thread->thread(), &QThread::finished, Interferometric_Formation_thread, &MyThread::deleteLater);
    connect(Interferometric_Formation_thread, &MyThread::endProcess, this, &Interferometric_Formation::endProcess);
    connect(this, &QWidget::destroyed, this, &Interferometric_Formation::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Interferometric_Formation::StopThread);// , Qt::QueuedConnection);
    connect(Interferometric_Formation_thread, &MyThread::sendModel, this, &Interferometric_Formation::TransitModel);
    Interferometric_Formation_thread->thread()->start();
    ChangeVision(false);
    //connect(thread, &MyThread::endProcess, this, &MainWindow::endProcess);
    emit operate(ui->Isdeflat->isChecked(),ui->Istopo_removal->isChecked(),ui->iscoherence->isChecked(),
        ui->comboBox_3->currentIndex(),win_width, win_height, ui->multilook_rg->text().toInt(),
        ui->multilook_az->text().toInt(),this->save_path, ui->comboBox->currentText(), ui->comboBox_2->currentText(),
        ui->file_name->text(), this->copy);
}

void Interferometric_Formation::on_buttonBox_rejected()
{
    this->close();
}