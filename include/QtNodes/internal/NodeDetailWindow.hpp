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

struct ParameterInfo;

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
    void renderPortCard(QVBoxLayout* layout, const PortDataInfo& info, QWidget* parent = nullptr);
    void renderParameterCard(QVBoxLayout* layout, const ParameterInfo& param, QWidget* parent = nullptr);

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
    QVector<ParameterInfo> _parameters;
    std::vector<QString> _processingInfo;
    std::vector<PortDataInfo> _outputPorts;

    // Style constants - GLASSMORPHISM VERSION (THEME AWARE)
    static constexpr char const* STYLE_WINDOW_LIGHT =
        "NodeDetailWindow {"
        "  background-color: rgba(241, 245, 249, 0.95);"  // Light theme window bg
        "}";

    static constexpr char const* STYLE_WINDOW_DARK =
        "NodeDetailWindow {"
        "  background-color: rgba(43, 64, 75, 0.95);"  // Dark theme window bg
        "}";

    static constexpr char const* STYLE_CARD =
        "#DetailCard {"
        "  background-color: rgba(255, 255, 255, 0.7);"  // Glass: 70% white
        "  border: 1px solid rgba(255, 255, 255, 0.15);"
        "  border-radius: 12px;"
        "}";

    static constexpr char const* STYLE_CARD_DARK =
        "#DetailCard {"
        "  background-color: rgba(64, 64, 64, 0.7);"
        "  border: 1px solid rgba(255, 255, 255, 0.1);"
        "  border-radius: 12px;"
        "}";

    static constexpr char const* STYLE_CARD_TITLE =
        "#CardTitle {"
        "  color: #1E3A8A;"  // System text color
        "  font-size: 14px;"
        "  font-weight: bold;"
        "  padding: 10px;"
        "  background: rgba(241, 245, 249, 0.5);"  // Light theme title bg
        "  border-bottom: 1px solid rgba(255, 255, 255, 0.15);"
        "  border-top-left-radius: 12px;"
        "  border-top-right-radius: 12px;"
        "}";

    static constexpr char const* STYLE_CARD_TITLE_DARK =
        "#CardTitle {"
        "  color: #FFFFFF;"  // Dark theme text
        "  font-size: 14px;"
        "  font-weight: bold;"
        "  padding: 10px;"
        "  background: rgba(64, 64, 64, 0.5);"  // Dark theme title bg
        "  border-bottom: 1px solid rgba(255, 255, 255, 0.15);"
        "  border-top-left-radius: 12px;"
        "  border-top-right-radius: 12px;"
        "}";

    static constexpr char const* STYLE_SCROLL_AREA =
        "QScrollArea {"
        "  border: none;"
        "  background: transparent;"
        "}";

    static constexpr char const* STYLE_SCROLL_CONTENT =
        "QWidget {"
        "  background: transparent;"
        "}";

    static constexpr char const* STYLE_INFO_LABEL_TEMPLATE =
        "QLabel {"
        "  background-color: rgba(241, 245, 249, 0.6);"
        "  color: #1E3A8A;"
        "  padding: 8px;"
        "  border-radius: 4px;"
        "  border-left: 3px solid #3B82F6;"
        "  font-size: %1px;"
        "}";

    static constexpr char const* STYLE_INFO_LABEL_TEMPLATE_DARK =
        "QLabel {"
        "  background-color: rgba(64, 64, 64, 0.6);"
        "  color: #FFFFFF;"
        "  padding: 8px;"
        "  border-radius: 4px;"
        "  border-left: 3px solid #2B404B;"
        "  font-size: %1px;"
        "}";

    static constexpr char const* STYLE_TEXT_PRIMARY_LIGHT = "color: #1E3A8A;";
    static constexpr char const* STYLE_TEXT_PRIMARY_DARK = "color: #FFFFFF;";
    static constexpr char const* STYLE_TEXT_SECONDARY_LIGHT = "color: #64748B;";
    static constexpr char const* STYLE_TEXT_SECONDARY_DARK = "color: #94A3B8;";
    static constexpr char const* STYLE_TEXT_TERTIARY = "color: #94A3B8;";

    // Close button styles
    static constexpr char const* STYLE_CLOSE_BUTTON =
        "QPushButton {"
        "  background-color: #3B82F6;"
        "  color: white;"
        "  padding: 8px 32px;"
        "  border-radius: 6px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover { background-color: #60A5FA; }"
        "QPushButton:pressed { background-color: #3B82F6; }";

    static constexpr char const* STYLE_CLOSE_BUTTON_LIGHT = STYLE_CLOSE_BUTTON;

    static constexpr char const* STYLE_CLOSE_BUTTON_DARK =
        "QPushButton {"
        "  background-color: #2B404B;"
        "  color: white;"
        "  padding: 8px 32px;"
        "  border-radius: 6px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover { background-color: #4A748D; }"
        "QPushButton:pressed { background-color: #2B404B; }";

    // Scrollbar styles (light theme)
    static constexpr char const* STYLE_SCROLLBAR =
        "QScrollBar:vertical {"
        "  background: rgba(241, 245, 249, 0.3);"
        "  width: 12px;"
        "  border-radius: 6px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: rgba(59, 130, 246, 0.6);"
        "  min-height: 20px;"
        "  border-radius: 6px;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }";

    static constexpr char const* STYLE_SCROLLBAR_LIGHT = STYLE_SCROLLBAR;

    static constexpr char const* STYLE_SCROLLBAR_DARK =
        "QScrollBar:vertical {"
        "  background: rgba(64, 64, 64, 0.3);"
        "  width: 12px;"
        "  border-radius: 6px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: rgba(74, 116, 141, 0.6);"
        "  min-height: 20px;"
        "  border-radius: 6px;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }";

    // Title label styles
    static constexpr char const* STYLE_TITLE =
        "QLabel { color: #1E3A8A; }";

    static constexpr char const* STYLE_TITLE_LIGHT = STYLE_TITLE;

    static constexpr char const* STYLE_TITLE_DARK =
        "QLabel { color: #FFFFFF; }";

    // Font sizes
    static constexpr int FONT_SIZE_INFO = 10;         // Info label

    static constexpr int SECTION_MIN_WIDTH = 250;
    static constexpr int CONTENT_MAX_HEIGHT = 400;

    /// Theme detection
    static bool isDarkTheme(QWidget* parent);
    static QString getThemeStylesheet(QWidget* parent);
};

} // namespace QtNodes
