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
    bool validateInputs() const;
    void updateWidgetSize();
    void onMethodChanged(int index);
    QString generateDefaultOutputName() const;
    void executeProcessing();
    void cleanUpThreadAndWorker();
    void startPreviewGeneration(const QStringList& h5Paths, const QStringList& generatedJpgPaths,
                                const QStringList& types, const QStringList& resultJpgPaths,
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

signals:
    void startDenoise(QList<int> para, double alpha, QString savePath, QString outputNode,
                      QStringList phaseNames, QStringList phasePaths);
};

} // namespace QtNodes

#endif // DENOISENODE_H
