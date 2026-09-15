#include "ClutterSuppressionAlgorithms.h"

#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace
{
constexpr float kEpsilon = 1e-6f;

struct RingStatistics
{
    cv::Mat mean;
    cv::Mat variance;
    cv::Mat count;
};

cv::Mat toFloatGray(const cv::Mat& input)
{
    if (input.empty()) return {};

    cv::Mat gray;
    if (input.channels() == 1)
        gray = input;
    else if (input.channels() == 3)
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    else if (input.channels() == 4)
        cv::cvtColor(input, gray, cv::COLOR_BGRA2GRAY);
    else
        return {};

    cv::Mat output;
    gray.convertTo(output, CV_32F);
    return output;
}

void ringSum(const cv::Mat& source, int guardRadius, int outerRadius, cv::Mat& sum)
{
    const int outerSize = 2 * outerRadius + 1;
    const int guardSize = 2 * guardRadius + 1;
    cv::Mat outer;
    cv::Mat guard;
    cv::boxFilter(source, outer, CV_32F, cv::Size(outerSize, outerSize),
                  cv::Point(-1, -1), false, cv::BORDER_REFLECT_101);
    cv::boxFilter(source, guard, CV_32F, cv::Size(guardSize, guardSize),
                  cv::Point(-1, -1), false, cv::BORDER_REFLECT_101);
    sum = outer - guard;
}

RingStatistics calculateRingStatistics(const cv::Mat& values,
                                       const cv::Mat& valid,
                                       int guardRadius,
                                       int outerRadius)
{
    cv::Mat weighted = values.mul(valid);
    cv::Mat squared = values.mul(values).mul(valid);
    cv::Mat sum;
    cv::Mat sumSquared;
    cv::Mat count;
    ringSum(weighted, guardRadius, outerRadius, sum);
    ringSum(squared, guardRadius, outerRadius, sumSquared);
    ringSum(valid, guardRadius, outerRadius, count);

    cv::Mat safeCount;
    cv::max(count, 1.0f, safeCount);
    RingStatistics result;
    result.mean = sum / safeCount;
    result.variance = sumSquared / safeCount - result.mean.mul(result.mean);
    cv::max(result.variance, 0.0f, result.variance);
    result.count = count;
    return result;
}

float cfarAlpha(double pfa, float trainingCellCount)
{
    const double safePfa = std::clamp(pfa, 1e-12, 0.25);
    const double n = std::max(1.0, static_cast<double>(trainingCellCount));
    return static_cast<float>(n * (std::pow(safePfa, -1.0 / n) - 1.0));
}

float robustPercentile(const cv::Mat& image, double quantile)
{
    std::vector<float> samples;
    const size_t total = image.total();
    const size_t stride = std::max<size_t>(1, total / 200000);
    samples.reserve(total / stride + 1);
    const float* data = image.ptr<float>();
    for (size_t i = 0; i < total; i += stride)
    {
        const float value = data[i];
        if (std::isfinite(value)) samples.push_back(value);
    }
    if (samples.empty()) return 1.0f;
    const size_t index = std::min(samples.size() - 1,
                                  static_cast<size_t>(quantile * (samples.size() - 1)));
    std::nth_element(samples.begin(), samples.begin() + index, samples.end());
    return std::max(samples[index], kEpsilon);
}

ClutterSuppressionResult finalizeResult(const cv::Mat& input,
                                        const cv::Mat& background,
                                        const cv::Mat& threshold)
{
    ClutterSuppressionResult result;
    result.backgroundMap = background.clone();
    result.scoreMap = input / (threshold + kEpsilon);
    cv::compare(result.scoreMap, 1.0f, result.targetMask, cv::CMP_GT);

    // Preserve bright targets while retaining a faint amount of scene context.
    cv::Mat residual;
    cv::max(input - background, 0.0f, residual);
    const float scale = robustPercentile(residual, 0.995);
    residual *= 255.0f / scale;
    cv::min(residual, 255.0f, residual);

    cv::Mat context = input * 0.12f;
    cv::Mat enhanced = context + residual * 0.88f;
    cv::min(enhanced, 255.0f, enhanced);
    cv::max(enhanced, 0.0f, enhanced);
    enhanced.convertTo(result.suppressedImage, CV_8U);
    return result;
}

ClutterSuppressionResult runCA(const cv::Mat& input,
                               const ClutterSuppressionParameters& p)
{
    cv::Mat valid(input.size(), CV_32F, cv::Scalar(1.0f));
    const int outer = p.guardRadius + p.clutterRadius;
    const cv::Mat power = input.mul(input);
    RingStatistics stats = calculateRingStatistics(power, valid, p.guardRadius, outer);
    const float n = static_cast<float>((2 * outer + 1) * (2 * outer + 1) -
                                       (2 * p.guardRadius + 1) * (2 * p.guardRadius + 1));
    cv::Mat threshold;
    cv::sqrt(stats.mean * cfarAlpha(p.probabilityFalseAlarm, n), threshold);
    cv::Mat background;
    cv::sqrt(stats.mean, background);
    return finalizeResult(input, background, threshold);
}

ClutterSuppressionResult runCensored(const cv::Mat& input,
                                     const ClutterSuppressionParameters& p,
                                     bool adaptive)
{
    const int outer = p.guardRadius + p.clutterRadius;
    cv::Mat valid(input.size(), CV_32F, cv::Scalar(1.0f));
    RingStatistics stats;

    for (int iteration = 0; iteration < 3; ++iteration)
    {
        stats = calculateRingStatistics(input, valid, p.guardRadius, outer);
        cv::Mat sigma;
        cv::sqrt(stats.variance + kEpsilon, sigma);
        cv::Mat cutoff;
        if (adaptive)
        {
            cv::Mat coefficient = sigma / (stats.mean + kEpsilon);
            cv::min(coefficient, 1.5f, coefficient);
            cutoff = stats.mean + sigma.mul(1.25f + coefficient * 1.5f);
        }
        else
        {
            // Normal-score approximation for fixed upper-tail censoring.
            const float fraction = static_cast<float>(std::clamp(p.censoringFraction, 0.01, 0.45));
            const float z = 1.75f - 2.4f * fraction;
            cutoff = stats.mean + sigma * std::max(0.55f, z);
        }
        cv::compare(input, cutoff, valid, cv::CMP_LE);
        valid.convertTo(valid, CV_32F, 1.0 / 255.0);
    }

    stats = calculateRingStatistics(input, valid, p.guardRadius, outer);
    const cv::Mat power = input.mul(input);
    const RingStatistics powerStats = calculateRingStatistics(power, valid, p.guardRadius, outer);
    cv::Mat safeCount;
    cv::max(stats.count, 1.0f, safeCount);
    cv::Mat alpha(stats.count.size(), CV_32F);
    cv::parallel_for_(cv::Range(0, alpha.rows), [&](const cv::Range& range) {
        for (int row = range.start; row < range.end; ++row)
        {
            const float* countRow = safeCount.ptr<float>(row);
            float* alphaRow = alpha.ptr<float>(row);
            for (int col = 0; col < alpha.cols; ++col)
                alphaRow[col] = cfarAlpha(p.probabilityFalseAlarm, countRow[col]);
        }
    });
    cv::Mat threshold;
    cv::sqrt(powerStats.mean.mul(alpha), threshold);
    cv::Mat background;
    cv::sqrt(powerStats.mean, background);
    return finalizeResult(input, background, threshold);
}

cv::Mat rectangleMean(const cv::Mat& input,
                      int width,
                      int height,
                      cv::Point anchor)
{
    cv::Mat result;
    cv::boxFilter(input, result, CV_32F, cv::Size(width, height), anchor,
                  true, cv::BORDER_REFLECT_101);
    return result;
}

cv::Mat shiftedReflect(const cv::Mat& input, int deltaX, int deltaY)
{
    const int padX = std::abs(deltaX);
    const int padY = std::abs(deltaY);
    cv::Mat padded;
    cv::copyMakeBorder(input, padded, padY, padY, padX, padX, cv::BORDER_REFLECT_101);
    return padded(cv::Rect(padX + deltaX, padY + deltaY,
                           input.cols, input.rows)).clone();
}

ClutterSuppressionResult runVI(const cv::Mat& input,
                               const ClutterSuppressionParameters& p)
{
    const int outer = p.guardRadius + p.clutterRadius;
    cv::Mat valid(input.size(), CV_32F, cv::Scalar(1.0f));
    const cv::Mat power = input.mul(input);
    RingStatistics stats = calculateRingStatistics(power, valid, p.guardRadius, outer);
    cv::Mat variability = stats.variance / (stats.mean.mul(stats.mean) + kEpsilon);

    const int band = std::max(1, p.clutterRadius);
    const int span = 2 * outer + 1;
    const int offset = p.guardRadius + (band + 1) / 2;
    const cv::Mat horizontalBand = rectangleMean(power, band, span, cv::Point(-1, -1));
    const cv::Mat verticalBand = rectangleMean(power, span, band, cv::Point(-1, -1));
    cv::Mat left = shiftedReflect(horizontalBand, -offset, 0);
    cv::Mat right = shiftedReflect(horizontalBand, offset, 0);
    cv::Mat top = shiftedReflect(verticalBand, 0, -offset);
    cv::Mat bottom = shiftedReflect(verticalBand, 0, offset);
    cv::Mat greatest;
    cv::max(left, right, greatest);
    cv::max(greatest, top, greatest);
    cv::max(greatest, bottom, greatest);

    cv::Mat heterogeneous;
    cv::compare(variability, 0.35f, heterogeneous, cv::CMP_GT);
    cv::Mat backgroundPower = stats.mean.clone();
    greatest.copyTo(backgroundPower, heterogeneous);

    const float n = static_cast<float>((2 * outer + 1) * (2 * outer + 1) -
                                       (2 * p.guardRadius + 1) * (2 * p.guardRadius + 1));
    cv::Mat threshold;
    cv::sqrt(backgroundPower * cfarAlpha(p.probabilityFalseAlarm, n), threshold);
    cv::Mat background;
    cv::sqrt(backgroundPower, background);
    return finalizeResult(input, background, threshold);
}

struct RayleighMixture
{
    std::vector<double> weights;
    std::vector<double> sigmaSquared;
};

RayleighMixture fitRayleighMixture(std::vector<float> samples, int componentCount)
{
    RayleighMixture mixture;
    if (samples.empty()) return mixture;

    componentCount = std::clamp(componentCount, 1, 4);
    const size_t cutoffIndex = static_cast<size_t>(0.995 * (samples.size() - 1));
    std::nth_element(samples.begin(), samples.begin() + cutoffIndex, samples.end());
    const float cutoff = samples[cutoffIndex];
    samples.erase(std::remove_if(samples.begin(), samples.end(),
                                 [cutoff](float v) { return v <= 0.0f || v > cutoff || !std::isfinite(v); }),
                  samples.end());
    if (samples.empty()) return mixture;

    mixture.weights.assign(componentCount, 1.0 / componentCount);
    mixture.sigmaSquared.resize(componentCount);
    std::vector<float> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    for (int k = 0; k < componentCount; ++k)
    {
        const size_t q = static_cast<size_t>((k + 0.5) * sorted.size() / componentCount);
        const double value = sorted[std::min(q, sorted.size() - 1)];
        mixture.sigmaSquared[k] = std::max(1e-4, value * value / (2.0 * std::log(2.0)));
    }

    std::vector<double> responsibilities(componentCount);
    for (int iteration = 0; iteration < 35; ++iteration)
    {
        std::vector<double> weightSum(componentCount, 0.0);
        std::vector<double> squareSum(componentCount, 0.0);
        for (float sample : samples)
        {
            const double x = sample;
            double denominator = 0.0;
            for (int k = 0; k < componentCount; ++k)
            {
                const double s2 = std::max(mixture.sigmaSquared[k], 1e-8);
                responsibilities[k] = mixture.weights[k] * (x / s2) * std::exp(-(x * x) / (2.0 * s2));
                denominator += responsibilities[k];
            }
            denominator = std::max(denominator, 1e-300);
            for (int k = 0; k < componentCount; ++k)
            {
                const double r = responsibilities[k] / denominator;
                weightSum[k] += r;
                squareSum[k] += r * x * x;
            }
        }
        for (int k = 0; k < componentCount; ++k)
        {
            mixture.weights[k] = std::max(1e-5, weightSum[k] / samples.size());
            mixture.sigmaSquared[k] = std::max(1e-8, squareSum[k] / (2.0 * std::max(weightSum[k], 1e-8)));
        }
        const double sumWeights = std::accumulate(mixture.weights.begin(), mixture.weights.end(), 0.0);
        for (double& weight : mixture.weights) weight /= sumWeights;
    }
    return mixture;
}

double rayleighMixtureThreshold(const RayleighMixture& mixture, double pfa)
{
    if (mixture.weights.empty()) return 1.0;
    double high = 0.0;
    for (double s2 : mixture.sigmaSquared)
        high = std::max(high, std::sqrt(s2) * 12.0);
    double low = 0.0;
    const double target = std::clamp(pfa, 1e-12, 0.25);
    for (int iteration = 0; iteration < 70; ++iteration)
    {
        const double mid = 0.5 * (low + high);
        double survival = 0.0;
        for (size_t k = 0; k < mixture.weights.size(); ++k)
            survival += mixture.weights[k] * std::exp(-(mid * mid) / (2.0 * mixture.sigmaSquared[k]));
        if (survival > target) low = mid;
        else high = mid;
    }
    return 0.5 * (low + high);
}

ClutterSuppressionResult runRmSAT(const cv::Mat& input,
                                  const ClutterSuppressionParameters& p)
{
    const int outer = p.guardRadius + p.clutterRadius;
    const int tileSize = std::max(64, 4 * outer + 1);
    const int gridRows = (input.rows + tileSize - 1) / tileSize;
    const int gridCols = (input.cols + tileSize - 1) / tileSize;
    cv::Mat coarse(gridRows, gridCols, CV_32F);

    cv::parallel_for_(cv::Range(0, gridRows), [&](const cv::Range& range) {
        for (int gy = range.start; gy < range.end; ++gy)
        {
            for (int gx = 0; gx < gridCols; ++gx)
            {
                const int x0 = std::max(0, gx * tileSize - outer);
                const int y0 = std::max(0, gy * tileSize - outer);
                const int x1 = std::min(input.cols, (gx + 1) * tileSize + outer);
                const int y1 = std::min(input.rows, (gy + 1) * tileSize + outer);
                const cv::Mat tile = input(cv::Rect(x0, y0, x1 - x0, y1 - y0));
                std::vector<float> samples;
                const size_t stride = std::max<size_t>(1, tile.total() / 20000);
                samples.reserve(tile.total() / stride + 1);
                for (size_t i = 0; i < tile.total(); i += stride)
                {
                    const int row = static_cast<int>(i / tile.cols);
                    const int col = static_cast<int>(i % tile.cols);
                    samples.push_back(tile.at<float>(row, col));
                }
                const RayleighMixture mixture = fitRayleighMixture(samples, p.maximumMixtureCount);
                coarse.at<float>(gy, gx) = static_cast<float>(rayleighMixtureThreshold(
                    mixture, p.probabilityFalseAlarm));
            }
        }
    });

    cv::Mat threshold;
    cv::resize(coarse, threshold, input.size(), 0.0, 0.0, cv::INTER_LINEAR);

    // SAT-based local scaling adapts the tile mixture threshold to gradual clutter changes.
    cv::Mat valid(input.size(), CV_32F, cv::Scalar(1.0f));
    RingStatistics stats = calculateRingStatistics(input, valid, p.guardRadius, outer);
    cv::Mat smoothMean;
    cv::blur(stats.mean, smoothMean, cv::Size(tileSize | 1, tileSize | 1));
    threshold = threshold.mul(stats.mean / (smoothMean + kEpsilon));
    return finalizeResult(input, stats.mean, threshold);
}
} // namespace

ClutterSuppressionResult ClutterSuppressionAlgorithms::process(
    const cv::Mat& inputGray,
    const ClutterSuppressionParameters& parameters)
{
    cv::Mat input = toFloatGray(inputGray);
    if (input.empty()) return {};

    ClutterSuppressionParameters p = parameters;
    p.guardRadius = std::clamp(p.guardRadius, 0, 32);
    p.clutterRadius = std::clamp(p.clutterRadius, 1, 128);
    p.probabilityFalseAlarm = std::clamp(p.probabilityFalseAlarm, 1e-12, 0.25);
    p.censoringFraction = std::clamp(p.censoringFraction, 0.01, 0.45);
    p.maximumMixtureCount = std::clamp(p.maximumMixtureCount, 1, 4);

    switch (p.method)
    {
    case ClutterSuppressionMethod::CACFAR: return runCA(input, p);
    case ClutterSuppressionMethod::ACCFAR: return runCensored(input, p, false);
    case ClutterSuppressionMethod::AAFCFAR: return runCensored(input, p, true);
    case ClutterSuppressionMethod::VICFAR: return runVI(input, p);
    case ClutterSuppressionMethod::RmSATCFAR: return runRmSAT(input, p);
    case ClutterSuppressionMethod::BM3D:
    default: return {};
    }
}

const char* ClutterSuppressionAlgorithms::methodName(ClutterSuppressionMethod method)
{
    switch (method)
    {
    case ClutterSuppressionMethod::BM3D: return "BM3D (legacy)";
    case ClutterSuppressionMethod::CACFAR: return "CA-CFAR";
    case ClutterSuppressionMethod::ACCFAR: return "AC-CFAR";
    case ClutterSuppressionMethod::AAFCFAR: return "AAF-CFAR";
    case ClutterSuppressionMethod::VICFAR: return "VI-CFAR";
    case ClutterSuppressionMethod::RmSATCFAR: return "RmSAT-CFAR";
    default: return "Unknown";
    }
}

const char* ClutterSuppressionAlgorithms::methodSuffix(ClutterSuppressionMethod method)
{
    switch (method)
    {
    case ClutterSuppressionMethod::BM3D: return "BM3D";
    case ClutterSuppressionMethod::CACFAR: return "CA_CFAR";
    case ClutterSuppressionMethod::ACCFAR: return "AC_CFAR";
    case ClutterSuppressionMethod::AAFCFAR: return "AAF_CFAR";
    case ClutterSuppressionMethod::VICFAR: return "VI_CFAR";
    case ClutterSuppressionMethod::RmSATCFAR: return "RmSAT_CFAR";
    default: return "Clutter";
    }
}
