#ifndef GENERICSARBATCHIMPORTNODE_H
#define GENERICSARBATCHIMPORTNODE_H

#include "ImportNodeBase.h"
#include "GenericSARImportTask.h"
#include "NodeDataTypes.h"
#include "NodeUtils.h"

#include <QWidget>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <atomic>
#include <memory>
#include <vector>

namespace QtNodes {

class GenericSARBatchImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    GenericSARBatchImportNode();
    ~GenericSARBatchImportNode() override;

    QString caption() const override { return QStringLiteral("Generic SAR Batch Import"); }
    QString name() const override { return QStringLiteral("GenericSARBatchImport"); }
    unsigned int nPorts(PortType portType) const override;
    ProductOutputContract productOutputContract(PortIndex portIndex) const override;
    void stopExecution() override;
    bool stopExecutionIsAsynchronous() const override { return true; }

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool validateAndRestoreOutput() override;

protected:
    QWidget* createWidget() override;
    void executeImport() override;
    QStringList getExpectedOutputFilePaths() const override;
    QStringList getExpectedPreviewFilePaths() const override;
    QStringList transactionInputPaths() const override;
    QString getOutputNodeName() const override;

    bool prepareToStart() override;

private slots:
    void onAddFilesClicked();
    void onRemoveFilesClicked();
    void onImportProgress(int progress, const QString& message);
    void onImportFinished();
    void onThreadError(const QString& error);
    void onImportCancelled();
    void onOutputsGenerated(const QString& dstNode,
                            const QStringList& outputNames,
                            const QStringList& outputPaths,
                            const QString& dataType,
                            const QString& satelliteFormat);

private:
    QString generateImportName(const QString& imagePath) const;

    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;

    QStringList m_imagePaths;
    QString m_outputNodeName;
    std::vector<QString> m_preparedOriginalFileList;
    std::vector<QString> m_preparedImportNameList;
    std::shared_ptr<std::atomic_bool> m_cancellationToken;
};

} // namespace QtNodes

#endif // GENERICSARBATCHIMPORTNODE_H
