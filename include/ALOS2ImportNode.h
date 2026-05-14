#ifndef ALOS2IMPORTNODE_H
#define ALOS2IMPORTNODE_H

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
// ALOS2ImportNode - Batch import ALOS-2 data
// ============================================================================
class ALOS2ImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    ALOS2ImportNode();
    ~ALOS2ImportNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("ALOS-2 Import"); }
    QString name() const override { return QStringLiteral("ALOS2Import"); }

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
    QString generateOutputFileName(const QString& imgPath) const;
    QString generateLEDPath(const QString& imgPath) const;

private slots:
    void onAddFilesClicked();
    void onRemoveFilesClicked();
    void onImportProgress(int progress, const QString& message);
    void onImportFinished();
    void onThreadError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);

signals:
    void startALOS2Import(QString, std::vector<QString>, std::vector<QString>, std::vector<QString>, QString, QString, QStandardItemModel*);

private:
    // UI elements
    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QComboBox* m_projectCombo;  // Target project dropdown (read-only)
    QPushButton* m_importButton;  // Kept for compatibility, not used in UI
    QPushButton* m_stopButton;  // Kept for compatibility, not used in UI
    QLabel* m_statusLabel;  // Kept for compatibility, not used in UI

    // State
    QStringList m_imgPaths;      // IMG file paths
    QStringList m_importedFilePaths;

    // Worker thread
    MyThread* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // ALOS2IMPORTNODE_H
