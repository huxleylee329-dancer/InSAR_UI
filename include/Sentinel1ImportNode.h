#ifndef SENTINEL1IMPORTNODE_H
#define SENTINEL1IMPORTNODE_H

#include "ImportNodeBase.h"
#include "MyThread.h"
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QThread>

namespace QtNodes {

// ============================================================================
// Sentinel1ImportNode - Single file Sentinel-1 import node
// ============================================================================
class Sentinel1ImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    Sentinel1ImportNode();
    ~Sentinel1ImportNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Sentinel-1 Import"); }
    QString name() const override { return QStringLiteral("Sentinel1Import"); }

protected:
    // ImportNodeBase interface
    QWidget* createWidget() override;
    void executeImport() override;
    QString getImportedFilePath() const override;
    QString getOutputNodeName() const override;

    // Helper methods
    QString generateOutputFileName() const;
    void onImportButtonClicked();
    void onStopButtonClicked();

private slots:
    void onManifestBrowseClicked();
    void onPodBrowseClicked();
    void onImportProgress(int progress, const QString& message);
    void onImportFinished();
    void onThreadError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);

signals:
    void startImport(QString, QString, QString, QString, QString, QString, QString, QString, QStandardItemModel*);

private:
    // UI elements
    QLineEdit* m_outputNodeNameEdit;
    QLineEdit* m_manifestEdit;
    QLineEdit* m_podEdit;
    QComboBox* m_subswathCombo;
    QComboBox* m_polarizationCombo;
    QPushButton* m_importButton;
    QPushButton* m_stopButton;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;

    // State
    QString m_manifestPath;
    QString m_podPath;
    QString m_importedFilePath;
    QString m_outputFileName;

    // Worker thread
    MyThread* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // SENTINEL1IMPORTNODE_H
