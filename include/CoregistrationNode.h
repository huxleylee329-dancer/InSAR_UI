#pragma once

#include "NodeDataTypes.h"
#include "ImportDataTypes.h"
#include "NodeUtils.h"
#include "QtNodes/internal/ExecutableNodeDelegateModel.hpp"
#include "CoregistrationWorker.h"
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QFutureWatcher>
#include <QStandardItemModel>
#include <QThread>

namespace QtNodes {

class CoregistrationNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    CoregistrationNode();
    ~CoregistrationNode() override;

    QString caption() const override { return QStringLiteral("Coregistration"); }
    QString name() const override { return QStringLiteral("Coregistration"); }

    // Port definitions
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    ProductInputContract productInputContract(PortIndex portIndex) const override;
    ProductOutputContract productOutputContract(PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;

    // Data flow
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;

    // Embedded widget UI
    QWidget* embeddedWidget() override;
    void createWidget();
    QStringList previewImagePaths() const override;

    bool supportsInterferometry() const override { return true; }
    ::QWidget* createInterferometryWidget(::QWidget* parent) override;
    QStringList getOutputPaths() const { return m_outputImagePaths; }
    int masterIndex() const { return m_masterIndex; }
    bool defaultFirstMaster() const { return m_defaultFirstMaster; }

    // Execution
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;
    bool stopExecutionIsAsynchronous() const override { return true; }
    bool supportsAutomaticRestartAfterInputChange() const override { return true; }

    // Serialization
    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

protected:
    bool prepareToStart() override;
    bool validateAndRestoreOutput() override;

private Q_SLOTS:
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onCancelled();
    void onError(const QString& error);
    void updateMasterImageCombo();
    void updateWidgetSize();
    void updateParameterWidgetsEnableState();

private:
    bool isReady() const;
    void executeProcessing();
    QString getRealSavePath() const;
    QString resolveOutputFileName(const QString& originalName) const;
    bool commitResultsToProjectXml(const QStringList& outputNames, const QStringList& outputPaths,
                                   const QList<int>& offsetRows, const QList<int>& offsetCols,
                                   const QString& temporalBaseline, const QString& effectiveBaseline,
                                   const QString& parallelBaseline);
    void publishResultsToProjectTree(const QStringList& outputNames, const QStringList& outputPaths);
    void startPreviewGeneration(const QStringList& h5Paths, const QStringList& finalJpgPaths,
                                bool completeExecution);

    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;
    XMLFile* projectXml() const;

    // UI Widgets
    QWidget* _widget = nullptr;
    QWidget* m_coarseParamsWidget = nullptr;
    QWidget* m_demRowWidget = nullptr;
    QComboBox* m_methodCombo = nullptr;
    QCheckBox* m_defaultFirstMasterCheckBox = nullptr;
    QComboBox* m_masterImageCombo = nullptr;
    
    // Coarse inputs
    QLabel* m_interpLabel = nullptr;
    QComboBox* m_interpCombo = nullptr;
    QLabel* m_blockSizeLabel = nullptr;
    QComboBox* m_blockSizeCombo = nullptr;

    // Fine inputs
    QLabel* m_demPathLabel = nullptr;
    QLineEdit* m_demPathEdit = nullptr;
    QPushButton* m_demBrowseBtn = nullptr;

    // Output node & pattern
    QLineEdit* m_outputNodeNameEdit = nullptr;
    QLineEdit* m_outputFileNameEdit = nullptr;

    // Data
    std::shared_ptr<ImportedFileData> m_inputData = nullptr;
    std::shared_ptr<ImportedFileData> m_demInputData = nullptr;
    std::shared_ptr<ImportedFileData> m_outputData = nullptr;
    std::shared_ptr<ImageInfoData> m_previewData = nullptr;

    QStringList m_outputImagePaths;
    QStringList m_outputJpgPaths;
    QStringList m_savedOutputFiles;
    QString m_outputNodeName;
    QString m_outputFileName;

    // Parameters
    QString m_method; // "Coarse" or "Fine"
    bool m_defaultFirstMaster = true;
    int m_masterIndex = 1;
    int m_interpTimes = 4;
    int m_blockSize = 64;
    QString m_demPath;

    // Threading / Watchers
    CoregistrationWorker* m_worker = nullptr;
    QThread* m_thread = nullptr;
    QFutureWatcher<void> m_remedyWatcher;
    bool m_isExecuting = false;
    bool m_previewGenerationPending = false;
    quint64 m_previewGenerationId = 0;
    QStringList m_previewTemporaryJpgPaths;

    // Prepared data for pre-execution lifecycle
    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    QString m_preparedSavePath;
    QString m_preparedDstNode;
    QString m_preparedProjectName;
    QStringList m_preparedInputPaths;
    QStringList m_preparedTransactionInputPaths;
    QStringList m_preparedH5Paths;
    QStringList m_preparedJpgPaths;
    QStringList m_preparedOutputNames;
    NodeUtils::OutputTransaction m_outputTransaction;
    bool m_xmlDirty = false;
    QStringList m_generatedOutputNames;
    QStringList m_generatedOutputPaths;
    QList<int> m_generatedOffsetRows;
    QList<int> m_generatedOffsetCols;
    QString m_generatedTemporalBaseline;
    QString m_generatedEffectiveBaseline;
    QString m_generatedParallelBaseline;
};

} // namespace QtNodes
