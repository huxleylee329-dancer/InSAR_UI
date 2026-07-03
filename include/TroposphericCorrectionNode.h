#ifndef TROPOSPHERICCORRECTIONNODE_H
#define TROPOSPHERICCORRECTIONNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "TroposphericCorrectionWorker.h"
#include "NodeUtils.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QStandardItemModel>
#include <QFutureWatcher>
#include <memory>

class IApplicationInterface;
class XMLFile;

namespace QtNodes {

class TroposphericCorrectionNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    TroposphericCorrectionNode();
    ~TroposphericCorrectionNode();

    QString caption() const override { return QStringLiteral("ERA5 Tropospheric Correction"); }
    QString name() const override { return QStringLiteral("ERA5 Tropospheric Correction"); }
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
    QLineEdit* m_era5DirEdit;
    QPushButton* m_browseBtn;
    QLineEdit* m_outputNodeNameEdit;

    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData;
    std::shared_ptr<ImageInfoData> m_imageInfoData;

    QString m_outputNodeName;
    QString m_era5Dir;

    TroposphericCorrectionWorker* m_workerThread;
    QThread* m_thread;
    QFutureWatcher<void> m_remedyWatcher;

    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    QString m_preparedDstNode;
    QString m_preparedSavePath;
    QString m_preparedProjectName;
    QString m_preparedSrcNode;
    QString m_preparedEra5Dir;

    void createWidget();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);
    bool validateInputs() const;
    void updateWidgetSize();
    QString generateDefaultOutputName() const;
    void executeProcessing();
    void browseEra5Dir();

    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;
    XMLFile* projectXml() const;

    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

signals:
    void startCorrection(QString era5Dir, QString save_path, QString project_name,
                         QString node_name, QString file_name,
                         QStandardItemModel* model);
};

} // namespace QtNodes

#endif // TROPOSPHERICCORRECTIONNODE_H
