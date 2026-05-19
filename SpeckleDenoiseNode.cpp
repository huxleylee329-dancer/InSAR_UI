#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "SpeckleDenoiseNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "icon_source.h"
#include "BM3DWrapper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <QDateTime>
#include <QStandardItemModel>
#include <cmath>
#include <algorithm>

namespace QtNodes {

SpeckleDenoiseNode::SpeckleDenoiseNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_inputImageLabel(nullptr)
    , m_saveToProjectCheckBox(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_statusLabel(nullptr)
    , m_inputData(nullptr)
    , m_outputData(nullptr)
{
}

SpeckleDenoiseNode::~SpeckleDenoiseNode()
{
}

unsigned int SpeckleDenoiseNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 2;
}

NodeDataType SpeckleDenoiseNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    return NodeDataType{"image_info", "Image Info"};
}

bool SpeckleDenoiseNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString SpeckleDenoiseNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return "输入图像";
    } else {
        if (portIndex == 0)
            return "成果 *";
        else if (portIndex == 1)
            return "预览 ?";
    }
    return QString();
}

bool SpeckleDenoiseNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

void SpeckleDenoiseNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(port);
    m_inputData = std::dynamic_pointer_cast<ImageInfoData>(data);

    if (m_inputImageLabel) {
        if (m_inputData && !m_inputData->filePath().isEmpty()) {
            QFileInfo fi(m_inputData->filePath());
            m_inputImageLabel->setText(fi.fileName());
        } else {
            m_inputImageLabel->setText("");
        }
    }

    if (m_inputData && m_outputNodeNameEdit && m_outputNodeNameEdit->text().isEmpty()) {
        m_outputNodeNameEdit->setText(generateOutputFileName());
    }

    ExecutableNodeDelegateModel::setInData(data, port);
}

std::shared_ptr<NodeData> SpeckleDenoiseNode::outData(PortIndex port)
{
    Q_UNUSED(port);
    return m_outputData;
}

QWidget* SpeckleDenoiseNode::embeddedWidget()
{
    if (!_widget) {
        createWidget();
    }
    return _widget;
}

void SpeckleDenoiseNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setMinimumWidth(260);

    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_inputImageLabel = new QLabel("");
    m_inputImageLabel->setWordWrap(true);
    layout->addWidget(m_inputImageLabel);

    m_saveToProjectCheckBox = new QCheckBox("保存到项目树");
    m_saveToProjectCheckBox->setChecked(true);
    connect(m_saveToProjectCheckBox, &QCheckBox::stateChanged, this, &SpeckleDenoiseNode::onSaveToProjectChanged);
    layout->addWidget(m_saveToProjectCheckBox);

    auto* nodeNameLayout = new QHBoxLayout();
    nodeNameLayout->addWidget(new QLabel("目标节点:"));
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText("输入节点名称");
    nodeNameLayout->addWidget(m_outputNodeNameEdit);
    layout->addLayout(nodeNameLayout);

    m_statusLabel = new QLabel();
    m_statusLabel->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(m_statusLabel);

    layout->addStretch();

    onSaveToProjectChanged(m_saveToProjectCheckBox->checkState());
}

void SpeckleDenoiseNode::onSaveToProjectChanged(int state)
{
    if (m_outputNodeNameEdit) {
        m_outputNodeNameEdit->setEnabled(state == Qt::Checked);
    }
}

void SpeckleDenoiseNode::stopExecution()
{
    setState(ExecutionState::Stopped);
}

void SpeckleDenoiseNode::processAutomatically()
{
    if (isReady()) {
        execute();
    }
}

bool SpeckleDenoiseNode::isReady() const
{
    if (!m_inputData) {
        return false;
    }
    if (m_inputData->filePath().isEmpty()) {
        return false;
    }

    if (m_saveToProjectCheckBox && m_saveToProjectCheckBox->isChecked()) {
        if (!m_outputNodeNameEdit || m_outputNodeNameEdit->text().trimmed().isEmpty()) {
            return false;
        }
    }

    return true;
}

void SpeckleDenoiseNode::execute()
{
    if (!isReady()) {
        if (m_statusLabel) {
            m_statusLabel->setText("状态: 未准备好");
        }
        return;
    }

    if (m_statusLabel) {
        m_statusLabel->setText("状态: 正在处理...");
    }

    QString inputPath = m_inputData->filePath();
    cv::Mat inputGray = cv::imread(inputPath.toStdString(), cv::IMREAD_GRAYSCALE);
    if (inputGray.empty()) {
        Q_EMIT executionError("无法读取输入图像");
        setState(ExecutionState::Error);
        if (m_statusLabel) {
            m_statusLabel->setText("状态: 读取失败");
        }
        return;
    }

    cv::Mat resultImage = runBm3dCoreLogic(inputGray);
    if (resultImage.empty()) {
        Q_EMIT executionError("图像去噪失败");
        setState(ExecutionState::Error);
        if (m_statusLabel) {
            m_statusLabel->setText("状态: 处理失败");
        }
        return;
    }

    bool saveSuccess = true;
    if (m_saveToProjectCheckBox && m_saveToProjectCheckBox->isChecked()) {
        saveSuccess = saveResultToProject(resultImage);
        if (!saveSuccess) {
            Q_EMIT executionError("保存结果到项目失败");
            setState(ExecutionState::Error);
            if (m_statusLabel) {
                m_statusLabel->setText("状态: 保存失败");
            }
            return;
        }
    } else {
        QString tempPath = QDir::tempPath() + QString("/speckle_denoise_%1.jpg")
            .arg(QDateTime::currentMSecsSinceEpoch());
        cv::imwrite(tempPath.toStdString(), resultImage);
        m_outputImagePath = tempPath;
    }

    m_outputData = std::make_shared<ImageInfoData>(m_outputImagePath);

    if (m_statusLabel) {
        m_statusLabel->setText("状态: 完成");
    }

    Q_EMIT dataUpdated(0);
    Q_EMIT dataUpdated(1);
    
    finishExecution();
}

QJsonObject SpeckleDenoiseNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    if (m_saveToProjectCheckBox)
        modelJson["saveToProject"] = m_saveToProjectCheckBox->isChecked();
    if (m_outputNodeNameEdit)
        modelJson["outputNodeName"] = m_outputNodeNameEdit->text();

    return modelJson;
}

void SpeckleDenoiseNode::load(QJsonObject const &json)
{
    ExecutableNodeDelegateModel::load(json);

    if (m_saveToProjectCheckBox) {
        QJsonValue v = json["saveToProject"];
        if (!v.isUndefined()) {
            m_saveToProjectCheckBox->setChecked(v.toBool(true));
        }
    }

    if (m_outputNodeNameEdit) {
        QJsonValue v = json["outputNodeName"];
        if (!v.isUndefined()) {
            m_outputNodeNameEdit->setText(v.toString());
        }
    }
}

QString SpeckleDenoiseNode::generateOutputFileName() const
{
    if (!m_inputData || m_inputData->filePath().isEmpty()) {
        return QString();
    }

    QFileInfo fi(m_inputData->filePath());
    QString baseName = fi.completeBaseName();
    return QString("%1_denoised").arg(baseName);
}

cv::Mat SpeckleDenoiseNode::runBm3dCoreLogic(const cv::Mat& inputGray) const
{
    const double noiseGain = 1.1;

    cv::Mat imgDouble;
    inputGray.convertTo(imgDouble, CV_64F);
    cv::Mat imgLog;
    cv::log(imgDouble + 1.0, imgLog);

    double minV = 0.0, maxV = 0.0;
    cv::minMaxLoc(imgLog, &minV, &maxV);
    double rangeV = maxV - minV;
    cv::Mat imgNorm = (imgLog - minV) / rangeV;

    double medianValue = calcMedian(imgLog);
    cv::Mat absDiff;
    cv::absdiff(imgLog, medianValue, absDiff);
    double sigmaEst = calcMedian(absDiff) / 0.6745;
    double sigmaFinal = (sigmaEst * noiseGain) / rangeV;

    cv::Mat imgDenNorm = runBm3dDenoise(imgNorm, sigmaFinal);
    if (imgDenNorm.empty()) {
        return cv::Mat();
    }

    cv::Mat imgDen = imgDenNorm * rangeV + minV;
    cv::Mat imgOut;
    cv::exp(imgDen, imgOut);
    imgOut = imgOut - 1.0;

    double meanInput = cv::mean(imgDouble)[0];
    double meanOutput = cv::mean(imgOut)[0];
    if (meanOutput != 0.0) {
        imgOut = imgOut * (meanInput / meanOutput);
    }

    cv::min(imgOut, 255.0, imgOut);
    cv::max(imgOut, 0.0, imgOut);

    cv::Mat output8U;
    imgOut.convertTo(output8U, CV_8U);
    return output8U;
}

cv::Mat SpeckleDenoiseNode::runBm3dDenoise(const cv::Mat& imgNorm, double sigmaFinal) const
{
    cv::Mat img8U;
    imgNorm.convertTo(img8U, CV_8U, 255.0);

    double sigma8 = sigmaFinal * 255.0;
    cv::Mat den8U = BM3DWrapper::DenoiseGray(img8U, sigma8);
    if (den8U.empty()) return cv::Mat();

    cv::Mat denNorm;
    den8U.convertTo(denNorm, CV_64F, 1.0 / 255.0);
    return denNorm;
}

IApplicationInterface* SpeckleDenoiseNode::getProjectContext() const
{
    // 1. Navigate up the widget hierarchy to find the interface (standard way)
    if (_widget)
    {
        QWidget* parent = _widget->parentWidget();
        while (parent)
        {
            auto* iface = dynamic_cast<IApplicationInterface*>(parent);
            if (iface) {
                return iface;
            }
            parent = parent->parentWidget();
        }
    }

    // 2. Fallback: If not found via hierarchy, try via main window
    foreach(QWidget * widget, QApplication::topLevelWidgets()) {
        MainWindow* mainWin = qobject_cast<MainWindow*>(widget);
        if (mainWin && mainWin->interfaceManager()) {
            auto* iface = mainWin->interfaceManager()->currentInterface();
            if (iface) {
                return iface;
            }
        }
    }

    return nullptr;
}

QStandardItemModel* SpeckleDenoiseNode::projectModel() const
{
    auto iface = getProjectContext();
    return iface ? iface->projectModel() : nullptr;
}

QString SpeckleDenoiseNode::projectPath() const
{
    auto iface = getProjectContext();
    return iface ? iface->projectPath() : QString();
}

QString SpeckleDenoiseNode::projectName() const
{
    auto iface = getProjectContext();
    return iface ? iface->projectName() : QString();
}

bool SpeckleDenoiseNode::saveResultToProject(const cv::Mat& resultImage)
{
    if (!m_outputNodeNameEdit || m_outputNodeNameEdit->text().trimmed().isEmpty()) {
        return false;
    }

    QStandardItemModel* model = projectModel();
    if (!model || model->rowCount() == 0) {
        Q_EMIT executionError("没有打开的项目");
        return false;
    }

    QString nodeName = m_outputNodeNameEdit->text().trimmed();
    QString projPath = projectPath();
    QString projName = projectName();
    
    // If projPath points to the .insar file, get the directory
    QString projDirStr = projPath;
    if (projPath.endsWith(".insar", Qt::CaseInsensitive)) {
        projDirStr = QFileInfo(projPath).absolutePath();
    }
    
    QDir dir(projDirStr);
    if (!dir.exists(nodeName)) {
        if (!dir.mkdir(nodeName)) {
            return false;
        }
    }

    m_outputImagePath = QString("%1/%2/denoised.jpg").arg(projDirStr, nodeName);

    if (!cv::imwrite(m_outputImagePath.toStdString(), resultImage)) {
        return false;
    }
    
    if (model) {
        QStandardItem* projectItem = nullptr;
        for (int i = 0; i < model->rowCount(); ++i) {
            QStandardItem* item = model->item(i, 0);
            if (item && item->text() == projName) {
                projectItem = item;
                break;
            }
        }

        if (projectItem) {
            QStandardItem* dataNode = nullptr;
            for (int i = 0; i < projectItem->rowCount(); ++i) {
                QStandardItem* child = projectItem->child(i, 0);
                if (child && child->text() == nodeName) {
                    dataNode = child;
                    break;
                }
            }

            if (!dataNode) {
                dataNode = new QStandardItem(nodeName);
                dataNode->setIcon(QIcon(FOLDER_ICON)); // Add folder icon
                projectItem->appendRow(dataNode);
            }

            // Check for existing items and remove any duplicates or old standards
            QStandardItem* fileItem = nullptr;
            QStandardItem* filePathItem = nullptr;
            
            // Collect all indices to remove to ensure only one "denoised" item remains
            QList<int> rowsToRemove;
            for (int i = 0; i < dataNode->rowCount(); ++i) {
                QString itemText = dataNode->child(i, 0)->text().trimmed();
                if (itemText.compare("denoised", Qt::CaseInsensitive) == 0 || 
                    itemText.compare("denoised.jpg", Qt::CaseInsensitive) == 0) {
                    if (!fileItem) {
                        fileItem = dataNode->child(i, 0);
                        filePathItem = dataNode->child(i, 1);
                    } else {
                        rowsToRemove.prepend(i); // Remove duplicates
                    }
                }
            }
            
            foreach(int row, rowsToRemove) {
                dataNode->removeRow(row);
            }

            if (!fileItem) {
                QStandardItem* nameItem = new QStandardItem("denoised");
                nameItem->setIcon(QIcon(IMAGEDATA_ICON));
                nameItem->setData(IMAGEDATA_ICON, Qt::UserRole + 10);
                nameItem->setToolTip("image"); // Use "image" to trigger standard loading logic
                
                QStandardItem* pathItem = new QStandardItem(m_outputImagePath);
                
                dataNode->appendRow(nameItem);
                dataNode->setChild(dataNode->rowCount() - 1, 1, pathItem);
            } else {
                fileItem->setText("denoised");
                fileItem->setToolTip("image"); // Set to "image"
                if (filePathItem) filePathItem->setText(m_outputImagePath);
            }
            
            // Refresh tree
            if (auto* iface = getProjectContext()) {
                iface->refreshProjectTree();
            }
        }
    }

    return true;
}

double SpeckleDenoiseNode::calcMedian(const cv::Mat& img) const
{
    cv::Mat imgCopy = img.clone();
    imgCopy = imgCopy.reshape(0, 1);
    std::sort(imgCopy.begin<double>(), imgCopy.end<double>());

    int n = imgCopy.total();
    if (n % 2 == 0) {
        return (imgCopy.at<double>(n / 2 - 1) + imgCopy.at<double>(n / 2)) / 2.0;
    } else {
        return imgCopy.at<double>(n / 2);
    }
}

} // namespace QtNodes
