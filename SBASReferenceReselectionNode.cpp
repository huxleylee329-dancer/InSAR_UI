#include "SBASReferenceReselectionNode.h"
#include "FormatConversion.h"
#include "NodeUtils.h"
#include "IApplicationInterface.h"
#include "InterfaceManager.h"
#include "MainWindow.h"
#include "WorkspaceUI.h"
#include "InSARLogManager.h"
#include "reselection_view.h"
#include <QTimer>
#include <QJsonDocument>
#include <QMessageBox>
#include <QFileInfo>
#include <QDebug>
#include <QDir>
#include <QApplication>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

namespace QtNodes {

// Helper function to copy files from source directory to destination directory
static bool copyDirectoryFiles(const QString& srcPath, const QString& dstPath)
{
    QDir srcDir(srcPath);
    if (!srcDir.exists()) return false;
    QDir dstDir(dstPath);
    if (!dstDir.exists())
    {
        if (!dstDir.mkpath(dstPath)) return false;
    }
    QStringList files = srcDir.entryList(QDir::Files);
    for (const QString& file : files)
    {
        QString srcFile = srcPath + "/" + file;
        QString dstFile = dstPath + "/" + file;
        if (QFile::exists(dstFile))
        {
            QFile::remove(dstFile);
        }
        if (!QFile::copy(srcFile, dstFile))
        {
            return false;
        }
    }
    return true;
}

SBASReferenceReselectionNode::SBASReferenceReselectionNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_inputNodeLabel(nullptr)
    , m_refPointLabel(nullptr)
    , m_gcpLabel(nullptr)
    , m_selectBtn(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_resultLabel(nullptr)
    , m_refRow(-1)
    , m_refCol(-1)
    , m_worker(nullptr)
    , m_thread(nullptr)
{
}

SBASReferenceReselectionNode::~SBASReferenceReselectionNode()
{
    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
    }
}

unsigned int SBASReferenceReselectionNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 2; // Port 0: H5 output, Port 1: JPG preview
}

NodeDataType SBASReferenceReselectionNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
    {
        return ImportedFileData().type();
    }
    else
    {
        if (portIndex == 0)
            return ImportedFileData().type();
        else
            return ImageInfoData().type();
    }
}

bool SBASReferenceReselectionNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    return true;
}

QString SBASReferenceReselectionNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
    {
        return tr("成果 *");
    }
    else
    {
        if (portIndex == 0)
            return tr("成果 *");
        else
            return tr("预览 ?");
    }
}

bool SBASReferenceReselectionNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

std::shared_ptr<NodeData> SBASReferenceReselectionNode::outData(PortIndex port)
{
    if (executionState() != ExecutionState::Completed)
        return nullptr;
    if (port == 0)
        return m_outputData;
    else
        return m_previewData;
}

void SBASReferenceReselectionNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);
    
    if (m_inputData && !m_inputData->filePaths().isEmpty())
    {
        QFileInfo fi(m_inputData->filePath());
        QString upstreamNode = fi.absoluteDir().dirName();
        if (m_inputNodeLabel)
        {
            m_inputNodeLabel->setText(upstreamNode);
        }
        
        // Auto-generate output name if empty or default placeholder
        if (m_outputNodeNameEdit && (m_outputNodeNameEdit->text().isEmpty() || m_outputNodeNameEdit->text() == QStringLiteral("自动生成或手动输入")))
        {
            m_outputNodeName = upstreamNode + "_Reselect";
            m_outputNodeNameEdit->setText(m_outputNodeName);
        }
    }
    else
    {
        if (m_inputNodeLabel)
        {
            m_inputNodeLabel->setText(QStringLiteral("未连接"));
        }
        m_outputNodeName.clear();
        if (m_outputNodeNameEdit)
        {
            m_outputNodeNameEdit->setText(QStringLiteral("自动生成或手动输入"));
        }
    }
    
    updateLabels();
    ExecutableNodeDelegateModel::setInData(data, port);
}

::QWidget* SBASReferenceReselectionNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

void SBASReferenceReselectionNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
    if (_widget)
    {
        bool isManual = (mode == ExecutionMode::Manual);
        m_selectBtn->setEnabled(isManual);
        m_outputNodeNameEdit->setEnabled(isManual);
    }
}

void SBASReferenceReselectionNode::createWidget()
{
    _widget = new ::QWidget();
    _widget->setFixedWidth(300);
    _widget->setStyleSheet("background-color: transparent;");

    QVBoxLayout* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(6);

    // Helper lambda to add parameter rows
    auto addFormRow = [&](const QString& labelText, ::QWidget* fieldWidget) {
        QHBoxLayout* row = new QHBoxLayout();
        QLabel* label = new QLabel(labelText, _widget);
        label->setFixedWidth(80);
        label->setStyleSheet("color: #E0E0E0; font-size: 11px;");
        row->addWidget(label);
        row->addWidget(fieldWidget);
        layout->addLayout(row);
    };

    // Input node display
    m_inputNodeLabel = new QLabel(QStringLiteral("未连接"), _widget);
    m_inputNodeLabel->setStyleSheet("color: #888888; font-size: 11px;");
    addFormRow(QStringLiteral("输入节点:"), m_inputNodeLabel);

    // Selected Reference Point display
    m_refPointLabel = new QLabel(QStringLiteral("未选择"), _widget);
    m_refPointLabel->setStyleSheet("color: #E0E0E0; font-size: 11px;");
    addFormRow(QStringLiteral("参考点:"), m_refPointLabel);

    // Selected GCPs count display
    m_gcpLabel = new QLabel(QStringLiteral("0 GCPs"), _widget);
    m_gcpLabel->setStyleSheet("color: #E0E0E0; font-size: 11px;");
    addFormRow(QStringLiteral("GCP数量:"), m_gcpLabel);

    // Select Button
    m_selectBtn = new QPushButton(QStringLiteral("选择参考点与GCP"), _widget);
    m_selectBtn->setStyleSheet("QPushButton { background-color: #3B82F6; color: white; border-radius: 4px; padding: 4px 8px; font-size: 11px; }"
                               "QPushButton:hover { background-color: #2563EB; }"
                               "QPushButton:disabled { background-color: #4B5563; color: #9CA3AF; }");
    connect(m_selectBtn, &QPushButton::clicked, this, &SBASReferenceReselectionNode::onSelectClicked);
    layout->addWidget(m_selectBtn);

    // Output node name
    m_outputNodeNameEdit = new QLineEdit(_widget);
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入"));
    m_outputNodeNameEdit->setText(QStringLiteral("自动生成或手动输入"));
    m_outputNodeNameEdit->setStyleSheet("color: white; background-color: #1F2937; border: 1px solid #4B5563; border-radius: 4px; padding: 2px; font-size: 11px;");
    connect(m_outputNodeNameEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_outputNodeName = text;
        updateWidgetSize();
    });
    addFormRow(QStringLiteral("输出节点:"), m_outputNodeNameEdit);

    // Progress/Result label
    m_resultLabel = new QLabel(_widget);
    m_resultLabel->setStyleSheet("color: #10B981; font-size: 10px;");
    m_resultLabel->setWordWrap(true);
    layout->addWidget(m_resultLabel);

    updateLabels();
}

void SBASReferenceReselectionNode::onSelectClicked()
{
    if (!m_inputData || m_inputData->filePaths().isEmpty())
    {
        QMessageBox::warning(nullptr, QStringLiteral("警告"), QStringLiteral("请先连接输入节点！"));
        return;
    }

    QString image_path = m_inputData->filePath();
    QFileInfo fileinfo(image_path);
    QString image_name = fileinfo.completeBaseName();
    QString jpg_path = fileinfo.absolutePath() + "/SBAS_time_series.jpg";

    if (!QFile::exists(jpg_path))
    {
        // Try to generate it synchronously
        Utils util;
        FormatConversion FC;
        Mat defomation_velocity, mask;
        int ret = -1;
        {
            NodeUtils::Hdf5Locker locker;
            ret = (NodeUtils::readMatFromH5(image_path, "defomation_velocity", defomation_velocity, CV_64F) &&
                   NodeUtils::readMatFromH5(image_path, "mask", mask)) ? 0 : -1;
        }
        if (ret == 0)
        {
            util.savephase_white(jpg_path.toStdString().c_str(), "jet", defomation_velocity, mask);
        }
    }

    if (!QFile::exists(jpg_path))
    {
        QMessageBox::warning(nullptr, QStringLiteral("错误"), QStringLiteral("无法生成预览图以进行选择，请确认输入数据是否完整！"));
        return;
    }

    reselection_view_Window* Pre_wnd = new reselection_view_Window();
    Pre_wnd->View->setPixmap(jpg_path);
    Pre_wnd->View->SetH5Path(image_path);
    connect(Pre_wnd, &reselection_view_Window::send_coordinate, this, &SBASReferenceReselectionNode::onCoordinatesSelected);
    Pre_wnd->show();
    Pre_wnd->setAttribute(Qt::WA_DeleteOnClose, true);
}

void SBASReferenceReselectionNode::onCoordinatesSelected(int ref_row, int ref_col, QList<QPoint> plist)
{
    m_refRow = ref_row;
    m_refCol = ref_col;
    m_GCPs = plist;
    
    updateLabels();
}

void SBASReferenceReselectionNode::updateLabels()
{
    if (m_refPointLabel)
    {
        if (m_refRow >= 0 && m_refCol >= 0)
            m_refPointLabel->setText(QString("Row: %1, Col: %2").arg(m_refRow).arg(m_refCol));
        else
            m_refPointLabel->setText(QStringLiteral("未选择"));
    }
    if (m_gcpLabel)
    {
        m_gcpLabel->setText(QString("%1 GCPs").arg(m_GCPs.size()));
    }
    
    updateWidgetSize();
}

void SBASReferenceReselectionNode::updateWidgetSize()
{
    if (_widget)
    {
        _widget->adjustSize();
        // Ensure parent graphics proxy widget updates bounds
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

bool SBASReferenceReselectionNode::validateInputs() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty())
        return false;
    if (m_refRow < 0 || m_refCol < 0)
        return false;
    if (m_GCPs.isEmpty())
        return false;
    if (m_outputNodeName.isEmpty() || m_outputNodeName == QStringLiteral("自动生成或手动输入"))
        return false;
    return true;
}

bool SBASReferenceReselectionNode::prepareToStart()
{
    if (!validateInputs())
    {
        return false;
    }

    QString outDir = projectPath() + "/" + m_outputNodeName;
    QString h5Path = outDir + "/SBAS_time_series.h5";
    QStringList pathsToCheck = QStringList() << h5Path;

    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
            NodeUtils::getProjectContext(_widget),
            m_outputNodeName,
            pathsToCheck,
            nullptr
        );
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void SBASReferenceReselectionNode::execute()
{
    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        validateAndRestoreOutput();
        return;
    }

    // Clean project tree first to avoid tree duplicates (SOP rule 14)
    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), m_outputNodeName);

    executeProcessing();
}

void SBASReferenceReselectionNode::executeProcessing()
{
    InSARLogManager::LogInfo("SBASReferenceReselectionNode", "executeProcessing started.");
    m_resultLabel->setText(QStringLiteral("正在复制并准备数据..."));
    
    // Copy the entire input folder contents to the output folder first (SOP rule 3 compatibility)
    QString inDir = QFileInfo(m_inputData->filePath()).absolutePath();
    QString outDir = projectPath() + "/" + m_outputNodeName;
    if (!copyDirectoryFiles(inDir, outDir))
    {
        InSARLogManager::LogError("SBASReferenceReselectionNode", "Failed to copy input data to " + outDir);
        m_resultLabel->setText(QStringLiteral("复制数据失败"));
        setState(ExecutionState::Error);
        return;
    }

    QString times_series_h5 = outDir + "/SBAS_time_series.h5";

    m_worker = new SBASReferenceReselectionWorker();
    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &SBASReferenceReselectionNode::startProcess, m_worker, [this, outDir, times_series_h5]() {
        m_worker->SBAS_reference_reselection(
            projectPath(),
            m_outputNodeName,
            times_series_h5,
            m_refRow,
            m_refCol,
            m_GCPs
        );
    });

    connect(m_worker, &SBASReferenceReselectionWorker::updateProcess, this, &SBASReferenceReselectionNode::onProgressUpdate);
    connect(m_worker, &SBASReferenceReselectionWorker::endProcess, this, &SBASReferenceReselectionNode::onProcessingFinished);
    connect(m_worker, &SBASReferenceReselectionWorker::endProcess, m_thread, &QThread::quit);
    connect(m_worker, &SBASReferenceReselectionWorker::errorProcess, this, &SBASReferenceReselectionNode::onError);
    connect(m_worker, &SBASReferenceReselectionWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_worker, &SBASReferenceReselectionWorker::cancelled, this, &SBASReferenceReselectionNode::onCancelled);
    connect(m_worker, &SBASReferenceReselectionWorker::cancelled, m_thread, &QThread::quit);

    deferAutomaticCompletion();
    m_thread->start();
    Q_EMIT startProcess();
}

void SBASReferenceReselectionNode::stopExecution()
{
    if (m_worker)
    {
        m_worker->StopProcess();
        m_resultLabel->setText(QStringLiteral("已请求取消..."));
    }
}

void SBASReferenceReselectionNode::processAutomatically()
{
    if (prepareToStart()) {
        execute();
    } else {
        setState(ExecutionState::Idle);
    }
}

void SBASReferenceReselectionNode::onProgressUpdate(int progress, const QString& message)
{
    if (isAutomaticExecutionObsolete()) {
        return;
    }

    m_resultLabel->setText(QString("%1%: %2").arg(progress).arg(message));
}

void SBASReferenceReselectionNode::onError(const QString& error)
{
    m_thread = nullptr;
    m_worker = nullptr;
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    InSARLogManager::LogError("SBASReferenceReselectionNode", "Error during SBAS reselection: " + error);
    m_resultLabel->setText(QStringLiteral("失败: ") + error);
    setState(ExecutionState::Error);
    
}

void SBASReferenceReselectionNode::onCancelled()
{
    InSARLogManager::LogInfo("SBASReferenceReselectionNode", "Reference reselection cancellation completed.");
    m_thread = nullptr;
    m_worker = nullptr;
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    m_outputData.reset();
    m_previewData.reset();
    QDir(projectPath() + "/" + m_outputNodeName).removeRecursively();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    m_resultLabel->setText(QStringLiteral("已取消"));
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

void SBASReferenceReselectionNode::onProcessingFinished()
{
    m_thread = nullptr;
    m_worker = nullptr;
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    InSARLogManager::LogInfo("SBASReferenceReselectionNode", "executeProcessing completed.");
    m_resultLabel->setText(QStringLiteral("重新计算完成，生成预览图..."));

    generateStaticPreviewJpg();
}

void SBASReferenceReselectionNode::generateStaticPreviewJpg()
{
    QString outDir = projectPath() + "/" + m_outputNodeName;
    QString h5Path = outDir + "/SBAS_time_series.h5";
    QString jpgPath = outDir + "/SBAS_time_series.jpg";

    QFutureWatcher<bool>* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, h5Path, jpgPath]() {
        if (discardObsoleteAutomaticExecution()) {
            watcher->deleteLater();
            return;
        }

        bool ok = watcher->result();
        watcher->deleteLater();

        if (ok)
        {
            m_outputData = std::make_shared<ImportedFileData>(h5Path, m_outputNodeName);
            m_previewData = std::make_shared<ImageInfoData>(jpgPath);
            m_resultLabel->setText(QStringLiteral("计算并生成预览完成！"));
            setState(ExecutionState::Completed);

            // Add node to XML and tree model (SOP rule 14)
            IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
            if (iface)
            {
                QString relativeH5Path = QString("/%1/SBAS_time_series.h5").arg(m_outputNodeName);
                NodeUtils::addSBASNodeToProjectXml(iface, m_outputNodeName, "SBAS_time_series", relativeH5Path);
                iface->refreshProjectTree();
            }

            Q_EMIT dataUpdated(0);
            Q_EMIT dataUpdated(1);
        }
        else
        {
            m_resultLabel->setText(QStringLiteral("预览图生成失败"));
            setState(ExecutionState::Error);
        }
    });

    QFuture<bool> future = QtConcurrent::run([h5Path, jpgPath]() -> bool {
        NodeUtils::Hdf5Locker locker;
        Utils util;
        FormatConversion FC;
        Mat defomation_velocity, mask;
        int ret = (NodeUtils::readMatFromH5(h5Path, "defomation_velocity", defomation_velocity, CV_64F) &&
                   NodeUtils::readMatFromH5(h5Path, "mask", mask)) ? 0 : -1;
        if (ret == 0)
        {
            util.savephase_white(jpgPath.toStdString().c_str(), "jet", defomation_velocity, mask);
            return true;
        }
        return false;
    });

    watcher->setFuture(future);
}

bool SBASReferenceReselectionNode::validateAndRestoreOutput()
{
    QString outDir = projectPath() + "/" + m_outputNodeName;
    QString h5Path = outDir + "/SBAS_time_series.h5";
    QString jpgPath = outDir + "/SBAS_time_series.jpg";

    if (QFile::exists(h5Path))
    {
        m_outputData = std::make_shared<ImportedFileData>(h5Path, m_outputNodeName);
        m_resultLabel->setText(QStringLiteral("已恢复现有成果。"));
        
        // Retrieve ref_row and ref_col from H5 if possible
        FormatConversion FC;
        Mat ref_i, ref_j;
        bool read_ok = false;
        {
            NodeUtils::Hdf5Locker locker;
            read_ok = (NodeUtils::readMatFromH5(h5Path, "ref_row", ref_i) &&
                       NodeUtils::readMatFromH5(h5Path, "ref_col", ref_j));
        }
        if (read_ok)
        {
            m_refRow = ref_i.at<int>(0, 0);
            m_refCol = ref_j.at<int>(0, 0);
            // GCP list can't be easily retrieved from H5, but ref row/col is updated.
        }

        updateLabels();

        if (QFile::exists(jpgPath))
        {
            m_previewData = std::make_shared<ImageInfoData>(jpgPath);
            setState(ExecutionState::Completed);
            Q_EMIT dataUpdated(0);
            Q_EMIT dataUpdated(1);
        }
        else
        {
            generateStaticPreviewJpg();
        }
        return true;
    }
    return false;
}

QStringList SBASReferenceReselectionNode::previewImagePaths() const
{
    QStringList paths;
    QString jpgPath = projectPath() + "/" + m_outputNodeName + "/SBAS_time_series.jpg";
    if (QFile::exists(jpgPath))
    {
        paths.append(jpgPath);
    }
    return paths;
}

QJsonObject SBASReferenceReselectionNode::save() const
{
    QJsonObject json = ExecutableNodeDelegateModel::save();
    json["refRow"] = m_refRow;
    json["refCol"] = m_refCol;
    json["outputNodeName"] = m_outputNodeName;
    
    QJsonArray gcpArray;
    for (const QPoint& pt : m_GCPs)
    {
        QJsonObject ptJson;
        ptJson["x"] = pt.x();
        ptJson["y"] = pt.y();
        gcpArray.append(ptJson);
    }
    json["GCPs"] = gcpArray;

    return json;
}

void SBASReferenceReselectionNode::load(QJsonObject const& json)
{
    ExecutableNodeDelegateModel::load(json);
    m_refRow = json["refRow"].toInt(-1);
    m_refCol = json["refCol"].toInt(-1);
    m_outputNodeName = json["outputNodeName"].toString();
    
    m_GCPs.clear();
    QJsonArray gcpArray = json["GCPs"].toArray();
    for (int i = 0; i < gcpArray.size(); ++i)
    {
        QJsonObject ptJson = gcpArray[i].toObject();
        m_GCPs.append(QPoint(ptJson["x"].toInt(), ptJson["y"].toInt()));
    }

    if (m_outputNodeNameEdit)
    {
        m_outputNodeNameEdit->setText(m_outputNodeName);
    }
    updateLabels();
}

QString SBASReferenceReselectionNode::projectPath() const
{
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    if (iface)
    {
        return iface->projectPath();
    }
    return QString();
}

QString SBASReferenceReselectionNode::projectName() const
{
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    if (iface)
    {
        return iface->projectName();
    }
    return QString();
}

} // namespace QtNodes
