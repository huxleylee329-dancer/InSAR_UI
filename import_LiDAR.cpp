#include "MainWindow.h"
#include "import_LiDAR.h"
#include "icon_source.h"
#include "qfiledialog.h"
#include <qmessagebox.h>
#include <QThread>

import_LiDAR::import_LiDAR(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::ImportLidar)
{
    ui->setupUi(this);
    import_lidar_thread = NULL;
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);

    // Default configuration for GEDI L2A
    ui->spinBox_rh_percentile->setEnabled(true);
    ui->spinBox_rh_percentile->setValue(100);
    
    // Set default node name
    ui->lineEdit_dst_node->setText("LiDAR_Import");
}

import_LiDAR::~import_LiDAR()
{
    import_lidar_thread = NULL;
    if (copy)
    {
        for (int i = 0; i < ui->comboBox_dst_project->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_dst_project->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_dst_project->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
}

bool import_LiDAR::generate_name(
    QListWidget* imageslist,
    std::vector<QString>& original_nameslist, 
    std::vector<QString>& import_nameslist
)
{
    if (!imageslist) return false;
    import_nameslist.clear();
    original_nameslist.clear();
    for (int i = 0; i < imageslist->count(); i++)
    {
        QString path = imageslist->item(i)->text();
        original_nameslist.push_back(path);
        QFileInfo fileinfo(path);
        import_nameslist.push_back(fileinfo.baseName());
    }
    return true;
}

void import_LiDAR::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox_dst_project->setDisabled(0);
        ui->lineEdit_dst_node->setDisabled(0);
        ui->comboBox_product_type->setDisabled(0);
        ui->spinBox_rh_percentile->setDisabled(ui->comboBox_product_type->currentIndex() == 1);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
        ui->pushButton_add->setDisabled(0);
        ui->pushButton_remove->setDisabled(0);
    }
    else
    {
        ui->comboBox_dst_project->setDisabled(1);
        ui->lineEdit_dst_node->setDisabled(1);
        ui->comboBox_product_type->setDisabled(1);
        ui->spinBox_rh_percentile->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
        ui->pushButton_add->setDisabled(1);
        ui->pushButton_remove->setDisabled(1);
    }
}

void import_LiDAR::updateProcess(int value, QString information)
{
    if (!ui->progressBar->isHidden())
    {
        ui->progressBar->setValue(value);
        ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
        ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
}

void import_LiDAR::endProcess()
{
    if (import_lidar_thread)
    {
        import_lidar_thread->thread()->quit();
        import_lidar_thread->thread()->wait();
    }
    ui->progressBar->hide();
    this->close();
}

void import_LiDAR::errorProcess(QString error_msg)
{
    QMessageBox::warning(NULL, "Error", error_msg);
    if (import_lidar_thread)
    {
        import_lidar_thread->thread()->quit();
        import_lidar_thread->thread()->wait();
        import_lidar_thread = NULL;
    }
    ui->progressBar->hide();
    ChangeVision(true);
}

void import_LiDAR::endThread()
{
    if (import_lidar_thread)
    {
        import_lidar_thread->thread()->quit();
        import_lidar_thread->thread()->wait();
        import_lidar_thread = NULL;
    }
}

void import_LiDAR::StopThread()
{
    if (import_lidar_thread != NULL)
    {
        if (import_lidar_thread->thread()->isRunning())
        {
            import_lidar_thread->thread()->requestInterruption();
            import_lidar_thread->thread()->quit();
            import_lidar_thread->thread()->wait();
        }
        import_lidar_thread = NULL;
    }
}

void import_LiDAR::TransitModel(QStandardItemModel* model)
{
    emit sendCopy(model);
}

void import_LiDAR::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox_dst_project->addItem(model->item(i, 0)->text());
        model->item(i, 0)->setStatusTip(IN_PROCESS);
    }
    ui->comboBox_dst_project->setCurrentIndex(0);
    this->save_path = model->item(0, 1)->text();
}

void import_LiDAR::on_comboBox_dst_project_currentIndexChanged()
{
    QStandardItem* project = this->copy->findItems(ui->comboBox_dst_project->currentText())[0];
    QModelIndex pro_index = this->copy->indexFromItem(project);
    QModelIndex pro_path_index = pro_index.siblingAtColumn(1);
    this->save_path = this->copy->itemFromIndex(pro_path_index)->text();
}

void import_LiDAR::on_pushButton_add_pressed()
{
    QString filename = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("导入LiDAR数据"),
        "",
        "HDF5 files (*.h5);;All files (*.*)"
    );
    if (!filename.isEmpty())
    {
        ui->listWidget->addItem(filename);
    }
    for (int i = 0; i < ui->listWidget->count(); i++)
    {
        if (ui->listWidget->item(i)->text().isEmpty())
        {
            ui->listWidget->takeItem(i);
        }
    }
}

void import_LiDAR::on_pushButton_remove_pressed()
{
    if (ui->listWidget->count() >= 1)
    {
        ui->listWidget->takeItem(ui->listWidget->currentRow());
    }
    for (int i = 0; i < ui->listWidget->count(); i++)
    {
        if (ui->listWidget->item(i)->text().isEmpty())
        {
            ui->listWidget->takeItem(i);
        }
    }
}

void import_LiDAR::on_buttonBox_rejected()
{
    close();
}

void import_LiDAR::on_buttonBox_accepted()
{
    if (ui->listWidget->count() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("导入图像文件为空！"));
        return;
    }
    if (ui->lineEdit_dst_node->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("目标节点名为空！"));
        return;
    }

    QStandardItem* project = this->copy->findItems(ui->comboBox_dst_project->currentText())[0];
    if (!project) {
        return;
    }
    bool same_name_node = false;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (ui->lineEdit_dst_node->text() == project->child(i, 0)->text() && project->child(i, 1)->text() != "complex-0.0")
        {
            same_name_node = true;
        }
    }
    if (same_name_node)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("目标节点已存在，且和导入数据级别不同，请重命名！"));
        return;
    }

    std::vector<QString> original_namelist;
    std::vector<QString> import_namelist;
    if (!generate_name(ui->listWidget, original_namelist, import_namelist))
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("输入数据不合法！"));
        return;
    }

    if (import_lidar_thread) {
        import_lidar_thread->thread()->quit();
        import_lidar_thread->thread()->wait();
    }

    import_lidar_thread = new LidarImportWorker;
    QThread* thread = new QThread(this);
    import_lidar_thread->moveToThread(thread);
    ui->progressBar->setValue(0);
    ui->progressBar->show();

    connect(this, &import_LiDAR::operate2, import_lidar_thread, &LidarImportWorker::import_Lidar_patch, Qt::QueuedConnection);
    connect(import_lidar_thread, &LidarImportWorker::updateProcess, this, &import_LiDAR::updateProcess);
    connect(thread, &QThread::finished, import_lidar_thread, &LidarImportWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    connect(import_lidar_thread, &LidarImportWorker::endProcess, this, &import_LiDAR::endProcess);
    connect(import_lidar_thread, &LidarImportWorker::errorProcess, this, &import_LiDAR::errorProcess);
    connect(this, &QWidget::destroyed, this, &import_LiDAR::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &import_LiDAR::StopThread);
    connect(import_lidar_thread, &LidarImportWorker::sendModel, this, &import_LiDAR::TransitModel);

    thread->start();

    emit operate2(
        this->save_path,
        original_namelist,
        import_namelist,
        ui->comboBox_product_type->currentText(),
        ui->spinBox_rh_percentile->value(),
        ui->lineEdit_dst_node->text(),
        ui->comboBox_dst_project->currentText(),
        this->copy
    );

    ChangeVision(false);
}

void import_LiDAR::on_comboBox_product_type_currentIndexChanged(int index)
{
    if (index == 0) // GEDI L2A
    {
        ui->spinBox_rh_percentile->setEnabled(true);
        ui->spinBox_rh_percentile->setValue(100);
    }
    else if (index == 1) // GEDI L2B
    {
        ui->spinBox_rh_percentile->setEnabled(false);
    }
    else if (index == 2) // ICESat-2 L3A
    {
        ui->spinBox_rh_percentile->setEnabled(true);
        ui->spinBox_rh_percentile->setValue(18);
    }
}
