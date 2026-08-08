#ifndef TSXIMPORTNODE_H
#define TSXIMPORTNODE_H

#include "ImportNodeBase.h"
#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace QtNodes {

// ============================================================================
// TSXImportNode - Single file TerraSAR-X import node
// ============================================================================
class TSXImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    TSXImportNode();
    ~TSXImportNode() = default;

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("TerraSAR-X Import"); }
    QString name() const override { return QStringLiteral("TSXImport"); }
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
    QString generateOutputFileName() const;

private slots:
    void onXmlBrowseClicked();

private:
    // UI elements
    QLineEdit* m_outputNodeNameEdit;
    QLineEdit* m_outputFileNameEdit;
    QLineEdit* m_xmlEdit;
    QComboBox* m_polarizationCombo;
    QLabel* m_projectLabel;

    // State
    QString m_xmlPath;
    QString m_outputNodeName;
    QString m_outputFileName;
    QString m_polarization = "HH";
};

} // namespace QtNodes

#endif // TSXIMPORTNODE_H
