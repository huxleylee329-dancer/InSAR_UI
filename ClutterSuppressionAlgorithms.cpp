#include "ClutterSuppressionAlgorithms.h"

#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>
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

struct DctDictionaryCache
{
    cv::Mat dictionary;
    cv::Mat gram;
};

DctDictionaryCache createOvercompleteDctDictionary(int patchSize)
{
    const int atomSide = patchSize * 2;
    cv::Mat basis(patchSize, atomSide, CV_32F);
    constexpr double pi = 3.14159265358979323846;

    for (int frequency = 0; frequency < atomSide; ++frequency)
    {
        double mean = 0.0;
        for (int position = 0; position < patchSize; ++position)
        {
            const float value = static_cast<float>(
                std::cos(position * frequency * pi / atomSide));
            basis.at<float>(position, frequency) = value;
            mean += value;
        }
        if (frequency > 0)
        {
            mean /= patchSize;
            for (int position = 0; position < patchSize; ++position)
                basis.at<float>(position, frequency) -= static_cast<float>(mean);
        }

        double norm = 0.0;
        for (int position = 0; position < patchSize; ++position)
        {
            const double value = basis.at<float>(position, frequency);
            norm += value * value;
        }
        norm = std::sqrt(std::max(norm, static_cast<double>(kEpsilon)));
        for (int position = 0; position < patchSize; ++position)
            basis.at<float>(position, frequency) /= static_cast<float>(norm);
    }

    const int patchArea = patchSize * patchSize;
    const int atomCount = atomSide * atomSide;
    DctDictionaryCache cache;
    cache.dictionary.create(patchArea, atomCount, CV_32F);
    for (int atomY = 0; atomY < atomSide; ++atomY)
    {
        for (int atomX = 0; atomX < atomSide; ++atomX)
        {
            const int atom = atomY * atomSide + atomX;
            for (int y = 0; y < patchSize; ++y)
            {
                const float yValue = basis.at<float>(y, atomY);
                for (int x = 0; x < patchSize; ++x)
                {
                    cache.dictionary.at<float>(y * patchSize + x, atom) =
                        yValue * basis.at<float>(x, atomX);
                }
            }
        }
    }
    cv::Mat basisGram;
    cv::gemm(basis, basis, 1.0, cv::Mat(), 0.0, basisGram, cv::GEMM_1_T);
    cache.gram.create(atomCount, atomCount, CV_32F);
    for (int firstY = 0; firstY < atomSide; ++firstY)
    {
        for (int firstX = 0; firstX < atomSide; ++firstX)
        {
            const int firstAtom = firstY * atomSide + firstX;
            float* row = cache.gram.ptr<float>(firstAtom);
            for (int secondY = 0; secondY < atomSide; ++secondY)
            {
                const float yProduct = basisGram.at<float>(firstY, secondY);
                for (int secondX = 0; secondX < atomSide; ++secondX)
                {
                    row[secondY * atomSide + secondX] =
                        yProduct * basisGram.at<float>(firstX, secondX);
                }
            }
        }
    }
    return cache;
}

std::vector<int> patchOrigins(int length, int patchSize, int stride)
{
    std::vector<int> origins;
    const int last = std::max(0, length - patchSize);
    for (int value = 0; value <= last; value += stride)
        origins.push_back(value);
    if (origins.empty() || origins.back() != last)
        origins.push_back(last);
    return origins;
}

float softThresholdValue(float value, float threshold)
{
    const float magnitude = std::abs(value);
    if (magnitude <= threshold) return 0.0f;
    return std::copysign(magnitude - threshold, value);
}

cv::Mat haarTvCorrection(const cv::Mat& input, float gamma)
{
    if (input.empty() || gamma <= 0.0f) return input.clone();

    const int paddedRows = input.rows + (input.rows & 1);
    const int paddedCols = input.cols + (input.cols & 1);
    cv::Mat padded;
    cv::copyMakeBorder(input, padded, 0, paddedRows - input.rows,
                       0, paddedCols - input.cols, cv::BORDER_REPLICATE);
    cv::Mat output = cv::Mat::zeros(padded.size(), CV_32F);

    for (int y = 0; y < padded.rows; y += 2)
    {
        const float* row0 = padded.ptr<float>(y);
        const float* row1 = padded.ptr<float>(y + 1);
        float* output0 = output.ptr<float>(y);
        float* output1 = output.ptr<float>(y + 1);
        for (int x = 0; x < padded.cols; x += 2)
        {
            const float a = row0[x];
            const float b = row0[x + 1];
            const float c = row1[x];
            const float d = row1[x + 1];
            const float ll = softThresholdValue((a + b + c + d) * 0.5f, gamma);
            const float lh = softThresholdValue((a - b + c - d) * 0.5f, gamma);
            const float hl = softThresholdValue((a + b - c - d) * 0.5f, gamma);
            const float hh = softThresholdValue((a - b - c + d) * 0.5f, gamma);
            output0[x] = (ll + lh + hl + hh) * 0.5f;
            output0[x + 1] = (ll - lh + hl - hh) * 0.5f;
            output1[x] = (ll + lh - hl - hh) * 0.5f;
            output1[x + 1] = (ll - lh - hl + hh) * 0.5f;
        }
    }
    return output(cv::Rect(0, 0, input.cols, input.rows)).clone();
}

cv::Mat curveletDirectionalThreshold(const cv::Mat& input,
                                     float threshold,
                                     int scaleCount,
                                     int angleCount)
{
    // A native, wrapping-inspired multiscale directional decomposition.
    // Each Gaussian annulus is split by the local gradient direction, hard
    // thresholded, and synthesized with its untouched coarse component.
    cv::Mat current = input.clone();
    cv::Mat reconstructed = cv::Mat::zeros(input.size(), CV_32F);
    const float pi = 3.14159265358979323846f;
    const float sectorWidth = pi / std::max(2, angleCount);

    for (int scale = 0; scale < scaleCount; ++scale)
    {
        const double sigma = std::pow(2.0, scale) * 0.8;
        cv::Mat coarse;
        cv::GaussianBlur(current, coarse, cv::Size(), sigma, sigma,
                         cv::BORDER_REFLECT_101);
        cv::Mat detail = current - coarse;
        cv::Mat gradX;
        cv::Mat gradY;
        cv::Sobel(detail, gradX, CV_32F, 1, 0, 3, 0.25, 0.0,
                  cv::BORDER_REFLECT_101);
        cv::Sobel(detail, gradY, CV_32F, 0, 1, 3, 0.25, 0.0,
                  cv::BORDER_REFLECT_101);
        cv::Mat retained = cv::Mat::zeros(detail.size(), CV_32F);
        const float scaleThreshold = threshold * (1.0f + 0.12f * scale);

        cv::parallel_for_(cv::Range(0, detail.rows), [&](const cv::Range& range) {
            for (int y = range.start; y < range.end; ++y)
            {
                const float* detailRow = detail.ptr<float>(y);
                const float* xRow = gradX.ptr<float>(y);
                const float* yRow = gradY.ptr<float>(y);
                float* outputRow = retained.ptr<float>(y);
                for (int x = 0; x < detail.cols; ++x)
                {
                    float orientation = std::atan2(yRow[x], xRow[x]);
                    if (orientation < 0.0f) orientation += pi;
                    if (orientation >= pi) orientation -= pi;
                    const int sector = std::min(angleCount - 1,
                        static_cast<int>(orientation / sectorWidth));
                    const float centre = (sector + 0.5f) * sectorWidth;
                    const float alignment = std::abs(std::cos(orientation - centre));
                    const float gradient = std::hypot(xRow[x], yRow[x]);
                    const float activity = std::abs(detailRow[x]) *
                                           (0.65f + 0.35f * alignment) +
                                           0.20f * gradient;
                    if (activity > scaleThreshold)
                        outputRow[x] = detailRow[x];
                }
            }
        });
        reconstructed += retained;
        current = coarse;
    }
    reconstructed += current;
    return reconstructed;
}

struct SparseCodingResult
{
    cv::Mat centredPatches;
    cv::Mat reconstructedCentredPatches;
    cv::Mat coefficients;
    std::vector<float> means;
};

SparseCodingResult sparseCodeImage(const cv::Mat& image,
                                   const std::vector<cv::Point>& positions,
                                   int patchSize,
                                   int sparsity,
                                   const cv::Mat& dictionary,
                                   const cv::Mat& gram)
{
    SparseCodingResult result;
    const int patchArea = patchSize * patchSize;
    const int patchCount = static_cast<int>(positions.size());
    const int atomCount = dictionary.cols;
    result.centredPatches.create(patchCount, patchArea, CV_32F);
    result.reconstructedCentredPatches =
        cv::Mat::zeros(patchCount, patchArea, CV_32F);
    result.coefficients = cv::Mat::zeros(patchCount, atomCount, CV_32F);
    result.means.resize(positions.size(), 0.0f);

    for (int index = 0; index < patchCount; ++index)
    {
        const cv::Mat patch = image(cv::Rect(positions[index].x, positions[index].y,
                                             patchSize, patchSize));
        const float mean = static_cast<float>(cv::mean(patch)[0]);
        result.means[index] = mean;
        float* destination = result.centredPatches.ptr<float>(index);
        for (int y = 0; y < patchSize; ++y)
        {
            const float* source = patch.ptr<float>(y);
            for (int x = 0; x < patchSize; ++x)
                destination[y * patchSize + x] = source[x] - mean;
        }
    }

    cv::Mat correlations;
    cv::gemm(result.centredPatches, dictionary, 1.0,
             cv::Mat(), 0.0, correlations);
    cv::parallel_for_(cv::Range(0, patchCount), [&](const cv::Range& range) {
        std::vector<float> residualCorrelation(atomCount);
        std::vector<unsigned char> selectedFlags(atomCount);
        std::vector<int> selected;
        selected.reserve(sparsity);

        for (int patchIndex = range.start; patchIndex < range.end; ++patchIndex)
        {
            const float* initialCorrelation = correlations.ptr<float>(patchIndex);
            std::copy(initialCorrelation, initialCorrelation + atomCount,
                      residualCorrelation.begin());
            std::fill(selectedFlags.begin(), selectedFlags.end(), 0);
            selected.clear();
            cv::Mat selectedCoefficients;

            for (int iteration = 0; iteration < sparsity; ++iteration)
            {
                int bestAtom = -1;
                float bestMagnitude = 0.0f;
                for (int atom = 0; atom < atomCount; ++atom)
                {
                    if (selectedFlags[atom]) continue;
                    const float magnitude = std::abs(residualCorrelation[atom]);
                    if (magnitude > bestMagnitude)
                    {
                        bestMagnitude = magnitude;
                        bestAtom = atom;
                    }
                }
                if (bestAtom < 0 || bestMagnitude <= kEpsilon) break;

                selectedFlags[bestAtom] = 1;
                selected.push_back(bestAtom);
                const int count = static_cast<int>(selected.size());
                cv::Mat system(count, count, CV_32F);
                cv::Mat rhs(count, 1, CV_32F);
                for (int row = 0; row < count; ++row)
                {
                    rhs.at<float>(row, 0) = initialCorrelation[selected[row]];
                    for (int column = 0; column < count; ++column)
                    {
                        float value = gram.at<float>(selected[row], selected[column]);
                        if (row == column) value += 1e-5f;
                        system.at<float>(row, column) = value;
                    }
                }
                if (!cv::solve(system, rhs, selectedCoefficients,
                               cv::DECOMP_CHOLESKY) &&
                    !cv::solve(system, rhs, selectedCoefficients,
                               cv::DECOMP_SVD))
                {
                    selected.pop_back();
                    break;
                }

                for (int atom = 0; atom < atomCount; ++atom)
                {
                    float approximation = 0.0f;
                    for (int coefficient = 0; coefficient < count; ++coefficient)
                    {
                        approximation += gram.at<float>(atom, selected[coefficient]) *
                                         selectedCoefficients.at<float>(coefficient, 0);
                    }
                    residualCorrelation[atom] =
                        initialCorrelation[atom] - approximation;
                }
            }

            float* reconstructed =
                result.reconstructedCentredPatches.ptr<float>(patchIndex);
            float* coefficientRow = result.coefficients.ptr<float>(patchIndex);
            for (int coefficient = 0;
                 coefficient < static_cast<int>(selected.size()); ++coefficient)
            {
                const int atomIndex = selected[coefficient];
                const float weight = selectedCoefficients.at<float>(coefficient, 0);
                coefficientRow[atomIndex] = weight;
                for (int sample = 0; sample < patchArea; ++sample)
                {
                    reconstructed[sample] +=
                        dictionary.at<float>(sample, atomIndex) * weight;
                }
            }
        }
    });
    return result;
}

cv::Mat aggregateSparsePatches(const SparseCodingResult& coding,
                               const std::vector<cv::Point>& positions,
                               cv::Size imageSize,
                               int patchSize)
{
    cv::Mat image = cv::Mat::zeros(imageSize, CV_32F);
    cv::Mat weights = cv::Mat::zeros(imageSize, CV_32F);
    for (int index = 0; index < static_cast<int>(positions.size()); ++index)
    {
        const float* centred =
            coding.reconstructedCentredPatches.ptr<float>(index);
        const float mean = coding.means[index];
        for (int y = 0; y < patchSize; ++y)
        {
            float* imageRow = image.ptr<float>(positions[index].y + y) +
                              positions[index].x;
            float* weightRow = weights.ptr<float>(positions[index].y + y) +
                               positions[index].x;
            for (int x = 0; x < patchSize; ++x)
            {
                imageRow[x] += centred[y * patchSize + x] + mean;
                weightRow[x] += 1.0f;
            }
        }
    }
    image /= weights + kEpsilon;
    return image;
}

void updateDictionaryKsvd(SparseCodingResult& coding,
                          cv::Mat& dictionary,
                          cv::Mat& gram,
                          int maximumAtomUpdates)
{
    const int patchCount = coding.centredPatches.rows;
    const int patchArea = coding.centredPatches.cols;
    const int atomCount = dictionary.cols;
    std::vector<std::pair<int, int>> usage;
    usage.reserve(atomCount);
    for (int atom = 0; atom < atomCount; ++atom)
    {
        int count = 0;
        for (int patch = 0; patch < patchCount; ++patch)
            count += std::abs(coding.coefficients.at<float>(patch, atom)) > 1e-6f;
        if (count >= 2) usage.emplace_back(count, atom);
    }
    std::sort(usage.begin(), usage.end(),
              [](const auto& left, const auto& right) {
                  return left.first > right.first;
              });
    if (static_cast<int>(usage.size()) > maximumAtomUpdates)
        usage.resize(maximumAtomUpdates);

    for (const auto& entry : usage)
    {
        const int atom = entry.second;
        std::vector<int> relevant;
        relevant.reserve(entry.first);
        for (int patch = 0; patch < patchCount; ++patch)
        {
            if (std::abs(coding.coefficients.at<float>(patch, atom)) > 1e-6f)
                relevant.push_back(patch);
        }
        std::sort(relevant.begin(), relevant.end(), [&](int left, int right) {
            return std::abs(coding.coefficients.at<float>(left, atom)) >
                   std::abs(coding.coefficients.at<float>(right, atom));
        });
        constexpr int maximumSamplesPerAtom = 96;
        if (static_cast<int>(relevant.size()) > maximumSamplesPerAtom)
            relevant.resize(maximumSamplesPerAtom);

        const cv::Mat oldAtom = dictionary.col(atom).clone();
        cv::Mat errors(patchArea, static_cast<int>(relevant.size()), CV_32F);
        for (int column = 0; column < static_cast<int>(relevant.size()); ++column)
        {
            const int patch = relevant[column];
            const float oldCoefficient =
                coding.coefficients.at<float>(patch, atom);
            for (int sample = 0; sample < patchArea; ++sample)
            {
                errors.at<float>(sample, column) =
                    coding.centredPatches.at<float>(patch, sample) -
                    coding.reconstructedCentredPatches.at<float>(patch, sample) +
                    oldAtom.at<float>(sample, 0) * oldCoefficient;
            }
        }

        cv::Mat singularValues;
        cv::Mat leftVectors;
        cv::Mat rightVectors;
        cv::SVD::compute(errors, singularValues, leftVectors, rightVectors,
                         cv::SVD::MODIFY_A);
        if (singularValues.empty() || leftVectors.empty() || rightVectors.empty())
            continue;

        cv::Mat newAtom = leftVectors.col(0).clone();
        float sign = 1.0f;
        for (int sample = 0; sample < patchArea; ++sample)
        {
            if (std::abs(newAtom.at<float>(sample, 0)) > 1e-6f)
            {
                sign = newAtom.at<float>(sample, 0) < 0.0f ? -1.0f : 1.0f;
                break;
            }
        }
        newAtom *= sign;
        newAtom.copyTo(dictionary.col(atom));

        std::vector<float> replacement(patchCount,
            std::numeric_limits<float>::quiet_NaN());
        const float singularValue = singularValues.at<float>(0, 0);
        for (int column = 0; column < static_cast<int>(relevant.size()); ++column)
        {
            replacement[relevant[column]] =
                sign * singularValue * rightVectors.at<float>(0, column);
        }
        for (int patch = 0; patch < patchCount; ++patch)
        {
            const float oldCoefficient =
                coding.coefficients.at<float>(patch, atom);
            if (std::abs(oldCoefficient) <= 1e-6f) continue;
            const float newCoefficient = std::isfinite(replacement[patch]) ?
                replacement[patch] : oldCoefficient;
            coding.coefficients.at<float>(patch, atom) = newCoefficient;
            for (int sample = 0; sample < patchArea; ++sample)
            {
                coding.reconstructedCentredPatches.at<float>(patch, sample) +=
                    newAtom.at<float>(sample, 0) * newCoefficient -
                    oldAtom.at<float>(sample, 0) * oldCoefficient;
            }
        }

        cv::Mat atomProducts;
        cv::gemm(dictionary, newAtom, 1.0, cv::Mat(), 0.0,
                 atomProducts, cv::GEMM_1_T);
        for (int other = 0; other < atomCount; ++other)
        {
            const float product = atomProducts.at<float>(other, 0);
            gram.at<float>(atom, other) = product;
            gram.at<float>(other, atom) = product;
        }
    }
}

ClutterSuppressionResult runMca(
    const cv::Mat& input,
    const ClutterSuppressionParameters& p)
{
    // Reproduce the control flow and fixed parameters used by MCA1.m and
    // image_MCA.m with the native multiscale directional transform backend.
    constexpr int imageSide = 512;
    constexpr int dictionaryIterations = 1;
    const int patchSize = std::clamp(p.mcaPatchSize, 8, 32);
    const int patchStride = std::clamp(p.mcaPatchStride, 1, patchSize);
    const int sparsity = std::clamp(p.mcaSparsity, 1, 20);
    const int mcaIterations = std::clamp(p.mcaIterations, 5, 40);
    const float terminalThreshold = static_cast<float>(
        std::clamp(p.mcaTerminalThreshold, 0.5, 20.0));
    const float tvGamma = static_cast<float>(
        std::clamp(p.mcaTvGamma, 0.0, 20.0));

    cv::Mat working;
    cv::resize(input, working, cv::Size(imageSide, imageSide),
               0.0, 0.0, input.total() > imageSide * imageSide
                             ? cv::INTER_AREA : cv::INTER_CUBIC);

    const std::vector<int> rows =
        patchOrigins(working.rows, patchSize, patchStride);
    const std::vector<int> columns =
        patchOrigins(working.cols, patchSize, patchStride);
    std::vector<cv::Point> positions;
    positions.reserve(rows.size() * columns.size());
    for (int y : rows)
        for (int x : columns)
            positions.emplace_back(x, y);

    DctDictionaryCache localCache;
    const DctDictionaryCache* initialCache = nullptr;
    if (patchSize == 20)
    {
        static const DctDictionaryCache defaultCache =
            createOvercompleteDctDictionary(20);
        initialCache = &defaultCache;
    }
    else
    {
        localCache = createOvercompleteDctDictionary(patchSize);
        initialCache = &localCache;
    }
    cv::Mat dictionary = initialCache->dictionary.clone();
    cv::Mat gram = initialCache->gram.clone();

    // FastKsvd2Analysis.m uses non-overlapping 20x20 patches only to obtain
    // the initial maximum sparse coefficient.
    const std::vector<int> analysisRows =
        patchOrigins(working.rows, patchSize, patchSize);
    const std::vector<int> analysisColumns =
        patchOrigins(working.cols, patchSize, patchSize);
    std::vector<cv::Point> analysisPositions;
    analysisPositions.reserve(analysisRows.size() * analysisColumns.size());
    for (int y : analysisRows)
        for (int x : analysisColumns)
            analysisPositions.emplace_back(x, y);
    const SparseCodingResult initialCoding = sparseCodeImage(
        working, analysisPositions, patchSize, sparsity, dictionary, gram);
    double maximumDictionaryCoefficient = 0.0;
    for (int patch = 0; patch < initialCoding.coefficients.rows; ++patch)
    {
        double patchMaximum = 0.0;
        cv::minMaxLoc(initialCoding.coefficients.row(patch), nullptr,
                      &patchMaximum);
        maximumDictionaryCoefficient = std::max(
            maximumDictionaryCoefficient, std::abs(patchMaximum));
    }

    // Native equivalent of the maximum CURVWRAP detail coefficient used by
    // StartingPoint.m.  The smaller maximum of the two dictionaries is the
    // original MCA starting threshold.
    cv::Mat initialBlur;
    cv::GaussianBlur(working, initialBlur, cv::Size(), 1.2, 1.2,
                     cv::BORDER_REFLECT_101);
    cv::Mat initialDetail;
    cv::absdiff(working, initialBlur, initialDetail);
    double maximumDirectionalCoefficient = 0.0;
    cv::minMaxLoc(initialDetail, nullptr, &maximumDirectionalCoefficient);
    float startingThreshold = static_cast<float>(std::min(
        maximumDirectionalCoefficient, maximumDictionaryCoefficient));
    if (!std::isfinite(startingThreshold) || startingThreshold <= 0.0f)
        startingThreshold = terminalThreshold;

    const int curveletScales = std::clamp(p.mcaCurveletScales, 1, 6);
    const int curveletAngles = std::clamp(p.mcaCurveletAngles, 2, 32);
    cv::Mat curveletPart = cv::Mat::zeros(working.size(), CV_32F);
    cv::Mat dictionaryPart = cv::Mat::zeros(working.size(), CV_32F);
    SparseCodingResult lastCoding;

    for (int dictionaryPass = 0;
         dictionaryPass < dictionaryIterations; ++dictionaryPass)
    {
        for (int iteration = 0; iteration < mcaIterations; ++iteration)
        {
            // image_MCA.m: delta = (deltay - miu) / itery
            const float threshold =
                (startingThreshold - terminalThreshold) / (iteration + 1.0f);
            cv::Mat residual = working - curveletPart - dictionaryPart;

            const cv::Mat curveletInput = curveletPart + residual;
            curveletPart = curveletDirectionalThreshold(
                curveletInput, threshold, curveletScales, curveletAngles);
            curveletPart = haarTvCorrection(curveletPart, tvGamma);
            cv::absdiff(curveletPart, cv::Scalar(0), curveletPart);

            residual = working - curveletPart - dictionaryPart;
            const cv::Mat dictionaryInput = dictionaryPart + residual;
            lastCoding = sparseCodeImage(dictionaryInput, positions,
                                         patchSize, sparsity,
                                         dictionary, gram);
            cv::Mat nextDictionaryPart = aggregateSparsePatches(
                lastCoding, positions, working.size(), patchSize);
            // image_MCA.m explicitly suppresses K-SVD part on itery == 1.
            if (iteration == 0)
                nextDictionaryPart.setTo(0.0f);
            dictionaryPart = haarTvCorrection(nextDictionaryPart, tvGamma);
            cv::absdiff(dictionaryPart, cv::Scalar(0), dictionaryPart);
        }

        // With the original iterDIC=1 this update cannot affect the returned
        // image. Keep it only when a future comparison enables another pass.
        if (dictionaryPass + 1 < dictionaryIterations &&
            !lastCoding.coefficients.empty())
        {
            updateDictionaryKsvd(lastCoding, dictionary, gram,
                                 dictionary.cols);
        }
    }

    // MCA_exe.m returns abs(parts(:,:,1)) without the 70/30 enhancement blend.
    cv::Mat matlabOutput;
    cv::absdiff(curveletPart, cv::Scalar(0), matlabOutput);
    cv::min(matlabOutput, 255.0f, matlabOutput);
    cv::max(matlabOutput, 0.0f, matlabOutput);

    cv::Mat restoredOutput;
    cv::Mat restoredBackground;
    if (input.size() != working.size())
    {
        cv::resize(matlabOutput, restoredOutput, input.size(),
                   0.0, 0.0, cv::INTER_LINEAR);
        cv::resize(dictionaryPart, restoredBackground, input.size(),
                   0.0, 0.0, cv::INTER_LINEAR);
    }
    else
    {
        restoredOutput = matlabOutput;
        restoredBackground = dictionaryPart;
    }

    ClutterSuppressionResult result;
    restoredBackground.copyTo(result.backgroundMap);
    restoredOutput.convertTo(result.suppressedImage, CV_8U);
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
    p.mcaPatchSize = std::clamp(p.mcaPatchSize, 8, 32);
    p.mcaPatchStride = std::clamp(p.mcaPatchStride, 1, p.mcaPatchSize);
    p.mcaSparsity = std::clamp(p.mcaSparsity, 1, 20);
    p.mcaIterations = std::clamp(p.mcaIterations, 5, 40);
    p.mcaTerminalThreshold = std::clamp(p.mcaTerminalThreshold, 0.5, 20.0);
    p.mcaTvGamma = std::clamp(p.mcaTvGamma, 0.0, 20.0);
    p.mcaCurveletScales = std::clamp(p.mcaCurveletScales, 1, 6);
    p.mcaCurveletAngles = std::clamp(p.mcaCurveletAngles, 2, 32);

    switch (p.method)
    {
    case ClutterSuppressionMethod::CACFAR: return runCA(input, p);
    case ClutterSuppressionMethod::ACCFAR: return runCensored(input, p, false);
    case ClutterSuppressionMethod::AAFCFAR: return runCensored(input, p, true);
    case ClutterSuppressionMethod::VICFAR: return runVI(input, p);
    case ClutterSuppressionMethod::RmSATCFAR: return runRmSAT(input, p);
    case ClutterSuppressionMethod::MCA:
        return runMca(input, p);
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
    case ClutterSuppressionMethod::MCA: return "MCA";
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
    case ClutterSuppressionMethod::MCA: return "MCA";
    default: return "Clutter";
    }
}
