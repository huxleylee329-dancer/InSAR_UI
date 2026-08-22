#ifndef LUTANIMPORTNODE_H
#define LUTANIMPORTNODE_H

#include "ImportNodeBase.h"
#include "NodeDataTypes.h"
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace QtNodes {

class LUTANImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    LUTANImportNode();
    ~LUTANImportNode() = default;

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("LuTan-1 Import"); }
    QString name() const override { return QStringLiteral("LUTANImport"); }
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
    void onDataBrowseClicked();
    void onXmlBrowseClicked();
    void onAddTaskClicked();
    void onRemoveTaskClicked();

private:
    // UI elements
    QLineEdit* m_dataEdit;
    QLineEdit* m_xmlEdit;
    QComboBox* m_modeCombo;
    QListWidget* m_fileListWidget;
    QLineEdit* m_outputNodeNameEdit;

    // State
    QStringList m_dataFiles;
    QStringList m_xmlFiles;
    QList<int> m_modes;
    QString m_outputNodeName;
};

} // namespace QtNodes

#endif // LUTANIMPORTNODE_H
