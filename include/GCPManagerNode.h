// include/GCPManagerNode.h
#pragma once
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "GCPPoint.h"
#include "GCPDatabase.h"
#include "GCPManagerWorker.h"
#include "NodeUtils.h"
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QThread>
#include <QJsonObject>
#include <memory>
#include <vector>

namespace QtNodes {

class GCPManagerNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT
public:
    GCPManagerNode();
    ~GCPManagerNode() override;

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("GCP Manager"); }
    QString name() const override { return QStringLiteral("GCPManager"); }
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    ::QWidget* embeddedWidget() override;

    // 序列化
    QJsonObject save() const override;
    void load(QJsonObject const &json) override;
    ProductInputContract productInputContract(PortIndex portIndex) const override;
    ProductOutputContract productOutputContract(PortIndex portIndex) const override;

protected:
    // 计算执行生命周期
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;
    bool prepareToStart() override;
    bool validateAndRestoreOutput() override;

private slots:
    void onOpenDialogClicked();
    
    // Worker 后台执行的接收信号槽
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onEvaluationFinished(const std::vector<GCPPoint>& updatedGcps, const GCPEvaluationResult& result, const QString& reportText);
    void onCancelled();
    void onError(const QString& error);

private:
    void createWidget();
    void updateLabels();
    void initDatabase();
    void invalidateNodeData(); // GCP数据失效与下游传播
    void updateWidgetSize();   // 尺寸同步更新自愈
    QString getOutputH5Path() const;
    void cleanUpThreadAndWorker();
    QString getReportTxtPath() const;
    QString projectDir() const;

    // UI 元素
    ::QWidget* _widget;
    QLabel* m_inputNodeLabel;
    QPushButton* m_btnOpenDialog;
    QLineEdit* m_maxResidualEdit;
    QLineEdit* m_sigmaThresholdEdit;
    QComboBox* m_minQualityCombo;
    QLineEdit* m_outputNodeNameEdit = nullptr;
    QLabel* m_statusLabel;

    // 内部数据库
    GCPDatabase* m_db;
    
    // 端口数据
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData;  // Port 0: GCPResults.h5
    std::shared_ptr<ImageInfoData> m_reportData;     // Port 1: 评估报告 txt 路径

    // 参数项
    double m_maxResidual;
    double m_thresholdSigma;
    int m_minQuality;
    QString m_outputNodeName;

    // Worker 线程
    GCPManagerWorker* m_worker;
    QThread* m_thread;
    std::vector<GCPPoint> m_pendingGcps;
    QString m_pendingReportText;
    double m_pendingRmsResidual2d = 0.0;
    int m_pendingNumGcpUsed = 0;
    int m_pendingNumGcpRejected = 0;
    bool m_hasPendingEvaluation = false;
    QStringList m_preparedInputPaths;
    QStringList m_preparedOutputPaths;
    NodeUtils::OutputTransaction m_outputTransaction;

    bool supportsAutomaticRestartAfterInputChange() const override { return true; }

signals:
    void startProcess(
        const QString& projectPath,
        const QString& projectName,
        const QString& inputH5Path,
        const QString& outputH5Path,
        const std::vector<GCPPoint>& gcps,
        double thresholdSigma,
        int minQuality
    );
};

} // namespace QtNodes
