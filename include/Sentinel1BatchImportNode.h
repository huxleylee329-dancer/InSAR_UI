#ifndef SENTINEL1BATCHIMPORTNODE_H
#define SENTINEL1BATCHIMPORTNODE_H

#include "ImportNodeBase.h"
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

// ============================================================================
// Sentinel1BatchImportNode - Batch import Sentinel-1 data
// ============================================================================
class Sentinel1BatchImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    Sentinel1BatchImportNode();
    ~Sentinel1BatchImportNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Sentinel-1 Batch Import"); }
    QString name() const override { return QStringLiteral("Sentinel1BatchImport"); }

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
    QString generateImportName(const QString& manifestPath) const;

private slots:
    void onAddFilesClicked();
    void onRemoveFilesClicked();
    void onImportProgress(int progress, const QString& message);
    void onImportFinished();
    void onThreadError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);

signals:
    void startBatchImport(std::vector<QString>, std::vector<QString>, QString, QString, QString, QString, QString, QStandardItemModel*);

private:
    // UI elements
    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QComboBox* m_subswathCombo;
    QComboBox* m_polarizationCombo;
    QComboBox* m_projectCombo;

    // State
    QStringList m_manifestPaths;
    QStringList m_importedFilePaths;

    // Worker thread
    MyThread* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // SENTINEL1BATCHIMPORTNODE_H
