#include"MainWindow.h"
#include"import_CSK.h"
#include"icon_source.h"
#include"qfiledialog.h"
#include<opencv2/highgui.hpp>
#include<qmessagebox.h>
import_CSK::import_CSK(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::import_CSK)
{
    ui->setupUi(this);
    import_CSK_thread = NULL;
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
}
import_CSK::~import_CSK()
{
    import_CSK_thread = NULL;
    if (copy)
    {
        for (int i = 0; i < ui->comboBox_dst_project->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_dst_project->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_dst_project->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
}

bool import_CSK::generate_name(QListWidget* imageslist, vector<QString>& original_nameslist, vector<QString>& import_nameslist)
{
    if (!imageslist) return false;
    import_nameslist.clear();
    original_nameslist.clear();
    for (int i = 0; i < imageslist->count(); i++)
    {
        original_nameslist.push_back(imageslist->item(i)->text());
        QFileInfo fileinfo = QFileInfo(imageslist->item(i)->text());
        string tmp = fileinfo.baseName().toStdString();
        if (tmp.size() == 82)
        {
            string polarization = tmp.substr(29, 2);
            string date = tmp.substr(37, 8);
            string name = date + "_" + polarization;
            import_nameslist.push_back(name.c_str());
        }
        else
        {
            import_nameslist.push_back(fileinfo.baseName());
        }
    }
    return true;
}

void import_CSK::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox_dst_project->setDisabled(0);
        ui->lineEdit_dst_node->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
        ui->pushButton_add->setDisabled(0);
        ui->pushButton_remove->setDisabled(0);
    }
    else
    {
        ui->comboBox_dst_project->setDisabled(1);
        ui->lineEdit_dst_node->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
        ui->pushButton_add->setDisabled(1);
        ui->pushButton_remove->setDisabled(1);
    }
}

void import_CSK::updateProcess(int value, QString information)
{
    if (!ui->progressBar->isHidden())
    {
        ui->progressBar->setValue(value);
        ui->progressBar->setFormat(QString::fromLocal8Bit("%1：%2%").arg(information).arg(value));
        ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
}
void import_CSK::endProcess()
{
    if (import_CSK_thread)
    {
        import_CSK_thread->thread()->quit();
        import_CSK_thread->thread()->wait();

    }
    ui->progressBar->hide();
    this->close();
}

void import_CSK::errorProcess(QString error_msg)
{
    QMessageBox::warning(NULL, "Error", error_msg);
    if (import_CSK_thread)
    {
        import_CSK_thread->thread()->quit();
        import_CSK_thread->thread()->wait();
        import_CSK_thread = NULL;
    }
    ui->progressBar->hide();
    ChangeVision(true);
}

void import_CSK::endThread()
{
    if (import_CSK_thread)
    {
        import_CSK_thread->thread()->quit();
        import_CSK_thread->thread()->wait();
        import_CSK_thread = NULL;
    }
}
void import_CSK::StopThread()
{
    if (import_CSK_thread != NULL)
    {
        if (import_CSK_thread->thread()->isRunning())
        {
            import_CSK_thread->thread()->requestInterruption();
            import_CSK_thread->thread()->quit();
            import_CSK_thread->thread()->wait();
        }
        import_CSK_thread = NULL;
    }
}
void import_CSK::TransitModel(QStandardItemModel* model)
{
    emit sendCopy(model);
}

void import_CSK::ShowProjectList(QStandardItemModel* model)
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

void import_CSK::on_comboBox_dst_project_currentIndexChanged()
{
    QStandardItem* project = this->copy->findItems(ui->comboBox_dst_project->currentText())[0];
    QModelIndex pro_index = this->copy->indexFromItem(project);
    QModelIndex pro_path_index = pro_index.siblingAtColumn(1);
    this->save_path = this->copy->itemFromIndex(pro_path_index)->text();
}

void import_CSK::on_pushButton_add_pressed()
{
    QString filename = QFileDialog::getOpenFileName(this,
        QString::fromLocal8Bit("导入COSMO-SkyMed数据"),
        "",
        "*.h5");
    ui->listWidget->addItem(filename);
    for (int i = 0; i < ui->listWidget->count(); i++)
    {
        if (ui->listWidget->item(i)->text().isEmpty())
        {
            ui->listWidget->takeItem(i);
        }
    }
}
void import_CSK::on_pushButton_remove_pressed()
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

void import_CSK::on_buttonBox_rejected()
{
    close();
}

void import_CSK::on_buttonBox_accepted()
{
    //检查导入文件list是否为空
    if (ui->listWidget->count() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("导入图像文件为空！"));
        return;
    }
    //检查目标节点名
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
        if (ui->lineEdit_dst_node->text() == project->child(i, 0)->text() && project->child(i, 1)->text() != "complex-0.0")
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


    import_CSK_thread = new MyThread;
    import_CSK_thread->moveToThread(new QThread(this));
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(this, &import_CSK::operate2, import_CSK_thread, &MyThread::import_CSK_patch, Qt::QueuedConnection);
    connect(import_CSK_thread, &MyThread::updateProcess, this, &import_CSK::updateProcess);
    connect(import_CSK_thread->thread(), &QThread::finished, import_CSK_thread, &MyThread::deleteLater);
    connect(import_CSK_thread, &MyThread::endProcess, this, &import_CSK::endProcess);
    connect(import_CSK_thread, &MyThread::errorProcess, this, &import_CSK::errorProcess);
    connect(this, &QWidget::destroyed, this, &import_CSK::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &import_CSK::StopThread);// , Qt::QueuedConnection);
    connect(import_CSK_thread, &MyThread::sendModel, this, &import_CSK::TransitModel);
    import_CSK_thread->thread()->start();
    emit operate2(
        this->save_path, //保存路径
        original_namelist,//原始文件名
        import_namelist, //导入文件名
        ui->lineEdit_dst_node->text(), //导入节点名
        ui->comboBox_dst_project->currentText(), //导入工程名
        this->copy);
    ChangeVision(false);
}
