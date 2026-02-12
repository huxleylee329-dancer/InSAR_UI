#include"Filter_ui.h"
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
Filter_ui::Filter_ui(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::Filter_ui)
{
    ui->setupUi(this);
    this->method = 0;
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
    ui->Slop_label->setVisible(0);
    ui->Slop_win->setVisible(0);
    ui->Prefilter_label->setVisible(0);
    ui->Prefilter_win->setVisible(0);
    ui->Goldstein_label->setHidden(1);
    ui->Goldstein_win->setHidden(1);
    ui->alpha->setHidden(1);
    ui->alpha_label->setHidden(1);
    ui->n_pad->setHidden(1);
    ui->n_pad_label->setHidden(1);
    connect(ui->SlopButton, &QRadioButton::clicked, this, &Filter_ui::Change_Setting);
    connect(ui->DLButton, &QRadioButton::clicked, this, &Filter_ui::Change_Setting);
    connect(ui->GoldsteinButton, &QRadioButton::clicked, this, &Filter_ui::Change_Setting);
}
Filter_ui::~Filter_ui()
{
    emit sendCopy(copy);
    Filter_thread = NULL;
    if (copy)
    {
        for (int i = 0; i < ui->comboBox->count(); i++)
        {
            if (!copy->findItems(ui->comboBox->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
}

void Filter_ui::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QString::fromLocal8Bit("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}
void Filter_ui::endProcess()
{
    Filter_thread->thread()->quit();
    Filter_thread->thread()->wait();
    ui->progressBar->hide();
    /*if (copy)
    {
        if (copy->findItems(ui->comboBox->currentText())[0])
            copy->findItems(ui->comboBox->currentText())[0]->setStatusTip(NOT_IN_PROCESS);
    }*/
    this->close();
}
void Filter_ui::endThread()
{
    Filter_thread->thread()->quit();
    Filter_thread->thread()->wait();
}
void Filter_ui::StopThread()
{
    if(Filter_thread != NULL)
        if (Filter_thread->thread()->isRunning())
    {
        Filter_thread->thread()->requestInterruption();
        Filter_thread->thread()->quit();
        Filter_thread->thread()->wait();
    }
    //if (copy)
    //{
    //    if (copy->findItems(ui->comboBox->currentText())[0])
    //        copy->findItems(ui->comboBox->currentText())[0]->setStatusTip(NOT_IN_PROCESS);
    //}
}
void Filter_ui::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    //if (copy)
    //{
    //    if (copy->findItems(ui->comboBox->currentText())[0])
    //        copy->findItems(ui->comboBox->currentText())[0]->setStatusTip(NOT_IN_PROCESS);
    //}
    emit sendCopy(model);
}

void Filter_ui::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox->setDisabled(0);
        ui->comboBox_2->setDisabled(0);
        ui->file_name->setDisabled(0);
        ui->SlopButton->setDisabled(0);
        ui->Slop_win->setDisabled(0);
        ui->GoldsteinButton->setDisabled(0);
        ui->Goldstein_win->setDisabled(0);
        ui->alpha->setDisabled(0);
        ui->DLButton->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
    }
    else
    {
        ui->comboBox->setDisabled(1);
        ui->comboBox_2->setDisabled(1);
        ui->file_name->setDisabled(1);
        ui->SlopButton->setDisabled(1);
        ui->Slop_win->setDisabled(1);
        ui->GoldsteinButton->setDisabled(1);
        ui->Goldstein_win->setDisabled(1);
        ui->alpha->setDisabled(1);
        ui->DLButton->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
    }


}


void Filter_ui::ShowProjectList(QStandardItemModel* model)
{
    if (!model) return;
    if (model->rowCount() < 1) return;
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
        if (model->data(model->index(i, 1, pro_index)).toString().compare("phase-1.0") == 0)
            ui->comboBox_2->addItem(model->data(model->index(i, 0, pro_index)).toString());
    }
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("未检测到可处理数据，请先生成干涉相位！"));
        this->deleteLater();
    }
    ui->comboBox_2->setCurrentIndex(0);
}
void Filter_ui::on_comboBox_currentIndexChanged()
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
            if (copy->data(copy->index(i, 1, pro_index)).toString().compare("phase-1.0") == 0)
                ui->comboBox_2->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
        }
        //ui->comboBox_2->setCurrentIndex(0);
    }
}

void Filter_ui::on_comboBox_2_currentIndexChanged()
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

void Filter_ui::on_buttonBox_accepted()
{
    bool bFlag = false;
    bool bFlag2 = false;
    bool bFlag3 = false;
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无可处理数据，请先生成干涉数据或更换工程！"));
        return;
    }
    if (this->method == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请选择滤波方法!"));
        return;
    }
    else if (ui->SlopButton->isChecked())
    {
        if (ui->Slop_win->text().isEmpty() || ui->Prefilter_win->text().isEmpty())
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入预滤波及斜坡窗口滤波的尺寸（奇数）!"));
            return;
        }
        int slop = ui->Slop_win->text().toInt(&bFlag);
        int pre = ui->Prefilter_win->text().toInt(&bFlag2);
        if (bFlag == false || bFlag2 == false)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("预滤波及斜坡窗口滤波的尺寸应为奇数!"));
            return;
        }
        else if (slop <= 0 || pre <= 0 || slop % 2 == 0 || pre % 2 == 0)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("预滤波及斜坡窗口滤波的尺寸应为奇数!"));
            return;
        }
    }
    else if (ui->GoldsteinButton->isChecked())
    {
        if (ui->Goldstein_win->text().isEmpty() || ui->alpha->text().isEmpty() || ui->n_pad->text().isEmpty())
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入Goldstein滤波所需参数!其中窗口尺寸及补零窗口应为2的正整次幂（如64,16），alpha应为0-1的小数。"));
            return;
        }
        int goldstein = ui->Goldstein_win->text().toInt(&bFlag);
        int pad = ui->n_pad->text().toInt(&bFlag2);
        double a = ui->alpha->text().toDouble(&bFlag3);
        if (!(bFlag & bFlag2 & bFlag3))
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请确认输入参数是否规范（窗口尺寸及补零窗口应为2的正整次幂（如64,16），alpha应为0-1的小数）"));
            return;
        }
        else if ((goldstein & (goldstein - 1)) != 0 || (pad & (pad - 1)) != 0)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请确认窗口尺寸及补零窗口为2的正整次幂（如64,16）"));
            return;
        }
        else if (goldstein == 1 || pad == 1)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("窗口尺寸及补零尺寸不应为1"));
            return;
        }
        else if (a < 0 || a >1)
        {
            QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请确保alpha在0-1区间，alpha越大，滤波程度越高，适用于噪声更强的图像。"));
            return;
        }

    }
    else if (ui->DLButton->isChecked())
    {

    }
    if (ui->file_name->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入保存滤波相位的文件夹名称！"));
        return;
    }
    bFlag = ui->file_name->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请注意文件夹名称应当为数字、字母及下划线的组合！"));
        return;
    }
    Filter_thread = new MyThread;
    Filter_thread->moveToThread(new QThread(this));
    //this->Process->setAutoClose(true);
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    QList<int> para;
    para.push_back(ui->Prefilter_win->text().toInt());
    para.push_back(ui->Slop_win->text().toInt());
    para.push_back(ui->Goldstein_win->text().toInt());
    //para.push_back(ui->alpha->text().toInt());
    para.push_back(ui->n_pad->text().toInt());
    para.push_back(this->method);
   // para.push_back(this->image_number);
    //this->thread()->msleep(1);
    connect(this, &Filter_ui::operate, Filter_thread, &MyThread::Denoise, Qt::QueuedConnection);
    connect(Filter_thread, &MyThread::updateProcess, this, &Filter_ui::updateProcess);
    connect(Filter_thread->thread(), &QThread::finished, Filter_thread, &MyThread::deleteLater);
    connect(Filter_thread, &MyThread::endProcess, this, &Filter_ui::endProcess);
    connect(this, &QWidget::destroyed, this, &Filter_ui::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Filter_ui::StopThread);// , Qt::QueuedConnection);
    connect(Filter_thread, &MyThread::sendModel, this, &Filter_ui::TransitModel);
    Filter_thread->thread()->start();
    ChangeVision(false);
    //connect(thread, &MyThread::endProcess, this, &MainWindow::endProcess);
    emit operate(para, ui->alpha->text().toDouble(), this->save_path, ui->comboBox->currentText(), ui->comboBox_2->currentText(), ui->file_name->text(), this->copy);
}

void Filter_ui::on_buttonBox_rejected()
{
    this->close();
}

void Filter_ui::Change_Setting()
{
    QWidget* Slop[] = { ui->Slop_label,ui->Slop_win, ui->Prefilter_label, ui->Prefilter_win };
    QWidget* Goldstein[] = { ui->Goldstein_label, ui->Goldstein_win };
    if (ui->SlopButton->isChecked())
    {
        ui->Slop_label->setVisible(1);
        ui->Slop_win->setVisible(1);
        ui->Prefilter_label->setVisible(1);
        ui->Prefilter_win->setVisible(1);
        ui->Goldstein_label->setHidden(1);
        ui->Goldstein_win->setHidden(1);
        ui->alpha->setHidden(1);
        ui->alpha_label->setHidden(1);
        ui->n_pad->setHidden(1);
        ui->n_pad_label->setHidden(1);
        this->method = 1;
    }
    else if (ui->GoldsteinButton->isChecked())
    {
        ui->Slop_label->setVisible(0);
        ui->Slop_win->setVisible(0);
        ui->Prefilter_label->setVisible(0);
        ui->Prefilter_win->setVisible(0);
        ui->Goldstein_label->setHidden(0);
        ui->Goldstein_win->setHidden(0);
        ui->alpha->setHidden(0);
        ui->alpha_label->setHidden(0);
        ui->n_pad->setHidden(0);
        ui->n_pad_label->setHidden(0);
        this->method = 2;
    }
    else if (ui->DLButton->isChecked())
    {
        ui->Slop_label->setVisible(0);
        ui->Slop_win->setVisible(0);
        ui->Prefilter_label->setVisible(0);
        ui->Prefilter_win->setVisible(0);
        ui->Goldstein_label->setHidden(1);
        ui->Goldstein_win->setHidden(1);
        ui->alpha->setHidden(1);
        ui->alpha_label->setHidden(1);
        ui->n_pad->setHidden(1);
        ui->n_pad_label->setHidden(1);
        this->method = 3;
    }
    else
    {
        ui->Slop_label->setVisible(0);
        ui->Slop_win->setVisible(0);
        ui->Prefilter_label->setVisible(0);
        ui->Prefilter_win->setVisible(0);
        ui->Goldstein_label->setHidden(1);
        ui->Goldstein_win->setHidden(1);
        ui->alpha->setHidden(1);
        ui->alpha_label->setHidden(1);
        ui->n_pad->setHidden(1);
        ui->n_pad_label->setHidden(1);
        this->method = 0;
    }
}
