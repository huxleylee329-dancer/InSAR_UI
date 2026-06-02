#include"Cut.h"
#include"icon_source.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<qmessagebox.h>
#include<Utils.h>
#include <QThread>
#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#endif
#include<FormatConversion.h>
Cut::Cut(QWidget* parent) :
    QWidget(parent), h5_left(-1), h5_right(-1), h5_top(-1), h5_bottom(-1),
    ui(new Ui::Cut)
{
    ui->setupUi(this);
    ui->tabWidget->setCurrentIndex(0);
    //this->DOC = new XMLFile;
    ui->progressBar->setHidden(1);
    ui->progressBar_2->setHidden(1);
    isPreviewPressed = false;
    isCutting = false;
    if (copy != NULL)
    {    
        copy = NULL;
    }

}
Cut::~Cut()
{
    Cut_thread = NULL;
    /*改变工程文件的处理状态为NOT_IN_PROCESS*/
    if (copy)
    {
        for (int i = 0; i < ui->comboBox->count(); i++)
        {
            if (!copy->findItems(ui->comboBox->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
        for (int i = 0; i < ui->comboBox_3->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_3->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_3->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
}
void Cut::updateProcess(int value, QString information)
{
    if (!ui->progressBar->isHidden())
    {
        ui->progressBar->setValue(value);
        ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
        ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    else if (!ui->progressBar_2->isHidden())
    {
        ui->progressBar_2->setValue(value);
        ui->progressBar_2->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
        ui->progressBar_2->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
   
}
void Cut::endProcess()
{
    Cut_thread->thread()->quit();
    Cut_thread->thread()->wait();
    ui->progressBar->hide();
    ui->progressBar_2->hide();
    isPreviewPressed = false;
    isCutting = false;
    ui->Preview->setDisabled(false);
    ui->Preview->setText(QStringLiteral("预览"));
    ui->Preview->repaint();
    ui->buttonBox_2->setDisabled(false);
    ui->comboBox_3->setDisabled(false);
    ui->comboBox_4->setDisabled(false);
    ui->lineEdit_2->setDisabled(false);
    this->close();
}
void Cut::endThread()
{
    Cut_thread->thread()->quit();
    Cut_thread->thread()->wait();
}
void Cut::StopThread()
{
    if (Cut_thread != NULL)
    {
        if (Cut_thread->thread()->isRunning())
        {
            Cut_thread->thread()->requestInterruption();
            Cut_thread->thread()->quit();
            Cut_thread->thread()->wait();
        }
    }
    isCutting = false;
}
void Cut::TransitModel(QStandardItemModel* model)
{
    emit sendCopy(model);
}
void Cut::ReceivePos(double left, double right, double top, double bottom)
{
    
    isCutting = true;
    h5_left = left;
    h5_right = right;
    h5_top = top;
    h5_bottom = bottom;

    if (ui->comboBox_4->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无可处理数据，请先配准再进行框选裁剪或更换工程！"));
        return;
    }
    if (ui->lineEdit_2->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请输入存放裁剪文件的文件夹名称！"));
        return;
    }
    bool bFlag = ui->lineEdit_2->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请注意文件夹名称应当为数字、字母及下划线的组合！"));
        return;
    }

    ui->Preview->setText(QStringLiteral("正在裁剪..."));
    ui->Preview->repaint();
    ui->buttonBox_2->setDisabled(true);
    ui->comboBox_3->setDisabled(true);
    ui->comboBox_4->setDisabled(true);
    ui->lineEdit_2->setDisabled(true);
    ui->progressBar_2->setHidden(0);
    ui->progressBar_2->setMinimum(0);
    ui->progressBar_2->setMaximum(100);
    Cut_thread = new CutWorker;
    Cut_thread->moveToThread(new QThread(this));
    ui->progressBar_2->setValue(0);
    ui->progressBar_2->show();
    connect(this, &Cut::operate2, Cut_thread, &CutWorker::Cut2, Qt::QueuedConnection);
    connect(Cut_thread, &CutWorker::updateProcess, this, &Cut::updateProcess);
    connect(Cut_thread->thread(), &QThread::finished, Cut_thread, &QObject::deleteLater);
    connect(Cut_thread->thread(), &QThread::finished, Cut_thread->thread(), &QObject::deleteLater);
    connect(Cut_thread, &CutWorker::endProcess, this, &Cut::endProcess);
    connect(this, &QWidget::destroyed, this, &Cut::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Cut::StopThread);
    connect(Cut_thread, &CutWorker::sendModel, this, &Cut::TransitModel);
    Cut_thread->thread()->start();
    ChangeVision(false);
    
    emit operate2(h5_left, h5_right, h5_top, h5_bottom, this->save_path, ui->comboBox_3->currentText(), ui->comboBox_4->currentText(), ui->lineEdit_2->text(), this->copy);
    
}

void Cut::cancelled()
{
    if (isCutting) return;//由于预览子窗口在点击确定或者关闭之后都会发送destroy消息，为了区分，设置是否正在裁剪标志。
    isPreviewPressed = false;
    ui->Preview->setDisabled(false);
    ui->Preview->setText(QStringLiteral("预览"));
    ui->Preview->repaint();
}

void Cut::ShowProjectList(QStandardItemModel *model)
{
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox->addItem(model->item(i,0)->text());
        ui->comboBox_3->addItem(model->item(i, 0)->text());
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
            ui->comboBox_3->setCurrentIndex(i);
            break;
        }

    }
    if (count == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("无可处理数据，请先导入数据！"));
        this->deleteLater();
    }
    QModelIndex pro_index = model->indexFromItem(project);
    ui->comboBox_2->clear();
    ui->comboBox_4->clear();
    for (int i = 0; i < count; i++)
    {
        if(model->data(model->index(i, 1, pro_index)).toString().compare("complex-0.0")==0)
            ui->comboBox_2->addItem(model->data(model->index(i,0,pro_index)).toString());
        if( model->data(model->index(i, 1, pro_index)).toString().compare("complex-0.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("complex-1.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("complex-2.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("complex-3.0") == 0
            )
            ui->comboBox_4->addItem(model->data(model->index(i, 0, pro_index)).toString());
        
    }
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("未检测到可处理数据，请先导入图像数据！"));
        this->deleteLater();
    }
    ui->comboBox_2->setCurrentIndex(0);

}
void Cut::on_comboBox_currentIndexChanged()
{
    if (ui->comboBox->count() != 0)
    {
        //this->save_path = copy->item(ui->comboBox->currentIndex(), 1)->text();
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        this->save_path = copy->item(project->row(), 1)->text();
        QModelIndex pro_index = copy->indexFromItem(project);
        int count = project->rowCount();
        ui->comboBox_2->clear();
        //ui->comboBox_2->setMaxCount(count);
        for (int i = 0; i < count; i++)
        {
            if (copy->data(copy->index(i, 1, pro_index)).toString().compare("complex-0.0") == 0)
                ui->comboBox_2->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
        }
        //ui->comboBox_2->setCurrentIndex(0);
    }

}

void Cut::writeXML()
{
    
}

void Cut::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox->setDisabled(0);
        ui->comboBox_2->setDisabled(0);
        ui->lineEdit->setDisabled(0);
        ui->lon->setDisabled(0);
        ui->lat->setDisabled(0);
        ui->Width->setDisabled(0);
        ui->Height->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
    }
    else
    {
        ui->comboBox->setDisabled(1);
        ui->comboBox_2->setDisabled(1);
        ui->lineEdit->setDisabled(1);
        ui->lon->setDisabled(1);
        ui->lat->setDisabled(1);
        ui->Width->setDisabled(1);
        ui->Height->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
    }


}

void Cut::on_comboBox_2_currentIndexChanged()
{
    if (ui->comboBox_2->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            if (project->child(i,0)->text() == ui->comboBox_2->currentText())
                image_number = project->child(i, 0)->rowCount();
        }
    }
    
}

void Cut::on_comboBox_3_currentIndexChanged()
{
    if (ui->comboBox_3->count() != 0)
    {
        //this->save_path = copy->item(ui->comboBox->currentIndex(), 1)->text();
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        this->save_path = copy->item(project->row(), 1)->text();
        QModelIndex pro_index = copy->indexFromItem(project);
        int count = project->rowCount();
        ui->comboBox_4->clear();
        //ui->comboBox_2->setMaxCount(count);
        for (int i = 0; i < count; i++)
        {
            if (copy->data(copy->index(i, 1, pro_index)).toString().compare("complex-2.0") == 0 ||
                copy->data(copy->index(i, 1, pro_index)).toString().compare("complex-3.0") == 0
                )
                ui->comboBox_4->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
        }
        //ui->comboBox_2->setCurrentIndex(0);
    }
}

void Cut::on_comboBox_4_currentIndexChanged()
{
    if (ui->comboBox_4->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            if (project->child(i, 0)->text() == ui->comboBox_4->currentText())
                image_number = project->child(i, 0)->rowCount();
        }
    }

}

void Cut::on_buttonBox_accepted()
{
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无可处理数据，请先导入数据或更换工程！"));
        return;
    }
    if (ui->lineEdit->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请输入存放裁剪文件的文件夹名称！"));
        return;
    }
    bool bFlag = ui->lineEdit->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请注意文件夹名称应当为数字、字母及下划线的组合！"));
        return;
    }
    if (ui->lon->text().isEmpty() || ui->lat->text().isEmpty() ||
        ui->Height->text().isEmpty() || ui->Width->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请确保输入参数完整！"));
        return;
    }
    ui->lon->text().toDouble(&bFlag);
    bool bFlag2 = ui->lat->text().toDouble(&bFlag2);
    if (bFlag ==false || bFlag2 == false)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("经纬度应为小数！"));
        return;
    }
    ui->Height->text().toDouble(&bFlag);
    ui->Width->text().toDouble(&bFlag2);
    if (bFlag == false || bFlag2 == false)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("高度与宽度应为小数！"));
        return;
    }
    ui->progressBar->setHidden(0);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    Cut_thread = new CutWorker;
    Cut_thread->moveToThread(new QThread(this));
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    QList<double> para;
    para.push_back(ui->lon->text().toDouble());
    para.push_back(ui->lat->text().toDouble());
    para.push_back(ui->Width->text().toDouble());
    para.push_back(ui->Height->text().toDouble());
    connect(this, &Cut::operate, Cut_thread, &CutWorker::Cut, Qt::QueuedConnection);
    connect(Cut_thread, &CutWorker::updateProcess, this, &Cut::updateProcess);
    connect(Cut_thread->thread(), &QThread::finished, Cut_thread, &QObject::deleteLater);
    connect(Cut_thread->thread(), &QThread::finished, Cut_thread->thread(), &QObject::deleteLater);
    connect(Cut_thread, &CutWorker::endProcess, this, &Cut::endProcess);
    connect(this, &QWidget::destroyed, this, &Cut::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Cut::StopThread);
    connect(Cut_thread, &CutWorker::sendModel, this, &Cut::TransitModel);
    Cut_thread->thread()->start();
    ChangeVision(false);
    //connect(thread, &MyThread::endProcess, this, &MainWindow::endProcess);
    emit operate(para, this->save_path, ui->comboBox->currentText(), ui->comboBox_2->currentText(), ui->lineEdit->text(), this->copy);

    
}

void Cut::on_buttonBox_rejected()
{
    this->close();
}

void Cut::on_Preview_pressed()
{
    if (isPreviewPressed) return;
    isPreviewPressed = true;
    ui->Preview->setDisabled(true);
    ui->Preview->setText(QStringLiteral("正在加载预览图..."));
    ui->Preview->repaint();
    QStandardItem* project = copy->findItems(ui->comboBox_3->currentText())[0];
    QString image_name;
    QString image_path;
    QString jpg_path;
    QFileInfo fileinfo;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (project->child(i, 0)->text() == ui->comboBox_4->currentText())
        {
            image_name = project->child(i, 0)->child(0, 0)->text();
            image_path = project->child(i, 0)->child(0, 1)->text();
            fileinfo = QFileInfo(image_path);
            jpg_path = QString("%1%2%3%4").arg(fileinfo.absolutePath()).arg("/").arg(image_name).arg(".jpg");
            break;
        }
    }
    if (QFile::exists(jpg_path))
    {

    }
    else
    {
        Utils util;
        ComplexMat SLC64;
        FormatConversion FC;
        FC.read_slc_from_h5(image_path.toStdString().c_str(), SLC64);
        util.SAR_image_quantify(jpg_path.toStdString().c_str(), /*65*/65, SLC64);
        if (QThread::currentThread()->isInterruptionRequested())
        {
            //emit endProcess();
            QFile::remove(jpg_path.toStdString().c_str());
            return;
        }
        if (SLC64.GetCols() * SLC64.GetRows() > 25e6)
        {
            int down_sample_times = (int)sqrt(floor(double(SLC64.GetCols() * SLC64.GetRows()) / 25e6));
            util.resampling(jpg_path.toStdString().c_str(), jpg_path.toStdString().c_str(), (int)(SLC64.GetRows() / down_sample_times),
                (int)(SLC64.GetCols() / down_sample_times));
        }
    }
    
    Preview_Window* Pre_wnd = new Preview_Window();
    Pre_wnd->View->setPixmap(jpg_path);
    connect(Pre_wnd->View, &Preview::SendPos, this, &Cut::ReceivePos);
    connect(Pre_wnd, &Preview::close, this, &Cut::cancelled);
    connect(Pre_wnd, &Preview::destroyed, this, &Cut::cancelled);
    Pre_wnd->show();
    Pre_wnd->setAttribute(Qt::WA_DeleteOnClose, true);
}

void Cut::on_buttonBox_2_accepted()
{
    QMessageBox::warning(NULL, "Warning!", QStringLiteral("请预览并选择裁剪区域！"));
}

void Cut::on_buttonBox_2_rejected()
{
    this->close();
}
