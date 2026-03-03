#include "NodeModels.h"
#include "NodeDataTypes.h"
#include "TestNodes.h"

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
    // TODO: Implement complete InSAR node models
    // Currently return test node registry
    return registerTestNodeModels();
}

} // namespace QtNodes
