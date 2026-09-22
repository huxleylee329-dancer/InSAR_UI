#include"SBAS_reference_reselection.h"
#include"SBASReferenceReselectionWorker.h"
#include"ui_SbasReferenceReselection.h"
#include"icon_source.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<qmessagebox.h>
#include<Utils.h>
#include "NodeUtils.h"
#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#endif
#include<FormatConversion.h>

SBAS_reference_reselection::SBAS_reference_reselection(QWidget* parent) :
    QWidget(parent), h5_left(-1), h5_right(-1), h5_top(-1), h5_bottom(-1),
    ui(new Ui::SbasReferenceReselection)
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
            if (QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox_project->itemText(i)))
                project->setStatusTip(NOT_IN_PROCESS);
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
    ui->reselection->setText(QStringLiteral("参考点重选: row = %1  col = %2 GCPs: %3").arg(this->ref_row).arg(this->ref_col).arg(this->plist.size()));
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
    ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
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
    ui->comboBox_project->clear();
    ui->comboBox_srcNode->clear();
    if (!model || model->rowCount() < 1 || model->columnCount() < 2 ||
        !model->item(0, 0) || !model->item(0, 1))
    {
        this->save_path.clear();
        return;
    }
    for (int i = 0; i < model->rowCount(); i++)
    {
        QStandardItem* projectItem = model->item(i, 0);
        if (!projectItem) continue;
        ui->comboBox_project->addItem(projectItem->text());
        projectItem->setStatusTip(IN_PROCESS);
    }
    this->save_path = copy->item(0, 1)->text();
    QStandardItem* project = NULL;
    int count = 0;
    for (int i = 0; i < model->rowCount(); i++)
    {
        QStandardItem* projectItem = model->item(i, 0);
        if (projectItem && projectItem->rowCount() != 0)
        {
            count = projectItem->rowCount();
            project = projectItem;
            ui->comboBox_project->setCurrentIndex(i);
            break;
        }

    }
    if (count == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("无可处理数据，请先导入数据！"));
        this->deleteLater();
        return;
    }
    QModelIndex pro_index = model->indexFromItem(project);
    for (int i = 0; i < count; i++)
    {
        if (model->data(model->index(i, 1, pro_index)).toString().compare("SBAS-1.0") == 0)
            ui->comboBox_srcNode->addItem(model->data(model->index(i, 0, pro_index)).toString());

    }
    if (ui->comboBox_srcNode->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("未检测到可处理数据，请先导入图像数据！"));
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
        QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox_project->currentText());
        if (!project) {
            ui->comboBox_srcNode->clear();
            QMessageBox::warning(this, "Warning!", QStringLiteral("未找到当前工程，请刷新工程列表后重试。"));
            return;
        }
        QStandardItem* pathItem = copy->item(project->row(), 1);
        this->save_path = pathItem ? pathItem->text() : QString();
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
        QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox_project->currentText());
        if (!project) {
            image_number = 0;
            QMessageBox::warning(this, "Warning!", QStringLiteral("未找到当前工程，请刷新工程列表后重试。"));
            return;
        }
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            QStandardItem* childItem = project->child(i, 0);
            if (childItem && childItem->text() == ui->comboBox_srcNode->currentText())
                image_number = childItem->rowCount();
        }
    }

}



void SBAS_reference_reselection::on_reselection_pressed()
{
    if (isReselectionPressed) return;
    QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox_project->currentText());
    if (!project) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("未找到当前工程，请刷新工程列表后重试。"));
        return;
    }
    isReselectionPressed = true;
    ui->reselection->setDisabled(true);
    ui->reselection->setText(QStringLiteral("正在重选参考点..."));
    ui->reselection->repaint();
    QString image_name;
    QString image_path;
    QString jpg_path;
    QFileInfo fileinfo;
    for (int i = 0; i < project->rowCount(); i++)
    {
        QStandardItem* nodeItem = project->child(i, 0);
        if (nodeItem && nodeItem->text() == ui->comboBox_srcNode->currentText())
        {
            QStandardItem* nameItem = nodeItem->child(0, 0);
            QStandardItem* pathItem = nodeItem->child(0, 1);
            if (!nameItem || !pathItem)
                break;
            image_name = nameItem->text();
            image_path = pathItem->text();
            fileinfo = QFileInfo(image_path);
            jpg_path = QString("%1%2%3%4").arg(fileinfo.absolutePath()).arg("/").arg(image_name).arg(".jpg");
            break;
        }
    }
    if (image_name.isEmpty() || image_path.isEmpty())
    {
        QMessageBox::warning(this, "Warning!", QStringLiteral("所选节点的图像数据不完整，无法重选参考点。"));
        cancelled();
        return;
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
        NodeUtils::readMatFromH5(image_path, "defomation_velocity", defomation_velocity, CV_64F);
        NodeUtils::readMatFromH5(image_path, "mask", mask);
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
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无可处理数据，请先进行配准或更换工程！"));
        return;
    }
    if (ui->comboBox_srcNode->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该节点下无可处理数据，请更换节点！"));
        return;
    }

    QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox_project->currentText());
    if (!project) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("未找到当前工程，请刷新工程列表后重试。"));
        return;
    }
    QStandardItem* image = nullptr;
    for (int i = 0; i < project->rowCount(); i++)
    {
        QStandardItem* childItem = project->child(i, 0);
        if (childItem && childItem->text() == ui->comboBox_srcNode->currentText())
        {
            image = childItem; break;
        }
    }
    if (!image) {
        QMessageBox::warning(this, "Warning!", QStringLiteral("所选数据节点不存在，请重新选择。"));
        return;
    }
    Q_UNUSED(image);
    QMessageBox::warning(this, QStringLiteral("不兼容的旧入口"),
                         QStringLiteral("旧版 SBAS 参考点重选不具备 provenance 重建契约。请使用 SBAS Reference Reselection 节点，并先重新运行 SBAS Time Series。"));
}

void SBAS_reference_reselection::on_buttonBox_rejected()
{
    this->close();
}

