#ifndef SENTINEL1IMPORTNODE_H
#define SENTINEL1IMPORTNODE_H

#include "ImportNodeBase.h"
#include "MyThread.h"
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
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

    // Helper methods
    QString generateOutputFileName() const;
    QString getOutputFileName() const;  // 获取输出文件名（优先使用用户输入，否则自动生成）

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
    QLineEdit* m_outputFileNameEdit;  // 目标文件名输入框
    QComboBox* m_projectCombo;      // 目标工程下拉框
    QLineEdit* m_manifestEdit;
    QLineEdit* m_podEdit;
    QComboBox* m_subswathCombo;
    QComboBox* m_polarizationCombo;

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
