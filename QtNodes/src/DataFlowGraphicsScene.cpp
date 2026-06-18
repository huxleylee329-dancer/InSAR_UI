#include "DataFlowGraphicsScene.hpp"

#include "ConnectionGraphicsObject.hpp"
#include "GraphicsView.hpp"
#include "NodeDelegateModelRegistry.hpp"
#include "NodeGraphicsObject.hpp"
#include "UndoCommands.hpp"

#include <QtCore/QFileInfo>
#include <QtWidgets/QGraphicsSceneMoveEvent>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QWidgetAction>

#include <QtCore/QBuffer>
#include <QtCore/QByteArray>
#include <QtCore/QDataStream>
#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QtGlobal>

#include <stdexcept>
#include <utility>

namespace QtNodes {

DataFlowGraphicsScene::DataFlowGraphicsScene(DataFlowGraphModel &graphModel, QObject *parent)
    : BasicGraphicsScene(graphModel, parent)
    , _graphModel(graphModel)
{
    connect(&_graphModel,
            &DataFlowGraphModel::inPortDataWasSet,
            [this](NodeId const nodeId, PortType const, PortIndex const) { onNodeUpdated(nodeId); });
}

// TODO constructor for an empyt scene?

std::vector<NodeId> DataFlowGraphicsScene::selectedNodes() const
{
    QList<QGraphicsItem *> graphicsItems = selectedItems();

    std::vector<NodeId> result;
    result.reserve(graphicsItems.size());

    for (QGraphicsItem *item : graphicsItems) {
        auto ngo = qgraphicsitem_cast<NodeGraphicsObject *>(item);

        if (ngo != nullptr) {
            result.push_back(ngo->nodeId());
        }
    }

    return result;
}

QMenu *DataFlowGraphicsScene::createSceneMenu(QPointF const scenePos)
{
    // 简化版：仅提供快速添加节点的过滤菜单
    // 完整的右键菜单已移至 GraphicsView::createCanvasContextMenu 统一管理
    QMenu *modelMenu = new QMenu();

    // 搜索框
    auto *txtBox = new QLineEdit(modelMenu);
    txtBox->setPlaceholderText(QStringLiteral("搜索节点..."));
    txtBox->setClearButtonEnabled(true);

    auto *txtBoxAction = new QWidgetAction(modelMenu);
    txtBoxAction->setDefaultWidget(txtBox);
    modelMenu->addAction(txtBoxAction);

    // 节点列表（扁平化，无分类树）
    auto registry = _graphModel.dataModelRegistry();
    auto const &assocMap = registry->registeredModelsCategoryAssociation();

    for (auto const &assoc : assocMap) {
        // 节点名 → 菜单项
        QAction *action = modelMenu->addAction(assoc.first);
        action->setData(assoc.first); // 存储模型名
    }

    // 点击菜单项 → 创建节点
    connect(modelMenu, &QMenu::triggered, [this, modelMenu, scenePos](QAction *action) {
        QString modelName = action->data().toString();
        if (!modelName.isEmpty()) {
            this->undoStack().push(new CreateCommand(this, modelName, scenePos));
        }
        modelMenu->close();
    });

    // 过滤：隐藏不匹配的菜单项
    connect(txtBox, &QLineEdit::textChanged, [modelMenu](const QString &text) {
        for (QAction *action : modelMenu->actions()) {
            // 跳过分隔符和搜索框 action（搜索框 action 没有 data）
            if (action->isSeparator() || action->data().toString().isEmpty())
                continue;
            QString name = action->data().toString();
            action->setVisible(text.isEmpty() || name.contains(text, Qt::CaseInsensitive));
        }
    });

    txtBox->setFocus();

    modelMenu->setAttribute(Qt::WA_DeleteOnClose);
    return modelMenu;
}

bool DataFlowGraphicsScene::save(const QString &filePath) const
{
    if (filePath.isEmpty())
        return false;

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(_graphModel.save()).toJson());
        return true;
    }
    return false;
}

bool DataFlowGraphicsScene::load(const QString &filePath)
{
    if (filePath.isEmpty() || !QFileInfo::exists(filePath))
        return false;

    QFile file(filePath);

    if (!file.open(QIODevice::ReadOnly))
        return false;

    clearScene();

    QByteArray const wholeFile = file.readAll();

    _graphModel.load(QJsonDocument::fromJson(wholeFile).object());

    Q_EMIT sceneLoaded();

    return true;
}

} // namespace QtNodes
