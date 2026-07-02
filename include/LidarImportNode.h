#ifndef LIDARIMPORTNODE_H
#define LIDARIMPORTNODE_H

#include "ImportNodeBase.h"
#include "NodeDataTypes.h"
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace QtNodes {

class LidarImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    LidarImportNode();
    ~LidarImportNode() = default;

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("LiDAR (GEDI/ICESat-2) Import"); }
    QString name() const override { return QStringLiteral("LidarImport"); }

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

protected:
    // ImportNodeBase interface
    QWidget* createWidget() override;
    void executeImport() override;
    QStringList getExpectedOutputFilePaths() const override;
    QString getOutputNodeName() const override;
    QString previewDataType() const override { return "dem"; }

private slots:
    void onAddFilesClicked();
    void onRemoveFilesClicked();
    void onProductTypeChanged(int index);

private:
    void updateWidgetSize();

    // UI elements
    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QLabel* m_projectLabel;
    QComboBox* m_productTypeCombo;
    QSpinBox* m_rhPercentileSpin;
    QLabel* m_rhLabel;

    // State
    QStringList m_filePaths;
    QString m_outputNodeName;
    QString m_productType;
    int m_rhPercentile;
};

} // namespace QtNodes

#endif // LIDARIMPORTNODE_H
