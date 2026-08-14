#ifndef S1TOPSBACKGEOCODINGNODE_H
#define S1TOPSBACKGEOCODINGNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "S1TopsBackGeocodingWorker.h"
#include "NodeUtils.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QPointer>
#include <QStandardItemModel>
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QVector>
#include <memory>
#include <vector>

class IApplicationInterface;
class XMLFile;

namespace QtNodes {

class S1TopsBackGeocodingNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    S1TopsBackGeocodingNode();
    ~S1TopsBackGeocodingNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("S1 TOPS Back-Geocoding"); }
    QString name() const override { return QStringLiteral("S1TopsBackGeocoding"); }
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

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;
    bool supportsInterferometry() const override { return true; }
    ::QWidget* createInterferometryWidget(::QWidget* parent) override;
    QStringList getOrderedH5Paths() const;
    QStringList getInputH5Paths() const;
    QStringList orderedInputH5Paths() const;
    std::vector<QString> processingInfo() const override;

protected:
    bool validateAndRestoreOutput() override;
    QStringList previewImagePaths() const override;
    bool prepareToStart() override;

private:
    struct RegistrationOffsetSummary
    {
        QString slaveName;
        double azimuthOffset = 0.0;
        double rangeOffset = 0.0;
        bool hasAzimuthOffset = false;
        bool hasRangeOffset = false;
    };

    struct PreviewGenerationResult
    {
        QStringList generationFailures;
    };

    QComboBox* m_masterImageCombo;
    QCheckBox* m_defaultMasterCheckBox;
    QCheckBox* m_esdCheckBox;
    QCheckBox* m_rangeRefineCheckBox;
    QLineEdit* m_outputNodeNameEdit;

    QLabel* m_demPathLabel = nullptr;
    QComboBox* m_demLabelCombo = nullptr;

    // Input/output data
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<DEMFileData> m_demInputData;
    std::shared_ptr<AuxiliaryDemData> m_auxiliaryDemEntityData;
    std::shared_ptr<AuxiliaryDemReferenceData> m_auxiliaryDemReferenceData;
    std::shared_ptr<ImportedFileData> m_outputData;
    std::shared_ptr<ImageInfoData> m_imageInfoData;
    QStringList m_savedOutputPaths;
    QString m_savedMasterOutputPath;
    QString m_outputNodeName;
    QString m_demPath;
    QString m_auxiliaryDemLabel;
    QString m_legacyDemResourceId;
    QString m_legacyDemProvenanceId;
    int m_masterIndex;
    int m_appliedMasterIndex = 1;
    bool m_useDefaultMaster;
    bool m_bESD;
    bool m_bRangeRefine;
    void refreshAuxiliaryDemLabels();

    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    QString m_preparedDstNode;
    QString m_preparedSavePath;
    QString m_preparedDstProject;
    QString m_preparedSrcNode;
    QString m_preparedDemPath;
    NodeUtils::AuxiliaryDemBinding m_preparedAuxiliaryDemBinding;
    NodeUtils::DemExecutionSnapshot m_preparedDemExecutionSnapshot;
    int m_preparedMasterIndex;
    bool m_preparedBESD;
    bool m_preparedBRangeRefine;
    int m_preparedImagesNumber;
    QStringList m_preparedInputPaths;
    QStringList m_preparedTransactionInputPaths;
    bool m_processingWarning = false;
    QStringList m_processingQualityWarnings;
    QVector<RegistrationOffsetSummary> m_registrationOffsets;
    QStringList m_registrationOverviewPaths;
    QElapsedTimer m_executionTimer;
    NodeUtils::OutputTransaction m_outputTransaction;
    quint64 m_executionGeneration = 0;
    quint64 m_activeGeneration = 0;
    QStringList m_pendingWorkerH5Paths;
    QStringList m_pendingPreviewH5Paths;
    QStringList m_pendingPreviewJpgPaths;
    QStringList m_pendingPreviewOverviewPaths;
    QString m_pendingWorkerError;
    bool m_pendingWorkerCancelled = false;
    bool m_workerFinishedSuccessfully = false;
    bool m_userCancellationRequested = false;
    bool m_executionSuperseded = false;
    bool m_destroying = false;
    int m_lastLoggedProgress = -1;

    // Worker thread
    QPointer<S1TopsBackGeocodingWorker> m_workerThread;
    QPointer<QThread> m_thread;

    // Remedy watcher for missing JPG regeneration
    QFutureWatcher<PreviewGenerationResult> m_remedyWatcher;

    // Helper methods
    void createWidget();
    void updateParameterWidgetsEnableState();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished(
        const QStringList& regisH5Paths,
        const QString& dstNode,
        const QString& dstProject,
        const QString& savePath,
        int masterIndex,
        bool hasQualityWarning,
        const QStringList& qualityWarnings
    );
    void onCancelled(const QStringList& cleanupFailures);
    void onError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);
    bool validateInputs() const;
    void updateLabels();
    void updateMasterImageCombo();
    QString generateDefaultOutputName() const;
    QStringList moveMasterToFront(const QStringList& paths, int masterIndex) const;
    QStringList jpgPathsFromH5Paths(const QStringList& h5Paths) const;
    QStringList registrationOverviewPathsFromH5Paths(const QStringList& h5Paths) const;
    void updateRegistrationOffsets(const QStringList& h5Paths);
    QString resolveSavedOutputPath(const QString& path, const QString& dstNode) const;
    QStringList restoreOrderedH5Paths(const QString& dstNode) const;
    bool isCompleteBackGeocodingOutput(const QString& path) const;
    bool syncProjectTreeOrder(const QStringList& h5Paths, const QString& dstNode);
    bool syncProjectXmlOrder(const QStringList& h5Paths, const QString& dstNode);
    bool isCurrentGeneration(quint64 generation) const;
    void invalidateExecutionGeneration();
    void beginStagedPreview(quint64 generation);
    void finalizeStagedTransaction(quint64 generation);
    void failStagedTransaction(const QString& error, bool cancelled = false);
    bool reloadProjectXmlAfterRecovery(QString* errorMessage);
    QStringList expectedH5PathsForTransaction() const;
    QStringList expectedPreviewPathsForH5Paths(const QStringList& h5Paths) const;
    void executeProcessing();

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

signals:
    void startBackGeocoding(int masterIndex, QString savePath, QString dstProject,
                            QString dstNode, QStringList inputPaths, bool b_ESD);
};

} // namespace QtNodes

#endif // S1TOPSBACKGEOCODINGNODE_H
