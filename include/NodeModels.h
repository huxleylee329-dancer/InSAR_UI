#ifndef NODEMODELS_H
#define NODEMODELS_H

#include <QtNodes/NodeDelegateModelRegistry>

#include <memory>

namespace QtNodes {

/**
 * @brief Create and return InSAR node model registry
 *
 * This function registers all available node models and returns a shared pointer.
 * Users can use this registry to create a DataFlowGraphModel.
 *
 * @return Shared pointer to node model registry
 */
std::shared_ptr<NodeDelegateModelRegistry> registerInSARNodeModels();


} // namespace QtNodes

#endif // NODEMODELS_H
