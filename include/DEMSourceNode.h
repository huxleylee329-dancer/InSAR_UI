#ifndef DEM_SOURCE_NODE_H
#define DEM_SOURCE_NODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "DEMSourceWorker.h"
#include "NodeUtils.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QStandardItemModel>
#include <QFutureWatcher>
#include <QPointer>
#include <memory>

class IApplicationInterface;
class XMLFile;

namespace QtNodes {

class DEMSourceNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    DEMSourceNode();
    ~DEMSourceNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("External DEM"); }
    QString name() const override { return QStringLiteral("DEMSource"); }
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
    QComboBox* m_demSourceCombo;
    QComboBox* m_resolutionCombo;
    QLineEdit* m_customResEdit;
    QLineEdit* m_cacheDirEdit;
    QPushButton* m_browseCacheBtn;
    QPushButton* m_clearCacheBtn;
    QLabel* m_cacheSizeLabel;
    QLineEdit* m_outputNodeNameEdit;

    // Input/output data
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData;
    std::shared_ptr<ImageInfoData> m_imageInfoData;

    // Parameters
    int m_demSource;            // 0=SRTM1, 1=SRTM3, 2=Copernicus, 3=ASTER
    int m_resMode;              // 0=Original, 1=30m, 2=90m, 3=Custom
    double m_customResolution;  // Custom target resolution
    QString m_cacheDir;
    QString m_outputNodeName;

    // Worker thread
    DEMSourceWorker* m_workerThread;
    QPointer<QThread> m_thread;

    // Remedy watcher for missing JPG regeneration
    QFutureWatcher<void> m_remedyWatcher;

    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    QString m_preparedDstNode;
    QString m_preparedSavePath;
    QString m_preparedProjectName;
    int m_preparedSource = 0;
    double m_preparedResolution = 0.0;
    QString m_preparedCacheDir;

    // Helper methods
    void createWidget();
    void updateCacheSizeLabel();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);
    bool validateInputs() const;
    void updateWidgetSize();
    void onResolutionModeChanged(int index);
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
    void startDemFetch(
        QString projectPath,
        QString projectName,
        QString dstNode,
        QStringList filePaths,
        int demSource,
        double targetResolution,
        QString cacheDir,
        QStandardItemModel* model
    );
};

} // namespace QtNodes

#endif // DEM_SOURCE_NODE_H
