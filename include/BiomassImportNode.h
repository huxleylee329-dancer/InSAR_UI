#ifndef BIOMASSIMPORTNODE_H
#define BIOMASSIMPORTNODE_H

#include "ImportNodeBase.h"
#include "NodeDataTypes.h"
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QInputDialog>

namespace QtNodes {

class BiomassImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    BiomassImportNode();
    ~BiomassImportNode() = default;

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Biomass L1A Import"); }
    QString name() const override { return QStringLiteral("BiomassImport"); }
    ProductOutputContract productOutputContract(PortIndex portIndex) const override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

protected:
    // ImportNodeBase interface
    QWidget* createWidget() override;
    void executeImport() override;
    QStringList getExpectedOutputFilePaths() const override;
    QString getOutputNodeName() const override;

private slots:
    void onAddFilesClicked();
    void onRemoveFilesClicked();

private:
    // UI elements
    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QWidget* m_widget;

    // State (parallel lists for batch imported datasets)
    QStringList m_ampPaths;
    QStringList m_phasePaths;
    QStringList m_xmlPaths;
    QStringList m_orbitPaths;
    QStringList m_polarizations;
    QStringList m_importNames;

    QString m_outputNodeName;
};

} // namespace QtNodes

#endif // BIOMASSIMPORTNODE_H
