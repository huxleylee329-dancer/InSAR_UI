#ifndef DEFORMATIONPREVIEWNODE_H
#define DEFORMATIONPREVIEWNODE_H

#include <QtNodes/NodeDelegateModel>
#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <memory>

namespace QtNodes {

class DeformationPreviewNode : public NodeDelegateModel
{
    Q_OBJECT

public:
    DeformationPreviewNode();
    ~DeformationPreviewNode() override;

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Deformation Visualization"); }
    QString name() const override { return QStringLiteral("DeformationPreview"); }

    ProductInputContract productInputContract(PortIndex portIndex) const override;
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;

    std::shared_ptr<NodeData> outData(PortIndex port) override { return nullptr; }
    void setInData(std::shared_ptr<NodeData> data, PortIndex portIndex) override;

    QWidget* embeddedWidget() override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

private slots:
    void onPreviewClicked();

private:
    void createWidget();
    void updateLabels();
    void updateWidgetSize();
    QString projectPath() const;

    // UI elements
    ::QWidget* _widget;
    QLabel* m_inputNodeLabel;
    QPushButton* m_previewBtn;

    // Input data
    std::shared_ptr<ImportedFileData> m_inputData;
};

} // namespace QtNodes

#endif // DEFORMATIONPREVIEWNODE_H
