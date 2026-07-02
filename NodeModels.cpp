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
#include "LUTANImportNode.h"
#include "HTHTImportNode.h"
#include "SpacetyImportNode.h"
#include "AIRSATImportNode.h"
#include "BiomassImportNode.h"
#include "LidarImportNode.h"
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
#include "UnwrapNode.h"
#include "DemNode.h"
#include "SLCDerampNode.h"
#include "BaselineFormationNode.h"
#include "SBASTimeSeriesNode.h"
#include "SBASReferenceReselectionNode.h"
#include "DeformationRateFieldNode.h"
#include "DeformationPreviewNode.h"
#include "ExportKMLNode.h"
#include "GeocodingNode.h"
#include "PSCandidateNode.h"
#include "PSNetworkNode.h"
#include "PSTimeSeriesNode.h"
#include "PSDeformationPreviewNode.h"
#include "GCPManagerNode.h"



#include <memory>

// Uncomment this line to disable test nodes when real InSAR nodes are implemented
#define ENABLE_TEST_NODES

namespace QtNodes {

// Create InSAR node model registry (full version, to be implemented)
std::shared_ptr<NodeDelegateModelRegistry> registerInSARNodeModels()
{
    auto registry = std::make_shared<NodeDelegateModelRegistry>();

    // Register Data Import nodes - Using 3-level hierarchy (Option 3: Business Categorization)
    // InSAR Data
    registry->registerModel<Sentinel1ImportNode>("Data Import/InSAR Data/Sentinel-1 Single Import");
    registry->registerModel<Sentinel1BatchImportNode>("Data Import/InSAR Data/Sentinel-1 Batch Import");
    registry->registerModel<TSXImportNode>("Data Import/InSAR Data/TerraSAR-X Single Import");
    registry->registerModel<TSXBatchImportNode>("Data Import/InSAR Data/TerraSAR-X Batch Import");
    registry->registerModel<CSKImportNode>("Data Import/InSAR Data/COSMO-SkyMed Import");
    registry->registerModel<ALOS2ImportNode>("Data Import/InSAR Data/ALOS-2 Import");
    registry->registerModel<LUTANImportNode>("Data Import/InSAR Data/LuTan-1 Import");
    registry->registerModel<HTHTImportNode>("Data Import/InSAR Data/Hongtu-1 Import");
    registry->registerModel<SpacetyImportNode>("Data Import/InSAR Data/Fucheng-1 Import");
    registry->registerModel<AIRSATImportNode>("Data Import/InSAR Data/AIRSAT Import");
    registry->registerModel<BiomassImportNode>("Data Import/InSAR Data/Biomass L1A Import");

    // LiDAR Data
    registry->registerModel<LidarImportNode>("Data Import/LiDAR Data/LiDAR (GEDI/ICESat-2) Import");
    
    // Generic SAR
    registry->registerModel<GenericSARImportNode>("Data Import/Generic SAR/Generic SAR Single Import");
    registry->registerModel<GenericSARBatchImportNode>("Data Import/Generic SAR/Generic SAR Batch Import");
    registry->registerModel<GeneralSARLoadingNode>("Data Import/Generic SAR/General SAR Loading");

    // ============================================================================
    // Preprocessing Nodes
    // ============================================================================
    registry->registerModel<CutNode>("Preprocessing");
    registry->registerModel<CoregistrationNode>("Preprocessing");
    
    // Sentinel-1 Preprocessing
    registry->registerModel<S1DeburstNode>("Preprocessing/Sentinel-1/Deburst");
    registry->registerModel<S1FrameMergeNode>("Preprocessing/Sentinel-1/Frame Merge");
    registry->registerModel<S1SwathMergeNode>("Preprocessing/Sentinel-1/Swath Merge");
    registry->registerModel<S1TopsBackGeocodingNode>("Preprocessing/Sentinel-1/Back-Geocoding");

    // ============================================================================
    // SAR Enhancement Nodes (in SAR)
    // ============================================================================
    registry->registerModel<SpeckleDenoiseNode>("SAR/Denoise & Enhancement/Speckle Denoise");
    registry->registerModel<ClutterSuppressionNode>("SAR/Denoise & Enhancement/Clutter Suppression");
    
    // InSAR & DInSAR Core Nodes
    registry->registerModel<SLCDerampNode>("InSAR");
    registry->registerModel<InterferometricFormationNode>("InSAR");
    registry->registerModel<DenoiseNode>("InSAR");
    registry->registerModel<UnwrapNode>("InSAR");
    registry->registerModel<DemNode>("InSAR");
    registry->registerModel<BaselinePreviewNode>("InSAR");

    // DInSAR/SBAS
    registry->registerModel<BaselineFormationNode>("DInSAR/SBAS/Baseline Estimation");
    registry->registerModel<SBASTimeSeriesNode>("DInSAR/SBAS/SBAS Time Series Analysis");
    registry->registerModel<SBASReferenceReselectionNode>("DInSAR/SBAS/Reference Point Re-selection");
    registry->registerModel<DeformationRateFieldNode>("DInSAR/SBAS/Rate Field Analysis");
    registry->registerModel<DeformationPreviewNode>("DInSAR/SBAS/Deformation Visualization");
    
    // DInSAR/PSI
    registry->registerModel<PSCandidateNode>("DInSAR/PSI/PS Candidate Selection");
    registry->registerModel<PSNetworkNode>("DInSAR/PSI/PS Network Construction");
    registry->registerModel<PSTimeSeriesNode>("DInSAR/PSI/PS Time Series Estimation");
    registry->registerModel<PSDeformationPreviewNode>("DInSAR/PSI/PS Deformation Preview");

    // ============================================================================
    // SAR Detection Nodes
    // ============================================================================
    registry->registerModel<TargetDetectionNode>("SAR/Detection/Target Detection");

    // ============================================================================
    // SAR Evaluation Nodes
    // ============================================================================
    registry->registerModel<EvaluationENLNode>("SAR/Evaluation/ENL Evaluation");
    registry->registerModel<EvaluationSCRNode>("SAR/Evaluation/SCR Evaluation");
    // Future Categories (placeholders for upcoming functionality)
    // ============================================================================

    // Export Nodes
    registry->registerModel<GeocodingNode>("Export");
    registry->registerModel<ExportKMLNode>("Export");

    // Tools
    registry->registerModel<GCPManagerNode>("Tools");

    // Register test nodes for development (can be removed when all InSAR nodes are implemented)
#ifdef ENABLE_TEST_NODES
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
