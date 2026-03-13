#ifndef TSXBATCHIMPORTNODE_H
#define TSXBATCHIMPORTNODE_H

#include "ImportNodeBase.h"
#include "MyThread.h"
#include <QWidget>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
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

protected:
    // ImportNodeBase interface
    QWidget* createWidget() override;
    void executeImport() override;
    QString getImportedFilePath() const override;
    QString getOutputNodeName() const override;

    // Helper methods
    QString generateOutputFileName(const QString& xmlPath) const;

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
    QProgressBar* m_progressBar;

    // State
    QStringList m_xmlPaths;
    QStringList m_importedFilePaths;

    // Worker thread
    MyThread* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // TSXBATCHIMPORTNODE_H
