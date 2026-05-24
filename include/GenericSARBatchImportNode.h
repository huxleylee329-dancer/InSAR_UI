#ifndef GENERICSARBATCHIMPORTNODE_H
#define GENERICSARBATCHIMPORTNODE_H

#include "ImportNodeBase.h"
#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "GenericSARImportTask.h"

#include <QWidget>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QThreadPool>

namespace QtNodes {

class GenericSARBatchImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    GenericSARBatchImportNode();
    ~GenericSARBatchImportNode();

    QString caption() const override { return QStringLiteral("Generic SAR Batch Import"); }
    QString name() const override { return QStringLiteral("GenericSARBatchImport"); }

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;

    void setExecutionMode(ExecutionMode mode) override;

protected:
    QWidget* createWidget() override;
    void executeImport() override;
    QString getImportedFilePath() const override;
    QString getOutputNodeName() const override;

    // Thread accessors
    GenericSARBatchImportTask* m_task = nullptr;
    MyThread* workerThread() const override { return nullptr; }
    QThread* qThread() const override { return nullptr; }
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
    QString generateImportName(const QString& imagePath) const;

    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QComboBox* m_projectCombo;

    QStringList m_imagePaths;
    QStringList m_importedFilePaths;
    QString m_outputNodeName;

    std::shared_ptr<ImageInfoData> m_imageInfoData;


};

} // namespace QtNodes

#endif // GENERICSARBATCHIMPORTNODE_H
