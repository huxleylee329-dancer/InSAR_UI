#include "MainWindow.h"
#include "import_LUTAN.h"
#include "ImportTask.h"
#include "ImportOutputPersistence.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "qfiledialog.h"
#include <qmessagebox.h>
#include <QThread>
#include <QFileInfo>

import_LUTAN::import_LUTAN(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::ImportLutan)
{
    ui->setupUi(this);
    import_LUTAN_thread = NULL;
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);

    ui->comboBox_mode->setItemData(0, 1); // 单星模式
    ui->comboBox_mode->setItemData(1, 2); // 双星干涉模式
}

import_LUTAN::~import_LUTAN()
{
    import_LUTAN_thread = NULL;
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
    delete ui;
}

void import_LUTAN::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox_dst_project->setDisabled(0);
        ui->lineEdit_dst_node->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
        ui->pushButton_add->setDisabled(0);
        ui->pushButton_remove->setDisabled(0);
        ui->pushButton_data_browse->setDisabled(0);
        ui->pushButton_xml_browse->setDisabled(0);
        ui->lineEdit_data_file->setDisabled(0);
        ui->lineEdit_xml_file->setDisabled(0);
        ui->comboBox_mode->setDisabled(0);
    }
    else
    {
        ui->comboBox_dst_project->setDisabled(1);
        ui->lineEdit_dst_node->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
        ui->pushButton_add->setDisabled(1);
        ui->pushButton_remove->setDisabled(1);
        ui->pushButton_data_browse->setDisabled(1);
        ui->pushButton_xml_browse->setDisabled(1);
        ui->lineEdit_data_file->setDisabled(1);
        ui->lineEdit_xml_file->setDisabled(1);
        ui->comboBox_mode->setDisabled(1);
    }
}

void import_LUTAN::updateProcess(int value, QString information)
{
    if (!ui->progressBar->isHidden())
    {
        ui->progressBar->setValue(value);
        ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
        ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
}

void import_LUTAN::endProcess()
{
    if (import_LUTAN_thread)
    {
        import_LUTAN_thread->thread()->quit();
        import_LUTAN_thread->thread()->wait();
    }
    ui->progressBar->hide();
    this->close();
}

void import_LUTAN::errorProcess(QString error_msg)
{
    QMessageBox::warning(NULL, "Error", error_msg);
    if (import_LUTAN_thread)
    {
        import_LUTAN_thread->thread()->quit();
        import_LUTAN_thread->thread()->wait();
        import_LUTAN_thread = NULL;
    }
    ui->progressBar->hide();
    ChangeVision(true);
}

void import_LUTAN::endThread()
{
    if (import_LUTAN_thread)
    {
        import_LUTAN_thread->thread()->quit();
        import_LUTAN_thread->thread()->wait();
        import_LUTAN_thread = NULL;
    }
}

void import_LUTAN::StopThread()
{
    if (import_LUTAN_thread != NULL)
    {
        if (import_LUTAN_thread->thread()->isRunning())
        {
            import_LUTAN_thread->thread()->requestInterruption();
            import_LUTAN_thread->thread()->quit();
            import_LUTAN_thread->thread()->wait();
        }
        import_LUTAN_thread = NULL;
    }
}

void import_LUTAN::TransitModel(QStandardItemModel* model)
{
    emit sendCopy(model);
}

void import_LUTAN::ShowProjectList(QStandardItemModel* model)
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

void import_LUTAN::on_comboBox_dst_project_currentIndexChanged()
{
    if (!this->copy || ui->comboBox_dst_project->currentIndex() < 0) return;
    QStandardItem* project = NodeUtils::findFirstModelItem(
        this->copy, ui->comboBox_dst_project->currentText());
    if (!project) return;
    QModelIndex pro_index = this->copy->indexFromItem(project);
    QModelIndex pro_path_index = pro_index.siblingAtColumn(1);
    this->save_path = this->copy->itemFromIndex(pro_path_index)->text();
}

void import_LUTAN::on_pushButton_data_browse_pressed()
{
    QString filename = QFileDialog::getOpenFileName(this,
        QStringLiteral("选择数据文件"),
        "",
        "Data Files (*.tiff *.tif *.h5);;All Files (*)");
    if (!filename.isEmpty())
    {
        ui->lineEdit_data_file->setText(filename);
        if (ui->lineEdit_dst_node->text().isEmpty())
        {
            ui->lineEdit_dst_node->setText(QFileInfo(filename).baseName() + "_LUTAN");
        }
    }
}

void import_LUTAN::on_pushButton_xml_browse_pressed()
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

void import_LUTAN::on_pushButton_add_pressed()
{
    QString data_file = ui->lineEdit_data_file->text();
    QString xml_file = ui->lineEdit_xml_file->text();
    if (data_file.isEmpty() || xml_file.isEmpty())
    {
        QMessageBox::warning(this, "Warning", QStringLiteral("请选择数据文件和参数 XML 文件！"));
        return;
    }
    int mode = ui->comboBox_mode->currentData().toInt();
    QString modeStr = ui->comboBox_mode->currentText();

    QString itemText = QString("%1 | XML: %2 | Mode: %3")
        .arg(QFileInfo(data_file).fileName())
        .arg(QFileInfo(xml_file).fileName())
        .arg(modeStr);

    QListWidgetItem* item = new QListWidgetItem(itemText);
    item->setData(Qt::UserRole, data_file);
    item->setData(Qt::UserRole + 1, xml_file);
    item->setData(Qt::UserRole + 2, mode);

    ui->listWidget->addItem(item);

    ui->lineEdit_data_file->clear();
    ui->lineEdit_xml_file->clear();
}

void import_LUTAN::on_pushButton_remove_pressed()
{
    int currRow = ui->listWidget->currentRow();
    if (currRow >= 0)
    {
        delete ui->listWidget->takeItem(currRow);
    }
}

void import_LUTAN::on_buttonBox_rejected()
{
    close();
}

void import_LUTAN::on_buttonBox_accepted()
{
    if (ui->listWidget->count() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("导入任务列表为空！"));
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

    std::vector<QString> data_files;
    std::vector<QString> xml_files;
    std::vector<int> modes;
    std::vector<QString> import_names;

    for (int i = 0; i < ui->listWidget->count(); i++)
    {
        QListWidgetItem* item = ui->listWidget->item(i);
        QString data_f = item->data(Qt::UserRole).toString();
        QString xml_f = item->data(Qt::UserRole + 1).toString();
        int md = item->data(Qt::UserRole + 2).toInt();

        data_files.push_back(data_f);
        xml_files.push_back(xml_f);
        modes.push_back(md);

        QFileInfo fi(data_f);
        import_names.push_back(fi.baseName());
    }

    if (import_LUTAN_thread) {
        import_LUTAN_thread->thread()->quit();
        import_LUTAN_thread->thread()->wait();
    }

    import_LUTAN_thread = new LUTANImportWorker;
    QThread* thread = new QThread(this);
    import_LUTAN_thread->moveToThread(thread);
    ui->progressBar->setValue(0);
    ui->progressBar->show();

    connect(import_LUTAN_thread, &LUTANImportWorker::updateProcess, this, &import_LUTAN::updateProcess);
    connect(thread, &QThread::finished, import_LUTAN_thread, &LUTANImportWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    connect(import_LUTAN_thread, &LUTANImportWorker::endProcess, this, &import_LUTAN::endProcess);
    connect(import_LUTAN_thread, &LUTANImportWorker::errorProcess, this, &import_LUTAN::errorProcess);
    connect(this, &QWidget::destroyed, this, &import_LUTAN::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &import_LUTAN::StopThread);
    connect(import_LUTAN_thread, &LUTANImportWorker::outputsGenerated, this,
        [this, projectName = ui->comboBox_dst_project->currentText(), savePath = save_path](const QString& dstNode, const QStringList& names, const QStringList& paths, const QString& dataType, const QString& format) {
            if (ImportOutputPersistence::persist(copy, projectName, savePath, dstNode, names, paths, dataType, format)) TransitModel(copy);
        });

    thread->start();

    // 构造 ImportTask 列表
    std::vector<ImportTask> tasks;
    for (size_t i = 0; i < data_files.size(); ++i) {
        ImportTask task;
        task.filename = import_names[i];
        task.arguments = QStringList{ data_files[i], xml_files[i], QString::number(modes[i]) };
        tasks.push_back(task);
    }

    QMetaObject::invokeMethod(import_LUTAN_thread, "import_patch",
        Q_ARG(QString, this->save_path),
        Q_ARG(std::vector<ImportTask>, tasks),
        Q_ARG(QString, ui->lineEdit_dst_node->text()));
    ChangeVision(false);
}
