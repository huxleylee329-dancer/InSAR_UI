#include "NodeModels.h"
#include "NodeDataTypes.h"
#include "TestNodeModels.h"
#include "SimpleTestNode.h"

#include <memory>
#include <QDebug>

namespace QtNodes {

// Static registry to avoid repeated allocation/deallocation issues
static std::shared_ptr<NodeDelegateModelRegistry> g_testRegistry = nullptr;

// Create test node model registry
std::shared_ptr<NodeDelegateModelRegistry> registerTestNodeModels()
{
    qDebug() << "registerTestNodeModels: Creating registry...";

    if (g_testRegistry) {
        qDebug() << "registerTestNodeModels: Returning existing registry at" << (void*)g_testRegistry.get();
        return g_testRegistry;
    }

    qDebug() << "registerTestNodeModels: Creating new registry...";

    // Create registry
    auto registry = new NodeDelegateModelRegistry();
    qDebug() << "registerTestNodeModels: Registry created at" << (void*)registry;

    // Wrap in shared_ptr with custom deleter that doesn't use DLL's delete
    // Actually, we can't change how the DLL manages memory from here
    // Let's try a different approach - register after construction

    qDebug() << "registerTestNodeModels: Registering SimpleSourceNode...";
    registry->registerModel<SimpleSourceNode>("Sources");
    qDebug() << "registerTestNodeModels: SimpleSourceNode registered";

    // Store globally
    g_testRegistry = std::shared_ptr<NodeDelegateModelRegistry>(registry);

    qDebug() << "registerTestNodeModels: Returning registry";
    return g_testRegistry;
}

// Create InSAR node model registry (full version, to be implemented)
std::shared_ptr<NodeDelegateModelRegistry> registerInSARNodeModels()
{
    // TODO: Implement complete InSAR node models
    // Currently return test node registry
    return registerTestNodeModels();
}

} // namespace QtNodes
