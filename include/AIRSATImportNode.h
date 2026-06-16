#ifndef AIRSATIMPORTNODE_H
#define AIRSATIMPORTNODE_H

#include "ImportNodeBase.h"
#include "ImportDataTypes.h"
#include "AIRSATImportWorker.h"
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QThread>

namespace QtNodes {

// ============================================================================
// AIRSATImportNode - Batch import AIRSAT data
// ============================================================================
class AIRSATImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    AIRSATImportNode();
    ~AIRSATImportNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("AIRSAT Import"); }
    QString name() const override { return QStringLiteral("AIRSATImport"); }

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
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;

    // Thread accessors
    QThread* qThread() const override { return m_thread; }
    void stopExecution() override;

    // Helper methods
    QString generateOutputFileName(const QString& filePath) const;
    void updateWidgetSize();

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
    void startAIRSATImport(QString, std::vector<QString>, std::vector<QString>, std::vector<QString>, QString, QString, QStandardItemModel*);

private:
    // UI elements
    QWidget* m_widget;
    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QLabel* m_projectLabel;

    // State
    QStringList m_dataFilePaths;
    QStringList m_xmlFilePaths;
    QStringList m_importedFilePaths;
    QString m_outputNodeName;

    // Worker thread
    AIRSATImportWorker* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // AIRSATIMPORTNODE_H
