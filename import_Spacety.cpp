#include "MainWindow.h"
#include "import_Spacety.h"
#include "ImportTask.h"
#include "ImportOutputPersistence.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "qfiledialog.h"
#include <qmessagebox.h>
#include <QThread>

import_Spacety::import_Spacety(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::ImportSpacety)
{
    ui->setupUi(this);
    import_Spacety_thread = NULL;
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &import_Spacety::on_buttonBox_accepted);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &import_Spacety::on_buttonBox_rejected);
}

import_Spacety::~import_Spacety()
{
    import_Spacety_thread = NULL;
    if (copy)
    {
        for (int i = 0; i < ui->comboBox_dst_project->count(); i++)
        {
            if (QStandardItem* project = NodeUtils::findFirstModelItem(
                    copy, ui->comboBox_dst_project->itemText(i)))
            {
                project->setStatusTip(NOT_IN_PROCESS);
            }
        }
    }
}

bool import_Spacety::generate_name(QListWidget* imageslist, std::vector<QString>& data_file_list, std::vector<QString>& xml_file_list, std::vector<QString>& import_nameslist)
{
    if (!imageslist) return false;
    import_nameslist.clear();
    data_file_list.clear();
    xml_file_list.clear();
    for (int i = 0; i < imageslist->count(); i++)
    {
        QListWidgetItem* item = imageslist->item(i);
        QString dataFile = item->data(Qt::UserRole).toString();
        QString xmlFile = item->data(Qt::UserRole + 1).toString();
        data_file_list.push_back(dataFile);
        xml_file_list.push_back(xmlFile);
        QFileInfo fileinfo = QFileInfo(dataFile);
        import_nameslist.push_back(fileinfo.baseName());
    }
    return true;
}

void import_Spacety::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox_dst_project->setDisabled(0);
        ui->lineEdit_dst_node->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
        ui->pushButton_add->setDisabled(0);
        ui->pushButton_remove->setDisabled(0);
        ui->pushButton_browse_data->setDisabled(0);
        ui->pushButton_browse_xml->setDisabled(0);
        ui->lineEdit_data_file->setDisabled(0);
        ui->lineEdit_xml_file->setDisabled(0);
        ui->checkBox_spotlight->setDisabled(0);
    }
    else
    {
        ui->comboBox_dst_project->setDisabled(1);
        ui->lineEdit_dst_node->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
        ui->pushButton_add->setDisabled(1);
        ui->pushButton_remove->setDisabled(1);
        ui->pushButton_browse_data->setDisabled(1);
        ui->pushButton_browse_xml->setDisabled(1);
        ui->lineEdit_data_file->setDisabled(1);
        ui->lineEdit_xml_file->setDisabled(1);
        ui->checkBox_spotlight->setDisabled(1);
    }
}

void import_Spacety::updateProcess(int value, QString information)
{
    if (!ui->progressBar->isHidden())
    {
        ui->progressBar->setValue(value);
        ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
        ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
}

void import_Spacety::endProcess()
{
    if (import_Spacety_thread)
    {
        import_Spacety_thread->thread()->quit();
        import_Spacety_thread->thread()->wait();
    }
    ui->progressBar->hide();
    this->close();
}

void import_Spacety::errorProcess(QString error_msg)
{
    QMessageBox::warning(NULL, "Error", error_msg);
    if (import_Spacety_thread)
    {
        import_Spacety_thread->thread()->quit();
        import_Spacety_thread->thread()->wait();
        import_Spacety_thread = NULL;
    }
    ui->progressBar->hide();
    ChangeVision(true);
}

void import_Spacety::endThread()
{
    if (import_Spacety_thread)
    {
        import_Spacety_thread->thread()->quit();
        import_Spacety_thread->thread()->wait();
        import_Spacety_thread = NULL;
    }
}

void import_Spacety::StopThread()
{
    if (import_Spacety_thread != NULL)
    {
        if (import_Spacety_thread->thread()->isRunning())
        {
            import_Spacety_thread->thread()->requestInterruption();
            import_Spacety_thread->thread()->quit();
            import_Spacety_thread->thread()->wait();
        }
        import_Spacety_thread = NULL;
    }
}

void import_Spacety::TransitModel(QStandardItemModel* model)
{
    emit sendCopy(model);
}

void import_Spacety::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    ui->comboBox_dst_project->clear();
    this->save_path.clear();
    if (!model || model->rowCount() < 1 || model->columnCount() < 2) return;

    QStandardItem* firstProject = model->item(0, 0);
    QStandardItem* firstPath = model->item(0, 1);
    if (!firstProject || !firstPath) return;

    for (int i = 0; i < model->rowCount(); i++)
    {
        QStandardItem* project = model->item(i, 0);
        QStandardItem* path = model->item(i, 1);
        if (!project || !path) continue;
        ui->comboBox_dst_project->addItem(project->text());
        project->setStatusTip(IN_PROCESS);
    }
    if (ui->comboBox_dst_project->count() < 1) return;
    ui->comboBox_dst_project->setCurrentIndex(0);
    this->save_path = firstPath->text();
}

void import_Spacety::on_comboBox_dst_project_currentIndexChanged()
{
    QStandardItem* project = NodeUtils::findFirstModelItem(
        this->copy, ui->comboBox_dst_project->currentText());
    if (!project) return;
    QModelIndex pro_index = this->copy->indexFromItem(project);
    QModelIndex pro_path_index = pro_index.siblingAtColumn(1);
    this->save_path = this->copy->itemFromIndex(pro_path_index)->text();
}

void import_Spacety::on_pushButton_browse_data_pressed()
{
    QString filename = QFileDialog::getOpenFileName(this,
        QStringLiteral("选择数据文件"),
        "",
        "Data Files (*.tiff *.h5);;All Files (*)");
    if (!filename.isEmpty())
    {
        ui->lineEdit_data_file->setText(filename);
    }
}

void import_Spacety::on_pushButton_browse_xml_pressed()
{
    QString filename = QFileDialog::getOpenFileName(this,
        QStringLiteral("选择参数XML文件"),
        "",
        "XML Files (*.xml);;All Files (*)");
    if (!filename.isEmpty())
    {
        ui->lineEdit_xml_file->setText(filename);
    }
}

void import_Spacety::on_pushButton_add_pressed()
{
    QString dataFile = ui->lineEdit_data_file->text().trimmed();
    QString xmlFile = ui->lineEdit_xml_file->text().trimmed();
    if (dataFile.isEmpty())
    {
        QMessageBox::warning(this, "Warning", QStringLiteral("请选择数据文件！"));
        return;
    }
    if (xmlFile.isEmpty())
    {
        QMessageBox::warning(this, "Warning", QStringLiteral("请选择参数XML文件！"));
        return;
    }
    
    QListWidgetItem* item = new QListWidgetItem(QFileInfo(dataFile).fileName() + " | " + QFileInfo(xmlFile).fileName());
    item->setData(Qt::UserRole, dataFile);
    item->setData(Qt::UserRole + 1, xmlFile);
    ui->listWidget->addItem(item);
    
    ui->lineEdit_data_file->clear();
    ui->lineEdit_xml_file->clear();
}

void import_Spacety::on_pushButton_remove_pressed()
{
    if (ui->listWidget->count() >= 1)
    {
        ui->listWidget->takeItem(ui->listWidget->currentRow());
    }
}

void import_Spacety::on_buttonBox_rejected()
{
    close();
}

void import_Spacety::on_buttonBox_accepted()
{
    if (ui->listWidget->count() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("导入图像列表为空！"));
        return;
    }
    if (ui->lineEdit_dst_node->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("目标节点名为空！"));
        return;
    }

    QStandardItem* project = NodeUtils::findFirstModelItem(
        this->copy, ui->comboBox_dst_project->currentText());
    if (!project) {
        QMessageBox::warning(this, QStringLiteral("目标工程不可用"),
            QStringLiteral("所选目标工程不存在或已被关闭，请重新选择工程。"));
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

    std::vector<QString> data_file_list;
    std::vector<QString> xml_file_list;
    std::vector<QString> import_namelist;
    if (!generate_name(ui->listWidget, data_file_list, xml_file_list, import_namelist)) return;

    if (import_Spacety_thread) {
        import_Spacety_thread->thread()->quit();
        import_Spacety_thread->thread()->wait();
    }

    import_Spacety_thread = new SpacetyImportWorker;
    QThread* thread = new QThread(this);
    import_Spacety_thread->moveToThread(thread);
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(import_Spacety_thread, &SpacetyImportWorker::updateProcess, this, &import_Spacety::updateProcess);
    connect(thread, &QThread::finished, import_Spacety_thread, &SpacetyImportWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    connect(import_Spacety_thread, &SpacetyImportWorker::endProcess, this, &import_Spacety::endProcess);
    connect(import_Spacety_thread, &SpacetyImportWorker::errorProcess, this, &import_Spacety::errorProcess);
    connect(this, &QWidget::destroyed, this, &import_Spacety::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &import_Spacety::StopThread);
    connect(import_Spacety_thread, &SpacetyImportWorker::outputsGenerated, this,
        [this, projectName = ui->comboBox_dst_project->currentText(), savePath = save_path](const QString& dstNode, const QStringList& names, const QStringList& paths, const QString& dataType, const QString& format) {
            if (ImportOutputPersistence::persist(copy, projectName, savePath, dstNode, names, paths, dataType, format)) TransitModel(copy);
        });
    thread->start();

    // 构造 ImportTask 列表
    std::vector<ImportTask> tasks;
    for (size_t i = 0; i < data_file_list.size(); ++i) {
        ImportTask task;
        task.filename = import_namelist[i];
        task.arguments = QStringList{ data_file_list[i], xml_file_list[i], ui->checkBox_spotlight->isChecked() ? "1" : "0" };
        tasks.push_back(task);
    }

    QMetaObject::invokeMethod(import_Spacety_thread, "import_patch",
        Q_ARG(QString, this->save_path),
        Q_ARG(std::vector<ImportTask>, tasks),
        Q_ARG(QString, ui->lineEdit_dst_node->text()));
    ChangeVision(false);
}
