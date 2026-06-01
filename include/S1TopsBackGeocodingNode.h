#ifndef S1TOPSBACKGEOCODINGNODE_H
#define S1TOPSBACKGEOCODINGNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "S1TopsBackGeocodingWorker.h"
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
    QComboBox* m_projectCombo;
    QComboBox* m_dataNodeCombo;
    QComboBox* m_masterImageCombo;
    QCheckBox* m_defaultMasterCheckBox;
    QCheckBox* m_esdCheckBox;
    QLineEdit* m_outputNodeNameEdit;

    // Input/output data
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData;
    std::shared_ptr<ImageInfoData> m_imageInfoData;
    QString m_outputNodeName;
    int m_masterIndex;
    bool m_useDefaultMaster;
    bool m_bESD;

    // Worker thread
    S1TopsBackGeocodingWorker* m_workerThread;
    QThread* m_thread;

    // Remedy watcher for missing JPG regeneration
    QFutureWatcher<void> m_remedyWatcher;

    // Helper methods
    void createWidget();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);
    bool validateInputs() const;
    void updateLabels();
    void updateMasterImageCombo();
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
    void startBackGeocoding(int images_number, int masterIndex, QString savePath, QString dstProject,
                            QString srcNode, QString dstNode, QStandardItemModel* model, bool b_ESD);
};

} // namespace QtNodes

#endif // S1TOPSBACKGEOCODINGNODE_H
