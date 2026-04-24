#pragma once

#include "AbstractNodePainter.hpp"
#include "ExecutableNodeGeometry.hpp"
#include "ExecutableNodeDelegateModel.hpp"
#include "DefaultNodePainter.hpp"

#include <QPixmap>

class QWidget;

namespace QtNodes {

// Material Design 3 Color System - ui2.md Section 33-81
namespace MD3Colors {
    // Light Theme
    constexpr QColor LIGHT_BACKGROUND       {249, 249, 249}; // #F9F9F9
    constexpr QColor LIGHT_ON_BACKGROUND    {26, 28, 28};    // #1A1C1C
    constexpr QColor LIGHT_SURFACE          {249, 249, 249}; // #F9F9F9
    constexpr QColor LIGHT_ON_SURFACE       {26, 28, 28};    // #1A1C1C
    constexpr QColor LIGHT_SURFACE_VARIANT  {226, 226, 226}; // #E2E2E2
    constexpr QColor LIGHT_ON_SURFACE_VARIANT {65, 71, 82};  // #414752
    constexpr QColor LIGHT_SURFACE_CONTAINER {238, 238, 238}; // #EEEEEE
    constexpr QColor LIGHT_SURFACE_CONTAINER_LOW {243, 243, 243}; // #F3F3F3
    constexpr QColor LIGHT_SURFACE_CONTAINER_HIGH {232, 232, 232}; // #E8E8E8
    constexpr QColor LIGHT_SURFACE_CONTAINER_HIGHEST {226, 226, 226}; // #E2E2E2
    constexpr QColor LIGHT_SURFACE_CONTAINER_LOWEST {255, 255, 255}; // #FFFFFF
    constexpr QColor LIGHT_PRIMARY          {0, 95, 172};    // #005FAC
    constexpr QColor LIGHT_ON_PRIMARY       {255, 255, 255}; // #FFFFFF
    constexpr QColor LIGHT_PRIMARY_CONTAINER {0, 120, 215};  // #0078D7
    constexpr QColor LIGHT_ON_PRIMARY_CONTAINER {0, 5, 16};  // #000510
    constexpr QColor LIGHT_PRIMARY_FIXED    {212, 227, 255}; // #D4E3FF
    constexpr QColor LIGHT_ON_PRIMARY_FIXED {0, 28, 57};    // #001C39
    constexpr QColor LIGHT_PRIMARY_FIXED_DIM {164, 201, 255}; // #A4C9FF
    constexpr QColor LIGHT_INVERSE_PRIMARY  {164, 201, 255}; // #A4C9FF
    constexpr QColor LIGHT_SECONDARY        {89, 95, 102};   // #595F66
    constexpr QColor LIGHT_ON_SECONDARY     {255, 255, 255}; // #FFFFFF
    constexpr QColor LIGHT_SECONDARY_CONTAINER {221, 227, 235}; // #DDE3EB
    constexpr QColor LIGHT_ON_SECONDARY_CONTAINER {95, 101, 108}; // #5F656C
    constexpr QColor LIGHT_SECONDARY_FIXED  {221, 227, 235}; // #DDE3EB
    constexpr QColor LIGHT_ON_SECONDARY_FIXED {22, 28, 34};   // #161C22
    constexpr QColor LIGHT_SECONDARY_FIXED_DIM {193, 199, 207}; // #C1C7CF
    constexpr QColor LIGHT_ON_SECONDARY_FIXED_VARIANT {65, 71, 78}; // #41474E
    constexpr QColor LIGHT_TERTIARY         {153, 71, 0};    // #994700
    constexpr QColor LIGHT_ON_TERTIARY      {255, 255, 255}; // #FFFFFF
    constexpr QColor LIGHT_TERTIARY_CONTAINER {191, 90, 0};  // #BF5A00
    constexpr QColor LIGHT_ON_TERTIARY_CONTAINER {14, 3, 0};  // #0E0300
    constexpr QColor LIGHT_TERTIARY_FIXED   {255, 219, 200}; // #FFDBC8
    constexpr QColor LIGHT_ON_TERTIARY_FIXED {50, 19, 0};    // #321300
    constexpr QColor LIGHT_TERTIARY_FIXED_DIM {255, 182, 136}; // #FFB688
    constexpr QColor LIGHT_ON_TERTIARY_FIXED_VARIANT {117, 52, 0}; // #753400
    constexpr QColor LIGHT_ERROR            {186, 26, 26};   // #BA1A1A
    constexpr QColor LIGHT_ON_ERROR         {255, 255, 255}; // #FFFFFF
    constexpr QColor LIGHT_ERROR_CONTAINER  {255, 218, 214}; // #FFDAD6
    constexpr QColor LIGHT_ON_ERROR_CONTAINER {147, 0, 10};  // #93000A
    constexpr QColor LIGHT_OUTLINE          {113, 119, 132}; // #717784
    constexpr QColor LIGHT_OUTLINE_VARIANT  {193, 199, 212}; // #C1C7D4
    constexpr QColor LIGHT_INVERSE_SURFACE  {47, 49, 49};    // #2F3131
    constexpr QColor LIGHT_INVERSE_ON_SURFACE {241, 241, 241}; // #F1F1F1
    constexpr QColor LIGHT_SURFACE_DIM      {218, 218, 218}; // #DADADA
    constexpr QColor LIGHT_SURFACE_BRIGHT   {249, 249, 249}; // #F9F9F9
    constexpr QColor LIGHT_SURFACE_TINT     {0, 95, 172};    // #005FAC

    // Dark Theme
    constexpr QColor DARK_BACKGROUND        {26, 28, 28};    // #1A1C1C
    constexpr QColor DARK_ON_BACKGROUND     {249, 249, 249}; // #F9F9F9
    constexpr QColor DARK_SURFACE           {26, 28, 28};    // #1A1C1C
    constexpr QColor DARK_ON_SURFACE        {249, 249, 249}; // #F9F9F9
    constexpr QColor DARK_SURFACE_VARIANT   {47, 49, 49};    // #2F3131
    constexpr QColor DARK_ON_SURFACE_VARIANT {193, 199, 207}; // #C1C7CF
    constexpr QColor DARK_SURFACE_CONTAINER {47, 49, 49};    // #2F3131
    constexpr QColor DARK_SURFACE_CONTAINER_LOW {43, 43, 43};  // #2B2B2B
    constexpr QColor DARK_SURFACE_CONTAINER_HIGH {42, 42, 42};  // #2A2A2A
    constexpr QColor DARK_SURFACE_CONTAINER_HIGHEST {42, 42, 42}; // #2A2A2A
    constexpr QColor DARK_SURFACE_CONTAINER_LOWEST {26, 28, 28};  // #1A1C1C
    constexpr QColor DARK_PRIMARY           {164, 201, 255}; // #A4C9FF
    constexpr QColor DARK_ON_PRIMARY        {0, 28, 57};     // #001C39
    constexpr QColor DARK_PRIMARY_CONTAINER {0, 72, 132};    // #004884
    constexpr QColor DARK_ON_PRIMARY_CONTAINER {212, 227, 255}; // #D4E3FF
    constexpr QColor DARK_PRIMARY_FIXED     {212, 227, 255}; // #D4E3FF
    constexpr QColor DARK_ON_PRIMARY_FIXED  {0, 28, 57};     // #001C39
    constexpr QColor DARK_PRIMARY_FIXED_DIM {164, 201, 255}; // #A4C9FF
    constexpr QColor DARK_INVERSE_PRIMARY   {0, 95, 172};    // #005FAC
    constexpr QColor DARK_SECONDARY         {193, 199, 207}; // #C1C7CF
    constexpr QColor DARK_ON_SECONDARY      {22, 28, 34};    // #161C22
    constexpr QColor DARK_SECONDARY_CONTAINER {65, 71, 78};  // #41474E
    constexpr QColor DARK_ON_SECONDARY_CONTAINER {221, 227, 235}; // #DDE3EB
    constexpr QColor DARK_SECONDARY_FIXED   {65, 71, 78};    // #41474E
    constexpr QColor DARK_ON_SECONDARY_FIXED {221, 227, 235}; // #DDE3EB
    constexpr QColor DARK_SECONDARY_FIXED_DIM {193, 199, 207}; // #C1C7CF
    constexpr QColor DARK_ON_SECONDARY_FIXED_VARIANT {65, 71, 78}; // #41474E
    constexpr QColor DARK_TERTIARY          {255, 182, 136}; // #FFB688
    constexpr QColor DARK_ON_TERTIARY       {50, 19, 0};     // #321300
    constexpr QColor DARK_TERTIARY_CONTAINER {117, 52, 0};   // #753400
    constexpr QColor DARK_ON_TERTIARY_CONTAINER {255, 219, 200}; // #FFDBC8
    constexpr QColor DARK_TERTIARY_FIXED    {255, 219, 200}; // #FFDBC8
    constexpr QColor DARK_ON_TERTIARY_FIXED {50, 19, 0};     // #321300
    constexpr QColor DARK_TERTIARY_FIXED_DIM {255, 182, 136}; // #FFB688
    constexpr QColor DARK_ON_TERTIARY_FIXED_VARIANT {117, 52, 0}; // #753400
    constexpr QColor DARK_ERROR             {255, 180, 171}; // #FFB4AB
    constexpr QColor DARK_ON_ERROR          {105, 0, 5};     // #690005
    constexpr QColor DARK_ERROR_CONTAINER   {147, 0, 10};    // #93000A
    constexpr QColor DARK_ON_ERROR_CONTAINER {255, 218, 214}; // #FFDAD6
    constexpr QColor DARK_OUTLINE           {143, 145, 152}; // #8F9198
    constexpr QColor DARK_OUTLINE_VARIANT   {65, 71, 82};    // #414752
    constexpr QColor DARK_INVERSE_SURFACE   {241, 241, 241}; // #F1F1F1
    constexpr QColor DARK_INVERSE_ON_SURFACE {47, 49, 49};    // #2F3131
    constexpr QColor DARK_SURFACE_DIM       {26, 28, 28};    // #1A1C1C
    constexpr QColor DARK_SURFACE_BRIGHT    {47, 49, 49};    // #2F3131
    constexpr QColor DARK_SURFACE_TINT      {0, 120, 215};   // #0078D7
}

// Node State Colors - ui2.md Section 85-110
namespace NodeStateColors {
    // Light Theme
    constexpr QColor LIGHT_IDLE_GRAY        {148, 163, 184}; // Grayscale + opacity 60
    constexpr QColor LIGHT_READY           {0, 95, 172};    // #005FAC
    constexpr QColor LIGHT_RUNNING         {0, 95, 172};    // #005FAC
    constexpr QColor LIGHT_COMPLETED       {16, 185, 129};  // #10B981
    constexpr QColor LIGHT_STOPPED         {245, 158, 11};  // #F59E0B
    constexpr QColor LIGHT_WARNING         {251, 191, 36};  // #FBBF24
    constexpr QColor LIGHT_ERROR           {239, 68, 68};   // #EF4444
    constexpr QColor LIGHT_DISABLED_GRAY   {148, 163, 184}; // Grayscale + opacity 40

    // Dark Theme
    constexpr QColor DARK_IDLE_GRAY        {80, 80, 80};    // Grayscale + opacity 60
    constexpr QColor DARK_READY            {0, 120, 215};   // #0078D7
    constexpr QColor DARK_RUNNING          {0, 120, 215};   // #0078D7
    constexpr QColor DARK_COMPLETED        {74, 169, 207};  // #4AA9CF
    constexpr QColor DARK_STOPPED          {251, 191, 36};  // #FBBF24
    constexpr QColor DARK_WARNING          {251, 191, 36};  // #FBBF24
    constexpr QColor DARK_ERROR            {239, 68, 68};   // #EF4444
    constexpr QColor DARK_DISABLED_GRAY    {80, 80, 80};    // Grayscale + opacity 40
}

// Node Header Styles - ui2.md Section 115-117
namespace NodeHeaderStyles {
    // Automatic Mode
    constexpr QColor AUTOMATIC_HEADER_BG  {0, 95, 172};    // #005FAC
    constexpr QColor AUTOMATIC_HEADER_TEXT {255, 255, 255}; // #FFFFFF

    // Manual Mode
    constexpr QColor MANUAL_HEADER_BG     {255, 255, 255}; // #FFFFFF
    constexpr QColor MANUAL_HEADER_TEXT   {26, 28, 28};    // #1A1C1C
}

// Icon Colors - ui2.md Section 122-131
namespace IconColors {
    constexpr QColor AUTOMATIC_ICON      {0, 95, 172};    // #005FAC
    constexpr QColor MANUAL_ICON         {153, 71, 0};    // #994700
    constexpr QColor PLAY_ICON           {16, 185, 129};  // #10B981
    constexpr QColor STOP_ICON           {239, 68, 68};   // #EF4444
    constexpr QColor EYE_ICON            {148, 163, 184}; // #94A3B8
    constexpr QColor WARNING_ICON        {251, 191, 36};  // #FBBF24
    constexpr QColor ERROR_ICON          {239, 68, 68};   // #EF4444
}

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
                      ExecutionMode mode, ExecutionState state,
                      ::QWidget* context) const;
    void drawLeftEar(QPainter *painter, NodeGraphicsObject &ngo,
                     ExecutableNodeGeometry &geo,
                     ExecutionMode mode, ExecutionState state,
                     ::QWidget* context) const;
    void drawRightEar(QPainter *painter, NodeGraphicsObject &ngo,
                      ExecutableNodeGeometry &geo,
                      ExecutionState state,
                      ::QWidget* context) const;
    void drawProgressBar(QPainter *painter, NodeGraphicsObject &ngo,
                         ExecutableNodeGeometry &geo,
                         int progress,
                         ::QWidget* context) const;
    void drawStartButton(QPainter *painter, QRectF rect, ExecutionState state) const;

    // Card layout drawing
    void drawCardLayout(QPainter *painter, NodeGraphicsObject &ngo,
                         ExecutableNodeDelegateModel *execModel) const;
    void drawCardHeader(QPainter *painter, NodeGraphicsObject &ngo,
                        QRectF bounds, ExecutionMode mode,
                        ExecutionState state, ::QWidget* context) const;
    void drawCardFooter(QPainter *painter, NodeGraphicsObject &ngo,
                        QRectF bounds, ExecutionState state,
                        int progress, ::QWidget* context) const;
    void drawCardHeaderButtons(QPainter *painter, QRectF bounds,
                               ExecutionMode mode, ExecutionState state) const;

    /// Get gradient start color based on mode and state (theme-aware)
    QColor gradientStartColor(ExecutionMode mode, ExecutionState state, ::QWidget* context) const;
    /// Get gradient end color based on mode and state (theme-aware)
    QColor gradientEndColor(ExecutionMode mode, ExecutionState state, ::QWidget* context) const;

    QColor stateColor(ExecutionState state) const;

    /// Check if dark theme is active (detects from parent widget)
    static bool isDarkTheme(::QWidget* widget);
    /// Get theme-aware color (light or dark)
    QColor themedColor(const QColor& lightColor, const QColor& darkColor, ::QWidget* context) const;

private:
    QPixmap loadAndColorizeIcon(const QString &resourcePath, const QColor &color, const QSize &size);

private:
    QPixmap _pixmapAutomatic;
    QPixmap _pixmapManual;
    QPixmap _pixmapPlay;
    QPixmap _pixmapStop;
    QPixmap _pixmapEye;

    // State icons for card footer
    QPixmap _pixmapStateIdle;
    QPixmap _pixmapStatePending;
    QPixmap _pixmapStateRunning;
    QPixmap _pixmapStateCompleted;
    QPixmap _pixmapStateStopped;
    QPixmap _pixmapStateWarning;
    QPixmap _pixmapStateError;
    QPixmap _pixmapStateDisabled;

    // Reusable default painter instance for performance
    mutable DefaultNodePainter _defaultPainter;
};

} // namespace QtNodes
