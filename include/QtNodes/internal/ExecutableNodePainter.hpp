#pragma once

#include "AbstractNodePainter.hpp"
#include "ExecutableNodeGeometry.hpp"
#include "ExecutableNodeDelegateModel.hpp"
#include "DefaultNodePainter.hpp"

#include <QPixmap>

namespace QtNodes {

class NodeGraphicsObject;

class NODE_EDITOR_PUBLIC ExecutableNodePainter : public AbstractNodePainter
{
public:
    ExecutableNodePainter();
    ~ExecutableNodePainter() override = default;

    void paint(QPainter *painter, NodeGraphicsObject &ngo) const override;

private:
    void drawMainRect(QPainter *painter, NodeGraphicsObject &ngo,
                      ExecutableNodeGeometry &geo,
                      ExecutionMode mode, ExecutionState state) const;
    void drawLeftEar(QPainter *painter, NodeGraphicsObject &ngo,
                     ExecutableNodeGeometry &geo,
                     ExecutionMode mode, ExecutionState state) const;
    void drawRightEar(QPainter *painter, NodeGraphicsObject &ngo,
                      ExecutableNodeGeometry &geo,
                      ExecutionState state) const;
    void drawProgressBar(QPainter *painter, NodeGraphicsObject &ngo,
                         ExecutableNodeGeometry &geo,
                         int progress) const;
    void drawStartButton(QPainter *painter, QRectF rect, ExecutionState state) const;

    /// Get gradient start color based on mode and state
    QColor gradientStartColor(ExecutionMode mode, ExecutionState state) const;
    /// Get gradient end color based on mode and state
    QColor gradientEndColor(ExecutionMode mode, ExecutionState state) const;
    /// Draw diagonal line texture for Manual mode
    void drawManualTexture(QPainter *painter, const QRectF &rect) const;
    /// Draw denser diagonal line texture for Disabled state
    void drawDisabledTexture(QPainter *painter, const QRectF &rect) const;

    QColor stateColor(ExecutionState state) const;

private:
    QPixmap loadAndColorizeIcon(const QString &resourcePath, const QColor &color, const QSize &size);

private:
    QPixmap _pixmapAutomatic;
    QPixmap _pixmapManual;
    QPixmap _pixmapPlay;
    QPixmap _pixmapStop;
    QPixmap _pixmapEye;

    // Reusable default painter instance for performance
    mutable DefaultNodePainter _defaultPainter;
};

} // namespace QtNodes
