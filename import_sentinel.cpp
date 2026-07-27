#include"MainWindow.h"
#include"import_sentinel.h"
#include"ImportTask.h"
#include "ImportOutputPersistence.h"
#include"icon_source.h"
#include"qfiledialog.h"
#include"NodeUtils.h"
#include<QDir>
#include<QFileInfo>
#include<QSettings>
#include<QRegularExpression>
#include<QDate>
#include<QDateTime>
#include<opencv2/highgui.hpp>
#include<qmessagebox.h>
#include "tinyxml.h"
import_sentinel::import_sentinel(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::ImportSentinel)
{
    ui->setupUi(this);
    import_sentinel_thread = NULL;
    import_sentinel_thread_2 = NULL;
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
    ui->progressBar_2->setMinimum(0);
    ui->progressBar_2->setMaximum(100);
    ui->progressBar_2->setHidden(1);

    subswath = "iw1"; polarization = "vv";
    ui->ComboBox_subswath->addItem("iw1");
    ui->ComboBox_subswath->addItem("iw2");
    ui->ComboBox_subswath->addItem("iw3");
    ui->ComboBox_subswath_2->addItem("iw1");
    ui->ComboBox_subswath_2->addItem("iw2");
    ui->ComboBox_subswath_2->addItem("iw3");
    ui->ComboBox_subswath->setCurrentIndex(0);
    ui->ComboBox_polarization->addItem("vv");
    ui->ComboBox_polarization->addItem("vh");
    ui->ComboBox_polarization->setCurrentIndex(0);
    ui->ComboBox_polarization_2->addItem("vv");
    ui->ComboBox_polarization_2->addItem("vh");
    ui->ComboBox_polarization_2->setCurrentIndex(0);
    ui->lineEdit_dst_node->setPlaceholderText(QStringLiteral("不要输入中文字符"));
    ui->lineEdit_dst_node_2->setPlaceholderText(QStringLiteral("不要输入中文字符"));
    old_path = "C:\\";
    date = "";
}
import_sentinel::~import_sentinel()
{
    import_sentinel_thread = NULL;
    import_sentinel_thread_2 = NULL;
    if (copy)
    {
        for (int i = 0; i < ui->comboBox_dst_project->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_dst_project->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_dst_project->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
        for (int i = 0; i < ui->ComboBox_dst_project_2->count(); i++)
        {
            if (!copy->findItems(ui->ComboBox_dst_project_2->itemText(i)).isEmpty())
                copy->findItems(ui->ComboBox_dst_project_2->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
}

bool import_sentinel::generate_name(QListWidget* imageslist, std::vector<QString>& original_nameslist, std::vector<QString>& import_nameslist)
{
    if (!imageslist) return false;
    import_nameslist.clear();
    original_nameslist.clear();
    for (int i = 0; i < imageslist->count(); i++)
    {
        original_nameslist.push_back(imageslist->item(i)->text());
        QFileInfo fileinfo = QFileInfo(imageslist->item(i)->text());
        QString tmp = fileinfo.baseName();
        QString filename = imageslist->item(i)->text();
        XMLFile xmldoc;
        int ret = xmldoc.XMLFile_load(filename.toStdString().c_str());
        if (ret < 0)
        {
            import_nameslist.push_back(tmp);
            continue;
        }
        TiXmlElement* root = NULL, * pnode = NULL;
        ret = xmldoc.find_node("dataObjectSection", root);
        if (ret < 0)
        {
            import_nameslist.push_back(tmp);
            continue;
        }
        ret = xmldoc._find_node(root, "dataObject", pnode);
        if (ret < 0)
        {
            import_nameslist.push_back(tmp);
            continue;
        }
        string tmp1(pnode->FirstAttribute()->Value());
        if (tmp1.length() > 18)
        {
            date = QString(tmp1.substr(18, 8).c_str());
            QString name = date + "_" + subswath + polarization;
            import_nameslist.push_back(name);
        }
        else
        {
            import_nameslist.push_back(tmp);
            continue;
        }
        root = NULL; pnode = NULL;
    }
    return true;
}

void import_sentinel::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox_dst_project->setDisabled(0);
        ui->ComboBox_dst_project_2->setDisabled(0);
        ui->lineEdit_dst_node->setDisabled(0);
        ui->lineEdit_dst_node_2->setDisabled(0);
        ui->LineEdit_dst_filename->setDisabled(0);
        ui->lineEdit_manifest_file->setDisabled(0);
        ui->lineEdit_POD->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
        ui->buttonBox_2->buttons().at(0)->setDisabled(0);
        ui->pushButton_POD->setDisabled(0);
        ui->browse_Button->setDisabled(0);
        ui->pushButton_add->setDisabled(0);
        ui->pushButton_remove->setDisabled(0);
        ui->ComboBox_polarization->setDisabled(0);
        ui->ComboBox_polarization_2->setDisabled(0);
        ui->ComboBox_subswath->setDisabled(0);
        ui->ComboBox_subswath_2->setDisabled(0);
    }
    else
    {
        ui->comboBox_dst_project->setDisabled(1);
        ui->ComboBox_dst_project_2->setDisabled(1);
        ui->lineEdit_dst_node->setDisabled(1);
        ui->lineEdit_dst_node_2->setDisabled(1);
        ui->LineEdit_dst_filename->setDisabled(1);
        ui->lineEdit_manifest_file->setDisabled(1);
        ui->lineEdit_POD->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
        ui->buttonBox_2->buttons().at(0)->setDisabled(1);
        ui->pushButton_POD->setDisabled(1);
        ui->browse_Button->setDisabled(1);
        ui->pushButton_add->setDisabled(1);
        ui->pushButton_remove->setDisabled(1);
        ui->ComboBox_polarization->setDisabled(1);
        ui->ComboBox_polarization_2->setDisabled(1);
        ui->ComboBox_subswath->setDisabled(1);
        ui->ComboBox_subswath_2->setDisabled(1);
    }
}


void import_sentinel::updateProcess(int value, QString information)
{
    if (!ui->progressBar->isHidden())
    {
        ui->progressBar->setValue(value);
        ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
        ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    else if (!ui->progressBar_2->isHidden())
    {
        ui->progressBar_2->setValue(value);
        ui->progressBar_2->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
        ui->progressBar_2->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
}
void import_sentinel::endProcess()
{
    if (import_sentinel_thread)
    {
        import_sentinel_thread->thread()->quit();
        import_sentinel_thread->thread()->wait();
        
    }
    if (import_sentinel_thread_2)
    {
        import_sentinel_thread_2->thread()->quit();
        import_sentinel_thread_2->thread()->wait();

    }
    ui->progressBar->hide();
    ui->progressBar_2->hide();
    this->close();
}
void import_sentinel::endThread()
{
    if (import_sentinel_thread)
    {
        import_sentinel_thread->thread()->quit();
        import_sentinel_thread->thread()->wait();
    }
    if (import_sentinel_thread_2)
    {
        import_sentinel_thread_2->thread()->quit();
        import_sentinel_thread_2->thread()->wait();
    }
    
}
void import_sentinel::StopThread()
{
    if (import_sentinel_thread != NULL)
        if (import_sentinel_thread->thread()->isRunning())
        {
            import_sentinel_thread->thread()->requestInterruption();
            import_sentinel_thread->thread()->quit();
            import_sentinel_thread->thread()->wait();
        }
    if (import_sentinel_thread_2 != NULL)
        if (import_sentinel_thread_2->thread()->isRunning())
        {
            import_sentinel_thread_2->thread()->requestInterruption();
            import_sentinel_thread_2->thread()->quit();
            import_sentinel_thread_2->thread()->wait();
        }

}
void import_sentinel::TransitModel(QStandardItemModel* model)
{
    emit sendCopy(model);
}

void import_sentinel::on_comboBox_dst_project_currentIndexChanged()
{
    QStandardItem* project = this->copy->findItems(ui->comboBox_dst_project->currentText())[0];
    QModelIndex pro_index = this->copy->indexFromItem(project);
    QModelIndex pro_path_index = pro_index.siblingAtColumn(1);
    this->save_path = this->copy->itemFromIndex(pro_path_index)->text();
}
void import_sentinel::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox_dst_project->addItem(model->item(i, 0)->text());
        ui->ComboBox_dst_project_2->addItem(model->item(i, 0)->text());
        model->item(i, 0)->setStatusTip(IN_PROCESS);
    }
    ui->comboBox_dst_project->setCurrentIndex(0);
    ui->ComboBox_dst_project_2->setCurrentIndex(0);
    this->save_path = model->item(0, 1)->text();
}
void import_sentinel::on_ComboBox_dst_project_2_currentIndexChanged()
{
    QStandardItem* project = this->copy->findItems(ui->ComboBox_dst_project_2->currentText())[0];
    QModelIndex pro_index = this->copy->indexFromItem(project);
    QModelIndex pro_path_index = pro_index.siblingAtColumn(1);
    this->save_path = this->copy->itemFromIndex(pro_path_index)->text();
}
void import_sentinel::on_pushButton_add_pressed()
{
    bool isWarning = 0;
    QString dirname = QFileDialog::getExistingDirectory(this,
        QStringLiteral("导入哨兵数据"),
        "/",
        QFileDialog::ShowDirsOnly);
    QDir* FileDir = new QDir(dirname);
    QStringList filter;
    filter << "S1A_*";
    FileDir->setNameFilters(filter);
    QList<QFileInfo>* Dirinfo = new QList<QFileInfo>(FileDir->entryInfoList(filter));
    int count = Dirinfo->count();
    //ui->listWidget->addItem(filename);
    for (int i = 0; i < count; i++)
    {
        QString filename = "";
        if(Dirinfo->at(i).fileName().contains(".SAFE", Qt::CaseSensitive))
        {
            filename = Dirinfo->at(i).filePath() + "/manifest.safe";
        }
        else
        {
            filename = Dirinfo->at(i).filePath() + "/" + Dirinfo->at(i).baseName() + ".SAFE/manifest.safe";
        }
        if (!filename.isEmpty())
        {
            bool isRepeated = 0;
            for (int j = 0; j < ui->listWidget->count(); j++)
            {
                if (filename == ui->listWidget->item(j)->text())
                {
                    isRepeated = 1;
                    isWarning = 1;
                    break;
                }
            }
            if (!isRepeated)
                ui->listWidget->addItem(filename);

        }
    }
    if (isWarning)
    {
        QMessageBox::warning(NULL, QStringLiteral("注意"), QStringLiteral("检测到您试图重复添加相同数据，已将其忽略。"));
    }
    for (int i = 0; i < ui->listWidget->count(); i++)
    {
        if (ui->listWidget->item(i)->text().isEmpty())
            ui->listWidget->takeItem(i);
    }
}
void import_sentinel::on_pushButton_remove_pressed()
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
void import_sentinel::on_browse_Button_pressed()
{
    QString filename = QFileDialog::getOpenFileName(this,
        "Open sentinel manifest.safe file",
        this->old_path,
        "file(*.safe)");
    if (QFile::exists(filename))
    {
        ui->lineEdit_manifest_file->setText(filename);
        //QFileInfo fileinfo = QFileInfo(filename);
        XMLFile xmldoc;
        int ret = xmldoc.XMLFile_load(filename.toStdString().c_str());
        if (ret < 0)
        {
            ui->LineEdit_dst_filename->setText(ui->ComboBox_subswath->currentText() + ui->ComboBox_polarization->currentText());
            return;
        }
        TiXmlElement* root = NULL, * pnode = NULL;
        ret = xmldoc.find_node("dataObjectSection", root);
        if (ret < 0)
        {
            ui->LineEdit_dst_filename->setText(ui->ComboBox_subswath->currentText() + ui->ComboBox_polarization->currentText());
            return;
        }
        ret = xmldoc._find_node(root, "dataObject", pnode);
        if (ret < 0)
        {
            ui->LineEdit_dst_filename->setText(ui->ComboBox_subswath->currentText() + ui->ComboBox_polarization->currentText());
            return;
        }
        string tmp(pnode->FirstAttribute()->Value());
        if (tmp.length() > 18)
        {
            date = QString(tmp.substr(18, 8).c_str());
            ui->LineEdit_dst_filename->setText(date + QString("_") + ui->ComboBox_subswath->currentText() + ui->ComboBox_polarization->currentText());
            
        }
        else
        {
            ui->LineEdit_dst_filename->setText(ui->ComboBox_subswath->currentText() + ui->ComboBox_polarization->currentText());
        }
    }
}

void import_sentinel::on_pushButton_POD_pressed()
{
    QString filename = QFileDialog::getOpenFileName(this,
        "Open sentinel POD file",
        this->old_path,
        "file(*.EOF)");
    if (QFile::exists(filename))
    {
        ui->lineEdit_POD->setText(filename);        
    }
}

static QString findMatchedEofFile(const QString& manifestOrSafePath, const QString& projName)
{
    // 1. 优先就近查找：去 manifest.safe 的同级或上级 .SAFE 目录下寻找是否存在 *.EOF
    QFileInfo manifestInfo(manifestOrSafePath);
    QDir safeDir = manifestInfo.dir(); // 一般为 .SAFE/
    QStringList eofFilters;
    eofFilters << "*.EOF" << "*.eofs";
    QStringList eofFiles = safeDir.entryList(eofFilters, QDir::Files);
    if (!eofFiles.isEmpty())
    {
        return safeDir.absoluteFilePath(eofFiles.first());
    }

    // 如果上级是 .SAFE 且里面也没有，向上多找一层
    if (manifestInfo.fileName().toLower() == "manifest.safe")
    {
        QDir parentDir = safeDir;
        parentDir.cdUp();
        QStringList parentEofFiles = parentDir.entryList(eofFilters, QDir::Files);
        if (!parentEofFiles.isEmpty())
        {
            return parentDir.absoluteFilePath(parentEofFiles.first());
        }
    }

    // 2. 如果就近没找到，提取影像的平台与拍摄日期做全局精轨库扫描匹配
    QString pathLower = manifestOrSafePath.toLower();
    QString platform = "S1A";
    if (pathLower.contains("s1b")) platform = "S1B";

    // 匹配日期，S1 命名标准中成像时间在第 5 段：如 S1A_IW_SLC__1SDV_20251204T015841_...
    QRegularExpression dateRe("(20\\d{6})t(\\d{6})");
    QRegularExpressionMatch dateMatch = dateRe.match(pathLower);
    if (!dateMatch.hasMatch()) return QString();

    QString dateStr = dateMatch.captured(1); // "20251204"
    QString timeStr = dateMatch.captured(2); // "015841"

    QDate centerDate = QDate::fromString(dateStr, "yyyyMMdd");
    if (!centerDate.isValid()) return QString();

    QDate prevDate = centerDate.addDays(-1);
    QDate nextDate = centerDate.addDays(1);

    // 3. 读取精轨缓存文件夹
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    QString cacheDir;
    if (!projName.isEmpty()) {
        cacheDir = settings.value(QString("Orbit/ProjectDir_%1").arg(projName), "").toString();
    }
    if (cacheDir.isEmpty()) {
        cacheDir = settings.value("Orbit/LastMatchDir", "").toString();
    }
    if (cacheDir.isEmpty() || !QDir(cacheDir).exists()) return QString();

    QDir globalDir(cacheDir);

    // A. 尝试精确定位 POEORB (精密轨道)
    QString poePattern = QString("*%1*V%2*_%3*.EOF")
                            .arg(platform)
                            .arg(prevDate.toString("yyyyMMdd"))
                            .arg(nextDate.toString("yyyyMMdd"));
    QStringList poeMatches = globalDir.entryList(QStringList{poePattern}, QDir::Files);
    if (!poeMatches.isEmpty())
    {
        return globalDir.absoluteFilePath(poeMatches.first());
    }

    // B. 如果未找到 POEORB，尝试匹配包含成像时刻的 RESORB (重构轨道)
    QString resorbPattern = QString("*%1*RESORB*V%2*.EOF").arg(platform).arg(dateStr);
    QStringList resorbMatches = globalDir.entryList(QStringList{resorbPattern}, QDir::Files);
    for (const QString& resFile : resorbMatches)
    {
        QRegularExpression valRe("V(\\d{8}T\\d{6})_(\\d{8}T\\d{6})");
        QRegularExpressionMatch valMatch = valRe.match(resFile);
        if (valMatch.hasMatch())
        {
            QDateTime startVal = QDateTime::fromString(valMatch.captured(1), "yyyyMMddTHHmmss");
            QDateTime endVal = QDateTime::fromString(valMatch.captured(2), "yyyyMMddTHHmmss");
            QDateTime imgTime = QDateTime::fromString(dateStr + "T" + timeStr, "yyyyMMddTHHmmss");
            if (imgTime >= startVal && imgTime <= endVal)
            {
                return globalDir.absoluteFilePath(resFile);
            }
        }
    }

    return QString();
}

void import_sentinel::on_buttonBox_2_accepted()
{
    //检查导入文件list是否为空
    if (ui->listWidget->count() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("导入图像文件为空！"));
        return;
    }
    //检查目标节点名
    if (ui->lineEdit_dst_node_2->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("目标节点名为空！"));
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
        if (ui->lineEdit_dst_node_2->text() == project->child(i)->text() && project->child(i, 1)->text() != "complex-0.0")
        {
            same_name_node = true;
        }
    }
    if (same_name_node)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("目标节点已存在，且和导入数据级别不同，请重命名！"));
        return;
    }

    //根据原始文件名及极化方式和子带生成导入文件名称
    vector<QString> original_namelist;
    vector<QString> import_namelist;
    if (!generate_name(ui->listWidget, original_namelist, import_namelist)) return;

    // Ensure previous threads are released
    if (import_sentinel_thread_2) {
        import_sentinel_thread_2->thread()->quit();
        import_sentinel_thread_2->thread()->wait();
    }

    import_sentinel_thread_2 = new Sentinel1ImportWorker;
    QThread* thread2 = new QThread(this);
    import_sentinel_thread_2->moveToThread(thread2);
    ui->progressBar_2->setValue(0);
    ui->progressBar_2->show();
    connect(import_sentinel_thread_2, &Sentinel1ImportWorker::updateProcess, this, &import_sentinel::updateProcess);
    connect(thread2, &QThread::finished, import_sentinel_thread_2, &Sentinel1ImportWorker::deleteLater);
    connect(thread2, &QThread::finished, thread2, &QThread::deleteLater);
    connect(import_sentinel_thread_2, &Sentinel1ImportWorker::endProcess, this, &import_sentinel::endProcess);
    connect(this, &QWidget::destroyed, this, &import_sentinel::StopThread);
    connect(ui->buttonBox_2, &QDialogButtonBox::rejected, this, &import_sentinel::StopThread);// , Qt::QueuedConnection);
    connect(import_sentinel_thread_2, &Sentinel1ImportWorker::outputsGenerated, this,
        [this, projectName = ui->ComboBox_dst_project_2->currentText(), savePath = save_path](const QString& dstNode, const QStringList& names, const QStringList& paths, const QString& dataType, const QString& format) {
            if (ImportOutputPersistence::persist(copy, projectName, savePath, dstNode, names, paths, dataType, format)) TransitModel(copy);
        });
    thread2->start();

    // 构造 ImportTask 列表
    std::vector<ImportTask> tasks;
    for (size_t i = 0; i < original_namelist.size(); ++i) {
        ImportTask task;
        task.filename = import_namelist[i];
        QStringList args = QStringList{ original_namelist[i], subswath, polarization };
        
        // 自动发现和匹配精轨文件 (.EOF)
        QString matchedEof = findMatchedEofFile(original_namelist[i], ui->ComboBox_dst_project_2->currentText());
        if (!matchedEof.isEmpty())
        {
            args.append(matchedEof);
        }
        
        task.arguments = args;
        tasks.push_back(task);
    }

    QMetaObject::invokeMethod(import_sentinel_thread_2, "import_patch",
        Q_ARG(QString, this->save_path),
        Q_ARG(std::vector<ImportTask>, tasks),
        Q_ARG(QString, ui->lineEdit_dst_node_2->text()));
    ChangeVision(false);
}

void import_sentinel::on_buttonBox_accepted()
{
    if (ui->lineEdit_dst_node->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("目标节点为空！"));
        return;
    }
    if (ui->lineEdit_manifest_file->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("输入图像文件为空！"));
        return;
    }
    if (ui->LineEdit_dst_filename->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("导入图像文件名为空！"));
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
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("目标节点已存在，且和导入数据级别不同，请重命名！"));
        return;
    }

    // Ensure previous threads are released
    if (import_sentinel_thread) {
        import_sentinel_thread->thread()->quit();
        import_sentinel_thread->thread()->wait();
    }

    import_sentinel_thread = new Sentinel1ImportWorker;
    QThread* thread = new QThread(this);
    import_sentinel_thread->moveToThread(thread);
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(import_sentinel_thread, &Sentinel1ImportWorker::updateProcess, this, &import_sentinel::updateProcess);
    connect(thread, &QThread::finished, import_sentinel_thread, &Sentinel1ImportWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    connect(import_sentinel_thread, &Sentinel1ImportWorker::endProcess, this, &import_sentinel::endProcess);
    connect(this, &QWidget::destroyed, this, &import_sentinel::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &import_sentinel::StopThread);// , Qt::QueuedConnection);
    connect(import_sentinel_thread, &Sentinel1ImportWorker::outputsGenerated, this,
        [this, projectName = ui->comboBox_dst_project->currentText(), savePath = save_path](const QString& dstNode, const QStringList& names, const QStringList& paths, const QString& dataType, const QString& format) {
            if (ImportOutputPersistence::persist(copy, projectName, savePath, dstNode, names, paths, dataType, format)) TransitModel(copy);
        });
    thread->start();

    // 构造 ImportTask 列表（单文件导入）
    std::vector<ImportTask> tasks;
    ImportTask task;
    task.filename = ui->LineEdit_dst_filename->text();
    task.arguments = QStringList{ ui->lineEdit_manifest_file->text(),
        ui->ComboBox_subswath->currentText(), ui->ComboBox_polarization->currentText(),
        ui->lineEdit_POD->text() };
    tasks.push_back(task);

    QMetaObject::invokeMethod(import_sentinel_thread, "import_patch",
        Q_ARG(QString, this->save_path),
        Q_ARG(std::vector<ImportTask>, tasks),
        Q_ARG(QString, ui->lineEdit_dst_node->text()));
    ChangeVision(false);
    
}
void import_sentinel::on_buttonBox_2_rejected()
{
    close();
}
void import_sentinel::on_buttonBox_rejected()
{
    close();
}

void import_sentinel::on_ComboBox_subswath_currentIndexChanged()
{
    if (date.length() > 0)
    {
        ui->LineEdit_dst_filename->setText(date + QString("_") + ui->ComboBox_subswath->currentText() + ui->ComboBox_polarization->currentText());
    }
    else
    {
        ui->LineEdit_dst_filename->setText(ui->ComboBox_subswath->currentText() + ui->ComboBox_polarization->currentText());
    }
    
}

void import_sentinel::on_ComboBox_subswath_2_currentIndexChanged()
{
    if (ui->ComboBox_subswath_2->count() > 0)
    {
        if (ui->ComboBox_subswath_2->currentIndex() == 0) subswath = "iw1";
        else if (ui->ComboBox_subswath_2->currentIndex() == 1) subswath = "iw2";
        else subswath = "iw3";
    }
}

void import_sentinel::on_ComboBox_polarization_currentIndexChanged()
{
    if (date.length() > 0)
    {
        ui->LineEdit_dst_filename->setText(date + QString("_") + ui->ComboBox_subswath->currentText() + ui->ComboBox_polarization->currentText());
    }
    else
    {
        ui->LineEdit_dst_filename->setText(ui->ComboBox_subswath->currentText() + ui->ComboBox_polarization->currentText());
    }
}

void import_sentinel::on_ComboBox_polarization_2_currentIndexChanged()
{
    if (ui->ComboBox_polarization_2->count() > 0)
    {
        if (ui->ComboBox_polarization_2->currentIndex() == 0) polarization = "vv";
        else polarization = "vh";
    }
}
