#ifndef GEOCODINGNODE_H
#define GEOCODINGNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "NodeUtils.h"
#include "GeocodingWorker.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QStandardItemModel>
#include <QFutureWatcher>
#include <QHash>
#include <memory>

class IApplicationInterface;
class XMLFile;

namespace QtNodes {

// 单个地理编码输出文件的诊断结果
struct GeocodingFileDiagnostics {
    QString fileName;
    double lonEast = 0.0;
    double lonWest = 0.0;
    double latNorth = 0.0;
    double latSouth = 0.0;
    bool hasGeoBounds = false;
    bool geoBoundsValid = false;

    int rows = 0;
    int cols = 0;
    double validPixelPercent = 0.0;
    QString primaryDataset;
    bool datasetFound = false;

    int fileType = 0;
    int fileMultiRg = 0;
    int fileMultiAz = 0;
    QString fileProductLevel;
    QString fileDemName;
    bool hasType = false;
    bool hasMultiRg = false;
    bool hasMultiAz = false;
    bool hasProductLevel = false;
    bool hasDemName = false;
};

// 地理编码验证结果汇总
struct GeocodingValidationResults {
    bool success = false;
    QString errorMsg;

    // 期望参数
    int expectedType = 1;
    int expectedMultiRg = 1;
    int expectedMultiAz = 1;
    QString expectedProductLevel;
    QString expectedDemName;
    bool hasExpectedDemName = false;

    // 实际聚合参数
    int actualType = 0;
    int actualMultiRg = 0;
    int actualMultiAz = 0;
    QString actualProductLevel;
    QString actualDemName;

    bool hasActualType = false;
    bool hasActualMultiRg = false;
    bool hasActualMultiAz = false;
    bool hasActualProductLevel = false;
    bool hasActualDemName = false;

    bool typeInconsistent = false;
    bool multiRgInconsistent = false;
    bool multiAzInconsistent = false;
    bool productLevelInconsistent = false;

    // 聚合特征值
    int totalFiles = 0;
    int validFiles = 0;
    int outRows = 0;
    int outCols = 0;
    double avgValidPixelPercent = 0.0;
    double minLonWest = 0.0;
    double maxLonEast = 0.0;
    double minLatSouth = 0.0;
    double maxLatNorth = 0.0;
    bool allGeoBoundsValid = false;
    bool hasAnyLegacyResults = false;

    QList<GeocodingFileDiagnostics> fileDiagnostics;
};

class GeocodingNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT
    friend class GeocodingValidationWidget;

public:
    GeocodingNode();
    ~GeocodingNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Geocoding"); }
    QString name() const override { return QStringLiteral("Geocoding"); }
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    ProductInputContract productInputContract(PortIndex portIndex) const override;
    ProductOutputContract productOutputContract(PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;
    QList<QList<PortIndex>> alternativeInputGroups() const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    ::QWidget* embeddedWidget() override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    std::vector<QString> processingInfo() const override;

    // Validation interface implementation
    bool supportsValidation() const override { return true; }
    ::QWidget* createValidationWidget(::QWidget* parent) override;

    QString validationCacheKey() const;
    bool loadValidationCache(const QString& key, GeocodingValidationResults& results) const;
    void storeValidationCache(const QString& key, const GeocodingValidationResults& results);
    QString expectedOutputProductLevel() const;
    QString effectiveAuxiliaryDemFileName() const;

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;
    QString portBindingSummary(PortType portType, PortIndex portIndex) const override;

protected:
    bool prepareToStart() override;
    bool validateAndRestoreOutput() override;
    QStringList previewImagePaths() const override;

private:
    ::QWidget* _widget;
    QComboBox* m_typeCombo;
    
    QLabel* m_multiRgLabel;
    QSpinBox* m_multiRgSpin;
    QLabel* m_multiAzLabel;
    QSpinBox* m_multiAzSpin;
    
    QLineEdit* m_outputNodeNameEdit;
    
    QLabel* m_demPathLabel = nullptr;
    QComboBox* m_demLabelCombo = nullptr;

    // Input/output data
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<AuxiliaryDemData> m_auxiliaryDemInputData;
    std::shared_ptr<AuxiliaryDemReferenceData> m_auxiliaryDemReferenceData;
    std::shared_ptr<InsarDemData> m_insarDemInputData;
    std::shared_ptr<ImportedFileData> m_outputData;
    std::shared_ptr<ImageInfoData> m_imageInfoData;
    
    // Parameters
    QString m_outputNodeName;
    QString m_demPath;
    QString m_auxiliaryDemLabel;
    QString m_legacyDemResourceId;
    QString m_legacyDemProvenanceId;
    int m_type;       // 1: 干涉产品, 2: SAR图像
    int m_multiRg;    // default 1
    int m_multiAz;    // default 1

    // Validation cache
    QHash<QString, GeocodingValidationResults> m_validationCache;

    // Worker thread
    GeocodingWorker* m_workerThread;
    QThread* m_thread;

    // Remedy watcher for missing JPG regeneration
    QFutureWatcher<void> m_remedyWatcher;

    // Helper methods
    void createWidget();
    void refreshAuxiliaryDemLabels();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onCancelled();
    void onError(const QString& error);
    void onGeocodingGenerated(const GeocodingFileResult& result);
    void commitGeocodingResult(const GeocodingFileResult& result);
    void publishGeocodingResultToProjectTree(const GeocodingFileResult& result);
    bool validateInputs() const;
    bool resolveInsarDemProduct(const std::shared_ptr<InsarDemData>& data,
                                QStringList* resolvedPaths,
                                QString* errorMessage = nullptr) const;
    void updateWidgetSize();
    void updateParameterWidgetsEnableState();
    void onTypeChanged(int index);
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
    QStringList m_preparedInputPaths;
    QString m_preparedProductLevel;
    int m_preparedMasterIndex = 0;
    int m_preparedType = 1;
    int m_preparedMultiRg = 1;
    int m_preparedMultiAz = 1;
    QString m_preparedDemPath;
    NodeUtils::AuxiliaryDemBinding m_preparedAuxiliaryDemBinding;
    NodeUtils::DemExecutionSnapshot m_preparedDemExecutionSnapshot;
    bool m_xmlDirty = false;
    NodeUtils::OutputTransaction m_outputTransaction;
    QList<GeocodingFileResult> m_pendingGeocodingResults;

signals:
    void startGeocoding(int type, int multi_rg, int multi_az, QString savePath, QStringList inputPaths,
        QString productLevel, int masterIndex, QString dstNode);
    void startGeocodingWithDem(int type, int multi_rg, int multi_az, QString savePath, QStringList inputPaths,
        QString productLevel, int masterIndex, QString dstNode, QString demPath);
};

} // namespace QtNodes

#endif // GEOCODINGNODE_H
