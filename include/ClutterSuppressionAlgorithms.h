#pragma once

#include <opencv2/core.hpp>
#include <string>

enum class ClutterSuppressionMethod
{
    BM3D,
    CACFAR,
    ACCFAR,
    AAFCFAR,
    VICFAR,
    RmSATCFAR,
    MCA
};

struct ClutterSuppressionParameters
{
    ClutterSuppressionMethod method = ClutterSuppressionMethod::RmSATCFAR;
    int guardRadius = 3;
    int clutterRadius = 12;
    double probabilityFalseAlarm = 1e-4;
    double censoringFraction = 0.20;
    int maximumMixtureCount = 3;
    int mcaPatchSize = 20;
    int mcaPatchStride = 15;
    int mcaSparsity = 10;
    int mcaIterations = 15;
    double mcaTerminalThreshold = 4.0;
    double mcaTvGamma = 6.0;
    int mcaCurveletScales = 4;
    int mcaCurveletAngles = 8;
};

struct ClutterSuppressionResult
{
    cv::Mat suppressedImage; // CV_8UC1, target-preserving clutter-suppressed preview
    cv::Mat targetMask;      // CV_8UC1, 0 or 255
    cv::Mat scoreMap;        // CV_32FC1, CFAR statistic / threshold
    cv::Mat backgroundMap;   // CV_32FC1, estimated clutter amplitude

    bool empty() const { return suppressedImage.empty(); }
};

class ClutterSuppressionAlgorithms
{
public:
    static ClutterSuppressionResult process(
        const cv::Mat& inputGray,
        const ClutterSuppressionParameters& parameters);

    static const char* methodName(ClutterSuppressionMethod method);
    static const char* methodSuffix(ClutterSuppressionMethod method);
};
