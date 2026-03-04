#include "NodeModels.h"
#include "NodeDataTypes.h"
#include "ImportDataTypes.h"
#include "TestNodes.h"
#include "Sentinel1ImportNode.h"
#include "Sentinel1BatchImportNode.h"

#include <memory>

// Uncomment this line to disable test nodes when real InSAR nodes are implemented
#define ENABLE_TEST_NODES

namespace QtNodes {

// Create test node model registry
std::shared_ptr<NodeDelegateModelRegistry> registerTestNodeModels()
{
    auto registry = std::make_shared<NodeDelegateModelRegistry>();

#ifdef ENABLE_TEST_NODES
    // Register test nodes in a "Test" category
    registry->registerModel<SimpleSourceNode>("Test");
    registry->registerModel<SimpleMathNode>("Test");
    registry->registerModel<SimpleDisplayNode>("Test");
#endif

    return registry;
}

// Create InSAR node model registry (full version, to be implemented)
std::shared_ptr<NodeDelegateModelRegistry> registerInSARNodeModels()
{
    auto registry = std::make_shared<NodeDelegateModelRegistry>();

    // Register Sentinel-1 import nodes
    registry->registerModel<Sentinel1ImportNode>("Import/Sentinel-1");
    registry->registerModel<Sentinel1BatchImportNode>("Import/Sentinel-1");

    // Register test nodes for development (can be removed when all InSAR nodes are implemented)
#ifdef ENABLE_TEST_NODES
    registry->registerModel<SimpleSourceNode>("Test");
    registry->registerModel<SimpleMathNode>("Test");
    registry->registerModel<SimpleDisplayNode>("Test");
#endif

    return registry;
}

} // namespace QtNodes
