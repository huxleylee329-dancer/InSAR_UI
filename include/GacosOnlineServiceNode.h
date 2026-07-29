#ifndef GACASONLINESERVICENODE_H
#define GACASONLINESERVICENODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "GacosOnlineServiceWorker.h"
#include "NodeUtils.h"
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

/**
 * @brief GACOS 在线服务接口节点
 * 通过 GACOS REST API 获取大气延迟校正数据
 */
class GacosOnlineServiceNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    GacosOnlineServiceNode();
    ~GacosOnlineServiceNode();

    QString caption() const override { return QStringLiteral("GACOS Online Service"); }
    QString name() const override { return QStringLiteral("GACOS Online Service"); }
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
    QLineEdit* m_apiKeyEdit;
    QLineEdit* m_emailEdit;
    QComboBox* m_dataFormatCombo;
    QLineEdit* m_outputNodeNameEdit;

    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData;
    std::shared_ptr<ImageInfoData> m_imageInfoData;

    QString m_outputNodeName;
    QString m_apiKey;
    QString m_email;
    int m_dataFormat; // 0=GeoTIFF, 1=Binary Grid

    GacosOnlineServiceWorker* m_workerThread;
    QThread* m_thread;
    QFutureWatcher<void> m_remedyWatcher;

    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    QString m_preparedDstNode;
    QString m_preparedSavePath;
    QString m_preparedProjectName;
    QString m_preparedSrcNode;
    QString m_preparedApiKey;
    QString m_preparedEmail;
    int m_preparedDataFormat = 0;
    QStringList m_generatedOutputNames;
    QStringList m_generatedOutputPaths;

    void createWidget();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onCancelled();
    void onError(const QString& error);
    void onResultsReceived(const QString& dstNode, const QStringList& outputNames,
                           const QStringList& outputPaths, const QString& savePath,
                           const QString& projectName);
    bool validateInputs() const;
    void updateWidgetSize();
    QString generateDefaultOutputName() const;
    void executeProcessing();
    void releaseFinishedThreadResources();
    void startPreviewGeneration(const QStringList& h5Paths, const QStringList& generatedJpgPaths,
                                const QStringList& types, const QStringList& resultH5Paths, const QStringList& resultJpgPaths,
                                bool completeExecution);

    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;
    XMLFile* projectXml() const;

    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;
    bool supportsAutomaticRestartAfterInputChange() const override { return true; }

signals:
    void startGacos(QString apiKey, QString email, int dataFormat,
                    QString save_path, QString project_name,
                    QString file_name, QStringList inputPaths);
};

} // namespace QtNodes

#endif // GACASONLINESERVICENODE_H
