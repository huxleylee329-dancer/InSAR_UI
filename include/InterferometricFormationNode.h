#ifndef INTERFEROMETRICFORMATIONNODE_H
#define INTERFEROMETRICFORMATIONNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "InterferometricFormationWorker.h"
#include "NodeUtils.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QThread>
#include <QStandardItemModel>
#include <QFileInfo>
#include <QRegularExpression>
#include <QFutureWatcher>
#include <memory>

class IApplicationInterface;
class XMLFile;

namespace QtNodes {

class InterferometricFormationNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    InterferometricFormationNode();
    ~InterferometricFormationNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Interferometric Formation"); }
    QString name() const override { return QStringLiteral("InterferometricFormation"); }
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
    bool prepareToStart() override;

private:
    ::QWidget* _widget;
    QComboBox* m_masterImageCombo;
    QCheckBox* m_defaultMasterCheckBox;
    QCheckBox* m_deflatCheckBox;
    QCheckBox* m_topoRemovalCheckBox;
    QCheckBox* m_coherenceCheckBox;
    QLabel* m_winWLabel;
    QLineEdit* m_winWEdit;
    QLabel* m_winHLabel;
    QLineEdit* m_winHEdit;
    QLineEdit* m_multilookRgEdit;
    QLineEdit* m_multilookAzEdit;
    QLineEdit* m_outputNodeNameEdit;

    // Input/output data
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData;
    std::shared_ptr<ImageInfoData> m_imageInfoData;
    
    // Parameters
    QString m_outputNodeName;
    int m_masterIndex;
    bool m_useDefaultMaster;
    bool m_isDeflat;
    bool m_isTopoRemoval;
    bool m_isCoherence;
    int m_winW;
    int m_winH;
    int m_multilookRg;
    int m_multilookAz;

    // Worker thread
    InterferometricFormationWorker* m_workerThread;
    QThread* m_thread;

    // Remedy watcher for missing JPG regeneration
    QFutureWatcher<void> m_remedyWatcher;

    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    QString m_preparedDstNode;
    QString m_preparedSavePath;
    QString m_preparedProjectName;
    QString m_preparedFileName;
    bool m_preparedIsDeflat = true;
    bool m_preparedIsTopoRemoval = false;
    bool m_preparedIsCoherence = false;
    int m_preparedMasterIndex = 0;
    int m_preparedWinW = 5;
    int m_preparedWinH = 5;
    int m_preparedMultilookRg = 1;
    int m_preparedMultilookAz = 1;

    // Helper methods
    void createWidget();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);
    bool validateInputs() const;
    void updateLabels();
    void updateMasterImageCombo();
    void onCoherenceStateChanged(int state);
    QString generateDefaultOutputName() const;
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

signals:
    void startInterferometric(bool isdeflat, bool istopo_removal, bool iscoherence,
                              int master_index, int win_width, int win_height,
                              int multilook_rg, int multilook_az, QString save_path,
                              QString project_name, QString node_name, QString file_name,
                              QStandardItemModel* model);
};

} // namespace QtNodes

#endif // INTERFEROMETRICFORMATIONNODE_H
