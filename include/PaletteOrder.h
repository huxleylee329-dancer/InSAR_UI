#ifndef PALETTEORDER_H
#define PALETTEORDER_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QList>

/**
 * @brief PaletteOrder - Palette order configuration for node library
 * Defines the display order of categories, subcategories, and leaf items
 */
struct PaletteOrder
{
    /**
     * @brief LeafItem - Leaf item with display name and actual caption mapping
     */
    struct LeafItem
    {
        QString displayName;   // Display name shown to user (e.g., "Single Import")
        QString caption;      // Actual caption for matching nodes (e.g., "Sentinel-1 Import")
    };

    QStringList topLevel;              // Top-level category order
    QMap<QString, QStringList> subcategories;  // Top-level -> subcategory order
    QMap<QString, QList<LeafItem>> leafItems;      // Subcategory path -> leaf item list
};

#endif // PALETTEORDER_H
