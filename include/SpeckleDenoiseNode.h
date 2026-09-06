#pragma once

#include "NodeDataTypes.h"
#include "NodeUtils.h"
#include "QtNodes/internal/ExecutableNodeDelegateModel.hpp"
#include "BM3DEnhancementTask.h"
#include <QLineEdit>
#include <QLabel>
#include <QCheckBox>
#include <QFileInfo>
#include <QPointer>
#include <QThreadPool>

class IApplicationInterface;
class QStandardItemModel;

namespace QtNodes {

class SpeckleDenoiseNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    SpeckleDenoiseNode();
    ~SpeckleDenoiseNode() override;

    QString caption() const override { return "Speckle Denoise"; }
    QString name() const override { return "SpeckleDenoise"; }

    // 端口定义
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    ProductInputContract productInputContract(PortIndex portIndex) const override;
    ProductOutputContract productOutputContract(PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;

    // 数据输入输出
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;

    // 嵌入Widget
    QWidget* embeddedWidget() override;
    void createWidget();
    QStringList previewImagePaths() const override;

    // 执行
    bool isReady() const;
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

    // 序列化
    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

protected:
    bool stopExecutionIsAsynchronous() const override { return true; }
    bool supportsAutomaticRestartAfterInputChange() const override { return true; }
    bool validateAndRestoreOutput() override;



private Q_SLOTS:
    void onSaveToProjectChanged(int state);
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onError(const QString& error);
    void onCancelled();
    void onAskUserError(quint64 requestId, const QString& message);

private:
    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;
    XMLFile* projectXml() const;

    void executeProcessing();
    void publishResultsToProjectTree(const QStringList& outputNames, const QStringList& outputPaths);
    QString generateOutputFileName() const;
    void updateParameterWidgetsEnableState();

    // UI控件
    QWidget* _widget = nullptr;
    QLabel* m_inputImageLabel = nullptr;
    QCheckBox* m_saveToProjectCheckBox = nullptr;
    QLineEdit* m_outputNodeNameEdit = nullptr;
    QLineEdit* m_outputFileNameEdit = nullptr;

    // 数据
    std::shared_ptr<ImageInfoData> m_inputData = nullptr;
    std::shared_ptr<ImageInfoData> m_outputData = nullptr;
    QStringList m_outputImagePaths;
    QStringList m_preparedOutputPaths;
    QStringList m_generatedOutputPaths;
    QString m_outputNodeName;
    QString m_outputFileName;
    QStringList m_savedOutputFiles;
    bool m_saveToProject = true;
    NodeUtils::OutputTransaction m_outputTransaction;

    // Threading
    QPointer<BM3DEnhancementTask> m_task;
    bool m_isExecuting = false;
};

} // namespace QtNodes
