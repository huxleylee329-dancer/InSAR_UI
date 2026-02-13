#include "NodeModels.h"
#include "NodeDataTypes.h"

#include <memory>

namespace QtNodes {

// Create test node model registry
std::shared_ptr<NodeDelegateModelRegistry> registerTestNodeModels()
{
    auto registry = std::make_shared<NodeDelegateModelRegistry>();
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
