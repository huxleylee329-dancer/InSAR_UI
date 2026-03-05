#ifndef CSKIMPORTNODE_H
#define CSKIMPORTNODE_H

#include "ImportNodeBase.h"
#include "MyThread.h"
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
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

protected:
    // ImportNodeBase interface
    QWidget* createWidget() override;
    void executeImport() override;
    QString getImportedFilePath() const override;
    QString getOutputNodeName() const override;

    // Helper methods
    QString generateOutputFileName(const QString& filePath) const;
    void onImportButtonClicked();
    void onStopButtonClicked();

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
    QPushButton* m_importButton;
    QPushButton* m_stopButton;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;

    // State
    QStringList m_filePaths;
    QStringList m_importedFilePaths;

    // Worker thread
    MyThread* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // CSKIMPORTNODE_H
