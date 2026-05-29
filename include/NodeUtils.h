#pragma once

#include <QString>

class QWidget;
class IApplicationInterface;

namespace NodeUtils {

/**
 * @brief Traverses widgets to find the application context (WorkspaceUI/MainWindow)
 * @param widget The node widget or any reference widget
 * @return Pointer to IApplicationInterface or nullptr
 */
IApplicationInterface* getProjectContext(QWidget* widget);

/**
 * @brief Remove a DataNode entry from the project tree model and XML file.
 *
 * Call this when a node's output name changes so the old entry doesn't
 * remain as a stale reference (which would prevent orphan detection).
 *
 * @param iface  Project context (IApplicationInterface*)
 * @param oldNodeName  The old DataNode name to remove
 */
void removeDataNodeFromProject(IApplicationInterface* iface, const QString& oldNodeName);

} // namespace NodeUtils
