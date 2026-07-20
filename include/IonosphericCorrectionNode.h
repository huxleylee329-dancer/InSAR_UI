#ifndef IONOSPHERICCORRECTIONNODE_H
#define IONOSPHERICCORRECTIONNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "IonosphericCorrectionWorker.h"
#include "NodeUtils.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QStandardItemModel>
#include <QFutureWatcher>
#include <memory>

class IApplicationInterface;
class XMLFile;

namespace QtNodes {

class IonosphericCorrectionNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    IonosphericCorrectionNode();
    ~IonosphericCorrectionNode();

    QString caption() const override { return QStringLiteral("Ionospheric Correction (Split-Spectrum)"); }
    QString name() const override { return QStringLiteral("Ionospheric Correction (Split-Spectrum)"); }
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
    void setExecutionMode(ExecutionMode mode) override;

protected:
    bool validateAndRestoreOutput() override;
    QStringList previewImagePaths() const override;
    bool prepareToStart() override;

private:
    ::QWidget* _widget;
    QDoubleSpinBox* m_subbandRatioSpin;
    QDoubleSpinBox* m_filterStrengthSpin;
    QCheckBox* m_outputTECCheck;
    QLineEdit* m_outputNodeNameEdit;

    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData;
    std::shared_ptr<ImageInfoData> m_imageInfoData;

    QString m_outputNodeName;
    double m_subbandRatio;
    double m_filterStrength;
    bool m_outputTEC;

    IonosphericCorrectionWorker* m_workerThread;
    QThread* m_thread;
    QFutureWatcher<void> m_remedyWatcher;

    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    QString m_preparedDstNode;
    QString m_preparedSavePath;
    QString m_preparedProjectName;
    QString m_preparedSrcNode;
    double m_preparedSubbandRatio = 0.3;
    double m_preparedFilterStrength = 1.0;
    bool m_preparedOutputTEC = false;

    void createWidget();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onCancelled();
    void onError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);
    bool validateInputs() const;
    void updateWidgetSize();
    QString generateDefaultOutputName() const;
    void executeProcessing();

    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;
    XMLFile* projectXml() const;

    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;
    bool supportsAutomaticRestartAfterInputChange() const override { return true; }

signals:
    void startCorrection(double subbandRatio, double filterStrength, bool outputTEC,
                         QString save_path, QString project_name,
                         QString node_name, QString file_name,
                         QStandardItemModel* model);
};

} // namespace QtNodes

#endif // IONOSPHERICCORRECTIONNODE_H
