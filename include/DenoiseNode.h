#ifndef DENOISENODE_H
#define DENOISENODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "NodeUtils.h"
#include "DenoiseWorker.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QStandardItemModel>
#include <QFutureWatcher>
#include <memory>

class IApplicationInterface;
class XMLFile;

namespace QtNodes {

// Aggregated phase quality metrics for a single image (or averaged across files).
struct PhaseQualityMetrics {
    double gradientRms = 0.0;
    double residueDensity = 0.0;
    double positiveResidueCount = 0.0;
    double negativeResidueCount = 0.0;
    double totalResidueCount = 0.0;
    double validPlaquetteCount = 0.0;
    bool hasGradient = false;
    bool hasResidueDensity = false;
};

// Aggregated validation results across all input/output image pairs.
struct ValidationResults {
    bool success = false;
    QString errorMsg;
    // Compare values
    int expectedMethod = 1;
    int actualMethod = 0;
    int expectedPrefilter = 5;
    int actualPrefilter = 0;
    int expectedSlopeWindow = 5;
    int actualSlopeWindow = 0;
    int expectedGoldsteinWin = 64;
    int actualGoldsteinWin = 0;
    int expectedNPad = 16;
    int actualNPad = 0;
    double expectedAlpha = 0.5;
    double actualAlpha = 0.0;
    bool hasActualMethod = false;
    bool hasActualPrefilter = false;
    bool hasActualSlopeWindow = false;
    bool hasActualGoldsteinWin = false;
    bool hasActualNPad = false;
    bool hasActualAlpha = false;
    bool hasActualDenoiseDl = false;
    bool methodInconsistent = false;
    bool prefilterInconsistent = false;
    bool slopeWindowInconsistent = false;
    bool goldsteinWinInconsistent = false;
    bool nPadInconsistent = false;
    bool alphaInconsistent = false;
    // Aggregated size / wrapped-difference stats
    int inRows = 0, inCols = 0;
    int outRows = 0, outCols = 0;
    int imagePairCount = 0;
    int matchingSizePairCount = 0;
    double wrappedDiffMean = 0.0;
    double wrappedDiffStd = 0.0;
    double wrappedDiffResultant = 0.0;
    bool hasWrappedDifference = false;
    PhaseQualityMetrics inputQuality;
    PhaseQualityMetrics outputQuality;
};

class DenoiseNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    DenoiseNode();
    ~DenoiseNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Phase Filtering"); }
    QString name() const override { return QStringLiteral("Denoise"); }
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    ProductInputContract productInputContract(PortIndex portIndex) const override;
    ProductOutputContract productOutputContract(PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    ::QWidget* embeddedWidget() override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    std::vector<QString> processingInfo() const override;

    // Validation result cache for the detail view (aggregated scalars only, no cv::Mat stored)
    QString validationCacheKey() const;
    bool loadValidationCache(const QString& key, ValidationResults& results) const;
    void storeValidationCache(const QString& key, const ValidationResults& results);

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;
    bool supportsValidation() const override { return true; }
    ::QWidget* createValidationWidget(::QWidget* parent) override;

protected:
    bool prepareToStart() override;
    bool validateAndRestoreOutput() override;
    QStringList previewImagePaths() const override;

private:
    // UI elements
    ::QWidget* _widget;
    QComboBox* m_methodCombo;
    
    QLabel* m_prefilterWinLabel;
    QLineEdit* m_prefilterWinEdit;
    QLabel* m_slopeWinLabel;
    QLineEdit* m_slopeWinEdit;
    
    QLabel* m_goldsteinWinLabel;
    QLineEdit* m_goldsteinWinEdit;
    QLabel* m_nPadLabel;
    QLineEdit* m_nPadEdit;
    QLabel* m_alphaLabel;
    QLineEdit* m_alphaEdit;
    
    QLineEdit* m_outputNodeNameEdit;

    // Input/output data
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData;
    std::shared_ptr<ImageInfoData> m_imageInfoData;
    
    // Parameters
    QString m_outputNodeName;
    int m_method; // 1: Slope, 2: Goldstein, 3: DL
    int m_prefilterWin;
    int m_slopeWin;
    int m_goldsteinWin;
    int m_nPad;
    double m_alpha;

    // Worker thread
    DenoiseWorker* m_workerThread;
    QThread* m_thread;

    // Remedy watcher for missing JPG regeneration
    QFutureWatcher<void> m_remedyWatcher;

    // Helper methods
    void createWidget();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onCancelled();
    void onError(const QString& error);
    void onDenoiseGenerated(const DenoiseFileResult& result);
    void commitDenoiseResult(const DenoiseFileResult& result);
    void publishDenoiseResultToProjectTree(const DenoiseFileResult& result);
    bool validateInputs() const;
    void updateWidgetSize();
    void onMethodChanged(int index);
    QString generateDefaultOutputName() const;
    void executeProcessing();
    void cleanUpThreadAndWorker();
    void startPreviewGeneration(const QStringList& h5Paths, const QStringList& generatedJpgPaths,
                                const QStringList& types, const QStringList& resultH5Paths, const QStringList& resultJpgPaths,
                                bool completeExecution);

    // Context helpers
    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;
    XMLFile* projectXml() const;

    // Executable interface implementation
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;
    bool stopExecutionIsAsynchronous() const override { return true; }
    bool supportsAutomaticRestartAfterInputChange() const override { return true; }

    // Prepared data for pre-execution lifecycle
    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    QString m_preparedDstNode;
    QStringList m_preparedOutputPaths;
    QList<int> m_preparedPara;
    double m_preparedAlpha = 0.0;
    NodeUtils::OutputTransaction m_outputTransaction;
    QList<DenoiseFileResult> m_pendingDenoiseResults;
    bool m_xmlDirty = false;

    // Validation result cache for the detail view. The key includes file fingerprints
    // (size + last modified time) of every input/output file, and the cache is cleared
    // whenever inputs change or a new output is committed, so stale results are never reused.
    QHash<QString, ValidationResults> m_validationCache;

signals:
    void startDenoise(QList<int> para, double alpha, QString savePath, QString outputNode,
                      QStringList phaseNames, QStringList phasePaths);
};

} // namespace QtNodes

#endif // DENOISENODE_H
