#include"Geocoding.h"
#include"ui_Geocoding.h"
#include"icon_source.h"
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<qmessagebox.h>
#include<QFile>
#include<QFileInfo>
#include<QDir>
#include<QFileDialog>
#include<QLineEdit>
#include<QPushButton>
#include "NodeUtils.h"
#include "FormatConversion.h"
#include "tinyxml.h"
#include<QThread>
Geocoding::Geocoding(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::Geocoding)
{
    ui->setupUi(this);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setMaximum(100);
    ui->progressBar->setHidden(1);
    ui->progressBar_2->setMinimum(0);
    ui->progressBar_2->setMaximum(100);
    ui->progressBar_2->setHidden(1);
    ui->spinBox_multi_az->setValue(1);
    ui->spinBox_multi_az->setMinimum(1);
    ui->spinBox_multi_rg->setValue(1);
    ui->spinBox_multi_rg->setMinimum(1);

    // Left panel DEM (Interferogram Geocoding)
    QHBoxLayout* demLayout1 = new QHBoxLayout();
    m_demPathLabel1 = new QLabel(QStringLiteral("DEM路径:"), this);
    m_demPathLabel1->setFixedWidth(80);
    
    m_demPathEdit1 = new QLineEdit(this);
    m_demPathEdit1->setObjectName("demPathEdit");
    m_demPathEdit1->clear();
    m_demPathEdit1->setPlaceholderText(QStringLiteral("选择DEM数据 (*.h5, *.tiff)..."));

    m_demBrowseBtn1 = new QPushButton(QStringLiteral("浏览..."), this);
    m_demBrowseBtn1->setFixedWidth(60);
    
    demLayout1->addWidget(m_demPathLabel1);
    demLayout1->addWidget(m_demPathEdit1);
    demLayout1->addWidget(m_demBrowseBtn1);
    
    if (ui->verticalLayout_2) {
        int index1 = ui->verticalLayout_2->indexOf(ui->buttonBox);
        if (index1 != -1) {
            ui->verticalLayout_2->insertLayout(index1, demLayout1);
        } else {
            ui->verticalLayout_2->addLayout(demLayout1);
        }
    }

    // Right panel DEM (SAR Image Geocoding)
    QHBoxLayout* demLayout2 = new QHBoxLayout();
    m_demPathLabel2 = new QLabel(QStringLiteral("DEM路径:"), this);
    m_demPathLabel2->setFixedWidth(80);
    
    m_demPathEdit2 = new QLineEdit(this);
    m_demPathEdit2->setObjectName("demPathEdit");
    m_demPathEdit2->clear();
    m_demPathEdit2->setPlaceholderText(QStringLiteral("选择DEM数据 (*.h5, *.tiff)..."));

    m_demBrowseBtn2 = new QPushButton(QStringLiteral("浏览..."), this);
    m_demBrowseBtn2->setFixedWidth(60);
    
    demLayout2->addWidget(m_demPathLabel2);
    demLayout2->addWidget(m_demPathEdit2);
    demLayout2->addWidget(m_demBrowseBtn2);
    
    if (ui->verticalLayout_4) {
        int index2 = ui->verticalLayout_4->indexOf(ui->buttonBox_2);
        if (index2 != -1) {
            ui->verticalLayout_4->insertLayout(index2, demLayout2);
        } else {
            ui->verticalLayout_4->addLayout(demLayout2);
        }
    }

    // Connections to sync both edits
    auto onBrowse1 = [this]() {
        QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择DEM数据"), "", "DEM Files (*.h5 *.tiff *.tif)");
        if (!file.isEmpty()) {
            m_demPathEdit1->setText(file);
            m_demPathEdit2->setText(file);
        }
    };
    connect(m_demBrowseBtn1, &QPushButton::clicked, this, onBrowse1);
    
    auto onBrowse2 = [this]() {
        QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择DEM数据"), "", "DEM Files (*.h5 *.tiff *.tif)");
        if (!file.isEmpty()) {
            m_demPathEdit1->setText(file);
            m_demPathEdit2->setText(file);
        }
    };
    connect(m_demBrowseBtn2, &QPushButton::clicked, this, onBrowse2);

    connect(m_demPathEdit1, &QLineEdit::editingFinished, this, [this]() {
        QString text = m_demPathEdit1->text().trimmed();
        m_demPathEdit2->setText(text);
    });

    connect(m_demPathEdit2, &QLineEdit::editingFinished, this, [this]() {
        QString text = m_demPathEdit2->text().trimmed();
        m_demPathEdit1->setText(text);
    });
}
Geocoding::~Geocoding()
{
    if (copy)
    {
        for (int i = 0; i < ui->comboBox_project1->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_project1->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_project1->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
        for (int i = 0; i < ui->comboBox_project2->count(); i++)
        {
            if (!copy->findItems(ui->comboBox_project2->itemText(i)).isEmpty())
                copy->findItems(ui->comboBox_project2->itemText(i))[0]->setStatusTip(NOT_IN_PROCESS);
        }
    }
    emit sendCopy(copy);
    Geocoding_thread = NULL;
}

void Geocoding::updateProcess(int value, QString information)
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

bool Geocoding::buildInputSnapshot(QStandardItem* project, const QString& srcNodeName, int type,
    const QString& projectPath, QStringList& inputPaths, QString& productLevel, int& masterIndex) const
{
    inputPaths.clear();
    productLevel.clear();
    masterIndex = 0;

    if (!project) {
        return false;
    }

    QStandardItem* sourceNode = nullptr;
    for (int i = 0; i < project->rowCount(); ++i) {
        if (project->child(i, 0) && project->child(i, 0)->text() == srcNodeName) {
            sourceNode = project->child(i, 0);
            if (project->child(i, 1)) {
                productLevel = project->child(i, 1)->text();
            }
            break;
        }
    }
    if (!sourceNode || (type == 1 && productLevel.isEmpty())) {
        return false;
    }

    for (int i = 0; i < sourceNode->rowCount(); ++i) {
        QStandardItem* pathItem = sourceNode->child(i, 1);
        if (!pathItem || pathItem->text().isEmpty()) {
            continue;
        }
        QString inputPath = pathItem->text();
        if (QDir::isRelativePath(inputPath)) {
            inputPath = QDir(projectPath).absoluteFilePath(inputPath);
        }
        inputPaths.append(inputPath);
    }
    if (inputPaths.isEmpty()) {
        return false;
    }

    if (type == 2) {
        QString xmlPath = QDir(projectPath).absoluteFilePath(project->text());
        if (!xmlPath.endsWith(".Insar", Qt::CaseInsensitive)) {
            xmlPath += ".Insar";
        }

        XMLFile xml;
        TiXmlElement* dataNode = nullptr;
        if (xml.XMLFile_load(xmlPath.toStdString().c_str()) < 0 ||
            xml.find_node_with_attribute("DataNode", "name", srcNodeName.toStdString().c_str(), dataNode) < 0 || !dataNode) {
            return false;
        }

        TiXmlElement* masterElement = nullptr;
        if (xml._find_node(dataNode, "master_image", masterElement) == 0 && masterElement && masterElement->GetText()) {
            bool ok = false;
            const int xmlMasterIndex = QString::fromUtf8(masterElement->GetText()).toInt(&ok);
            if (ok && xmlMasterIndex > 0) {
                masterIndex = xmlMasterIndex - 1;
            }
        }
        if (masterIndex < 0 || masterIndex >= inputPaths.size()) {
            return false;
        }
    }

    return true;
}

void Geocoding::persistGeneratedResults()
{
    if (m_generatedResults.isEmpty() || m_activeProjectName.isEmpty() || m_activeProjectPath.isEmpty()) {
        return;
    }

    QString xmlPath = QDir(m_activeProjectPath).absoluteFilePath(m_activeProjectName);
    if (!xmlPath.endsWith(".Insar", Qt::CaseInsensitive)) {
        xmlPath += ".Insar";
    }

    XMLFile xml;
    if (xml.XMLFile_load(xmlPath.toStdString().c_str()) < 0) {
        QMessageBox::warning(this, "Warning!", "Unable to load the project XML for geocoding output persistence.");
        m_generatedResults.clear();
        return;
    }

    for (const GeocodingFileResult& result : m_generatedResults) {
        if (xml.XMLFile_add_geocoding(result.dstNode.toStdString().c_str(), result.geocodeName.toStdString().c_str(),
                result.relativePath.toStdString().c_str(), result.rankLevel.toStdString().c_str()) < 0 ||
            (result.rankLevel == QStringLiteral("coherence-1.1") &&
             xml.XMLFile_set_coherence_semantics(
                 result.dstNode.toStdString().c_str(), result.geocodeName.toStdString().c_str(),
                 result.coherenceSemantics.toStdString().c_str()) < 0)) {
            QMessageBox::warning(this, "Warning!", "Unable to persist coherence semantics to the project XML.");
            m_generatedResults.clear();
            return;
        }
    }
    if (xml.XMLFile_save(xmlPath.toStdString().c_str()) < 0) {
        QMessageBox::warning(this, "Warning!", "Unable to save geocoding output to the project XML.");
    } else {
        emit sendCopy(copy);
    }
    m_generatedResults.clear();
}

void Geocoding::endProcess()
{
    persistGeneratedResults();
    Geocoding_thread->thread()->quit();
    Geocoding_thread->thread()->wait();
    ui->progressBar->hide();
    ui->progressBar_2->hide();
    this->close();
}
void Geocoding::endThread()
{
    Geocoding_thread->thread()->quit();
    Geocoding_thread->thread()->wait();
}
void Geocoding::StopThread()
{
    if (Geocoding_thread != NULL)
    {
        if (Geocoding_thread->thread()->isRunning())
        {
            Geocoding_thread->thread()->requestInterruption();
            Geocoding_thread->thread()->quit();
            Geocoding_thread->thread()->wait();
        }
    }

}
void Geocoding::TransitModel(QStandardItemModel* model)
{
    this->copy = model;
    emit sendCopy(model);
}

void Geocoding::ChangeVision(bool Editable)
{
    if (Editable)
    {
        ui->comboBox_project1->setDisabled(0);
        ui->comboBox_project2->setDisabled(0);
        ui->comboBox_node1->setDisabled(0);
        ui->comboBox_node2->setDisabled(0);
        ui->lineEdit_dstnode1->setDisabled(0);
        ui->lineEdit_dstnode2->setDisabled(0);
        ui->spinBox_multi_az->setDisabled(0);
        ui->spinBox_multi_rg->setDisabled(0);
        ui->buttonBox->buttons().at(0)->setDisabled(0);
        ui->buttonBox_2->buttons().at(0)->setDisabled(0);
        
        if (m_demPathLabel1) m_demPathLabel1->setEnabled(true);
        if (m_demPathEdit1) m_demPathEdit1->setEnabled(true);
        if (m_demBrowseBtn1) m_demBrowseBtn1->setEnabled(true);
        if (m_demPathLabel2) m_demPathLabel2->setEnabled(true);
        if (m_demPathEdit2) m_demPathEdit2->setEnabled(true);
        if (m_demBrowseBtn2) m_demBrowseBtn2->setEnabled(true);
    }
    else
    {
        ui->comboBox_project1->setDisabled(1);
        ui->comboBox_project2->setDisabled(1);
        ui->comboBox_node1->setDisabled(1);
        ui->comboBox_node2->setDisabled(1);
        ui->lineEdit_dstnode1->setDisabled(1);
        ui->lineEdit_dstnode2->setDisabled(1);
        ui->spinBox_multi_az->setDisabled(1);
        ui->spinBox_multi_rg->setDisabled(1);
        ui->buttonBox->buttons().at(0)->setDisabled(1);
        ui->buttonBox_2->buttons().at(0)->setDisabled(1);
        
        if (m_demPathLabel1) m_demPathLabel1->setEnabled(false);
        if (m_demPathEdit1) m_demPathEdit1->setEnabled(false);
        if (m_demBrowseBtn1) m_demBrowseBtn1->setEnabled(false);
        if (m_demPathLabel2) m_demPathLabel2->setEnabled(false);
        if (m_demPathEdit2) m_demPathEdit2->setEnabled(false);
        if (m_demBrowseBtn2) m_demBrowseBtn2->setEnabled(false);
    }
}

void Geocoding::ShowProjectList(QStandardItemModel* model)
{
    this->copy = model;
    for (int i = 0; i < model->rowCount(); i++)
    {
        ui->comboBox_project1->addItem(model->item(i, 0)->text());
        ui->comboBox_project2->addItem(model->item(i, 0)->text());
        model->item(i, 0)->setStatusTip(IN_PROCESS);
    }
    this->save_path = copy->item(0, 1)->text();
    QStandardItem* project = NULL;
    int count = 0;
    for (int i = 0; i < model->rowCount(); i++)
    {
        if (model->item(i, 0)->rowCount() != 0)
        {
            count = model->item(i, 0)->rowCount();
            project = model->item(i, 0);
            ui->comboBox_project1->setCurrentIndex(i);
            ui->comboBox_project2->setCurrentIndex(i);
            break;
        }

    }
    if (count == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("无可处理数据，请先导入数据！"));
        this->deleteLater();
        return;
    }
    QModelIndex pro_index = model->indexFromItem(project);
    ui->comboBox_node1->clear();
    ui->comboBox_node2->clear();
    bool isnodefound = false;
    QStandardItem* node = NULL, * imagedata = NULL;
    for (int i = 0; i < count; i++)
    {
        if (model->data(model->index(i, 1, pro_index)).toString().compare("complex-2.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("complex-3.0") == 0
            )
        {
            ui->comboBox_node2->addItem(model->data(model->index(i, 0, pro_index)).toString());
            if (!isnodefound)
            {
                node = project->child(i, 0);
                isnodefound = true;
            }
        }
    }
    for (int i = 0; i < count; i++)
    {
        if (model->data(model->index(i, 1, pro_index)).toString().compare("phase-1.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("phase-2.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("phase-3.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("dem-1.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("SBAS-1.0") == 0 ||
            model->data(model->index(i, 1, pro_index)).toString().compare("coherence-1.0") == 0
            )
        {
            ui->comboBox_node1->addItem(model->data(model->index(i, 0, pro_index)).toString());
            if (!isnodefound)
            {
                node = project->child(i, 0);
                isnodefound = true;
            }
        }
    }
    if (ui->comboBox_node1->count() == 0 && ui->comboBox_node2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("未检测到可处理数据！"));
        this->deleteLater();
        return;
    }
    if(ui->comboBox_node1->count() > 0) ui->comboBox_node1->setCurrentIndex(0);
    if (ui->comboBox_node2->count() > 0) ui->comboBox_node2->setCurrentIndex(0);
}

void Geocoding::on_comboBox_project1_currentIndexChanged()
{
    if (ui->comboBox_project1->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox_project1->currentText())[0];
        if (!project) return;
        this->save_path = copy->item(project->row(), 1)->text();
        QModelIndex pro_index = copy->indexFromItem(project);
        int count = project->rowCount();
        if (count < 1)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无数据！"));
            this->deleteLater();
            return;
        }
        QStandardItem* node = NULL;
        ui->comboBox_node1->clear();
        for (int i = 0; i < count; i++)
        {
            if (copy->data(copy->index(i, 1, pro_index)).toString().compare("phase-1.0") == 0 ||
                copy->data(copy->index(i, 1, pro_index)).toString().compare("phase-2.0") == 0 ||
                copy->data(copy->index(i, 1, pro_index)).toString().compare("phase-3.0") == 0 ||
                copy->data(copy->index(i, 1, pro_index)).toString().compare("dem-1.0") == 0 ||
                copy->data(copy->index(i, 1, pro_index)).toString().compare("SBAS-1.0") == 0 ||
                copy->data(copy->index(i, 1, pro_index)).toString().compare("coherence-1.0") == 0
                )
            {
                ui->comboBox_node1->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
                if (!node) node = project->child(i, 0);
            }
        }
        if (!node)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无满足需求的数据！"));
            this->deleteLater();
            return;
        }
        ui->comboBox_node1->setCurrentIndex(0);
    }
}

void Geocoding::on_comboBox_project2_currentIndexChanged()
{
    if (ui->comboBox_project2->count() != 0)
    {
        QStandardItem* project = copy->findItems(ui->comboBox_project2->currentText())[0];
        if (!project) return;
        this->save_path = copy->item(project->row(), 1)->text();
        QModelIndex pro_index = copy->indexFromItem(project);
        int count = project->rowCount();
        if (count < 1)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无数据！"));
            this->deleteLater();
            return;
        }
        QStandardItem* node = NULL;
        ui->comboBox_node2->clear();
        for (int i = 0; i < count; i++)
        {
            if (copy->data(copy->index(i, 1, pro_index)).toString().compare("complex-2.0") == 0 ||
                copy->data(copy->index(i, 1, pro_index)).toString().compare("complex-3.0") == 0
                )
            {
                ui->comboBox_node2->addItem(copy->data(copy->index(i, 0, pro_index)).toString());
                if (!node) node = project->child(i, 0);
            }
        }
        if (!node)
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无满足需求的数据！"));
            this->deleteLater();
            return;
        }
        ui->comboBox_node2->setCurrentIndex(0);
    }
}


void Geocoding::on_buttonBox_accepted()
{
    if (ui->comboBox_node1->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无可处理数据！"));
        return;
    }
    bool bFlag = ui->lineEdit_dstnode1->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("名称应当为数字、字母及下划线的组合！"));
        return;
    }
    //防重名检查
    QStandardItem* project = this->copy->findItems(ui->comboBox_project1->currentText())[0];
    if (!project) {
        return;
    }
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (ui->lineEdit_dstnode1->text() == project->child(i)->text())
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("目标节点已存在，请重命名！"));
            return;
        }
    }


    QString projectPath = save_path;
    QFileInfo projectPathInfo(projectPath);
    if (projectPathInfo.isFile()) {
        projectPath = projectPathInfo.absolutePath();
    }
    QStringList inputPaths;
    QString productLevel;
    int masterIndex = 0;
    if (!buildInputSnapshot(project, ui->comboBox_node1->currentText(), 1, projectPath,
        inputPaths, productLevel, masterIndex)) {
        QMessageBox::warning(this, "Warning!", "Unable to prepare geocoding input data.");
        return;
    }
    m_activeProjectName = ui->comboBox_project1->currentText();
    m_activeProjectPath = projectPath;
    m_generatedResults.clear();

    Geocoding_thread = new GeocodingWorker;
    Geocoding_thread->moveToThread(new QThread(this));
    ui->progressBar->setValue(0);
    ui->progressBar->show();
    connect(this, &Geocoding::operate, Geocoding_thread, &GeocodingWorker::GeocodingWithDem, Qt::QueuedConnection);
    connect(Geocoding_thread, &GeocodingWorker::geocodingGenerated, this, [this](const GeocodingFileResult& res) {
        m_generatedResults.append(res);
        if (!copy) return;
        QList<QStandardItem*> foundProjects = copy->findItems(m_activeProjectName);
        if (foundProjects.isEmpty()) return;
        QStandardItem* project = foundProjects[0];

        QStandardItem* geocodeNode = NodeUtils::findOrCreateProjectNode(project, res.dstNode, res.rankLevel);
        if (geocodeNode) {
            geocodeNode->setToolTip(m_activeProjectName);
            QStandardItem* itemImg = nullptr;
            for (int j = 0; j < geocodeNode->rowCount(); j++) {
                if (geocodeNode->child(j, 0)->text() == res.geocodeName) {
                    itemImg = geocodeNode->child(j, 0);
                    break;
                }
            }
            if (!itemImg) {
                QStandardItem* geocodeNameItem = new QStandardItem(res.geocodeName);
                if (res.rankLevel == "coherence-1.0") geocodeNameItem->setToolTip("coherence");
                else if (res.rankLevel.startsWith("phase")) geocodeNameItem->setToolTip("phase");
                else if (res.rankLevel == "dem-1.0") geocodeNameItem->setToolTip("dem");
                else if (res.rankLevel == "SBAS-1.0") geocodeNameItem->setToolTip("SBAS");
                else geocodeNameItem->setToolTip("amplitude");

                QStandardItem* geocodePathItem = new QStandardItem(res.geocodePath);
                geocodeNameItem->setIcon(QIcon(IMAGEDATA_ICON));
                geocodeNode->appendRow(geocodeNameItem);
                geocodeNode->setChild(geocodeNode->rowCount() - 1, 1, geocodePathItem);
            } else {
                geocodeNode->setChild(itemImg->row(), 1, new QStandardItem(res.geocodePath));
            }
        }
    });
    connect(Geocoding_thread, &GeocodingWorker::updateProcess, this, &Geocoding::updateProcess);
    connect(Geocoding_thread->thread(), &QThread::finished, Geocoding_thread, &GeocodingWorker::deleteLater);
    connect(Geocoding_thread, &GeocodingWorker::endProcess, this, &Geocoding::endProcess);
    connect(this, &QWidget::destroyed, this, &Geocoding::StopThread);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Geocoding::StopThread);// , Qt::QueuedConnection);
    Geocoding_thread->thread()->start();
    ChangeVision(false);
    operate(1, 1, 1, projectPath, inputPaths, productLevel, masterIndex,
        ui->lineEdit_dstnode1->text(), m_demPathEdit1->text().trimmed());
}

void Geocoding::on_buttonBox_2_accepted()
{
    if (ui->comboBox_node2->count() == 0)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程无可处理数据！"));
        return;
    }
    bool bFlag = ui->lineEdit_dstnode2->text().contains(QRegularExpression("^\\w+$"));
    if (!bFlag)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("名称应当为数字、字母及下划线的组合！"));
        return;
    }
    //防重名检查
    QStandardItem* project = this->copy->findItems(ui->comboBox_project2->currentText())[0];
    if (!project) {
        return;
    }
    for (int i = 0; i < project->rowCount(); i++)
    {
        if (ui->lineEdit_dstnode2->text() == project->child(i)->text())
        {
            QMessageBox::warning(NULL, "Warning!", QStringLiteral("目标节点已存在，请重命名！"));
            return;
        }
    }


    QString projectPath = save_path;
    QFileInfo projectPathInfo(projectPath);
    if (projectPathInfo.isFile()) {
        projectPath = projectPathInfo.absolutePath();
    }
    QStringList inputPaths;
    QString productLevel;
    int masterIndex = 0;
    if (!buildInputSnapshot(project, ui->comboBox_node2->currentText(), 2, projectPath,
        inputPaths, productLevel, masterIndex)) {
        QMessageBox::warning(this, "Warning!", "Unable to prepare geocoding input data.");
        return;
    }
    m_activeProjectName = ui->comboBox_project2->currentText();
    m_activeProjectPath = projectPath;
    m_generatedResults.clear();

    Geocoding_thread = new GeocodingWorker;
    Geocoding_thread->moveToThread(new QThread(this));
    ui->progressBar_2->setValue(0);
    ui->progressBar_2->show();
    connect(this, &Geocoding::operate, Geocoding_thread, &GeocodingWorker::GeocodingWithDem, Qt::QueuedConnection);
    connect(Geocoding_thread, &GeocodingWorker::geocodingGenerated, this, [this](const GeocodingFileResult& res) {
        m_generatedResults.append(res);
        if (!copy) return;
        QList<QStandardItem*> foundProjects = copy->findItems(m_activeProjectName);
        if (foundProjects.isEmpty()) return;
        QStandardItem* project = foundProjects[0];

        QStandardItem* geocodeNode = NodeUtils::findOrCreateProjectNode(project, res.dstNode, res.rankLevel);
        if (geocodeNode) {
            geocodeNode->setToolTip(m_activeProjectName);
            QStandardItem* itemImg = nullptr;
            for (int j = 0; j < geocodeNode->rowCount(); j++) {
                if (geocodeNode->child(j, 0)->text() == res.geocodeName) {
                    itemImg = geocodeNode->child(j, 0);
                    break;
                }
            }
            if (!itemImg) {
                QStandardItem* geocodeNameItem = new QStandardItem(res.geocodeName);
                if (res.rankLevel == "coherence-1.0") geocodeNameItem->setToolTip("coherence");
                else if (res.rankLevel.startsWith("phase")) geocodeNameItem->setToolTip("phase");
                else if (res.rankLevel == "dem-1.0") geocodeNameItem->setToolTip("dem");
                else if (res.rankLevel == "SBAS-1.0") geocodeNameItem->setToolTip("SBAS");
                else geocodeNameItem->setToolTip("amplitude");

                QStandardItem* geocodePathItem = new QStandardItem(res.geocodePath);
                geocodeNameItem->setIcon(QIcon(IMAGEDATA_ICON));
                geocodeNode->appendRow(geocodeNameItem);
                geocodeNode->setChild(geocodeNode->rowCount() - 1, 1, geocodePathItem);
            } else {
                geocodeNode->setChild(itemImg->row(), 1, new QStandardItem(res.geocodePath));
            }
        }
    });
    connect(Geocoding_thread, &GeocodingWorker::updateProcess, this, &Geocoding::updateProcess);
    connect(Geocoding_thread->thread(), &QThread::finished, Geocoding_thread, &GeocodingWorker::deleteLater);
    connect(Geocoding_thread, &GeocodingWorker::endProcess, this, &Geocoding::endProcess);
    connect(this, &QWidget::destroyed, this, &Geocoding::StopThread);
    connect(ui->buttonBox_2, &QDialogButtonBox::rejected, this, &Geocoding::StopThread);// , Qt::QueuedConnection);
    Geocoding_thread->thread()->start();
    ChangeVision(false);
    operate(2, ui->spinBox_multi_rg->value(), ui->spinBox_multi_az->value(), projectPath,
        inputPaths, productLevel, masterIndex, ui->lineEdit_dstnode2->text(), m_demPathEdit2->text().trimmed());
}

void Geocoding::on_buttonBox_rejected()
{
    this->close();
}

void Geocoding::on_buttonBox_2_rejected()
{
    this->close();
}
