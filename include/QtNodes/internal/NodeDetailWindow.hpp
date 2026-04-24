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
    QWidget* createTitleBar();
    QWidget* createInputSection();
    QWidget* createProcessingSection();
    QWidget* createOutputSection();
    QWidget* createFooter();
    void renderPortCard(QVBoxLayout* layout, const PortDataInfo& info, bool isOutput, QWidget* parent = nullptr);
    void renderParameterCard(QVBoxLayout* layout, const ParameterInfo& param, QWidget* parent = nullptr);
    QWidget* createColumnSeparator();

    QPushButton* _closeButton;
    QPushButton* _titleCloseButton;
    QLabel* _titleIcon;
    QLabel* _titleText;
    QLabel* _titleState;

    // Section widgets
    QWidget* _titleBarWidget;
    QWidget* _inputWidget;
    QWidget* _processingWidget;
    QWidget* _outputWidget;
    QWidget* _footerWidget;
    QVBoxLayout* _inputLayout;
    QVBoxLayout* _processingLayout;
    QVBoxLayout* _outputLayout;

    // Stored data
    std::vector<PortDataInfo> _inputPorts;
    QVector<ParameterInfo> _parameters;
    std::vector<QString> _processingInfo;
    std::vector<PortDataInfo> _outputPorts;

    // Style constants - following ui2.md Section 135-168
    static constexpr char const* STYLE_WINDOW_LIGHT =
        "NodeDetailWindow {"
        "  background-color: #F9FAFB;"
        "  border: 1px solid #E5E7EB;"
        "  border-radius: 6px;"
        "}";

    static constexpr char const* STYLE_WINDOW_DARK =
        "NodeDetailWindow {"
        "  background-color: #1F2937;"
        "  border: 1px solid #374151;"
        "  border-radius: 6px;"
        "}";

    // Title bar styles
    static constexpr char const* STYLE_TITLE_BAR_LIGHT =
        "#TitleBar {"
        "  background-color: #FFFFFF;"
        "  border-bottom: 1px solid #E5E7EB;"
        "}";

    static constexpr char const* STYLE_TITLE_BAR_DARK =
        "#TitleBar {"
        "  background-color: #374151;"
        "  border-bottom: 1px solid #4B5563;"
        "}";

    // Section title styles (Input Data / Processing Info / Output Data)
    // ui2.md Section 145: #1D4ED8 for light theme card title text
    static constexpr char const* STYLE_SECTION_TITLE_LIGHT =
        "QLabel {"
        "  color: #1D4ED8;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "  text-transform: uppercase;"
        "  letter-spacing: 1px;"
        "}";

    static constexpr char const* STYLE_SECTION_TITLE_DARK =
        "QLabel {"
        "  color: #60A5FA;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "  text-transform: uppercase;"
        "  letter-spacing: 1px;"
        "}";

    // Card styles
    static constexpr char const* STYLE_CARD =
        "#DetailCard {"
        "  background-color: transparent;"
        "  border: none;"
        "}";

    static constexpr char const* STYLE_CARD_DARK = STYLE_CARD;

    // Port card styles - ui2.md Section 136-143
    static constexpr char const* STYLE_PORT_CARD_LIGHT =
        "#PortCard {"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #E5E7EB;"
        "  border-radius: 4px;"
        "}"
        "#PortCard:hover {"
        "  border-color: #93C5FD;"
        "}";

    static constexpr char const* STYLE_PORT_CARD_DARK =
        "#PortCard {"
        "  background-color: #374151;"
        "  border: 1px solid #4B5563;"
        "  border-radius: 4px;"
        "}"
        "#PortCard:hover {"
        "  border-color: #60A5FA;"
        "}";

    // Output port card (same style as input, just special blue border)
    static constexpr char const* STYLE_OUTPUT_CARD_LIGHT =
        "#OutputCard {"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #BFDBFE;"
        "  border-radius: 4px;"
        "}";

    static constexpr char const* STYLE_OUTPUT_CARD_DARK =
        "#OutputCard {"
        "  background-color: #374151;"
        "  border: 1px solid #3B82F6;"
        "  border-radius: 4px;"
        "}";

    // Status badge styles - ui2.md info tag style
    static constexpr char const* STYLE_BADGE_EMPTY =
        "#Badge {"
        "  background-color: #F3F4F6;"
        "  color: #6B7280;"
        "  border: 1px solid #E5E7EB;"
        "  border-radius: 4px;"
        "  padding: 1px 5px;"
        "  font-size: 10px;"
        "}";

    static constexpr char const* STYLE_BADGE_READY =
        "#Badge {"
        "  background-color: #EFF6FF;"
        "  color: #2563EB;"
        "  border: 1px solid #DBEAFE;"
        "  border-radius: 4px;"
        "  padding: 1px 5px;"
        "  font-size: 10px;"
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

    // Info label for processing section
    static constexpr char const* STYLE_INFO_LABEL_TEMPLATE =
        "QLabel {"
        "  background-color: #F9FAFB;"
        "  color: #374151;"
        "  padding: 8px;"
        "  border-radius: 4px;"
        "  border-left: 3px solid #3B82F6;"
        "  font-size: %1px;"
        "}";

    static constexpr char const* STYLE_INFO_LABEL_TEMPLATE_DARK =
        "QLabel {"
        "  background-color: #374151;"
        "  color: #F3F4F6;"
        "  padding: 8px;"
        "  border-radius: 4px;"
        "  border-left: 3px solid #3B82F6;"
        "  font-size: %1px;"
        "}";

    // Processing section placeholder (empty state)
    static constexpr char const* STYLE_PROCESSING_EMPTY_LIGHT =
        "#ProcessingEmpty {"
        "  border: 2px dashed #E5E7EB;"
        "  border-radius: 8px;"
        "  background-color: transparent;"
        "}";

    static constexpr char const* STYLE_PROCESSING_EMPTY_DARK =
        "#ProcessingEmpty {"
        "  border: 2px dashed #4B5563;"
        "  border-radius: 8px;"
        "  background-color: transparent;"
        "}";

    // Close button (footer)
    static constexpr char const* STYLE_FOOTER_LIGHT =
        "#Footer {"
        "  background-color: #F3F4F6;"
        "  border-top: 1px solid #E5E7EB;"
        "}";

    static constexpr char const* STYLE_FOOTER_DARK =
        "#Footer {"
        "  background-color: #374151;"
        "  border-top: 1px solid #4B5563;"
        "}";

    static constexpr char const* STYLE_CLOSE_BUTTON =
        "QPushButton {"
        "  background-color: #374151;"
        "  color: white;"
        "  padding: 6px 24px;"
        "  border: 1px solid #1F2937;"
        "  border-radius: 4px;"
        "  font-weight: 500;"
        "  font-size: 12px;"
        "  box-shadow: 0 1px 2px 0 rgba(0, 0, 0, 0.05);"
        "}"
        "QPushButton:hover { background-color: #1F2937; }"
        "QPushButton:pressed { background-color: #111827; }";

    static constexpr char const* STYLE_CLOSE_BUTTON_LIGHT = STYLE_CLOSE_BUTTON;
    static constexpr char const* STYLE_CLOSE_BUTTON_DARK = STYLE_CLOSE_BUTTON;

    // Column separator line
    static constexpr char const* STYLE_COLUMN_SEPARATOR_LIGHT =
        "#ColumnSeparator {"
        "  background-color: #E5E7EB;"
        "}";

    static constexpr char const* STYLE_COLUMN_SEPARATOR_DARK =
        "#ColumnSeparator {"
        "  background-color: #4B5563;"
        "}";

    // Middle column (Processing Info) background
    static constexpr char const* STYLE_MIDDLE_COLUMN_LIGHT =
        "#MiddleColumn {"
        "  background-color: rgba(249, 250, 251, 0.5);"
        "}";

    static constexpr char const* STYLE_MIDDLE_COLUMN_DARK =
        "#MiddleColumn {"
        "  background-color: rgba(55, 65, 81, 0.3);"
        "}";

    // Scrollbar styles - ui2.md Section 148-150, 165-167
    static constexpr char const* STYLE_SCROLLBAR =
        "QScrollBar:vertical {"
        "  background: #F1F1F1;"
        "  width: 8px;"
        "  border-radius: 4px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #C1C1C1;"
        "  min-height: 20px;"
        "  border-radius: 4px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "  background: #A8A8A8;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }";

    static constexpr char const* STYLE_SCROLLBAR_LIGHT = STYLE_SCROLLBAR;

    static constexpr char const* STYLE_SCROLLBAR_DARK =
        "QScrollBar:vertical {"
        "  background: #374151;"
        "  width: 8px;"
        "  border-radius: 4px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #6B7280;"
        "  min-height: 20px;"
        "  border-radius: 4px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "  background: #9CA3AF;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }";

    // Title text styles
    static constexpr char const* STYLE_TITLE_TEXT_LIGHT =
        "#TitleText {"
        "  color: #1F2937;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}";

    static constexpr char const* STYLE_TITLE_TEXT_DARK =
        "#TitleText {"
        "  color: #F9FAFB;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}";

    // State text (in title)
    static constexpr char const* STYLE_STATE_TEXT_LIGHT =
        "#StateText {"
        "  color: #9CA3AF;"
        "  font-size: 12px;"
        "  font-weight: normal;"
        "}";

    static constexpr char const* STYLE_STATE_TEXT_DARK =
        "#StateText {"
        "  color: #6B7280;"
        "  font-size: 12px;"
        "  font-weight: normal;"
        "}";

    // Font sizes
    static constexpr int FONT_SIZE_INFO = 9;         // Info label

    static constexpr int SECTION_MIN_WIDTH = 270;
    static constexpr int CONTENT_MAX_HEIGHT = 450;

    /// Theme detection
    static bool isDarkTheme(QWidget* parent);
    static QString getThemeStylesheet(QWidget* parent);
};

} // namespace QtNodes
