#pragma once

#include "Export.hpp"
#include <QtWidgets/QWidget>
#include <QtWidgets/QDialog>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QFrame>
#include "QtNodes/internal/NodeDataSnapshot.hpp"

namespace QtNodes {

/// Detail window displaying 3-column view of node information
/// Shows: Input Info | Processing Info | Output Info
class NODE_EDITOR_PUBLIC NodeDetailWindow : public QDialog
{
    Q_OBJECT

public:
    explicit NodeDetailWindow(QWidget* parent = nullptr);
    ~NodeDetailWindow() override = default;

    /// Load data into the detail window (static snapshot)
    void loadData(const NodeDataSnapshot& snapshot);

    /// Clear all displayed data
    void clearData();

Q_SIGNALS:
    /// Emitted when close button is clicked (triggers reverse animation)
    void closeRequested();

private:
    void setupUI();
    QWidget* createInputSection();
    QWidget* createProcessingSection();
    QWidget* createOutputSection();
    void addPortData(QVBoxLayout* layout, const PortDataInfo& info, const QString& title);

    QPushButton* _closeButton;
    QLabel* _titleLabel;

    // Section widgets
    QWidget* _inputWidget;
    QWidget* _processingWidget;
    QWidget* _outputWidget;
    QVBoxLayout* _inputLayout;
    QVBoxLayout* _processingLayout;
    QVBoxLayout* _outputLayout;

    // Stored data
    std::vector<PortDataInfo> _inputPorts;
    std::vector<QString> _processingInfo;
    std::vector<PortDataInfo> _outputPorts;

    // Style constants
    static constexpr char const* STYLE_WINDOW =
        "NodeDetailWindow {"
        "  background-color: #F5F5F5;"
        "}";

    static constexpr char const* STYLE_CARD =
        "#DetailCard {"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #E8E8E8;"
        "  border-radius: 8px;"
        "}";

    static constexpr char const* STYLE_CARD_TITLE =
        "#CardTitle {"
        "  color: #333333;"
        "  font-size: 14px;"
        "  font-weight: bold;"
        "  padding: 8px;"
        "  background: #FAFAFA;"
        "  border-bottom: 1px solid #E8E8E8;"
        "  border-top-left-radius: 8px;"
        "  border-top-right-radius: 8px;"
        "}";

    static constexpr char const* STYLE_SCROLL_AREA =
        "QScrollArea {"
        "  border: none;"
        "  background: transparent;"
        "}";

    static constexpr char const* STYLE_PORT_CARD =
        "QFrame {"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #F0F0F0;"
        "  border-radius: 4px;"
        "}";

    static constexpr char const* STYLE_INFO_LABEL_TEMPLATE =
        "QLabel {"
        "  background-color: #F0F4FF;"
        "  color: #333333;"
        "  padding: 8px;"
        "  border-radius: 4px;"
        "  border: 1px solid #E0E8FF;"
        "  font-size: %1px;"
        "}";

    static constexpr char const* STYLE_TEXT_PRIMARY = "color: #333333;";
    static constexpr char const* STYLE_TEXT_SECONDARY = "color: #666666;";
    static constexpr char const* STYLE_TEXT_TERTIARY = "color: #999999;";

    static constexpr char const* STYLE_TITLE =
        "QLabel {"
        "  color: #333333;"
        "}";

    static constexpr char const* STYLE_CLOSE_BUTTON =
        "QPushButton {"
        "  background-color: #0078D4;"
        "  color: #FFFFFF;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 8px 24px;"
        "  font-size: 13px;"
        "  font-weight: 500;"
        "}"
        "QPushButton:hover {"
        "  background-color: #0069C0;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #005A9E;"
        "}";

    static constexpr char const* STYLE_SCROLL_CONTENT =
        "QWidget {"
        "  background-color: #FFFFFF;"
        "}";

    static constexpr char const* STYLE_SCROLLBAR =
        "QScrollBar:vertical {"
        "  border: none;"
        "  background: #F5F5F5;"
        "  width: 8px;"
        "  margin: 0px;"
        "  border-radius: 4px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #D0D0D0;"
        "  min-height: 20px;"
        "  border-radius: 4px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "  background: #B0B0B0;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0px;"
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "  background: none;"
        "}";

    // Font sizes
    static constexpr int FONT_SIZE_TITLE = 14;      // Card title
    static constexpr int FONT_SIZE_PORT_NAME = 11;  // Port name
    static constexpr int FONT_SIZE_PORT_TYPE = 10;  // Port type/status
    static constexpr int FONT_SIZE_PORT_VALUE = 11;  // Port value
    static constexpr int FONT_SIZE_INFO = 10;         // Info label

    static constexpr int SECTION_MIN_WIDTH = 250;
    static constexpr int CONTENT_MAX_HEIGHT = 400;
};

} // namespace QtNodes
