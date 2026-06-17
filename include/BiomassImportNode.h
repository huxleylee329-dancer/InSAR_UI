#ifndef BIOMASSIMPORTNODE_H
#define BIOMASSIMPORTNODE_H

#include "ImportNodeBase.h"
#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "BiomassImportWorker.h"
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QInputDialog>
#include <QThread>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>

namespace QtNodes {

class BiomassImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    BiomassImportNode();
    ~BiomassImportNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Biomass L1A Import"); }
    QString name() const override { return QStringLiteral("BiomassImport"); }

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

private:
    // UI elements
    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QLabel* m_projectLabel;
    QWidget* m_widget;

    // State (parallel lists for batch imported datasets)
    QStringList m_ampPaths;
    QStringList m_phasePaths;
    QStringList m_xmlPaths;
    QStringList m_orbitPaths;
    QStringList m_polarizations;
    QStringList m_importNames;

    QStringList m_importedFilePaths;
    QString m_outputNodeName;

    // Port 1 预览数据
    std::shared_ptr<ImageInfoData> m_imageInfoData;
    QFutureWatcher<void> m_remedyWatcher;

    // Worker thread
    BiomassImportWorker* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // BIOMASSIMPORTNODE_H
