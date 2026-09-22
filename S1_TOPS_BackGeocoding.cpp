#include"S1_TOPS_BackGeocoding.h"
#include"icon_source.h"
#include "IApplicationInterface.h"
#include "NodeUtils.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<QThread>
#include<FormatConversion.h>
#include "tinyxml.h"
#include<qmessagebox.h>
#include<QFile>
#include<QDir>
#ifdef _DEBUG
#pragma comment(lib, "FormatConversion_d.lib")
#else
//#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#endif
S1_TOPS_BackGeocoding::S1_TOPS_BackGeocoding(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::S1TopsBackGeocoding),
    copy(nullptr),
    S1_TOPS_BackGeocoding_thread(nullptr),
    image_number(0)
{
    ui->setupUi(this);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
    ui->checkBox->setChecked(true);
}
S1_TOPS_BackGeocoding::~S1_TOPS_BackGeocoding()
{
    StopThread();
    if (copy && ui->comboBox->count() > 0)
    {
        for (int i = 0; i < ui->comboBox->count(); i++)
        {
            QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox->itemText(i));
            if (project)
                project->setStatusTip(NOT_IN_PROCESS);
        }
    }
    emit sendCopy(copy);
    S1_TOPS_BackGeocoding_thread = NULL;
    delete ui;
    ui = nullptr;
}

void S1_TOPS_BackGeocoding::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QStringLiteral("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}
void S1_TOPS_BackGeocoding::endProcess()
{
    if (S1_TOPS_BackGeocoding_thread && S1_TOPS_BackGeocoding_thread->thread()) {
        S1_TOPS_BackGeocoding_thread->thread()->quit();
        S1_TOPS_BackGeocoding_thread->thread()->wait();
    }
    ui->progressBar->hide();
    this->close();
}
void S1_TOPS_BackGeocoding::endThread()
{
    if (S1_TOPS_BackGeocoding_thread && S1_TOPS_BackGeocoding_thread->thread()) {
        S1_TOPS_BackGeocoding_thread->thread()->quit();
        S1_TOPS_BackGeocoding_thread->thread()->wait();
    }
}
void S1_TOPS_BackGeocoding::StopThread()
{
    S1TopsBackGeocodingWorker* worker = S1_TOPS_BackGeocoding_thread.data();
    if (worker)
    {
        QThread* workerThread = worker->thread();
        worker->requestCancel();
        if (workerThread && workerThread->isRunning())
        {
            workerThread->requestInterruption();
            workerThread->quit();
            if (QThread::currentThread() != workerThread)
                workerThread->wait();
        }
    }
}
void S1_TOPS_BackGeocoding::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    emit sendCopy(model);
}

void S1_TOPS_BackGeocoding::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox->setDisabled(0);
        ui->comboBox_2->setDisabled(0);
        ui->fileedit->setDisabled(0);
        ui->comboBox_3->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
        ui->checkBox->setDisabled(0);
    }
    else
    {
        ui->comboBox->setDisabled(1);
        ui->comboBox_2->setDisabled(1);
        ui->fileedit->setDisabled(1);
        ui->comboBox_3->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
        ui->checkBox->setDisabled(1);
    }


}

void S1_TOPS_BackGeocoding::ShowProjectList(QStandardItemModel* model)
{
    XMLFile xmldoc;
    TiXmlElement* pnode = NULL, * pchild = NULL;
    int ret, count = 0;
    this->copy = model;
    ui->comboBox->clear();
    ui->comboBox_2->clear();
    ui->comboBox_3->clear();
    save_path.clear();
    projectFile.clear();
    if (!copy || copy->rowCount() < 1 || copy->columnCount() < 2)
    {
        QMessageBox::warning(this, "Warning!", QStringLiteral("工程数据不完整，请重新打开工程。"));
        return;
    }
    for (int i = 0; i < copy->rowCount(); i++)
    {
        QStandardItem* nameItem = copy->item(i, 0);
        QStandardItem* pathItem = copy->item(i, 1);
        if (!nameItem || !pathItem)
            continue;
        QString tmpProjectFile = pathItem->text() + "/" + nameItem->text();
        ret = xmldoc.XMLFile_load(tmpProjectFile.toStdString().c_str());
        if (ret < 0) continue;
        ret = xmldoc.find_node("DataNode", pnode);
        if (ret < 0) continue;
        while (pnode)
        {
            ret = xmldoc._find_node(pnode, "Sensor", pchild);
            const char* sensorText = (ret == 0 && pchild) ? pchild->GetText() : nullptr;
            if (sensorText && strcmp(sensorText, "sentinel") == 0)
            {
                ui->comboBox->addItem(nameItem->text());
                nameItem->setStatusTip(IN_PROCESS);
                this->save_path = pathItem->text();
                this->projectFile = this->save_path + "/" + nameItem->text();
                ui->comboBox->setCurrentIndex(count++);
                break;
            }
            pnode = pnode->NextSiblingElement();
        }
    }

    if (ui->comboBox->count() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("无可处理数据，请先导入数据！"));
        this->deleteLater();
        return;
    }
    //工程文件
    ret = xmldoc.XMLFile_load(this->projectFile.toStdString().c_str());
    if (ret < 0) return;
    ret = xmldoc.find_node("DataNode", pnode);
    if (ret < 0) return;
    while (pnode)
    {
        ret = xmldoc._find_node(pnode, "Sensor", pchild);
        const char* sensorText = (ret == 0 && pchild) ? pchild->GetText() : nullptr;
        const char* nodeName = pnode->Attribute("name");
        if (sensorText && nodeName && strcmp(sensorText, "sentinel") == 0)
        {
            ui->comboBox_2->addItem(nodeName);
        }
        pnode = pnode->NextSiblingElement();
    }
    if (ui->comboBox_2->count() < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("未检测到可处理数据！"));
        this->deleteLater();
        return;
    }
    ui->comboBox_2->setCurrentIndex(0);

    //初始化图像数据节点
    ret = xmldoc.find_node("DataNode", pnode);
    if (ret < 0) return;
    while (pnode)
    {
        ret = xmldoc._find_node(pnode, "Sensor", pchild);
        const char* sensorText = (ret == 0 && pchild) ? pchild->GetText() : nullptr;
        if (sensorText && strcmp(sensorText, "sentinel") == 0)
        {
            break;
        }
        pnode = pnode->NextSiblingElement();
    }
    if (!pnode) return;
    ret = xmldoc._find_node(pnode, "Data", pchild);
    if (ret < 0) return;
    while (pchild)
    {
        if (strcmp(pchild->Value(), "Data") != 0) break;
        ret = xmldoc._find_node(pchild, "Data_Name", pnode); if (ret < 0 || !pnode || !pnode->GetText()) return;
        ui->comboBox_3->addItem(pnode->GetText());
        pchild = pchild->NextSiblingElement();
    }
    ui->comboBox_3->setCurrentIndex(0);
}

void S1_TOPS_BackGeocoding::on_comboBox_currentIndexChanged()
{
    if (ui->comboBox->count() != 0)
    {
        QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox->currentText());
        if (!project)
        {
            QMessageBox::warning(this, "Warning!", QStringLiteral("所选工程不存在或已被关闭，请刷新工程列表后重试。"));
            return;
        }
        QStandardItem* pathItem = copy->item(project->row(), 1);
        if (!pathItem) {
            QMessageBox::warning(this, "Warning!", QStringLiteral("当前工程路径信息缺失，请重新打开工程。"));
            return;
        }
        this->save_path = pathItem->text();
        ui->comboBox_2->clear();

        this->projectFile = this->save_path + "/" + project->text();
        XMLFile xmldoc;
        int ret = xmldoc.XMLFile_load(this->projectFile.toStdString().c_str());
        if (ret < 0) return;
        TiXmlElement* pnode = NULL, * pchild = NULL;
        ret = xmldoc.find_node("DataNode", pnode);
        if (ret < 0) return;
        while (pnode)
        {
            ret = xmldoc._find_node(pnode, "Sensor", pchild);
            const char* sensorText = (ret == 0 && pchild) ? pchild->GetText() : nullptr;
            const char* nodeName = pnode->Attribute("name");
            if (sensorText && nodeName && strcmp(sensorText, "sentinel") == 0)
            {
                ui->comboBox_2->addItem(nodeName);
            }
            pnode = pnode->NextSiblingElement();
        }
        if (ui->comboBox_2->count() == 0)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("未检测到可处理数据！"));
            this->deleteLater();
            return;
        }
        ui->comboBox_2->setCurrentIndex(0);

        //初始化图像数据节点
        ui->comboBox_3->clear();
        ret = xmldoc.find_node("DataNode", pnode);
        if (ret < 0) return;
        while (pnode)
        {
            ret = xmldoc._find_node(pnode, "Sensor", pchild);
            const char* sensorText = (ret == 0 && pchild) ? pchild->GetText() : nullptr;
            if (sensorText && strcmp(sensorText, "sentinel") == 0)
            {
                break;
            }
            pnode = pnode->NextSiblingElement();
        }
        if (!pnode) return;
        ret = xmldoc._find_node(pnode, "Data", pchild);
        if (ret < 0) return;
        while (pchild)
        {
            if (strcmp(pchild->Value(), "Data") != 0) break;
            ret = xmldoc._find_node(pchild, "Data_Name", pnode);
            if (ret < 0 || !pnode || !pnode->GetText()) return;
            ui->comboBox_3->addItem(pnode->GetText());
            pchild = pchild->NextSiblingElement();
        }
        ui->comboBox_3->setCurrentIndex(0);
    }
}

void S1_TOPS_BackGeocoding::on_comboBox_2_currentIndexChanged()
{
    if (ui->comboBox_2->count() != 0)
    {
        QStandardItem* project = NodeUtils::findFirstModelItem(copy, ui->comboBox->currentText());
        if (!project)
        {
            QMessageBox::warning(this, "Warning!", QStringLiteral("所选工程不存在或已被关闭，请刷新工程列表后重试。"));
            return;
        }
        QModelIndex pro_index = copy->indexFromItem(project);
        for (int i = 0; i < project->rowCount(); i++)
        {
            QStandardItem* childItem = project->child(i, 0);
            if (childItem && childItem->text() == ui->comboBox_2->currentText())
                image_number = childItem->rowCount();
        }

        TiXmlElement* pnode = NULL, * pchild = NULL;
        XMLFile xmldoc;
        int ret = xmldoc.XMLFile_load(this->projectFile.toStdString().c_str());
        if (ret < 0) return;
        //初始化图像数据节点
        ui->comboBox_3->clear();
        ret = xmldoc.find_node("DataNode", pnode);
        if (ret < 0) return;
        while (pnode)
        {
            ret = xmldoc._find_node(pnode, "Sensor", pchild);
            const char* sensorText = (ret == 0 && pchild) ? pchild->GetText() : nullptr;
            const char* nodeName = pnode->Attribute("name");
            if (sensorText && nodeName && strcmp(sensorText, "sentinel") == 0 &&
                QString::fromUtf8(nodeName) == ui->comboBox_2->currentText())
            {
                break;
            }
            pnode = pnode->NextSiblingElement();
        }
        if (!pnode) return;
        ret = xmldoc._find_node(pnode, "Data", pchild);
        if (ret < 0) return;
        while (pchild)
        {
            if (strcmp(pchild->Value(), "Data") != 0) break;
            ret = xmldoc._find_node(pchild, "Data_Name", pnode);
            if (ret < 0 || !pnode || !pnode->GetText()) return;
            ui->comboBox_3->addItem(pnode->GetText());
            pchild = pchild->NextSiblingElement();
        }
        ui->comboBox_3->setCurrentIndex(0);
    }

}

void S1_TOPS_BackGeocoding::on_comboBox_3_currentIndexChanged()
{

}

void S1_TOPS_BackGeocoding::on_buttonBox_accepted()
{
    if (ui->comboBox_2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无可处理数据！"));
        return;
    }
    if (ui->fileedit->text().isEmpty())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请输入存放后向地理编码处理结果文件的文件夹名称！"));
        return;
    }
    bool bFlag = ui->fileedit->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请注意文件夹名称应当为数字、字母及下划线的组合！"));
        return;
    }
    int index = ui->comboBox_3->currentIndex() + 1;
    this->image_number = ui->comboBox_3->count();
    if (image_number < 2) return;
    if (!bFlag || index < 1)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("主图像序号应为正整数！"));
        return;
    }
    else if (index > image_number)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("主图像索引超出范围，请确认该数字不超过节点下总图像数量！"));
        return;
    }


    QStringList inputPaths;
    if (copy) {
        const QList<QStandardItem*> projects = copy->findItems(ui->comboBox->currentText());
        if (!projects.isEmpty()) {
            QStandardItem* project = projects.first();
            for (int i = 0; i < project->rowCount(); ++i) {
                QStandardItem* node = project->child(i, 0);
                if (!node || node->text() != ui->comboBox_2->currentText()) {
                    continue;
                }
                for (int j = 0; j < node->rowCount(); ++j) {
                    QStandardItem* pathItem = node->child(j, 1);
                    if (pathItem && !pathItem->text().isEmpty()) {
                        inputPaths.append(pathItem->text());
                    }
                }
                break;
            }
        }
    }
    if (inputPaths.size() != image_number) {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("Unable to snapshot all input image paths."));
        return;
    }

    S1_TOPS_BackGeocoding_thread = new S1TopsBackGeocodingWorker;
    S1_TOPS_BackGeocoding_thread->moveToThread(new QThread(this));
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(this, &S1_TOPS_BackGeocoding::operate, S1_TOPS_BackGeocoding_thread, &S1TopsBackGeocodingWorker::S1_TOPS_BackGeocoding, Qt::QueuedConnection);
    connect(S1_TOPS_BackGeocoding_thread, &S1TopsBackGeocodingWorker::updateProcess, this, &S1_TOPS_BackGeocoding::updateProcess);
    connect(S1_TOPS_BackGeocoding_thread->thread(), &QThread::finished, S1_TOPS_BackGeocoding_thread, &S1TopsBackGeocodingWorker::deleteLater);
    connect(S1_TOPS_BackGeocoding_thread, &S1TopsBackGeocodingWorker::endProcess, this, &S1_TOPS_BackGeocoding::endProcess);
    connect(S1_TOPS_BackGeocoding_thread, &S1TopsBackGeocodingWorker::errorProcess, this,
        [this](const QString& error) {
            QMessageBox::critical(this, tr("Back-Geocoding Error"), error);
            endProcess();
        });
    connect(S1_TOPS_BackGeocoding_thread, &S1TopsBackGeocodingWorker::cancelled, this,
        [this](const QStringList& cleanupFailures) {
            if (!cleanupFailures.isEmpty()) {
                QMessageBox::warning(this, tr("Back-Geocoding Cancelled"),
                                     tr("任务已取消，但部分临时文件未能删除：\n%1")
                                         .arg(cleanupFailures.join('\n')));
            }
            endProcess();
        });
    connect(S1_TOPS_BackGeocoding_thread, &S1TopsBackGeocodingWorker::registrationFinished, this,
        [this](const QStringList& paths, const QString& dstNode, const QString& dstProject,
               const QString& savePath, int masterIndex, bool hasQualityWarning, const QStringList& qualityWarnings) {
            onRegistrationFinished(paths, dstNode, dstProject, savePath, masterIndex);
            if (hasQualityWarning && !qualityWarnings.isEmpty()) {
                QMessageBox::warning(this, tr("Back-Geocoding Warning"), qualityWarnings.join('\n'));
            }
        });
    connect(this, &QWidget::destroyed, this, &S1_TOPS_BackGeocoding::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &S1_TOPS_BackGeocoding::StopThread);// , Qt::QueuedConnection);
    S1_TOPS_BackGeocoding_thread->thread()->start();
    ChangeVision(false);
    emit operate(index, this->save_path,
        ui->comboBox->currentText(), 
        ui->fileedit->text(),
        inputPaths,
        ui->checkBox->isChecked()
    );
}

void S1_TOPS_BackGeocoding::on_buttonBox_rejected()
{
    this->close();
}

void S1_TOPS_BackGeocoding::onRegistrationFinished(
    const QStringList& regisH5Paths,
    const QString& dstNode,
    const QString& dstProject,
    const QString& savePath,
    int masterIndex
)
{
    // 1. 更新全局 XML 并保存
    IApplicationInterface* iface = NodeUtils::getProjectContext(this);
    XMLFile* xml = iface ? iface->projectXml() : nullptr;
    if (xml)
    {
        TiXmlElement* root = nullptr;
        xml->get_root(root);
        if (root)
        {
            for (int i = 0; i < regisH5Paths.size(); i++)
            {
                QFileInfo fileinfo(regisH5Paths.at(i));
                QString relativePath = QString("/%1/%2").arg(dstNode).arg(fileinfo.fileName());
                QString dataName = fileinfo.baseName();

                // 查找是否已存在该 DataNode
                TiXmlElement* dataNodeElem = nullptr;
                for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; p = p->NextSiblingElement()) {
                    const char* nameAttr = p->Attribute("name");
                    if (nameAttr && strcmp(p->Value(), "DataNode") == 0 && QString(nameAttr) == dstNode) {
                        dataNodeElem = p;
                        break;
                    }
                }

                if (!dataNodeElem) {
                    // 创建新的 DataNode
                    dataNodeElem = new TiXmlElement("DataNode");
                    dataNodeElem->SetAttribute("name", dstNode.toStdString().c_str());
                    dataNodeElem->SetAttribute("data_count", "1");
                    dataNodeElem->SetAttribute("data_processing", "coregistration");
                    dataNodeElem->SetAttribute("rank", "complex-2.0");

                    int index = 1;
                    TiXmlElement* root_child = root->FirstChildElement();
                    if (root_child) {
                        root_child = root_child->NextSiblingElement(); // 略过 project_info
                    }

                    TiXmlElement* insertBeforeNode = nullptr;
                    for (TiXmlElement* p = root_child; p != nullptr; p = p->NextSiblingElement(), index++) {
                        const char* rankAttr = p->Attribute("rank");
                        if (rankAttr && (strcmp(rankAttr, "complex-0.0") == 0 ||
                                         strcmp(rankAttr, "complex-1.0") == 0 ||
                                         strcmp(rankAttr, "complex-2.0") == 0)) {
                            continue;
                        } else {
                            insertBeforeNode = p;
                            break;
                        }
                    }
                    dataNodeElem->SetAttribute("index", QString::number(index).toStdString().c_str());

                    TiXmlElement* dataElem = new TiXmlElement("Data");

                    TiXmlElement* dataNameNode = new TiXmlElement("Data_Name");
                    dataNameNode->LinkEndChild(new TiXmlText(dataName.toStdString().c_str()));
                    dataElem->LinkEndChild(dataNameNode);

                    TiXmlElement* dataRankNode = new TiXmlElement("Data_Rank");
                    dataRankNode->LinkEndChild(new TiXmlText("complex-2.0"));
                    dataElem->LinkEndChild(dataRankNode);

                    TiXmlElement* dataIndexNode = new TiXmlElement("Data_Index");
                    dataIndexNode->LinkEndChild(new TiXmlText("1"));
                    dataElem->LinkEndChild(dataIndexNode);

                    TiXmlElement* dataPathNode = new TiXmlElement("Data_Path");
                    dataPathNode->LinkEndChild(new TiXmlText(relativePath.toStdString().c_str()));
                    dataElem->LinkEndChild(dataPathNode);

                    TiXmlElement* rowOffsetNode = new TiXmlElement("Row_Offset");
                    rowOffsetNode->LinkEndChild(new TiXmlText("0"));
                    dataElem->LinkEndChild(rowOffsetNode);

                    TiXmlElement* colOffsetNode = new TiXmlElement("Col_Offset");
                    colOffsetNode->LinkEndChild(new TiXmlText("0"));
                    dataElem->LinkEndChild(colOffsetNode);

                    dataNodeElem->LinkEndChild(dataElem);

                    TiXmlElement* paramsElem = new TiXmlElement("Data_Processing_Parameters");
                    TiXmlElement* masterImageElem = new TiXmlElement("master_image");
                    masterImageElem->LinkEndChild(new TiXmlText(QString::number(masterIndex).toStdString().c_str()));
                    paramsElem->LinkEndChild(masterImageElem);
                    dataNodeElem->LinkEndChild(paramsElem);

                    if (insertBeforeNode) {
                        root->InsertBeforeChild(insertBeforeNode, *dataNodeElem);
                        delete dataNodeElem;

                        for (TiXmlElement* p = insertBeforeNode; p != nullptr; p = p->NextSiblingElement()) {
                            index++;
                            p->SetAttribute("index", QString::number(index).toStdString().c_str());
                        }
                    } else {
                        root->LinkEndChild(dataNodeElem);
                    }
                } else {
                    // 成果节点已存在，追加新的 Data 元素
                    const char* countAttr = dataNodeElem->Attribute("data_count");
                    int count = countAttr ? QString(countAttr).toInt() : 0;
                    count++;
                    dataNodeElem->SetAttribute("data_count", QString::number(count).toStdString().c_str());

                    TiXmlElement* lastChildNode = dataNodeElem->LastChild() ? dataNodeElem->LastChild()->ToElement() : nullptr;

                    TiXmlElement* dataElem = new TiXmlElement("Data");

                    TiXmlElement* dataNameNode = new TiXmlElement("Data_Name");
                    dataNameNode->LinkEndChild(new TiXmlText(dataName.toStdString().c_str()));
                    dataElem->LinkEndChild(dataNameNode);

                    TiXmlElement* dataRankNode = new TiXmlElement("Data_Rank");
                    dataRankNode->LinkEndChild(new TiXmlText("complex-2.0"));
                    dataElem->LinkEndChild(dataRankNode);

                    TiXmlElement* dataIndexNode = new TiXmlElement("Data_Index");
                    dataIndexNode->LinkEndChild(new TiXmlText(QString::number(count).toStdString().c_str()));
                    dataElem->LinkEndChild(dataIndexNode);

                    TiXmlElement* dataPathNode = new TiXmlElement("Data_Path");
                    dataPathNode->LinkEndChild(new TiXmlText(relativePath.toStdString().c_str()));
                    dataElem->LinkEndChild(dataPathNode);

                    TiXmlElement* rowOffsetNode = new TiXmlElement("Row_Offset");
                    rowOffsetNode->LinkEndChild(new TiXmlText("0"));
                    dataElem->LinkEndChild(rowOffsetNode);

                    TiXmlElement* colOffsetNode = new TiXmlElement("Col_Offset");
                    colOffsetNode->LinkEndChild(new TiXmlText("0"));
                    dataElem->LinkEndChild(colOffsetNode);

                    if (lastChildNode) {
                        dataNodeElem->InsertBeforeChild(lastChildNode, *dataElem);
                        delete dataElem;
                    } else {
                        dataNodeElem->LinkEndChild(dataElem);
                    }
                }
            }
            xml->XMLFile_save(this->projectFile.toStdString().c_str());
        }
    }

    // 2. 主线程挂载项目树 UI
    QStandardItemModel* model = this->copy;
    if (model)
    {
        QList<QStandardItem*> foundProjects = model->findItems(dstProject);
        if (!foundProjects.isEmpty())
        {
            QStandardItem* project = foundProjects.first();

            /*建立配准根节点*/
            QStandardItem* regis = NULL;
            for (int i = 0; i < project->rowCount(); i++)
            {
                QStandardItem* childItem = project->child(i, 0);
                if (childItem && childItem->text() == dstNode)
                {
                    regis = childItem;
                    break;
                }
            }

            if (!regis)
            {
                regis = new QStandardItem(dstNode);
                regis->setToolTip(dstProject);
                int insert = 0;
                for (; insert < project->rowCount(); insert++)
                {
                    QStandardItem* rankItem = project->child(insert, 1);
                    if (!rankItem)
                        break;
                    if (rankItem->text().compare("complex-0.0") == 0 ||
                        rankItem->text().compare("complex-1.0") == 0 ||
                        rankItem->text().compare("complex-2.0") == 0)
                        continue;
                    else
                        break;
                }
                regis->setIcon(QIcon(FOLDER_ICON));
                project->insertRow(insert, regis);
                QStandardItem* regis_Rank = new QStandardItem("complex-2.0");
                project->setChild(insert, 1, regis_Rank);
            }

            /*添加图像到model中*/
            for (int i = 0; i < regisH5Paths.size(); i++)
            {
                QFileInfo fileinfo(regisH5Paths.at(i));
                QString regis_name = fileinfo.baseName();
                QStandardItem* item_img = NULL;
                for (int j = 0; j < regis->rowCount(); j++)
                {
                    QStandardItem* childItem = regis->child(j, 0);
                    if (childItem && childItem->text() == regis_name)
                    {
                        item_img = childItem;
                        break;
                    }
                }

                if (!item_img)
                {
                    QStandardItem* regis_images_name = new QStandardItem(regis_name);
                    regis_images_name->setToolTip("complex");
                    QStandardItem* regis_images_path = new QStandardItem(fileinfo.absoluteFilePath());
                    regis_images_name->setIcon(QIcon(IMAGEDATA_ICON));
                    regis->appendRow(regis_images_name);
                    regis->setChild(regis->rowCount() - 1, 1, regis_images_path);
                }
                else
                {
                    regis->setChild(item_img->row(), 1, new QStandardItem(fileinfo.absoluteFilePath()));
                }
            }
        }
    }
}
