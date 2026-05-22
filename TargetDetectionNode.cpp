
#include "TargetDetectionNode.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QApplication>
#include <QDebug>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QTableWidget>
#include <QHeaderView>

namespace QtNodes {

TargetDetectionNode::TargetDetectionNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_inputImageLabel(nullptr)
    , m_modelComboBox(nullptr)
    , m_thresholdEdit(nullptr)
    , m_resultsTable(nullptr)
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
    if (portType == PortType::In)
        return 1;
    return 0;
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
        return QStringLiteral("输入图像");
    } else {
        return QStringLiteral("原图输出");
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
    modelLayout->addWidget(new QLabel(QStringLiteral("模型选择：")));
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
    confLayout->addWidget(new QLabel(QStringLiteral("置信度：")));
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
    layout->addWidget(new QLabel(QStringLiteral("检查结果：")));

    m_resultsTable = new QTableWidget();
    m_resultsTable->setColumnCount(3);
    m_resultsTable->setHorizontalHeaderLabels({QStringLiteral("图像"), QStringLiteral("结果"), QStringLiteral("概率")});
    m_resultsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_resultsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsTable->setMinimumHeight(100);
    layout->addWidget(m_resultsTable);

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
    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
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
        if (m_statusLabel) m_statusLabel->setText(QStringLiteral("状态：未准备好"));
        return;
    }

    setProgress(0);
    if (m_statusLabel) m_statusLabel->setText(QStringLiteral("状态：正在初始化..."));
    
    // Clear previous results
    if (m_resultsTable) {
        m_resultsTable->setRowCount(0);
    }

    QStringList inputPaths = m_inputData->filePaths();
    QString modelPath = m_selectedModelPath;
    float thresholdValue = m_thresholdValue;

    m_thread = new QThread(this);
    m_workerThread = new MyThread();
    m_workerThread->moveToThread(m_thread);

    connect(m_thread, &QThread::started, [this, inputPaths, modelPath, thresholdValue]() {
        Q_EMIT startTargetDetection(inputPaths, modelPath, thresholdValue);
    });
    connect(this, &TargetDetectionNode::startTargetDetection, m_workerThread, &MyThread::Target_Detection, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::updateProcess, this, &TargetDetectionNode::onProgressUpdate, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::sendTargetDetectionResult, this, &TargetDetectionNode::onDetectionFinished, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::errorProcess, this, &TargetDetectionNode::onError, Qt::UniqueConnection);
    connect(m_workerThread, &MyThread::askUserError, this, &TargetDetectionNode::onAskUserError, Qt::BlockingQueuedConnection);

    m_thread->start();
    
    if (m_modelComboBox) m_modelComboBox->setEnabled(false);
    if (m_thresholdEdit) m_thresholdEdit->setEnabled(false);
}

void TargetDetectionNode::onProgressUpdate(int progress, const QString& message)
{
    setProgress(progress);
    if (m_statusLabel) {
        m_statusLabel->setText(QStringLiteral("状态：") + message);
    }
}

void TargetDetectionNode::onDetectionFinished(int imageIndex, bool success, float shipProb, QString resultText, QString errorMsg)
{
    if (m_resultsTable && m_inputData && imageIndex < m_inputData->filePaths().size()) {
        int row = m_resultsTable->rowCount();
        m_resultsTable->insertRow(row);
        
        QString fileName = QFileInfo(m_inputData->filePaths()[imageIndex]).fileName();
        m_resultsTable->setItem(row, 0, new QTableWidgetItem(fileName));
        
        if (success) {
            m_resultsTable->setItem(row, 1, new QTableWidgetItem(resultText));
            m_resultsTable->setItem(row, 2, new QTableWidgetItem(QString::number(shipProb * 100.0f, 'f', 2) + "%"));
        } else {
            m_resultsTable->setItem(row, 1, new QTableWidgetItem("Error"));
            m_resultsTable->setItem(row, 2, new QTableWidgetItem(errorMsg));
        }
    }
    
    // We only finish execution if this is the last image.
    if (m_inputData && imageIndex == m_inputData->filePaths().size() - 1) {
        if (m_statusLabel) m_statusLabel->setText(QStringLiteral("状态：完成"));

        m_outputData = m_inputData;
        setOutputData(0, m_outputData);
        Q_EMIT dataUpdated(0);
        
        finishExecution();

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
}

void TargetDetectionNode::onError(const QString& error)
{
    Q_EMIT executionError(error);
    setState(ExecutionState::Error);
    if (m_statusLabel) {
        m_statusLabel->setText(QStringLiteral("状态：错误 - ") + error);
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
    modelJson["resultText"] = m_savedResultText;
    modelJson["shipProb"] = m_savedShipProb;
    return modelJson;
}

void TargetDetectionNode::load(QJsonObject const &json)
{
    // Assign fields first
    m_thresholdValue = json["thresholdValue"].toDouble(0.65);
    m_savedResultText = json["resultText"].toString("--");
    m_savedShipProb = json["shipProb"].toDouble(0.0);

    // Call base class load which will trigger validateAndRestoreOutput()
    ExecutableNodeDelegateModel::load(json);

    if (m_thresholdEdit) {
        m_thresholdEdit->setText(QString::number(m_thresholdValue, 'f', 2));
    }

    if (executionState() == ExecutionState::Completed) {
        // Not saving individual result for table now.
        if (m_statusLabel) m_statusLabel->setText(QStringLiteral("状态：完成"));
    }
}

void TargetDetectionNode::onAskUserError(const QString& message, bool* skip)
{
    QMessageBox::StandardButton reply = QMessageBox::question(
        nullptr,
        QStringLiteral("错误"), // 错误
        message,
        QMessageBox::Yes | QMessageBox::No
    );
    *skip = (reply == QMessageBox::Yes);
}

bool TargetDetectionNode::validateAndRestoreOutput()
{
    // Target detection does not output a new file, it just displays text results.
    // If we reach here, it means it was previously marked as Completed.
    // We can directly return true to restore the state.
    return true;
}

} // namespace QtNodes