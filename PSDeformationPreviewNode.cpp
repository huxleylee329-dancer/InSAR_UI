#include "PSDeformationPreviewNode.h"
#include "Deformation_Preview_Window.h"
#include "FormatConversion.h"
#include "Utils.h"
#include "NodeUtils.h"
#include "InSARLogManager.h"
#include "IApplicationInterface.h"
#include "InterfaceManager.h"
#include "MainWindow.h"
#include "WorkspaceUI.h"
#include <QMessageBox>
#include <QFileInfo>
#include <QFile>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace QtNodes {

PSDeformationPreviewNode::PSDeformationPreviewNode()
    : _widget(nullptr)
    , m_inputNodeLabel(nullptr)
    , m_previewBtn(nullptr)
{
}

PSDeformationPreviewNode::~PSDeformationPreviewNode()
{
}

unsigned int PSDeformationPreviewNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 0;
}

NodeDataType PSDeformationPreviewNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    if (portType == PortType::In) {
        return NodeDataType{"imported_file", "Imported File"};
    }
    return NodeDataType();
}

void PSDeformationPreviewNode::setInData(std::shared_ptr<NodeData> data, PortIndex portIndex)
{
    Q_UNUSED(portIndex);
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);
    updateLabels();
}

::QWidget* PSDeformationPreviewNode::embeddedWidget()
{
    if (!_widget) {
        createWidget();
    }
    return _widget;
}

void PSDeformationPreviewNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300);

    QVBoxLayout* mainLayout = new QVBoxLayout(_widget);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(4);

    // Row 1: Input node display
    QHBoxLayout* inputLayout = new QHBoxLayout();
    QLabel* inputTitleLabel = new QLabel(QStringLiteral("输入节点:"));
    inputTitleLabel->setFixedWidth(80);
    m_inputNodeLabel = new QLabel(QStringLiteral("等待输入"));
    m_inputNodeLabel->setStyleSheet("color: gray;");
    inputLayout->addWidget(inputTitleLabel);
    inputLayout->addWidget(m_inputNodeLabel);
    mainLayout->addLayout(inputLayout);

    // Row 2: Preview button
    m_previewBtn = new QPushButton(QStringLiteral("查看 PS 形变结果"));
    m_previewBtn->setMinimumHeight(36);
    m_previewBtn->setEnabled(false);
    mainLayout->addWidget(m_previewBtn);

    connect(m_previewBtn, &QPushButton::clicked, this, &PSDeformationPreviewNode::onPreviewClicked);

    updateLabels();
}

void PSDeformationPreviewNode::updateLabels()
{
    if (m_inputNodeLabel) {
        if (m_inputData) {
            m_inputNodeLabel->setText(m_inputData->nodeName());
            m_inputNodeLabel->setStyleSheet("color: black;");
            m_previewBtn->setEnabled(true);
        } else {
            m_inputNodeLabel->setText(QStringLiteral("等待输入"));
            m_inputNodeLabel->setStyleSheet("color: gray;");
            m_previewBtn->setEnabled(false);
        }
    }
    updateWidgetSize();
}

void PSDeformationPreviewNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

void PSDeformationPreviewNode::onPreviewClicked()
{
    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        QMessageBox::warning(nullptr, QStringLiteral("警告"), QStringLiteral("请先连接输入节点！"));
        return;
    }

    QString image_path = m_inputData->filePath();
    QFileInfo fileinfo(image_path);
    QString jpg_path = fileinfo.absolutePath() + "/deformation_velocity.jpg";

    if (!QFile::exists(jpg_path)) {
        // 后台静默生成 JPG 预览（如果不存在）
        Utils util;
        FormatConversion FC;
        cv::Mat velocity_1d, ps_coords, mask;
        int ret = -1;
        {
            NodeUtils::Hdf5Locker locker;
            ret = (NodeUtils::readMatFromH5(image_path, "deformation_velocity", velocity_1d, CV_64F) &&
                   NodeUtils::readMatFromH5(image_path, "ps_coordinates", ps_coords) &&
                   NodeUtils::readMatFromH5(image_path, "mask", mask)) ? 0 : -1;
        }
        
        if (ret == 0 && !velocity_1d.empty() && !ps_coords.empty() && !mask.empty()) {
            int rows = mask.rows;
            int cols = mask.cols;
            cv::Mat velocity_2d = cv::Mat::zeros(rows, cols, CV_64FC1);
            int ps_count = velocity_1d.rows;

            for (int i = 0; i < ps_count; ++i) {
                int r = ps_coords.at<int>(i, 0);
                int c = ps_coords.at<int>(i, 1);
                velocity_2d.at<double>(r, c) = velocity_1d.at<double>(i, 0);
            }

            util.savephase_white(jpg_path.toStdString().c_str(), "jet", velocity_2d, mask);
        }
    }

    if (!QFile::exists(jpg_path)) {
        QMessageBox::warning(nullptr, QStringLiteral("错误"), QStringLiteral("无法生成预览图，请确认输入数据是否完整！"));
        return;
    }

    Deformation_Preview_Window* Pre_wnd = new Deformation_Preview_Window();
    Pre_wnd->View->setPixmap(jpg_path);
    Pre_wnd->View->SetH5Path(image_path);
    Pre_wnd->show();
    Pre_wnd->setAttribute(Qt::WA_DeleteOnClose, true);
    
    InSARLogManager::LogInfo("PSDeformationPreviewNode", "onPreviewClicked completed.");
}

QJsonObject PSDeformationPreviewNode::save() const
{
    return NodeDelegateModel::save();
}

void PSDeformationPreviewNode::load(QJsonObject const& json)
{
    NodeDelegateModel::load(json);
}

QString PSDeformationPreviewNode::projectPath() const
{
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        return iface->projectPath();
    }
    return QString();
}

} // namespace QtNodes
