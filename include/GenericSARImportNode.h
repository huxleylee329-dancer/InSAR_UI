#ifndef GENERICSARIMPORTNODE_H
#define GENERICSARIMPORTNODE_H

#include "ImportNodeBase.h"
#include "GenericSARImportTask.h"
#include "NodeDataTypes.h"
#include "NodeUtils.h"

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <atomic>
#include <memory>

namespace QtNodes {

class GenericSARImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    GenericSARImportNode();
    ~GenericSARImportNode() override;

    QString caption() const override { return QStringLiteral("Generic SAR Import"); }
    QString name() const override { return QStringLiteral("GenericSARImport"); }
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
    void onImportFinished();
    void onImageBrowseClicked();
    void onImportProgress(int progress, const QString& message);
    void onThreadError(const QString& error);
    void onImportCancelled();
    void onOutputsGenerated(const QString& dstNode,
                            const QStringList& outputNames,
                            const QStringList& outputPaths,
                            const QString& dataType,
                            const QString& satelliteFormat);

private:
    QLineEdit* m_imageEdit;
    QLineEdit* m_outputNodeNameEdit;
    QLineEdit* m_outputFileNameEdit;
    QLabel* m_projectLabel;

    QString m_imagePath;
    QString m_outputNodeName;
    QString m_outputFileName;
    QString m_preparedOutputFileName;
    std::shared_ptr<std::atomic_bool> m_cancellationToken;
};

} // namespace QtNodes

#endif // GENERICSARIMPORTNODE_H
