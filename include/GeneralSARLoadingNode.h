#ifndef GENERALSARLOADINGNODE_H
#define GENERALSARLOADINGNODE_H

#include "ImportNodeBase.h"
#include "NodeDataTypes.h"
#include <QWidget>
#include <QListWidget>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace QtNodes {

class PopupComboBox : public QComboBox
{
    Q_OBJECT
public:
    using QComboBox::QComboBox;
signals:
    void aboutToShowPopup();
protected:
    void showPopup() override {
        emit aboutToShowPopup();
        QComboBox::showPopup();
    }
};

class GeneralSARLoadingNode : public ImportNodeBase
{
    Q_OBJECT

public:
    GeneralSARLoadingNode();
    ~GeneralSARLoadingNode() override = default;

    QString caption() const override { return QStringLiteral("General SAR Loading"); }
    QString name() const override { return QStringLiteral("GeneralSARLoading"); }
    unsigned int nPorts(PortType portType) const override;
    ProductInputContract productInputContract(PortIndex portIndex) const override;
    ProductOutputContract productOutputContract(PortIndex portIndex) const override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool validateAndRestoreOutput() override;

protected:
    QWidget* createWidget() override;
    void executeImport() override;
    QStringList getExpectedOutputFilePaths() const override;
    QString getOutputNodeName() const override;
    bool prepareToStart() override;

public slots:
    void onProjectModelChanged(QStandardItemModel* model);

private slots:
    void refreshUI();
    void refreshNodeList();
    void onSelectionChanged();

private:
    QLabel* m_projectNameLabel = nullptr;
    PopupComboBox* m_loadingNodeCombo = nullptr;
    QListWidget* m_fileListWidget = nullptr;

    QString m_loadingNodeName;
    QStringList m_checkedFilePaths;
    bool m_isNewNode = true;
};

} // namespace QtNodes

#endif // GENERALSARLOADINGNODE_H
