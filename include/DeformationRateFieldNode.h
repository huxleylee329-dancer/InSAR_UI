#ifndef DEFORMATIONRATEFIELDNODE_H
#define DEFORMATIONRATEFIELDNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "DeformationRateFieldWorker.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointer>
#include <memory>
#include "NodeUtils.h"

namespace QtNodes {

class DeformationRateFieldNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    DeformationRateFieldNode();
    ~DeformationRateFieldNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Rate Field Analysis"); }
    QString name() const override { return QStringLiteral("DeformationRateField"); }
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    ::QWidget* embeddedWidget() override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;

protected:
    bool validateAndRestoreOutput() override;
    QStringList previewImagePaths() const override;

private:
    // UI elements
    ::QWidget* _widget;
    QLabel* m_inputNodeLabel;
    QComboBox* m_modelTypeCombo;
    QComboBox* m_confidenceLevelCombo;
    QLineEdit* m_cohThreshHighEdit;
    QLineEdit* m_cohThreshMidEdit;
    QLineEdit* m_uncertaintyThreshHighEdit;
    QLineEdit* m_uncertaintyThreshMidEdit;
    QComboBox* m_colorMapCombo;
    QCheckBox* m_showContourCheck;
    QLineEdit* m_contourIntervalEdit;
    QCheckBox* m_showArrowCheck;
    QLineEdit* m_arrowSpacingEdit;
    QLineEdit* m_outputNodeNameEdit;
    QLabel* m_resultLabel;

    // Input/output data
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData; // H5 output
    std::shared_ptr<ImageInfoData> m_previewData;  // JPG preview output

    // Parameters
    int    m_modelType;              // 1=线性, 2=二次多项式
    double m_confidenceLevel;        // 置信水平 (0.95)
    double m_coherenceThreshHigh;
    double m_coherenceThreshMid;
    double m_uncertaintyThreshHigh;
    double m_uncertaintyThreshMid;
    int    m_colorMap;               // 0=蓝-白-红, 1=热力图, 2=彩虹
    bool   m_showContour;
    int    m_contourInterval;
    bool   m_showArrow;
    int    m_arrowSpacing;
    QString m_outputNodeName;

    // Worker thread
    QPointer<DeformationRateFieldWorker> m_worker;
    QPointer<QThread> m_thread;
    QString m_generatedOutputPath;
    bool m_resultPublishingFailed = false;

    // Helper methods
    void createWidget();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onCancelled();
    void onError(const QString& error);
    void onResultsGenerated(const QString& dstNode, const QString& outputH5Path);
    bool validateInputs() const;
    void updateLabels();
    void updateWidgetSize();
    void executeProcessing();
    void cleanUpThreadAndWorker();
    void generateStaticPreviewJpg(bool completeExecution = false);
    QString projectPath() const;
    QString projectName() const;

    // Executable interface implementation
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;
    bool prepareToStart() override;
    bool supportsAutomaticRestartAfterInputChange() const override { return true; }

private:
    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;

signals:
    void startProcess();
};

} // namespace QtNodes

#endif // DEFORMATIONRATEFIELDNODE_H
