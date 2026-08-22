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

    bool supportsValidation() const override { return true; }
    ::QWidget* createValidationWidget(::QWidget* parent) override;

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("TerraSAR-X Batch Import"); }
    QString name() const override { return QStringLiteral("TSXBatchImport"); }
    ProductOutputContract productOutputContract(PortIndex portIndex) const override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    // This name is captured with a committed manifest snapshot. It is not a
    // source-product lookup key.
    QString validationOutputNodeName() const { return getOutputNodeName(); }

    // These paths restore the node's current configuration so Detail View can
    // preselect a source product for an explicit, user-visible comparison.
    // They do not prove the source of a previously committed run.
    QStringList validationSourceXmlPaths() const { return m_xmlPaths; }

protected:
    // ImportNodeBase interface
    QWidget* createWidget() override;
    void executeImport() override;
    QStringList getExpectedOutputFilePaths() const override;
    QStringList transactionInputPaths() const override;
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

    // State
    QStringList m_xmlPaths;
    QString m_outputNodeName;
    QString m_polarization = "HH";
};

} // namespace QtNodes

#endif // TSXBATCHIMPORTNODE_H
