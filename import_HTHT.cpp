#include "MainWindow.h"
#include "import_HTHT.h"
#include "icon_source.h"
#include "qfiledialog.h"
#include <qmessagebox.h>
#include <QThread>
#include <QFileInfo>

import_HTHT::import_HTHT(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::ImportHtht)
{
    ui->setupUi(this);
    import_HTHT_thread = NULL;
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);

    ui->comboBox_mode->setItemData(0, 0); // 单星模式 / value 0
    ui->comboBox_mode->setItemData(1, 1); // 多星干涉模式 / value 1
}

import_HTHT::~import_HTHT()
{
    import_HTHT_thread = NULL;
    if (copy)
    {
        for (int i = 0; i < ui->comboBox_dst_project->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_dst_project->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_dst_project->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
    delete ui;
}

void import_HTHT::ChangeVision(bool Editable)
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

void import_HTHT::updateProcess(int value, QString information)
{
    if (!ui->progressBar->isHidden())
    {
        ui->progressBar->setValue(value);
        ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
        ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
}

void import_HTHT::endProcess()
{
    if (import_HTHT_thread)
    {
        import_HTHT_thread->thread()->quit();
        import_HTHT_thread->thread()->wait();
    }
    ui->progressBar->hide();
    this->close();
}

void import_HTHT::errorProcess(QString error_msg)
{
    QMessageBox::warning(NULL, "Error", error_msg);
    if (import_HTHT_thread)
    {
        import_HTHT_thread->thread()->quit();
        import_HTHT_thread->thread()->wait();
        import_HTHT_thread = NULL;
    }
    ui->progressBar->hide();
    ChangeVision(true);
}

void import_HTHT::endThread()
{
    if (import_HTHT_thread)
    {
        import_HTHT_thread->thread()->quit();
        import_HTHT_thread->thread()->wait();
        import_HTHT_thread = NULL;
    }
}

void import_HTHT::StopThread()
{
    if (import_HTHT_thread != NULL)
    {
        if (import_HTHT_thread->thread()->isRunning())
        {
            import_HTHT_thread->thread()->requestInterruption();
            import_HTHT_thread->thread()->quit();
            import_HTHT_thread->thread()->wait();
        }
        import_HTHT_thread = NULL;
    }
}

void import_HTHT::TransitModel(QStandardItemModel* model)
{
    emit sendCopy(model);
}

void import_HTHT::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox_dst_project->addItem(model->item(i, 0)->text());
        model->item(i, 0)->setStatusTip(IN_PROCESS);
    }
    if (model->rowCount() > 0)
    {
        ui->comboBox_dst_project->setCurrentIndex(0);
        this->save_path = model->item(0, 1)->text();
    }
}

void import_HTHT::on_comboBox_dst_project_currentIndexChanged()
{
    if (!this->copy || ui->comboBox_dst_project->currentIndex() < 0) return;
    QStandardItem* project = this->copy->findItems(ui->comboBox_dst_project->currentText())[0];
    QModelIndex pro_index = this->copy->indexFromItem(project);
    QModelIndex pro_path_index = pro_index.siblingAtColumn(1);
    this->save_path = this->copy->itemFromIndex(pro_path_index)->text();
}

void import_HTHT::on_pushButton_data_browse_pressed()
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
            ui->lineEdit_dst_node->setText(QFileInfo(filename).baseName() + "_HTHT");
        }
    }
}

void import_HTHT::on_pushButton_xml_browse_pressed()
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

void import_HTHT::on_pushButton_add_pressed()
{
    QString data_file = ui->lineEdit_data_file->text().trimmed();
    QString xml_file = ui->lineEdit_xml_file->text().trimmed();
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

void import_HTHT::on_pushButton_remove_pressed()
{
    int currRow = ui->listWidget->currentRow();
    if (currRow >= 0)
    {
        delete ui->listWidget->takeItem(currRow);
    }
}

void import_HTHT::on_buttonBox_rejected()
{
    close();
}

void import_HTHT::on_buttonBox_accepted()
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

    if (import_HTHT_thread) {
        import_HTHT_thread->thread()->quit();
        import_HTHT_thread->thread()->wait();
    }

    import_HTHT_thread = new HTHTImportWorker;
    QThread* thread = new QThread(this);
    import_HTHT_thread->moveToThread(thread);
    ui->progressBar->setValue(0);
    ui->progressBar->show();

    connect(this, &import_HTHT::operate2, import_HTHT_thread, &HTHTImportWorker::import_HTHT_patch, Qt::QueuedConnection);
    connect(import_HTHT_thread, &HTHTImportWorker::updateProcess, this, &import_HTHT::updateProcess);
    connect(thread, &QThread::finished, import_HTHT_thread, &HTHTImportWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    connect(import_HTHT_thread, &HTHTImportWorker::endProcess, this, &import_HTHT::endProcess);
    connect(import_HTHT_thread, &HTHTImportWorker::errorProcess, this, &import_HTHT::errorProcess);
    connect(this, &QWidget::destroyed, this, &import_HTHT::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &import_HTHT::StopThread);
    connect(import_HTHT_thread, &HTHTImportWorker::sendModel, this, &import_HTHT::TransitModel);

    thread->start();
    emit operate2(
        this->save_path,
        data_files,
        xml_files,
        modes,
        import_names,
        ui->lineEdit_dst_node->text(),
        ui->comboBox_dst_project->currentText(),
        this->copy
    );
    ChangeVision(false);
}
