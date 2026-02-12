#ifndef NODEMODELS_H
#define NODEMODELS_H

#include <QtNodes/NodeDelegateModelRegistry>
#include <QtNodes/NodeDelegateModel>

#include <memory>

namespace QtNodes {

// Forward declarations for node model classes

// Data import nodes
class Sentinel1ImportNode;
class TSXImportNode;
class CSKImportNode;
class ALOS2ImportNode;

// Preprocessing nodes
class S1DeburstNode;
class S1FrameMergeNode;
class S1SwathMergeNode;

// Registration nodes
class RegistrationNode;
class S1BackGeocodingNode;

// Interferometric processing nodes
class InterferogramNode;
class FilterNode;
class UnwrapNode;
class DemNode;

// Baseline processing nodes
class BaselineNode;
class BaselineFormationNode;

// SBAS processing nodes
class SBASTimeSeriesNode;
class SBASReferenceReselectionNode;

// Visualization nodes
class DisplayNode;
class DeformationPreviewNode;
class BaselinePreviewNode;

// Utility nodes
class CutNode;
class GeocodingNode;

/**
 * @brief Create and return InSAR node model registry
 *
 * This function registers all available node models and returns a shared pointer.
 * Users can use this registry to create a DataFlowGraphModel.
 *
 * @return Shared pointer to node model registry
 */
std::shared_ptr<NodeDelegateModelRegistry> registerInSARNodeModels();

/**
 * @brief Create basic test node models (for verification)
 *
 * This function registers simple test nodes for verifying QtNodes library integration.
 *
 * @return Shared pointer to node model registry
 */
std::shared_ptr<NodeDelegateModelRegistry> registerTestNodeModels();

} // namespace QtNodes

#endif // NODEMODELS_H
