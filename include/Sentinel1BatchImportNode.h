#ifndef SENTINEL1BATCHIMPORTNODE_H
#define SENTINEL1BATCHIMPORTNODE_H

#include "ImportNodeBase.h"
#include "NodeDataTypes.h"
#include "NodeUtils.h"
#include <QWidget>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <vector>

namespace QtNodes {

// ============================================================================
// Sentinel1BatchImportNode - Batch import Sentinel-1 data
// ============================================================================
class Sentinel1BatchImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    Sentinel1BatchImportNode();
    ~Sentinel1BatchImportNode() = default;

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Sentinel-1 Batch Import"); }
    QString name() const override { return QStringLiteral("Sentinel1BatchImport"); }

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

protected:
    // ImportNodeBase interface
    QWidget* createWidget() override;
    void executeImport() override;
    bool prepareToStart() override;
    QStringList getExpectedOutputFilePaths() const override;
    QString getOutputNodeName() const override;

    // Helper methods
    QString generateImportName(const QString& manifestPath) const;
    void updateAvailableParameters();

private slots:
    void onAddFilesClicked();
    void onRemoveFilesClicked();

private:
    // UI elements
    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QComboBox* m_subswathCombo;
    QComboBox* m_polarizationCombo;
    QLabel* m_projectLabel;

    // State
    QStringList m_manifestPaths;
    QString m_outputNodeName;
    QString m_subswath = "iw1";
    QString m_polarization = "vv";

    std::vector<QString> m_preparedOriginalNameList;
    std::vector<QString> m_preparedImportNameList;
    QString m_preparedOutputNodeName;
    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
};

} // namespace QtNodes

#endif // SENTINEL1BATCHIMPORTNODE_H
