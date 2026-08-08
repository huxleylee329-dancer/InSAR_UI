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
#include <QCheckBox>
#include <QSpinBox>
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
    // 启用验证面板
    bool supportsValidation() const override { return true; }
    ::QWidget* createValidationWidget(::QWidget* parent) override;

    // 启用干涉测量分析面板（第3个选项卡）
    bool supportsInterferometry() const override { return true; }
    ::QWidget* createInterferometryWidget(::QWidget* parent) override;

    QString caption() const override { return QStringLiteral("Sentinel-1 Batch Import"); }
    QString name() const override { return QStringLiteral("Sentinel1BatchImport"); }
    ProductOutputContract productOutputContract(PortIndex portIndex) const override;
    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    const QStringList& manifestPaths() const { return m_manifestPaths; }
    QString generateImportName(const QString& manifestPath) const;
    QStringList getExpectedOutputFilePaths() const override;

protected:
    // ImportNodeBase interface
    QWidget* createWidget() override;
    void executeImport() override;
    bool prepareToStart() override;
    QString getOutputNodeName() const override;

    // Helper methods
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
    QLineEdit* m_orbitCacheDirEdit;
    QPushButton* m_browseOrbitCacheBtn;
    QCheckBox* m_importAllBurstsCheckBox;
    QSpinBox* m_startBurstSpin;
    QSpinBox* m_endBurstSpin;

    // State
    QStringList m_manifestPaths;
    QString m_outputNodeName;
    QString m_subswath = "iw1";
    QString m_polarization = "vv";
    QString m_orbitCacheDir;
    bool m_importAllBursts = true;
    int m_startBurst = 0;
    int m_endBurst = 0;

    std::vector<QString> m_preparedOriginalNameList;
    std::vector<QString> m_preparedImportNameList;
    QStringList m_preparedOrbitPaths;
    QString m_preparedOutputNodeName;
};

} // namespace QtNodes

#endif // SENTINEL1BATCHIMPORTNODE_H
