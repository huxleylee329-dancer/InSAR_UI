#ifndef SENTINEL1_ORBIT_NODE_H
#define SENTINEL1_ORBIT_NODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "OrbitSourceWorker.h"
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
#include <QPointer>
#include <memory>

class IApplicationInterface;
class XMLFile;

namespace QtNodes {

class Sentinel1OrbitNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    Sentinel1OrbitNode();
    ~Sentinel1OrbitNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Apply Orbit File"); }
    QString name() const override { return QStringLiteral("Sentinel1Orbit"); }
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

    // 启用验证面板（检查精密轨道是否成功写入 H5）
    bool supportsValidation() const override { return true; }
    ::QWidget* createValidationWidget(::QWidget* parent) override;

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;

protected:
    bool validateAndRestoreOutput() override;
    QStringList previewImagePaths() const override;
    bool prepareToStart() override;

private:
    ::QWidget* _widget;
    QComboBox* m_orbitSourceCombo;
    QLineEdit* m_cacheDirEdit;
    QPushButton* m_browseCacheBtn;
    QPushButton* m_clearCacheBtn;
    QLabel* m_cacheSizeLabel;

    // 目标节点名称（输出在项目树中的位置）
    QLineEdit* m_outputNodeNameEdit;

    // 登录相关控件
    QLabel* m_loginStatusLabel;
    QPushButton* m_loginBtn;
    QPushButton* m_logoutBtn;

    // Input/output data
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData;
    std::shared_ptr<ImageInfoData> m_previewData;  // 可选预览输出

    // Parameters
    int m_orbitSource; // 0=NASA ASF, 1=ESA CDSE
    QString m_cacheDir;
    QString m_outputNodeName;  // 目标节点名

    // Worker thread
    OrbitSourceWorker* m_workerThread;
    QPointer<QThread> m_thread;

    QString m_preparedSavePath;
    QString m_preparedProjectName;
    QStringList m_preparedFilePaths;
    int m_preparedSource = 0;
    QString m_preparedCacheDir;
    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;

    // 工程恢复用：保存输出文件路径，使 validateAndRestoreOutput() 不依赖 m_inputData
    QStringList m_savedOutputPaths;

    // Helper methods
    void createWidget();
    void updateCacheSizeLabel();
    void updateLoginStatus();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished(
        const QStringList& newH5Paths,
        int podApplyOk,
        int podApplyFail,
        int podSkipped,
        const QString& targetDirName
    );
    void onError(const QString& error);
    void onCancelled();
    bool validateInputs() const;
    void updateWidgetSize();
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
    void startOrbitFetch(
        QString projectPath,
        QString projectName,
        QStringList filePaths,
        int orbitSource,
        QString cacheDir,
        QString targetDirName,
        QStandardItemModel* model
    );
};

} // namespace QtNodes

#endif // SENTINEL1_ORBIT_NODE_H
