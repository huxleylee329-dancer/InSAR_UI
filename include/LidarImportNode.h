#ifndef LIDARIMPORTNODE_H
#define LIDARIMPORTNODE_H

#include "ImportNodeBase.h"
#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "LidarImportWorker.h"
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QThread>
#include <QJsonObject>
#include <QJsonArray>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>

namespace QtNodes {

class LidarImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    LidarImportNode();
    ~LidarImportNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("LiDAR Import"); }
    QString name() const override { return QStringLiteral("LidarImport"); }

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;

protected:
    // ImportNodeBase interface
    QWidget* createWidget() override;
    void executeImport() override;
    QStringList getImportedFilePaths() const override;
    QString getOutputNodeName() const override;
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    QStringList previewImagePaths() const override;

    // Thread accessors
    QThread* qThread() const override { return m_thread; }
    void stopExecution() override;

protected:
    bool validateAndRestoreOutput() override;

private slots:
    void onAddFilesClicked();
    void onRemoveFilesClicked();
    void onImportProgress(int progress, const QString& message);
    void onImportFinished();
    void onThreadError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);
    void onProductTypeChanged(int index);

signals:
    void startLidarImport(
        QString savepath,
        std::vector<QString> original_file_list,
        std::vector<QString> import_namelist,
        QString product_type,
        int rh_percentile,
        QString dst_node,
        QString dst_project,
        QStandardItemModel* model
    );

private:
    void updateWidgetSize();

    // UI elements
    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QLabel* m_projectLabel;
    QComboBox* m_productTypeCombo;
    QSpinBox* m_rhPercentileSpin;
    QLabel* m_rhLabel;

    // State
    QStringList m_filePaths;
    QStringList m_importedFilePaths;
    QString m_outputNodeName;
    QString m_productType;
    int m_rhPercentile;

    // Port 1 预览数据
    std::shared_ptr<ImageInfoData> m_imageInfoData;
    QFutureWatcher<void> m_remedyWatcher;

    // Worker thread
    LidarImportWorker* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // LIDARIMPORTNODE_H
