#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "TargetDetectionNode.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <QDebug>

namespace QtNodes {

TargetDetectionNode::TargetDetectionNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_inputImageLabel(nullptr)
    , m_modelComboBox(nullptr)
    , m_thresholdEdit(nullptr)
    , m_resultLabel(nullptr)
    , m_probabilityLabel(nullptr)
    , m_confidenceLabel(nullptr)
    , m_statusLabel(nullptr)
    , m_inputData(nullptr)
    , m_outputData(nullptr)
    , m_thread(nullptr)
    , m_workerThread(nullptr)
{
    setExecutionMode(ExecutionMode::Automatic);
}

TargetDetectionNode::~TargetDetectionNode()
{
    stopExecution();

    if (m_workerThread)
    {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    if (m_thread)
    {
        if (m_thread->isRunning())
        {
            m_thread->quit();
            m_thread->wait();
        }
        m_thread->deleteLater();
        m_thread = nullptr;
    }
}

unsigned int TargetDetectionNode::nPorts(PortType portType) const
{
    return 1;
}

NodeDataType TargetDetectionNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    return NodeDataType{"image_info", "Image Info"};
}

bool TargetDetectionNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString TargetDetectionNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return QString::fromUtf8("\xe8\xbe\x93\xe5\x85\xa5\xe5\x9b\xbe\xe5\x83\x8f");
    } else {
        return QString::fromUtf8("\xe5\x8e\x9f\xe5\x9b\xbe\xe8\xbe\x93\xe5\x87\xba");
    }
}

void TargetDetectionNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
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

    ExecutableNodeDelegateModel::setInData(data, port);
}

std::shared_ptr<NodeData> TargetDetectionNode::outData(PortIndex port)
{
    Q_UNUSED(port);
    return m_outputData;
}

QWidget* TargetDetectionNode::embeddedWidget()
{
    if (!_widget) {
        createWidget();
    }
    return _widget;
}

void TargetDetectionNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setMinimumWidth(260);

    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // Input image label
    m_inputImageLabel = new QLabel("");
    m_inputImageLabel->setWordWrap(true);
    if (m_inputData && !m_inputData->filePath().isEmpty()) {
        QFileInfo fi(m_inputData->filePath());
        m_inputImageLabel->setText(fi.fileName());
    }
    layout->addWidget(m_inputImageLabel);

    // Model Selection
    auto* modelLayout = new QHBoxLayout();
    modelLayout->addWidget(new QLabel(QString::fromUtf8("\xe6\xa8\xa1\xe5\x9e\x8b\xe9\x80\x89\xe6\x8b\xa9\xef\xbc\x9a")));
    m_modelComboBox = new QComboBox();
    m_modelComboBox->addItem("SAR Ship Model 0429", QDir::currentPath() + "/sar_ship_model0429.onnx");
    m_selectedModelPath = m_modelComboBox->currentData().toString();
    connect(m_modelComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        m_selectedModelPath = m_modelComboBox->currentData().toString();
    });
    modelLayout->addWidget(m_modelComboBox);
    layout->addLayout(modelLayout);

    // Confidence
    auto* confLayout = new QHBoxLayout();
    confLayout->addWidget(new QLabel(QString::fromUtf8("\xe7\xbd\xae\xe4\xbf\xa1\xe5\xba\xa6\xef\xbc\x9a")));
    m_thresholdEdit = new QLineEdit();
    m_thresholdEdit->setText(QString::number(m_thresholdValue, 'f', 2));
    connect(m_thresholdEdit, &QLineEdit::textChanged, this, [this](const QString& text) { 
        bool ok;
        float val = text.toFloat(&ok);
        if (ok) m_thresholdValue = val;
    });
    confLayout->addWidget(m_thresholdEdit);
    layout->addLayout(confLayout);

    // Separator line
    QFrame* line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    // Output Results Labels
    layout->addWidget(new QLabel(QString::fromUtf8("\xe6\xa3\x80\xe6\x9f\xa5\xe7\xbb\x93\xe6\x9e\x9c\xef\xbc\x9a")));

    auto* resultLayout = new QHBoxLayout();
    resultLayout->addWidget(new QLabel(QString::fromUtf8("\xe7\xbb\x93\xe6\x9e\x9c\xef\xbc\x9a")));
    m_resultLabel = new QLabel("--");
    resultLayout->addWidget(m_resultLabel);
    layout->addLayout(resultLayout);

    auto* probLayout = new QHBoxLayout();
    probLayout->addWidget(new QLabel(QString::fromUtf8("\xe7\x9b\xae\xe6\xa0\x87\xe5\x90\x8e\xe9\xaa\x8c\xe6\xa6\x82\xe7\x8e\x87\xef\xbc\x9a")));
    m_probabilityLabel = new QLabel("--");
    probLayout->addWidget(m_probabilityLabel);
    layout->addLayout(probLayout);

    auto* finalConfLayout = new QHBoxLayout();
    finalConfLayout->addWidget(new QLabel(QString::fromUtf8("\xe7\xbd\xae\xe4\xbf\xa1\xe5\xba\xa6\xef\xbc\x9a")));
    m_confidenceLabel = new QLabel("--");
    finalConfLayout->addWidget(m_confidenceLabel);
    layout->addLayout(finalConfLayout);

    // Status label
    m_statusLabel = new QLabel();
    m_statusLabel->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(m_statusLabel);

    layout->addStretch();
}

void TargetDetectionNode::stopExecution()
{
    if (m_workerThread)
    {
        m_workerThread->StopProcess();
    }
    setState(ExecutionState::Stopped);
}

void TargetDetectionNode::processAutomatically()
{
    if (m_thread || m_workerThread) {
        return;
    }

    if (isReady()) {
        executeProcessing();
    }
}

bool TargetDetectionNode::isReady() const
{
    if (!m_inputData || m_inputData->filePath().isEmpty()) {
        return false;
    }
    if (m_selectedModelPath.isEmpty()) {
        return false;
    }
    return true;
}

void TargetDetectionNode::execute()
{
    executeProcessing();
}

void TargetDetectionNode::executeProcessing()
{
    if (m_thread || m_workerThread)
    {
        if (m_thread && m_thread->isRunning())
        {
            m_thread->quit();
            m_thread->wait();
        }
        if (m_thread) {
            m_thread->deleteLater();
            m_thread = nullptr;
        }
        if (m_workerThread) {
            m_workerThread->deleteLater();
            m_workerThread = nullptr;
        }
        disconnect(this, &TargetDetectionNode::startTargetDetection, nullptr, nullptr);
    }

    if (!isReady()) {
        if (m_statusLabel) m_statusLabel->setText(QString::fromUtf8("\xe7\x8a\xb6\xe6\x80\x81\xef\xbc\x9a\xe6\x9c\xaa\xe5\x87\x86\xe5\xa4\x87\xe5\xa5\xbd"));
        return;
    }

    setProgress(0);
    if (m_statusLabel) m_statusLabel->setText(QString::fromUtf8("\xe7\x8a\xb6\xe6\x80\x81\xef\xbc\x9a\xe6\xad\xa3\xe5\x9c\xa8\xe5\x88\x9d\xe5\xa7\x8b\xe5\x8c\x96..."));
    
    // Clear previous results
    if (m_resultLabel) m_resultLabel->setText("--");
    if (m_probabilityLabel) m_probabilityLabel->setText("--");
    if (m_confidenceLabel) m_confidenceLabel->setText("--");

    QString inputPath = m_inputData->filePath();
    QString modelPath = m_selectedModelPath;
    float thresholdValue = m_thresholdValue;

    m_thread = new QThread(this);
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    connect(m_thread, &QThread::started, [this, inputPath, modelPath, thresholdValue]() {
        Q_EMIT startTargetDetection(inputPath, modelPath, thresholdValue);
    });
    connect(this, &TargetDetectionNode::startTargetDetection, m_workerThread, &MyThread::Target_Detection, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::updateProcess, this, &TargetDetectionNode::onProgressUpdate, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::sendTargetDetectionResult, this, &TargetDetectionNode::onDetectionFinished, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::errorProcess, this, &TargetDetectionNode::onError, Qt::UniqueConnection);

    m_thread->start();
    
    if (m_modelComboBox) m_modelComboBox->setEnabled(false);
    if (m_thresholdEdit) m_thresholdEdit->setEnabled(false);
}

void TargetDetectionNode::onProgressUpdate(int progress, const QString& message)
{
    setProgress(progress);
    if (m_statusLabel) {
        m_statusLabel->setText(QString::fromUtf8("\xe7\x8a\xb6\xe6\x80\x81\xef\xbc\x9a") + message);
    }
}

void TargetDetectionNode::onDetectionFinished(bool success, float shipProb, QString resultText, QString errorMsg)
{
    if (success) {
        if (m_resultLabel) m_resultLabel->setText(resultText);
        if (m_probabilityLabel) m_probabilityLabel->setText(QString::number(shipProb * 100.0f, 'f', 2) + "%");
        if (m_confidenceLabel) m_confidenceLabel->setText(QString::number(m_thresholdValue, 'f', 2));
        
        if (m_statusLabel) m_statusLabel->setText(QString::fromUtf8("\xe7\x8a\xb6\xe6\x80\x81\xef\xbc\x9a\xe5\xae\x8c\xe6\x88\x90"));

        m_outputData = m_inputData;
        setOutputData(0, m_outputData);
        Q_EMIT dataUpdated(0);
        
        finishExecution();
    } else {
        onError(errorMsg);
    }

    if (m_modelComboBox) m_modelComboBox->setEnabled(true);
    if (m_thresholdEdit) m_thresholdEdit->setEnabled(true);

    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    if (m_workerThread)
    {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }
}

void TargetDetectionNode::onError(const QString& error)
{
    Q_EMIT executionError(error);
    setState(ExecutionState::Error);
    if (m_statusLabel) {
        m_statusLabel->setText(QString::fromUtf8("\xe7\x8a\xb6\xe6\x80\x81\xef\xbc\x9a\xe9\x94\x99\xe8\xaf\xaf - ") + error);
    }
    
    if (m_modelComboBox) m_modelComboBox->setEnabled(true);
    if (m_thresholdEdit) m_thresholdEdit->setEnabled(true);

    if (m_thread)
    {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    if (m_workerThread)
    {
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }

    m_outputData.reset();
}

QJsonObject TargetDetectionNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();
    modelJson["thresholdValue"] = m_thresholdValue;
    return modelJson;
}

void TargetDetectionNode::load(QJsonObject const &json)
{
    ExecutableNodeDelegateModel::load(json);

    m_thresholdValue = json["thresholdValue"].toDouble(0.65);
    if (m_thresholdEdit) {
        m_thresholdEdit->setText(QString::number(m_thresholdValue, 'f', 2));
    }
}

} // namespace QtNodes