#ifndef GENERICSARIMPORTNODE_H
#define GENERICSARIMPORTNODE_H

#include "ImportNodeBase.h"
#include "GenericSARImportTask.h"
#include "NodeDataTypes.h"

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QThread>

namespace QtNodes {

class GenericSARImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    GenericSARImportNode();
    ~GenericSARImportNode();

    QString caption() const override { return QStringLiteral("Generic SAR Import"); }
    QString name() const override { return QStringLiteral("GenericSARImport"); }

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
    QStringList previewImagePaths() const override;

    // Threading
    GenericSARImportTask* m_task = nullptr;
    MyThread* workerThread() const override { return nullptr; }
    QThread* qThread() const override { return nullptr; }
    void stopExecution() override;

protected:
    void onImportFinished() override;
    bool validateAndRestoreOutput() override;

private slots:
    void onImageBrowseClicked();
    void onImportProgress(int progress, const QString& message);
    void onThreadError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);

signals:
    void startGenericSARImport(QString, QString, QString, QString, QString, QStandardItemModel*);

private:
    QLineEdit* m_imageEdit;
    QLineEdit* m_outputNodeNameEdit;
    QLineEdit* m_outputFileNameEdit;
    QComboBox* m_projectCombo;

    QString m_imagePath;
    QString m_importedFilePath;
    QString m_outputNodeName;
    QString m_outputFileName;

    std::shared_ptr<ImageInfoData> m_imageInfoData;

    MyThread* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // GENERICSARIMPORTNODE_H
