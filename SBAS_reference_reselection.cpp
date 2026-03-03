#include"SBAS_reference_reselection.h"
#include"ui_SBAS_reference_reselection.h"
#include"icon_source.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<qmessagebox.h>
#include<Utils.h>
#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#endif
#include<FormatConversion.h>

SBAS_reference_reselection::SBAS_reference_reselection(QWidget* parent) :
    QWidget(parent), h5_left(-1), h5_right(-1), h5_top(-1), h5_bottom(-1),
    ui(new Ui::SBAS_reference_reselection)
{
    ui->setupUi(this);
    //this->DOC = new XMLFile;
    isReselectionPressed = false;
    isSBAS_reference_reselection = false;
    ref_row = 0;
    ref_col = 0;
    plist.clear();
    if (copy != NULL)
    {
        copy = NULL;
    }
    ui->progressBar->hide();
}
SBAS_reference_reselection::~SBAS_reference_reselection()
{
    SBAS_reference_reselection_thread = NULL;
    /*改变工程文件的处理状态为NOT_IN_PROCESS*/
    if (copy)
    {
        for (int i = 0; i < ui->comboBox_project->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_project->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_project->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
}
void SBAS_reference_reselection::TransitModel(QStandardItemModel* model)
{
    emit sendCopy(model);
}

void SBAS_reference_reselection::cancelled()
{
    isReselectionPressed = false;
    ui->reselection->setDisabled(false);
    ui->reselection->setText(QString::fromLocal8Bit("参考点重选: row = %1  col = %2 GCPs: %3").arg(this->ref_row).arg(this->ref_col).arg(this->plist.size()));
    ui->reselection->repaint();
}

void SBAS_reference_reselection::receive_coordinate(int ref_row, int ref_col, QList<QPoint> plist)
{
    this->ref_row = ref_row;
    this->ref_col = ref_col;
    for (int i = 0; i < plist.size(); i++)
    {
        this->plist.push_back(plist[i]);
    }
}

void SBAS_reference_reselection::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QString::fromLocal8Bit("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

void SBAS_reference_reselection::endProcess()
{
    SBAS_reference_reselection_thread->thread()->quit();
    SBAS_reference_reselection_thread->thread()->wait();
    ui->progressBar->hide();
    this->close();
}

void SBAS_reference_reselection::endThread()
{
    SBAS_reference_reselection_thread->thread()->quit();
    SBAS_reference_reselection_thread->thread()->wait();
}

void SBAS_reference_reselection::StopThread()
{
    if (SBAS_reference_reselection_thread != NULL)
        if (SBAS_reference_reselection_thread->thread()->isRunning())
        {
            SBAS_reference_reselection_thread->thread()->requestInterruption();
            SBAS_reference_reselection_thread->thread()->quit();
            SBAS_reference_reselection_thread->thread()->wait();
        }
}


void SBAS_reference_reselection::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox_project->addItem(model->item(i, 0)->text());
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
            ui->comboBox_project->setCurrentIndex(i);
            break;
        }

    }
    if (count == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("无可处理数据，请先导入数据！"));
        this->deleteLater();
    }
    QModelIndex pro_index = model->indexFromItem(project);
    ui->comboBox_srcNode->clear();
    for (int i = 0; i < count; i++)
    {
        if (model->data(model->index(i, 1, pro_index)).toString().compare("SBAS-1.0") == 0)
            ui->comboBox_srcNode->addItem(model->data(model->index(i, 0, pro_index)).toString());

    }
    if (ui->comboBox_srcNode->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("未检测到可处理数据，请先导入图像数据！"));
        this->deleteLater();
    }
    ui->comboBox_srcNode->setCurrentIndex(0);

}



void SBAS_reference_reselection::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox_project->setDisabled(0);
        ui->comboBox_srcNode->setDisabled(0);
        ui->reselection->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
    }
    else
    {
        ui->comboBox_project->setDisabled(1);
        ui->comboBox_srcNode->setDisabled(1);
        ui->reselection->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
    }


}

void SBAS_reference_reselection::on_comboBox_project_currentIndexChanged()
{
    if (ui->comboBox_project->count() != 0)
    {
        //this->save_path = copy->item(ui->comboBox->currentIndex(), 1)->text();
        QStandardItem* project = copy->findItems(ui->comboBox_project->currentText())[0];
        this->save_path = copy->item(project->row(), 1)->text();
        QModelIndex pro_index = copy->indexFromItem(project);
        int count = project->rowCount();
        ui->comboBox_srcNode->clear();
        //ui->comboBox_2->setMaxCount(count);
        for (int i = 0; i < count; i++)
        {
            if (copy->data(copy->index(i, 1, pro_index)).toString().compare("SBAS-1.0") == 0)
                ui->comboBox_srcNode->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
        }
        //ui->comboBox_2->setCurrentIndex(0);
    }
}

void SBAS_reference_reselection::on_comboBox_srcNode_currentIndexChanged()
{
    if (ui->comboBox_srcNode->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox_project->currentText())[0];
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            if (project->child(i, 0)->text() == ui->comboBox_srcNode->currentText())
                image_number = project->child(i, 0)->rowCount();
        }
    }

}



void SBAS_reference_reselection::on_reselection_pressed()
{
    if (isReselectionPressed) return;
    isReselectionPressed = true;
    ui->reselection->setDisabled(true);
    ui->reselection->setText(QString::fromLocal8Bit("正在重选参考点..."));
    ui->reselection->repaint();
    QStandardItem* project = copy->findItems(ui->comboBox_project->currentText())[0];
    QString image_name;
    QString image_path;
    QString jpg_path;
    QFileInfo fileinfo;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (project->child(i, 0)->text() == ui->comboBox_srcNode->currentText())
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
        Mat defomation_velocity, mask;
        Mat image;
        int ret = FC.read_array_from_h5(image_path.toStdString().c_str(), "defomation_velocity", defomation_velocity);
        ret = FC.read_array_from_h5(image_path.toStdString().c_str(), "mask", mask);
        util.savephase_white(jpg_path.toStdString().c_str(), "jet", defomation_velocity, mask);
        if (QThread::currentThread()->isInterruptionRequested())
        {
            //emit endProcess();
            QFile::remove(jpg_path.toStdString().c_str());
            return;
        }
    }
    reselection_view_Window* Pre_wnd = new reselection_view_Window();
    Pre_wnd->View->setPixmap(jpg_path);
    Pre_wnd->View->SetH5Path(image_path);
    connect(Pre_wnd, &reselection_view_Window::close, this, &SBAS_reference_reselection::cancelled);
    connect(Pre_wnd, &reselection_view_Window::destroyed, this, &SBAS_reference_reselection::cancelled);
    connect(Pre_wnd, &reselection_view_Window::send_coordinate, this, &SBAS_reference_reselection::receive_coordinate);
    Pre_wnd->show();
    Pre_wnd->setAttribute(Qt::WA_DeleteOnClose, true);
}

void SBAS_reference_reselection::on_buttonBox_accepted()
{
    if (ui->comboBox_project->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程无可处理数据，请先进行配准或更换工程！"));
        return;
    }
    if (ui->comboBox_srcNode->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该节点下无可处理数据，请更换节点！"));
        return;
    }

    SBAS_reference_reselection_thread = new MyThread;
    SBAS_reference_reselection_thread->moveToThread(new QThread(this));
    ui->progressBar->setValue(0);
    ui->progressBar->show();

    connect(this, &SBAS_reference_reselection::operate, SBAS_reference_reselection_thread, &MyThread::SBAS_reference_reselection, Qt::QueuedConnection);
    connect(SBAS_reference_reselection_thread, &MyThread::updateProcess, this, &SBAS_reference_reselection::updateProcess);
    connect(SBAS_reference_reselection_thread->thread(), &QThread::finished, SBAS_reference_reselection_thread, &MyThread::deleteLater);
    connect(SBAS_reference_reselection_thread, &MyThread::endProcess, this, &SBAS_reference_reselection::endProcess);
    connect(this, &QWidget::destroyed, this, &SBAS_reference_reselection::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &SBAS_reference_reselection::StopThread);// , Qt::QueuedConnection);
    connect(SBAS_reference_reselection_thread, &MyThread::sendModel, this, &SBAS_reference_reselection::TransitModel);
    SBAS_reference_reselection_thread->thread()->start();
    ChangeVision(false);
    emit operate(ui->comboBox_project->currentText(), ui->comboBox_srcNode->currentText(), ref_row, ref_col, plist, this->copy);
}

void SBAS_reference_reselection::on_buttonBox_rejected()
{
    this->close();
}

