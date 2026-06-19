#include"Deformation_Average.h"
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
Deformation_Average::Deformation_Average(QWidget* parent) :
    QWidget(parent), h5_left(-1), h5_right(-1), h5_top(-1), h5_bottom(-1),
    ui(new Ui::DeformationAverage)
{
    ui->setupUi(this);
    //this->DOC = new XMLFile;
    isPreviewPressed = false;
    isDeformation_Averageting = false;
    if (copy != NULL)
    {
        copy = NULL;
    }

}
Deformation_Average::~Deformation_Average()
{
    /*改变工程文件的处理状态为NOT_IN_PROCESS*/
    if (copy)
    {
        for (int i = 0; i < ui->comboBox->count(); i++)
        {
            if (!copy->findItems(ui->comboBox->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
}
void Deformation_Average::cancelled()
{
    isPreviewPressed = false;
    ui->Preview->setDisabled(false);
    ui->Preview->setText(QStringLiteral("查看"));
    ui->Preview->repaint();
}


void Deformation_Average::ShowProjectList(QStandardItemModel* model)
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
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("无可处理数据，请先导入数据！"));
        this->deleteLater();
    }
    QModelIndex pro_index = model->indexFromItem(project);
    ui->comboBox_2->clear();
    for (int i = 0; i < count; i++)
    {
        if (model->data(model->index(i, 1, pro_index)).toString().compare("SBAS-1.0") == 0)
            ui->comboBox_2->addItem(model->data(model->index(i, 0, pro_index)).toString());

    }
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("未检测到可处理数据，请先导入图像数据！"));
        this->deleteLater();
    }
    ui->comboBox_2->setCurrentIndex(0);

}



void Deformation_Average::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox->setDisabled(0);
        ui->comboBox_2->setDisabled(0);
    }
    else
    {
        ui->comboBox->setDisabled(1);
        ui->comboBox_2->setDisabled(1);
    }


}

void Deformation_Average::on_comboBox_currentIndexChanged()
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
            if (copy->data(copy->index(i, 1, pro_index)).toString().compare("SBAS-1.0") == 0 )
                ui->comboBox_2->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
        }
        //ui->comboBox_2->setCurrentIndex(0);
    }
}

void Deformation_Average::on_comboBox_2_currentIndexChanged()
{
    if (ui->comboBox_2->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            if (project->child(i, 0)->text() == ui->comboBox_2->currentText())
                image_number = project->child(i, 0)->rowCount();
        }
    }

}



void Deformation_Average::on_Preview_pressed()
{
    if (isPreviewPressed) return;
    isPreviewPressed = true;
    ui->Preview->setDisabled(true);
    ui->Preview->setText(QStringLiteral("正在查看..."));
    ui->Preview->repaint();
    QStandardItem* project = copy->findItems(ui->comboBox->currentText())[0];
    QString image_name;
    QString image_path;
    QString jpg_path;
    QFileInfo fileinfo;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (project->child(i, 0)->text() == ui->comboBox_2->currentText())
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
        if (defomation_velocity.type() != CV_64F)
        {
            defomation_velocity.convertTo(defomation_velocity, CV_64F);
        }
        util.savephase_white(jpg_path.toStdString().c_str(), "jet", defomation_velocity, mask);
        if (QThread::currentThread()->isInterruptionRequested())
        {
            //emit endProcess();
            QFile::remove(jpg_path.toStdString().c_str());
            return;
        }
    }
    Deformation_Preview_Window* Pre_wnd = new Deformation_Preview_Window();
    Pre_wnd->View->setPixmap(jpg_path);
    Pre_wnd->View->SetH5Path(image_path);
    connect(Pre_wnd, &Deformation_Preview_Window::close, this, &Deformation_Average::cancelled);
    connect(Pre_wnd, &Deformation_Preview_Window::destroyed, this, &Deformation_Average::cancelled);
    Pre_wnd->show();
    Pre_wnd->setAttribute(Qt::WA_DeleteOnClose, true);
}

