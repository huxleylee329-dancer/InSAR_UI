#include "NodeModels.h"
#include "NodeDataTypes.h"
#include "ImportDataTypes.h"
#include "TestNodes.h"
#include "NoteNode.h"
#include "Sentinel1ImportNode.h"
#include "Sentinel1BatchImportNode.h"
#include "TSXImportNode.h"
#include "TSXBatchImportNode.h"
#include "CSKImportNode.h"
#include "ALOS2ImportNode.h"
#include "S1DeburstNode.h"
#include "S1FrameMergeNode.h"
#include "S1SwathMergeNode.h"
#include "S1TopsBackGeocodingNode.h"
#include "GenericSARImportNode.h"
#include "GenericSARBatchImportNode.h"
#include "GeneralSARLoadingNode.h"
#include "SpeckleDenoiseNode.h"
#include "ClutterSuppressionNode.h"
#include "ImageDisplayNode.h"
#include "TargetDetectionNode.h"
#include "EvaluationENLNode.h"
#include "EvaluationSCRNode.h"
#include "LoggerNode.h"
#include "CoregistrationNode.h"
#include "CutNode.h"
#include "InterferometricFormationNode.h"
#include "BaselinePreviewNode.h"
#include "DenoiseNode.h"

#include <memory>

// Uncomment this line to disable test nodes when real InSAR nodes are implemented
#define ENABLE_TEST_NODES

namespace QtNodes {

// Create test node model registry
std::shared_ptr<NodeDelegateModelRegistry> registerTestNodeModels()
{
    auto registry = std::make_shared<NodeDelegateModelRegistry>();

    // Register Note node (layout management tool)
    registry->registerModel<NoteNode>("Information");

#ifdef ENABLE_TEST_NODES
    // Register test nodes in a "Test" category
    registry->registerModel<SimpleSourceNode>("Test");
    registry->registerModel<SimpleMathNode>("Test");
    registry->registerModel<SimpleDisplayNode>("Test");

    // Card-based layout test nodes (new style)
    registry->registerModel<CardSimpleSourceNode>("Test");
    registry->registerModel<CardSimpleMathNode>("Test");
    registry->registerModel<CardSimpleDisplayNode>("Test");
#endif

    return registry;
}

// Create InSAR node model registry (full version, to be implemented)
std::shared_ptr<NodeDelegateModelRegistry> registerInSARNodeModels()
{
    auto registry = std::make_shared<NodeDelegateModelRegistry>();

    // Register Data Import nodes - Using 3-level hierarchy
    // Sentinel-1
    registry->registerModel<Sentinel1ImportNode>("Data Import/Sentinel-1/Single Import");
    registry->registerModel<Sentinel1BatchImportNode>("Data Import/Sentinel-1/Batch Import");

    // TerraSAR-X
    registry->registerModel<TSXImportNode>("Data Import/TerraSAR-X/Single Import");
    registry->registerModel<TSXBatchImportNode>("Data Import/TerraSAR-X/Batch Import");

    // COSMO-SkyMed
    registry->registerModel<CSKImportNode>("Data Import/COSMO-SkyMed/Batch Import");

    // ALOS-2
    registry->registerModel<ALOS2ImportNode>("Data Import/ALOS-2/Batch Import");

    
    // Generic SAR
    registry->registerModel<GenericSARImportNode>("SAR/Import/Generic SAR/Single Import");
    registry->registerModel<GenericSARBatchImportNode>("SAR/Import/Generic SAR/Batch Import");
    registry->registerModel<GeneralSARLoadingNode>("SAR/Import/General SAR Loading");

    // ============================================================================
    // Preprocessing Nodes
    // ============================================================================
    // Sentinel-1 Preprocessing
    registry->registerModel<S1DeburstNode>("Preprocessing/Sentinel-1/Deburst");
    registry->registerModel<S1FrameMergeNode>("Preprocessing/Sentinel-1/Frame Merge");
    registry->registerModel<S1SwathMergeNode>("Preprocessing/Sentinel-1/Swath Merge");
    registry->registerModel<S1TopsBackGeocodingNode>("Preprocessing/Sentinel-1/Back-Geocoding");


    // ============================================================================
    // SAR Enhancement Nodes
    // ============================================================================
    registry->registerModel<SpeckleDenoiseNode>("SAR/Enhancement/Speckle Denoise");
    registry->registerModel<ClutterSuppressionNode>("SAR/Enhancement/Clutter Suppression");
    registry->registerModel<CoregistrationNode>("InSAR");
    registry->registerModel<InterferometricFormationNode>("InSAR");
    registry->registerModel<DenoiseNode>("InSAR");
    registry->registerModel<BaselinePreviewNode>("InSAR");

    // ============================================================================
    // SAR Detection Nodes
    // ============================================================================
    registry->registerModel<TargetDetectionNode>("SAR/Detection/Target Detection");

    // ============================================================================
    // SAR Evaluation Nodes
    // ============================================================================
    registry->registerModel<EvaluationENLNode>("SAR/Evaluation/Evaluation-ENL");
    registry->registerModel<EvaluationSCRNode>("SAR/Evaluation/Evaluation-SCR");
    // Future Categories (placeholders for upcoming functionality)
    // ============================================================================

    // Preprocessing nodes (to be implemented)
    registry->registerModel<CutNode>("Preprocessing/Region Crop/AOI Crop");
    // registry->registerModel<Cut2Node>("Preprocessing/Region Crop/Frame Crop");
    // registry->registerModel<FilterNode>("Preprocessing/Filter/Goldstein");
    // registry->registerModel<UnwrapNode>("Preprocessing/Phase Unwrapping/SNAPHU");

    // Registration nodes (to be implemented)
    // registry->registerModel<RegisNode>("Registration/Intensity Based/Coarse");
    // registry->registerModel<DEMAssistCoregNode>("Registration/DEM Assisted/Fine");
    // registry->registerModel<TOPSBackGeocodingNode>("Registration/TopSAR/Back-Geocoding");

    // Interferometry nodes (to be implemented)
    // registry->registerModel<InterferometricNode>("Interferometry/Interferogram Formation");
    // registry->registerModel<BaselineFormationNode>("Interferometry/Baseline Estimation");

    // SBAS/DInSAR nodes (to be implemented)
    // registry->registerModel<SBASTimeSeriesNode>("SBAS/Time Series Analysis");
    // registry->registerModel<SBASReferenceReselectionNode>("SBAS/Reference Reselection");
    // registry->registerModel<DeformationVisualNode>("SBAS/Deformation Visualization");

    // Export nodes (to be implemented)
    // registry->registerModel<GeocodingNode>("Export/Geocoding/Image");
    // registry->registerModel<KMLExportNode>("Export/KML");

    // Register test nodes for development (can be removed when all InSAR nodes are implemented)
#ifdef ENABLE_TEST_NODES
    registry->registerModel<SimpleSourceNode>("Test");
    registry->registerModel<SimpleMathNode>("Test");
    registry->registerModel<SimpleDisplayNode>("Test");

    // Card-based layout test nodes (new style)
    registry->registerModel<CardSimpleSourceNode>("Test");
    registry->registerModel<CardSimpleMathNode>("Test");
    registry->registerModel<CardSimpleDisplayNode>("Test");
#endif

    // Information Nodes
    registry->registerModel<NoteNode>("Information");
    registry->registerModel<LoggerNode>("Information");

    // Display Nodes
    registry->registerModel<ImageDisplayNode>("Display");

    return registry;
}

} // namespace QtNodes
