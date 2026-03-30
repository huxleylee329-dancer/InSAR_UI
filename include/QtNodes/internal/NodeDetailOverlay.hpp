#pragma once

#include "Export.hpp"
#include <QtWidgets/QWidget>
#include <QtGui/QPainter>
#include <QtWidgets/QGraphicsOpacityEffect>

namespace QtNodes {

/// Overlay widget that dims the background scene when detail view is open
/// Creates a semi-transparent dark overlay to indicate non-interactive state
class NODE_EDITOR_PUBLIC NodeDetailOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit NodeDetailOverlay(QWidget* parent = nullptr);
    ~NodeDetailOverlay() override = default;

    void showOverlay();
    void hideOverlay();

    /// Get the opacity effect for animation
    QGraphicsOpacityEffect* opacityEffect() { return _opacityEffect; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QColor _overlayColor;
    QGraphicsOpacityEffect* _opacityEffect;
};

} // namespace QtNodes
