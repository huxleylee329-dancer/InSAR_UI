#ifndef GENERICSARBATCHIMPORTNODE_H
#define GENERICSARBATCHIMPORTNODE_H

#include "ImportNodeBase.h"
#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "MyThread.h"

#include <QWidget>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QThread>

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
    std::shared_ptr<NodeData> outData(PortIndex port) override;

    void setExecutionMode(ExecutionMode mode) override;

protected:
    QWidget* createWidget() override;
    void executeImport() override;
    QString getImportedFilePath() const override;
    QString getOutputNodeName() const override;

    // Thread accessors
    MyThread* workerThread() const override { return m_workerThread; }
    QThread* qThread() const override { return m_thread; }

protected:
    bool validateAndRestoreOutput() override;

private slots:
    void onAddFilesClicked();
    void onRemoveFilesClicked();
    void onImportProgress(int progress, const QString& message);
    void onImportFinished();
    void onThreadError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);

signals:
    void startGenericSARBatchImport(QString, std::vector<QString>, std::vector<QString>, QString, QString, QStandardItemModel*);

private:
    QString generateImportName(const QString& imagePath) const;

    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QComboBox* m_projectCombo;

    QStringList m_imagePaths;
    QStringList m_importedFilePaths;
    QString m_outputNodeName;

    std::shared_ptr<ImageInfoData> m_imageInfoData;

    MyThread* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // GENERICSARBATCHIMPORTNODE_H
