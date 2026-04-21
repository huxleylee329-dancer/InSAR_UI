#ifndef CSKIMPORTNODE_H
#define CSKIMPORTNODE_H

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "ImportNodeBase.h"
#include "MyThread.h"
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
// CSKImportNode - Batch import COSMO-SkyMed data
// ============================================================================
class CSKImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    CSKImportNode();
    ~CSKImportNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("COSMO-SkyMed Import"); }
    QString name() const override { return QStringLiteral("CSKImport"); }

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;

protected:
    // ImportNodeBase interface
    QWidget* createWidget() override;
    void executeImport() override;
    QString getImportedFilePath() const override;
    QString getOutputNodeName() const override;

    // Helper methods
    QString generateOutputFileName(const QString& filePath) const;

private slots:
    void onAddFilesClicked();
    void onRemoveFilesClicked();
    void onImportProgress(int progress, const QString& message);
    void onImportFinished();
    void onThreadError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);

signals:
    void startCSKImport(QString, std::vector<QString>, std::vector<QString>, QString, QString, QStandardItemModel*);

private:
    // UI elements
    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QComboBox* m_projectCombo;  // Target project dropdown (read-only)
    QPushButton* m_importButton;  // Kept for compatibility, not used in UI
    QPushButton* m_stopButton;  // Kept for compatibility, not used in UI
    QLabel* m_statusLabel;  // Kept for compatibility, not used in UI

    // State
    QStringList m_filePaths;
    QStringList m_importedFilePaths;

    // Worker thread
    MyThread* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // CSKIMPORTNODE_H
