#ifndef GEOCODINGNODE_H
#define GEOCODINGNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "GeocodingWorker.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
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

class GeocodingNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    GeocodingNode();
    ~GeocodingNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Geocoding"); }
    QString name() const override { return QStringLiteral("Geocoding"); }
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
    ::QWidget* _widget;
    QComboBox* m_typeCombo;
    
    QLabel* m_multiRgLabel;
    QSpinBox* m_multiRgSpin;
    QLabel* m_multiAzLabel;
    QSpinBox* m_multiAzSpin;
    
    QLineEdit* m_outputNodeNameEdit;

    // Input/output data
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData;
    std::shared_ptr<ImageInfoData> m_imageInfoData;
    
    // Parameters
    QString m_outputNodeName;
    int m_type;       // 1: 干涉产品, 2: SAR图像
    int m_multiRg;    // default 1
    int m_multiAz;    // default 1

    // Worker thread
    GeocodingWorker* m_workerThread;
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
    void updateWidgetSize();
    void onTypeChanged(int index);
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
    void startGeocoding(int type, int multi_rg, int multi_az, QString project, QString srcNode, QString dstNode, QStandardItemModel* model);
};

} // namespace QtNodes

#endif // GEOCODINGNODE_H
