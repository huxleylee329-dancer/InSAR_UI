#include "MainWindow.h"
#include "Import_Macao.h"
#include "icon_source.h"
#include "qfiledialog.h"
#include <QFile>
#include <QFileInfo>
#include <qmessagebox.h>

Import_Macao::Import_Macao(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::ImportMacao),
    copy(NULL),
    import_Macao_thread(NULL),
    import_Macao_thread2(NULL)
{
    ui->setupUi(this);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
    ui->progressBar_2->setMinimum(0);
    ui->progressBar_2->setMaximum(100);
    ui->progressBar_2->setHidden(1);
}

Import_Macao::~Import_Macao()
{
    import_Macao_thread = NULL;
    import_Macao_thread2 = NULL;

    if (copy)
    {
        for (int i = 0; i < ui->comboBox_dst_project->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_dst_project->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_dst_project->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
        for(int i = 0; i < ui->comboBox_dst_project_2->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_dst_project_2->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_dst_project_2->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
    delete ui;
}

void Import_Macao::ShowProjectList(QStandardItemModel* model)
{
    if (!model) return;
    if (model->rowCount() < 1) return;

    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox_dst_project->addItem(model->item(i, 0)->text());
        ui->comboBox_dst_project_2->addItem(model->item(i, 0)->text());
        model->item(i, 0)->setStatusTip(IN_PROCESS);
    }
    ui->comboBox_dst_project->setCurrentIndex(0);
    ui->comboBox_dst_project_2->setCurrentIndex(0);
    this->save_path = model->item(0, 1)->text();
}

void Import_Macao::on_comboBox_dst_project_currentIndexChanged()
{
    if (!copy || ui->comboBox_dst_project->count() == 0) return;

    QStandardItem* project = this->copy->findItems(ui->comboBox_dst_project->currentText())[0];
    QModelIndex pro_index = this->copy->indexFromItem(project);
    QModelIndex pro_path_index = pro_index.siblingAtColumn(1);
    this->save_path = this->copy->itemFromIndex(pro_path_index)->text();
}

void Import_Macao::on_button_xml_browse_pressed()
{
    QString filename = QFileDialog::getOpenFileName(this,
        QString::fromLocal8Bit("Import Macao data"),
        this->xml_path,
        "Images (*.jpg *.jpeg *.png *.bmp *.tif *.tiff)");

    if (QFile::exists(filename))
    {
        ui->LineEdit_xml->setText(filename);
        QFileInfo fileinfo(filename);
        ui->LineEdit_dst_filename->setText(fileinfo.baseName());
    }
}

void Import_Macao::on_buttonBox_accepted()
{
        if (ui->LineEdit_xml->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入TerrSAR xml文件！"));
        return;
    }
    bool bFlag = ui->LineEdit_xml->text().contains(QRegularExpression("^[\\n\\w:.\\()-/]+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请注意路径中应当仅包含数字、字母及下划线！"));
        return;
    }
    if (ui->LineEdit_dst_filename->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请输入想要保存的图片名称！"));
        return;
    }
    bFlag = ui->LineEdit_dst_filename->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("请注意图像名称应当为数字、字母及下划线的组合！"));
        return;
    }

    if (ui->lineEdit_dst_node->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("目标节点名为空！"));
        return;
    }

    //防重名检查

    QStandardItem* project = this->copy->findItems(ui->comboBox_dst_project->currentText())[0];
    if (!project) {
        return;
    }
    bool same_name_node = false;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (ui->lineEdit_dst_node->text() == project->child(i)->text() && project->child(i, 1)->text() != "complex-0.0")
        {
            same_name_node = true;
        }
    }
    if (same_name_node)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("目标节点已存在，且和导入数据级别不同，请重命名！"));
        return;
    }

    // Ensure previous threads are released
    if (import_Macao_thread) {
        import_Macao_thread->thread()->quit();
        import_Macao_thread->thread()->wait();
    }

    import_Macao_thread = new MyThread;
    QThread* thread = new QThread(this);
    import_Macao_thread->moveToThread(thread);
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(this, &Import_Macao::operate, import_Macao_thread, &MyThread::import_Macao, Qt::QueuedConnection);
    connect(import_Macao_thread, &MyThread::updateProcess, this, &Import_Macao::updateProcess);
    connect(thread, &QThread::finished, import_Macao_thread, &MyThread::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    connect(import_Macao_thread, &MyThread::endProcess, this, &Import_Macao::endProcess);
    connect(this, &QWidget::destroyed, this, &Import_Macao::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Import_Macao::StopThread);// , Qt::QueuedConnection);
    connect(import_Macao_thread, &MyThread::sendModel, this, &Import_Macao::TransitModel);
    thread->start();
    emit operate(
        ui->LineEdit_xml->text(),
        this->save_path,
        ui->lineEdit_dst_node->text(),
        ui->LineEdit_dst_filename->text(),
        ui->comboBox_dst_project->currentText(),
        this->copy
    );
    ChangeVision(false);
}

void Import_Macao::on_buttonBox_rejected()
{
    this->close();
}

void Import_Macao::updateProcess(int value, QString information)
{
    if (!ui->progressBar->isHidden())
    {
        ui->progressBar->setValue(value);
        ui->progressBar->setFormat(QString::fromLocal8Bit("%1：%2%").arg(information).arg(value));
        ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    else if (!ui->progressBar_2->isHidden())
    {
        ui->progressBar_2->setValue(value);
        ui->progressBar_2->setFormat(QString::fromLocal8Bit("%1：%2%").arg(information).arg(value));
        ui->progressBar_2->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
}

void Import_Macao::endProcess()
{
    if (import_Macao_thread)
    {
        import_Macao_thread->thread()->quit();
        import_Macao_thread->thread()->wait();

    }
    if (import_Macao_thread2)
    {
        import_Macao_thread2->thread()->quit();
        import_Macao_thread2->thread()->wait();

    }
    ui->progressBar->hide();
    ui->progressBar_2->hide();
    this->close();
}

void Import_Macao::StopThread()
{
    if (import_Macao_thread != NULL)
        if (import_Macao_thread->thread()->isRunning())
        {
            import_Macao_thread->thread()->requestInterruption();
            import_Macao_thread->thread()->quit();
            import_Macao_thread->thread()->wait();
        }
    if (import_Macao_thread2 != NULL)
        if (import_Macao_thread2->thread()->isRunning())
        {
            import_Macao_thread2->thread()->requestInterruption();
            import_Macao_thread2->thread()->quit();
            import_Macao_thread2->thread()->wait();
        }
}

void Import_Macao::TransitModel(QStandardItemModel* model)
{
    emit sendCopy(model);
}

void Import_Macao::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox_dst_project->setDisabled(0);
        ui->comboBox_dst_project_2->setDisabled(0);
        ui->lineEdit_dst_node->setDisabled(0);
        ui->lineEdit_dst_node_2->setDisabled(0);
        ui->LineEdit_dst_filename->setDisabled(0);
        ui->LineEdit_xml->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
        ui->buttonBox_2->buttons().at(0)->setDisabled(0);
        ui->button_xml_browse->setDisabled(0);
        ui->pushButton_add->setDisabled(0);
        ui->pushButton_remove->setDisabled(0);

    }
    else
    {
        ui->comboBox_dst_project->setDisabled(1);
        ui->comboBox_dst_project_2->setDisabled(1);
        ui->lineEdit_dst_node->setDisabled(1);
        ui->lineEdit_dst_node_2->setDisabled(1);
        ui->LineEdit_dst_filename->setDisabled(1);
        ui->LineEdit_xml->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
        ui->buttonBox_2->buttons().at(0)->setDisabled(1);
        ui->button_xml_browse->setDisabled(1);
        ui->pushButton_add->setDisabled(1);
        ui->pushButton_remove->setDisabled(1);

    }
}

void Import_Macao::on_buttonBox_2_accepted()
{
    //检查导入文件list是否为空
    if (ui->listWidget->count() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("导入图像文件为空！"));
        return;
    }
    //检查目标节点名
    if (ui->lineEdit_dst_node_2->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("目标节点名为空！"));
        return;
    }
    

    //防重名检查
    QStandardItem* project = this->copy->findItems(ui->comboBox_dst_project_2->currentText())[0];
    if (!project) {
        return;
    }
    bool same_name_node = false;
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (ui->lineEdit_dst_node_2->text() == project->child(i, 0)->text() && project->child(i, 1)->text() != "complex-0.0")
        {
            same_name_node = true;
        }
    }
    if (same_name_node)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("目标节点已存在，且和导入数据级别不同，请重命名！"));
        return;
    }

    //根据原始文件日期生成导入文件名称
    vector<QString> original_namelist;
    vector<QString> import_namelist;
    if (!generate_name(ui->listWidget, original_namelist, import_namelist)) return;


    // Ensure previous threads are released
    if (import_Macao_thread2) {
        import_Macao_thread2->thread()->quit();
        import_Macao_thread2->thread()->wait();
    }

    import_Macao_thread2 = new MyThread;
    QThread* thread2 = new QThread(this);
    import_Macao_thread2->moveToThread(thread2);
    ui->progressBar_2->setValue(0);
    ui->progressBar_2->show();
    connect(this, &Import_Macao::operate2, import_Macao_thread2, &MyThread::import_Macao_patch, Qt::QueuedConnection);
    connect(import_Macao_thread2, &MyThread::updateProcess, this, &Import_Macao::updateProcess);
    connect(thread2, &QThread::finished, import_Macao_thread2, &MyThread::deleteLater);
    connect(thread2, &QThread::finished, thread2, &QThread::deleteLater);
    connect(import_Macao_thread2, &MyThread::endProcess, this, &Import_Macao::endProcess);
    connect(this, &QWidget::destroyed, this, &Import_Macao::StopThread);
    connect(ui->buttonBox_2, &QDialogButtonBox::rejected, this, &Import_Macao::StopThread);// , Qt::QueuedConnection);
    connect(import_Macao_thread2, &MyThread::sendModel, this, &Import_Macao::TransitModel);
    thread2->start();
    emit operate2(
        this->save_path, //保存路径
        original_namelist,//原始文件名
        import_namelist, //导入文件名b    
        ui->lineEdit_dst_node_2->text(), //导入节点名
        ui->comboBox_dst_project_2->currentText(), //导入工程名
        this->copy);
    ChangeVision(false);
}
void Import_Macao::on_buttonBox_2_rejected()
{
    close();
}

bool Import_Macao::generate_name(QListWidget* imageslist, vector<QString>& original_nameslist, vector<QString>& import_nameslist)
{
    if (!imageslist) return false;
    import_nameslist.clear();
    original_nameslist.clear();
    for (int i = 0; i < imageslist->count(); i++)
    {
        original_nameslist.push_back(imageslist->item(i)->text());
        QFileInfo fileinfo = QFileInfo(imageslist->item(i)->text());
        string tmp = fileinfo.baseName().toStdString();
        if (tmp.rfind("T") < tmp.length() && tmp.rfind("T") >= 0)
        {
            int start = tmp.rfind("T") - 8;
            start = start < 0 ? 0 : start;
            import_nameslist.push_back(QString(tmp.substr(start, 8).c_str()));
        }
        else
        {
            import_nameslist.push_back(fileinfo.baseName());
        }
    }
    return true;
}

void Import_Macao::on_comboBox_dst_project_2_currentIndexChanged()
{
    QStandardItem* project = this->copy->findItems(ui->comboBox_dst_project_2->currentText())[0];
    QModelIndex pro_index = this->copy->indexFromItem(project);
    QModelIndex pro_path_index = pro_index.siblingAtColumn(1);
    this->save_path = this->copy->itemFromIndex(pro_path_index)->text();
}

void Import_Macao::on_pushButton_add_pressed()
{
    QStringList filenames = QFileDialog::getOpenFileNames(this,
    QString::fromLocal8Bit("导入 Macao 数据"),
    "",
    "Images (*.jpg *.jpeg *.png *.bmp *.tif *.tiff)");

    for (int i = 0; i < filenames.size(); i++)
    {
        if (filenames[i].isEmpty())
        {
            continue;
        }

        bool exists = false;
        for (int j = 0; j < ui->listWidget->count(); j++)
        {
            if (ui->listWidget->item(j)->text() == filenames[i])
            {
                exists = true;
                break;
            }
        }

        if (!exists)
        {
            ui->listWidget->addItem(filenames[i]);
        }
    }
}

void Import_Macao::on_pushButton_remove_pressed()
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
