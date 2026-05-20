#pragma once

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "NodeDataTypes.h"
#include "QtNodes/internal/ExecutableNodeDelegateModel.hpp"
#include "MyThread.h"
#include <QLineEdit>
#include <QLabel>
#include <QCheckBox>
#include <QFileInfo>
#include <QThread>


class IApplicationInterface;
class QStandardItemModel;

namespace QtNodes {

class ClutterSuppressionNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    ClutterSuppressionNode();
    ~ClutterSuppressionNode() override;

    QString caption() const override { return "Clutter Suppression"; }
    QString name() const override { return "ClutterSuppression"; }

    // 端口定义
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;

    // 数据输入输出
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;

    // 嵌入Widget
    QWidget* embeddedWidget() override;
    void createWidget();

    // 执行
    bool isReady() const;
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

    // 序列化
    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

signals:
    void startClutterSuppression(QString inputPath, QString outputPath, QString nodeName, QString fileName,
                            QString projectPath, QString projectName, QStandardItemModel* model, bool saveToProject, XMLFile* projectXml);

private Q_SLOTS:
    void onSaveToProjectChanged(int state);
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);

private:
    IApplicationInterface* getProjectContext() const;
    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;
    XMLFile* projectXml() const;

    void executeProcessing();
    QString generateOutputFileName() const;

    // UI控件
    QWidget* _widget = nullptr;
    QLabel* m_inputImageLabel = nullptr;
    QCheckBox* m_saveToProjectCheckBox = nullptr;
    QLineEdit* m_outputNodeNameEdit = nullptr;
    QLineEdit* m_outputFileNameEdit = nullptr;
    QLabel* m_statusLabel = nullptr;

    // 数据
    std::shared_ptr<ImageInfoData> m_inputData = nullptr;
    std::shared_ptr<ImageInfoData> m_outputData = nullptr;
    QString m_outputImagePath;
    QString m_outputFileName;

    // Threading
    QThread* m_thread = nullptr;
    MyThread* m_workerThread = nullptr;
};

} // namespace QtNodes
