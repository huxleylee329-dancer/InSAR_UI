#include "DeformationPreviewNode.h"
#include "NodeUtils.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "Deformation_Preview_Window.h"
#include "FormatConversion.h"
#include "Utils.h"
#include "InSARLogManager.h"
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <opencv2/opencv.hpp>

using namespace cv;

namespace QtNodes {

DeformationPreviewNode::DeformationPreviewNode()
    : NodeDelegateModel()
    , _widget(nullptr)
    , m_inputNodeLabel(nullptr)
    , m_previewBtn(nullptr)
{
}

DeformationPreviewNode::~DeformationPreviewNode()
{
}

unsigned int DeformationPreviewNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    return 0; // No output ports
}

NodeDataType DeformationPreviewNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
        return ImportedFileData().type();
    return NodeDataType();
}

void DeformationPreviewNode::setInData(std::shared_ptr<NodeData> data, PortIndex portIndex)
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
    }
    else
    {
        if (m_inputNodeLabel)
        {
            m_inputNodeLabel->setText(QStringLiteral("未连接"));
        }
    }
    
    updateLabels();
}

::QWidget* DeformationPreviewNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

void DeformationPreviewNode::createWidget()
{
    _widget = new ::QWidget();
    _widget->setFixedWidth(300);
    _widget->setStyleSheet("background-color: transparent;");

    QVBoxLayout* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(6);

    // Form row helper
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

    // Preview Button
    m_previewBtn = new QPushButton(QStringLiteral("查看形变时间序列"), _widget);
    m_previewBtn->setStyleSheet("QPushButton { background-color: #10B981; color: white; border-radius: 4px; padding: 6px 12px; font-size: 11px; font-weight: bold; }"
                                "QPushButton:hover { background-color: #059669; }"
                                "QPushButton:disabled { background-color: #4B5563; color: #9CA3AF; }");
    connect(m_previewBtn, &QPushButton::clicked, this, &DeformationPreviewNode::onPreviewClicked);
    layout->addWidget(m_previewBtn);

    updateLabels();
}

void DeformationPreviewNode::onPreviewClicked()
{
    InSARLogManager::LogInfo("DeformationPreviewNode", "onPreviewClicked started.");
    if (!m_inputData || m_inputData->filePaths().isEmpty())
    {
        QMessageBox::warning(nullptr, QStringLiteral("警告"), QStringLiteral("请先连接输入节点！"));
        return;
    }

    QString image_path = m_inputData->filePath();
    QFileInfo fileinfo(image_path);
    QString jpg_path = fileinfo.absolutePath() + "/SBAS_time_series.jpg";

    if (!QFile::exists(jpg_path))
    {
        // Generate JPG preview synchronously
        Utils util;
        FormatConversion FC;
        Mat defomation_velocity, mask;
        int ret = FC.read_array_from_h5(image_path.toStdString().c_str(), "defomation_velocity", defomation_velocity);
        ret += FC.read_array_from_h5(image_path.toStdString().c_str(), "mask", mask);
        if (ret == 0)
        {
            if (defomation_velocity.type() != CV_64F)
            {
                defomation_velocity.convertTo(defomation_velocity, CV_64F);
            }
            util.savephase_white(jpg_path.toStdString().c_str(), "jet", defomation_velocity, mask);
        }
    }

    if (!QFile::exists(jpg_path))
    {
        QMessageBox::warning(nullptr, QStringLiteral("错误"), QStringLiteral("无法生成预览图，请确认输入数据是否完整！"));
        return;
    }

    Deformation_Preview_Window* Pre_wnd = new Deformation_Preview_Window();
    Pre_wnd->View->setPixmap(jpg_path);
    Pre_wnd->View->SetH5Path(image_path);
    Pre_wnd->show();
    Pre_wnd->setAttribute(Qt::WA_DeleteOnClose, true);
    InSARLogManager::LogInfo("DeformationPreviewNode", "onPreviewClicked completed.");
}

void DeformationPreviewNode::updateLabels()
{
    updateWidgetSize();
}

void DeformationPreviewNode::updateWidgetSize()
{
    if (_widget)
    {
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

QJsonObject DeformationPreviewNode::save() const
{
    return NodeDelegateModel::save();
}

void DeformationPreviewNode::load(QJsonObject const& json)
{
    NodeDelegateModel::load(json);
}

QString DeformationPreviewNode::projectPath() const
{
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    if (iface)
    {
        return iface->projectPath();
    }
    return QString();
}

} // namespace QtNodes
