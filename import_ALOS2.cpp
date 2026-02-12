#include"MainWindow.h"
#include"import_ALOS2.h"
#include"icon_source.h"
#include"qfiledialog.h"
#include<opencv2/highgui.hpp>
#include<qmessagebox.h>
import_ALOS2::import_ALOS2(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::import_ALOS2)
{
    ui->setupUi(this);
    import_ALOS2_thread = NULL;
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
}
import_ALOS2::~import_ALOS2()
{
    import_ALOS2_thread = NULL;
    if (copy)
    {
        for (int i = 0; i < ui->comboBox_dst_project->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_dst_project->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_dst_project->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
}

bool import_ALOS2::generate_name(
    QListWidget* imageslist,
    vector<QString>& original_nameslist, 
    vector<QString>& import_nameslist,
    vector<QString>& original_nameslist2
)
{
    if (!imageslist) return false;
    import_nameslist.clear();
    original_nameslist.clear();
    original_nameslist2.clear();
    for (int i = 0; i < imageslist->count(); i++)
    {
        original_nameslist.push_back(imageslist->item(i)->text());
        QFileInfo fileinfo = QFileInfo(imageslist->item(i)->text());
        QString path = fileinfo.absolutePath();
        if (path.at(path.length() - 1) != '/') path = path + "/";

        string tmp = imageslist->item(i)->text().toStdString().substr(path.length());
        if (tmp.size() == 39)//ALOS2
        {
            QString led = path + "LED" + tmp.substr(6).c_str();
            QFileInfo temp_led = QFileInfo(led);
            if (!temp_led.exists()) return false;
            original_nameslist2.push_back(led);

            string polarization = tmp.substr(4, 2);
            string date = "20" + tmp.substr(22, 6);
            string name = date + "_" + polarization;
            import_nameslist.push_back(name.c_str());
        }
        else if (tmp.size() == 30)//ALOS1
        {
            QString led = path + "LED" + tmp.substr(6).c_str();
            QFileInfo temp_led = QFileInfo(led);
            if (!temp_led.exists()) return false;
            original_nameslist2.push_back(led);
            string polarization = tmp.substr(4, 2);
            //从LED文件中读取拍摄日期
            string name;
            FILE* fp = NULL;
            fp = fopen(led.toStdString().c_str(), "rb");
            if (!fp)
            {
                char str[256];
                sprintf(str, "%d_", imageslist->currentRow());
                name = str + polarization;
            }

            char str[2048]; memset(str, 0, 2048);
            fseek(fp, 720 + 69 - 1, SEEK_SET);
            fread(str, 1, 8, fp);
            fclose(fp);
            string date = str;
            name = date + "_" + polarization;
            import_nameslist.push_back(name.c_str());
        }
        else
        {
            return false;
        }
    }
    return true;
}

void import_ALOS2::ChangeVision(bool Editable)
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

void import_ALOS2::updateProcess(int value, QString information)
{
    if (!ui->progressBar->isHidden())
    {
        ui->progressBar->setValue(value);
        ui->progressBar->setFormat(QString::fromLocal8Bit("%1：%2%").arg(information).arg(value));
        ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
}
void import_ALOS2::endProcess()
{
    if (import_ALOS2_thread)
    {
        import_ALOS2_thread->thread()->quit();
        import_ALOS2_thread->thread()->wait();

    }
    ui->progressBar->hide();
    this->close();
}

void import_ALOS2::errorProcess(QString error_msg)
{
    QMessageBox::warning(NULL, "Error", error_msg);
    if (import_ALOS2_thread)
    {
        import_ALOS2_thread->thread()->quit();
        import_ALOS2_thread->thread()->wait();
        import_ALOS2_thread = NULL;
    }
    ui->progressBar->hide();
    ChangeVision(true);
}

void import_ALOS2::endThread()
{
    if (import_ALOS2_thread)
    {
        import_ALOS2_thread->thread()->quit();
        import_ALOS2_thread->thread()->wait();
        import_ALOS2_thread = NULL;
    }
}
void import_ALOS2::StopThread()
{
    if (import_ALOS2_thread != NULL)
    {
        if (import_ALOS2_thread->thread()->isRunning())
        {
            import_ALOS2_thread->thread()->requestInterruption();
            import_ALOS2_thread->thread()->quit();
            import_ALOS2_thread->thread()->wait();
        }
        import_ALOS2_thread = NULL;
    }
}
void import_ALOS2::TransitModel(QStandardItemModel* model)
{
    emit sendCopy(model);
}

void import_ALOS2::ShowProjectList(QStandardItemModel* model)
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

void import_ALOS2::on_comboBox_dst_project_currentIndexChanged()
{
    QStandardItem* project = this->copy->findItems(ui->comboBox_dst_project->currentText())[0];
    QModelIndex pro_index = this->copy->indexFromItem(project);
    QModelIndex pro_path_index = pro_index.siblingAtColumn(1);
    this->save_path = this->copy->itemFromIndex(pro_path_index)->text();
}

void import_ALOS2::on_pushButton_add_pressed()
{
    QString filename = QFileDialog::getOpenFileName(this,
        QString::fromLocal8Bit("导入ALOS2数据"),
        "",
        "");
    ui->listWidget->addItem(filename);
    for (int i = 0; i < ui->listWidget->count(); i++)
    {
        if (ui->listWidget->item(i)->text().isEmpty())
        {
            ui->listWidget->takeItem(i);
        }
    }
}
void import_ALOS2::on_pushButton_remove_pressed()
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

void import_ALOS2::on_buttonBox_rejected()
{
    close();
}

void import_ALOS2::on_buttonBox_accepted()
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
    vector<QString> original_namelist, original_namelist2;
    vector<QString> import_namelist;
    if (!generate_name(ui->listWidget, original_namelist, import_namelist, original_namelist2))
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("输入IMG数据不合法，或者同级目录下没有LED文件！"));
        return;
    }


    import_ALOS2_thread = new MyThread;
    import_ALOS2_thread->moveToThread(new QThread(this));
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(this, &import_ALOS2::operate2, import_ALOS2_thread, &MyThread::import_ALOS2_patch, Qt::QueuedConnection);
    connect(import_ALOS2_thread, &MyThread::updateProcess, this, &import_ALOS2::updateProcess);
    connect(import_ALOS2_thread->thread(), &QThread::finished, import_ALOS2_thread, &MyThread::deleteLater);
    connect(import_ALOS2_thread, &MyThread::endProcess, this, &import_ALOS2::endProcess);
    connect(import_ALOS2_thread, &MyThread::errorProcess, this, &import_ALOS2::errorProcess);
    connect(this, &QWidget::destroyed, this, &import_ALOS2::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &import_ALOS2::StopThread);// , Qt::QueuedConnection);
    connect(import_ALOS2_thread, &MyThread::sendModel, this, &import_ALOS2::TransitModel);
    import_ALOS2_thread->thread()->start();
    emit operate2(
        this->save_path, //保存路径
        original_namelist,//原始文件名(IMG文件)
        original_namelist2,//（LED文件）
        import_namelist, //导入文件名
        ui->lineEdit_dst_node->text(), //导入节点名
        ui->comboBox_dst_project->currentText(), //导入工程名
        this->copy);
    ChangeVision(false);
}
