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
#include <vector>

namespace QtNodes {

class GenericSARBatchImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    GenericSARBatchImportNode();
    ~GenericSARBatchImportNode() = default;

    QString caption() const override { return QStringLiteral("Generic SAR Batch Import"); }
    QString name() const override { return QStringLiteral("GenericSARBatchImport"); }

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool validateAndRestoreOutput() override;

protected:
    QWidget* createWidget() override;
    void executeImport() override;
    QStringList getExpectedOutputFilePaths() const override;
    QString getOutputNodeName() const override;

    // Thread accessors
    GenericSARBatchImportTask* m_task = nullptr;
    bool prepareToStart() override;

private slots:
    void onAddFilesClicked();
    void onRemoveFilesClicked();
    void onImportProgress(int progress, const QString& message);
    void onImportFinished();
    void onThreadError(const QString& error);
    void onOutputsGenerated(const QString& dstNode,
                            const QStringList& outputNames,
                            const QStringList& outputPaths,
                            const QString& dataType,
                            const QString& satelliteFormat);

private:
    QString generateImportName(const QString& imagePath) const;

    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QLabel* m_projectLabel;

    QStringList m_imagePaths;
    QString m_outputNodeName;
    std::vector<QString> m_preparedOriginalFileList;
    std::vector<QString> m_preparedImportNameList;
};

} // namespace QtNodes

#endif // GENERICSARBATCHIMPORTNODE_H
