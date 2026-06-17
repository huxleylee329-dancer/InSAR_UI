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

namespace QtNodes {

class GenericSARImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    GenericSARImportNode();

    QString caption() const override { return QStringLiteral("Generic SAR Import"); }
    QString name() const override { return QStringLiteral("GenericSARImport"); }

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

protected:
    QWidget* createWidget() override;
    void executeImport() override;
    QStringList getExpectedOutputFilePaths() const override;
    QString getOutputNodeName() const override;

    // Threading
    GenericSARImportTask* m_task = nullptr;
    bool prepareToStart() override;

private slots:
    void onImportFinished();
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
    QLabel* m_projectLabel;

    QString m_imagePath;
    QString m_outputNodeName;
    QString m_outputFileName;
    QString m_preparedOutputFileName;
    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
};

} // namespace QtNodes

#endif // GENERICSARIMPORTNODE_H
