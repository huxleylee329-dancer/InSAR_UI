#ifndef TSXBATCHIMPORTNODE_H
#define TSXBATCHIMPORTNODE_H

#include "ImportNodeBase.h"
#include "NodeDataTypes.h"
#include <QWidget>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace QtNodes {

// ============================================================================
// TSXBatchImportNode - Batch import TerraSAR-X data
// ============================================================================
class TSXBatchImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    TSXBatchImportNode();
    ~TSXBatchImportNode() = default;

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("TerraSAR-X Batch Import"); }
    QString name() const override { return QStringLiteral("TSXBatchImport"); }
    ProductOutputContract productOutputContract(PortIndex portIndex) const override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

protected:
    // ImportNodeBase interface
    QWidget* createWidget() override;
    void executeImport() override;
    QStringList getExpectedOutputFilePaths() const override;
    QString getOutputNodeName() const override;

    // Helper methods
    QString generateOutputFileName(const QString& xmlPath) const;

private slots:
    void onAddFilesClicked();
    void onRemoveFilesClicked();

private:
    // UI elements
    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QComboBox* m_polarizationCombo;
    QLabel* m_projectLabel;

    // State
    QStringList m_xmlPaths;
    QString m_outputNodeName;
    QString m_polarization = "HH";
};

} // namespace QtNodes

#endif // TSXBATCHIMPORTNODE_H
