#include "MainWindow.h"
#include "import_AIRSAT.h"
#include "ImportTask.h"
#include "ImportOutputPersistence.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "qfiledialog.h"
#include <opencv2/highgui.hpp>
#include <qmessagebox.h>
#include <QThread>

import_AIRSAT::import_AIRSAT(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::ImportAirsat)
{
    ui->setupUi(this);
    import_AIRSAT_thread = NULL;
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
    ui->lineEdit_dst_node->setText("AIRSAT_Import");
}

import_AIRSAT::~import_AIRSAT()
{
    import_AIRSAT_thread = NULL;
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

bool import_AIRSAT::generate_name(
    QListWidget* imageslist,
    std::vector<QString>& data_files,
    std::vector<QString>& xml_files,
    std::vector<QString>& import_names
)
{
    if (!imageslist) return false;
    data_files.clear();
    xml_files.clear();
    import_names.clear();
    for (int i = 0; i < imageslist->count(); i++)
    {
        QString text = imageslist->item(i)->text();
        QStringList parts = text.split(" | ");
        if (parts.size() != 2)
            continue;
        QString data_path = parts[0].trimmed();
        QString xml_path = parts[1].trimmed();
        data_files.push_back(data_path);
        xml_files.push_back(xml_path);
        
        QFileInfo fileinfo(data_path);
        import_names.push_back(fileinfo.baseName());
    }
    return !data_files.empty();
}

void import_AIRSAT::ChangeVision(bool Editable)
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
    }
}

void import_AIRSAT::updateProcess(int value, QString information)
{
    if (!ui->progressBar->isHidden())
    {
        ui->progressBar->setValue(value);
        ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
        ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
}

void import_AIRSAT::endProcess()
{
    if (import_AIRSAT_thread)
    {
        import_AIRSAT_thread->thread()->quit();
        import_AIRSAT_thread->thread()->wait();
    }
    ui->progressBar->hide();
    this->close();
}

void import_AIRSAT::errorProcess(QString error_msg)
{
    QMessageBox::warning(NULL, "Error", error_msg);
    if (import_AIRSAT_thread)
    {
        import_AIRSAT_thread->thread()->quit();
        import_AIRSAT_thread->thread()->wait();
        import_AIRSAT_thread = NULL;
    }
    ui->progressBar->hide();
    ChangeVision(true);
}

void import_AIRSAT::endThread()
{
    if (import_AIRSAT_thread)
    {
        import_AIRSAT_thread->thread()->quit();
        import_AIRSAT_thread->thread()->wait();
        import_AIRSAT_thread = NULL;
    }
}

void import_AIRSAT::StopThread()
{
    if (import_AIRSAT_thread != NULL)
    {
        if (import_AIRSAT_thread->thread()->isRunning())
        {
            import_AIRSAT_thread->thread()->requestInterruption();
            import_AIRSAT_thread->thread()->quit();
            import_AIRSAT_thread->thread()->wait();
        }
        import_AIRSAT_thread = NULL;
    }
}

void import_AIRSAT::TransitModel(QStandardItemModel* model)
{
    emit sendCopy(model);
}

void import_AIRSAT::ShowProjectList(QStandardItemModel* model)
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

void import_AIRSAT::on_comboBox_dst_project_currentIndexChanged()
{
    if (!this->copy || ui->comboBox_dst_project->currentIndex() < 0) return;
    QStandardItem* project = NodeUtils::findFirstModelItem(
        this->copy, ui->comboBox_dst_project->currentText());
    if (!project) return;
    QModelIndex pro_index = this->copy->indexFromItem(project);
    QModelIndex pro_path_index = pro_index.siblingAtColumn(1);
    this->save_path = this->copy->itemFromIndex(pro_path_index)->text();
}

void import_AIRSAT::on_pushButton_browse_data_clicked()
{
    QString filename = QFileDialog::getOpenFileName(this,
        QStringLiteral("选择数据文件"),
        "",
        "Data Files (*.tiff *.tif *.h5)");
    if (!filename.isEmpty())
    {
        ui->lineEdit_data_file->setText(filename);
    }
}

void import_AIRSAT::on_pushButton_browse_xml_clicked()
{
    QString filename = QFileDialog::getOpenFileName(this,
        QStringLiteral("选择参数 XML 文件"),
        "",
        "XML Files (*.xml)");
    if (!filename.isEmpty())
    {
        ui->lineEdit_xml_file->setText(filename);
    }
}

void import_AIRSAT::on_pushButton_add_pressed()
{
    QString data_file = ui->lineEdit_data_file->text().trimmed();
    QString xml_file = ui->lineEdit_xml_file->text().trimmed();
    if (data_file.isEmpty() || xml_file.isEmpty())
    {
        QMessageBox::warning(this, "Warning", QStringLiteral("数据文件和XML文件路径都不能为空！"));
        return;
    }
    if (!QFileInfo::exists(data_file))
    {
        QMessageBox::warning(this, "Warning", QStringLiteral("数据文件不存在！"));
        return;
    }
    if (!QFileInfo::exists(xml_file))
    {
        QMessageBox::warning(this, "Warning", QStringLiteral("XML文件不存在！"));
        return;
    }
    
    QString itemText = data_file + " | " + xml_file;
    for (int i = 0; i < ui->listWidget->count(); ++i)
    {
        if (ui->listWidget->item(i)->text() == itemText)
        {
            QMessageBox::warning(this, "Warning", QStringLiteral("该文件对已在列表中！"));
            return;
        }
    }
    ui->listWidget->addItem(itemText);
    ui->lineEdit_data_file->clear();
    ui->lineEdit_xml_file->clear();
}

void import_AIRSAT::on_pushButton_remove_pressed()
{
    if (ui->listWidget->count() >= 1)
    {
        ui->listWidget->takeItem(ui->listWidget->currentRow());
    }
}

void import_AIRSAT::on_buttonBox_rejected()
{
    close();
}

void import_AIRSAT::on_buttonBox_accepted()
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

    if (import_AIRSAT_thread) {
        import_AIRSAT_thread->thread()->quit();
        import_AIRSAT_thread->thread()->wait();
    }

    import_AIRSAT_thread = new AIRSATImportWorker;
    QThread* thread = new QThread(this);
    import_AIRSAT_thread->moveToThread(thread);
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(import_AIRSAT_thread, &AIRSATImportWorker::updateProcess, this, &import_AIRSAT::updateProcess);
    connect(thread, &QThread::finished, import_AIRSAT_thread, &AIRSATImportWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    connect(import_AIRSAT_thread, &AIRSATImportWorker::endProcess, this, &import_AIRSAT::endProcess);
    connect(import_AIRSAT_thread, &AIRSATImportWorker::errorProcess, this, &import_AIRSAT::errorProcess);
    connect(this, &QWidget::destroyed, this, &import_AIRSAT::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &import_AIRSAT::StopThread);
    connect(import_AIRSAT_thread, &AIRSATImportWorker::outputsGenerated, this,
        [this, projectName = ui->comboBox_dst_project->currentText(), savePath = save_path](const QString& dstNode, const QStringList& names, const QStringList& paths, const QString& dataType, const QString& format) {
            if (ImportOutputPersistence::persist(copy, projectName, savePath, dstNode, names, paths, dataType, format)) TransitModel(copy);
        });
    thread->start();

    // 构造 ImportTask 列表
    std::vector<ImportTask> tasks;
    for (size_t i = 0; i < data_file_list.size(); ++i) {
        ImportTask task;
        task.filename = import_namelist[i];
        task.arguments = QStringList{ data_file_list[i], xml_file_list[i] };
        tasks.push_back(task);
    }

    QMetaObject::invokeMethod(import_AIRSAT_thread, "import_patch",
        Q_ARG(QString, this->save_path),
        Q_ARG(std::vector<ImportTask>, tasks),
        Q_ARG(QString, ui->lineEdit_dst_node->text()));
    ChangeVision(false);
}
