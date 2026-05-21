#ifndef TSXBATCHIMPORTNODE_H
#define TSXBATCHIMPORTNODE_H

#include "ImportNodeBase.h"
#include "ImportDataTypes.h"
#include "MyThread.h"
#include <QWidget>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QThread>

namespace QtNodes {

// ============================================================================
// TSXBatchImportNode - Batch import TerraSAR-X data
// ============================================================================
class TSXBatchImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    TSXBatchImportNode();
    ~TSXBatchImportNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("TerraSAR-X Batch Import"); }
    QString name() const override { return QStringLiteral("TSXBatchImport"); }

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;

protected:
    // ImportNodeBase interface
    QWidget* createWidget() override;
    void executeImport() override;
    QString getImportedFilePath() const override;
    QString getOutputNodeName() const override;

    // Thread accessors
    MyThread* workerThread() const override { return m_workerThread; }
    QThread* qThread() const override { return m_thread; }

    // Helper methods
    QString generateOutputFileName(const QString& xmlPath) const;

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
    void startTSXBatchImport(QString, QString, std::vector<QString>, std::vector<QString>, QString, QString, QStandardItemModel*);

private:
    // UI elements
    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QComboBox* m_polarizationCombo;
    QComboBox* m_projectCombo;

    // State
    QStringList m_xmlPaths;
    QStringList m_importedFilePaths;
    QString m_outputNodeName;

    // Worker thread
    MyThread* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // TSXBATCHIMPORTNODE_H
