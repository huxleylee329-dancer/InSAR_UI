#define NOMINMAX

#include <Deflat.h>
#include <Hdf5IO.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr double kSpeedOfLightMetersPerSecond = 299792458.0;
    constexpr double kInterpolationMarginSeconds = 10.0;

    struct Arguments
    {
        std::string masterPath;
        std::string slavePath;
        std::string outputPath;
        std::vector<int> sourceRows{ 4459, 4633 };
        std::vector<int> columns{ 256, 12478, 24700 };
        bool overwrite = false;
    };

    struct InputProduct
    {
        TopsBurstPhaseMetadata tops;
        cv::Mat sourceRowMap;
        int sourceRowCount = 0;
        int sourceBurstOffset = 0;
        int outputOffsetColumn = 0;
        int outputColumns = 0;
        double carrierFrequencyHz = 0.0;
        double prfHz = 0.0;
        int lookSide = 0;
        TopsFepV5Orbit baseFineOrbit;
    };

    struct PointResult
    {
        int sourceRow = -1;
        int column = -1;
        int status = 0;
        TopsFepV5FixedPointDiagnostic diagnostic;
    };

    struct StrategyResults
    {
        std::string name;
        std::vector<PointResult> points;
    };

    void usage()
    {
        std::fprintf(stderr,
            "Usage:\n"
            "  TopsEdgeProbe_d.exe --master <registered-master.h5> --slave <registered-slave.h5> --out <probe.json>\n"
            "      [--rows 4459,4633] [--columns 256,12478,24700] [--overwrite]\n\n"
            "The probe is read-only. It never reads SLC samples or writes H5 products.\n");
    }

    bool parseIntegerList(const char* text, std::vector<int>& values)
    {
        values.clear();
        if (!text || !*text) return false;
        std::stringstream stream(text);
        std::string token;
        while (std::getline(stream, token, ','))
        {
            if (token.empty()) return false;
            char* end = nullptr;
            const long value = std::strtol(token.c_str(), &end, 10);
            if (!end || *end != '\0' || value < 0 || value > std::numeric_limits<int>::max()) return false;
            values.push_back(static_cast<int>(value));
        }
        return !values.empty();
    }

    bool parseArguments(int argc, char** argv, Arguments& arguments)
    {
        for (int index = 1; index < argc; ++index)
        {
            const std::string option(argv[index]);
            if (option == "--master" || option == "--slave" || option == "--out" ||
                option == "--rows" || option == "--columns")
            {
                if (++index >= argc) return false;
                const char* value = argv[index];
                if (option == "--master") arguments.masterPath = value;
                else if (option == "--slave") arguments.slavePath = value;
                else if (option == "--out") arguments.outputPath = value;
                else if (option == "--rows" && !parseIntegerList(value, arguments.sourceRows)) return false;
                else if (option == "--columns" && !parseIntegerList(value, arguments.columns)) return false;
            }
            else if (option == "--overwrite")
            {
                arguments.overwrite = true;
            }
            else
            {
                return false;
            }
        }
        return !arguments.masterPath.empty() && !arguments.slavePath.empty() && !arguments.outputPath.empty();
    }

    bool fileExists(const std::string& path)
    {
        std::ifstream stream(path.c_str(), std::ios::binary);
        return stream.good();
    }

    bool readMat(const std::string& path, const char* dataset, cv::Mat& value)
    {
        value.release();
        return Hdf5IO::readArray(path.c_str(), dataset, value) == 0 && !value.empty();
    }

    bool readString(const std::string& path, const char* dataset, std::string& value)
    {
        value.clear();
        return Hdf5IO::readString(path.c_str(), dataset, value) == 0;
    }

    bool readIntScalar(const std::string& path, const char* dataset, int& value)
    {
        cv::Mat matrix;
        if (!readMat(path, dataset, matrix) || matrix.total() != 1) return false;
        switch (matrix.type())
        {
        case CV_32S: value = matrix.at<int>(0, 0); return true;
        case CV_64F:
            if (!std::isfinite(matrix.at<double>(0, 0))) return false;
            value = static_cast<int>(std::llround(matrix.at<double>(0, 0)));
            return std::fabs(matrix.at<double>(0, 0) - value) < 1e-9;
        default: return false;
        }
    }

    bool readDoubleScalar(const std::string& path, const char* dataset, double& value)
    {
        cv::Mat matrix;
        if (!readMat(path, dataset, matrix) || matrix.total() != 1) return false;
        if (matrix.type() == CV_64F) value = matrix.at<double>(0, 0);
        else if (matrix.type() == CV_32F) value = matrix.at<float>(0, 0);
        else if (matrix.type() == CV_32S) value = matrix.at<int>(0, 0);
        else return false;
        return std::isfinite(value);
    }

    bool isValidFineStateVectors(const cv::Mat& stateVectors, double geometryStartGps,
        double geometryStopGps, double& osvStartGps, double& osvStopGps)
    {
        if (stateVectors.type() != CV_64F || stateVectors.cols != 7 || stateVectors.rows < 4 ||
            !std::isfinite(geometryStartGps) || !std::isfinite(geometryStopGps) ||
            geometryStopGps <= geometryStartGps) return false;
        for (int row = 0; row < stateVectors.rows; ++row)
        {
            for (int column = 0; column < stateVectors.cols; ++column)
            {
                if (!std::isfinite(stateVectors.at<double>(row, column))) return false;
            }
            if (row > 0 && stateVectors.at<double>(row, 0) <= stateVectors.at<double>(row - 1, 0)) return false;
        }
        osvStartGps = stateVectors.at<double>(0, 0);
        osvStopGps = stateVectors.at<double>(stateVectors.rows - 1, 0);
        return osvStartGps <= geometryStartGps - kInterpolationMarginSeconds &&
            osvStopGps >= geometryStopGps + kInterpolationMarginSeconds;
    }

    bool readTopsMetadata(const std::string& path, bool requireRegistration, TopsBurstPhaseMetadata& tops)
    {
        tops = TopsBurstPhaseMetadata();
        if (!readMat(path, "burstAzimuthTime", tops.burstAzimuthTime) ||
            !readMat(path, "azimuthFmRateList", tops.azimuthFmRateList) ||
            !readMat(path, "dcEstimateList", tops.dcEstimateList) ||
            !readMat(path, "firstValidLine", tops.firstValidLine) ||
            !readMat(path, "lastValidLine", tops.lastValidLine) ||
            !readMat(path, "firstValidSample", tops.firstValidSample) ||
            !readMat(path, "lastValidSample", tops.lastValidSample) ||
            !readIntScalar(path, "linesPerBurst", tops.linesPerBurst) ||
            !readDoubleScalar(path, "azimuthSteeringRate", tops.azimuthSteeringRate) ||
            !readDoubleScalar(path, "range_spacing", tops.rangeSpacing) ||
            !readDoubleScalar(path, "slant_range_first_pixel", tops.slantRangeFirstPixel) ||
            tops.linesPerBurst < 1) return false;
        if (!requireRegistration) return true;

        std::string mappingSemantics;
        return readMat(path, "s1_tops_registration_mapping_coefficients", tops.registrationMappingCoefficients) &&
            readMat(path, "s1_tops_retained_master_burst_indices", tops.registrationMappingMasterBurstIndices) &&
            readString(path, "s1_tops_registration_mapping_semantics", mappingSemantics) &&
            mappingSemantics == "pull_source_row_and_column_offsets_a0_a1_column_a2_master_burst_line_v1";
    }

    bool nativeTopsGeometryCoverage(const TopsBurstPhaseMetadata& tops, int masterLinesPerBurst,
        const cv::Mat& sourceRowMap, int slaveBurstOffset, bool useSlaveBurst, double extraSearchSeconds,
        double& startGps, double& stopGps)
    {
        if (sourceRowMap.type() != CV_32S || sourceRowMap.cols != 1 || sourceRowMap.rows < 1 ||
            tops.burstAzimuthTime.type() != CV_64F || tops.burstAzimuthTime.cols != 1 ||
            tops.linesPerBurst < 1 || masterLinesPerBurst < 1 ||
            !std::isfinite(tops.azimuthIntervalSeconds) || tops.azimuthIntervalSeconds <= 0.0) return false;
        startGps = std::numeric_limits<double>::infinity();
        stopGps = -std::numeric_limits<double>::infinity();
        for (int row = 0; row < sourceRowMap.rows; ++row)
        {
            const int sourceRow = sourceRowMap.at<int>(row, 0);
            const int masterBurst = sourceRow / masterLinesPerBurst;
            const int nativeLine = sourceRow % masterLinesPerBurst;
            const int burst = useSlaveBurst ? masterBurst + slaveBurstOffset : masterBurst;
            if (sourceRow < 0 || masterBurst < 0 || burst < 0 || burst >= tops.burstAzimuthTime.rows) return false;
            const double burstStart = tops.burstAzimuthTime.at<double>(burst, 0);
            const double first = useSlaveBurst ? burstStart : burstStart + nativeLine * tops.azimuthIntervalSeconds;
            const double last = useSlaveBurst ? burstStart + (tops.linesPerBurst - 1) * tops.azimuthIntervalSeconds : first;
            if (!std::isfinite(first) || !std::isfinite(last)) return false;
            startGps = std::min(startGps, first);
            stopGps = std::max(stopGps, last);
        }
        startGps -= extraSearchSeconds;
        stopGps += extraSearchSeconds;
        return std::isfinite(startGps) && std::isfinite(stopGps) && stopGps > startGps;
    }

    bool loadFineOrbit(const std::string& path, double geometryStartGps, double geometryStopGps, TopsFepV5Orbit& orbit)
    {
        orbit = TopsFepV5Orbit();
        std::string referenceVersion;
        std::string acquisitionScale;
        std::string stateVectorScale;
        std::string fineScale;
        if (!readString(path, "h5_time_reference_version", referenceVersion) || referenceVersion != "2" ||
            !readString(path, "acquisition_time_gps_scale", acquisitionScale) || acquisitionScale != "GPS" ||
            !readString(path, "state_vec_time_scale", stateVectorScale) || stateVectorScale != "GPS" ||
            !readString(path, "fine_state_vec_time_scale", fineScale) || fineScale != "GPS" ||
            !readDoubleScalar(path, "acquisition_start_time_gps", orbit.acquisitionStartGps) ||
            !readDoubleScalar(path, "acquisition_stop_time_gps", orbit.acquisitionStopGps) ||
            !readMat(path, "fine_state_vec", orbit.stateVectors) ||
            orbit.acquisitionStopGps <= orbit.acquisitionStartGps) return false;

        orbit.geometryStartGps = geometryStartGps;
        orbit.geometryStopGps = geometryStopGps;
        orbit.interpolationMarginSeconds = kInterpolationMarginSeconds;
        if (!isValidFineStateVectors(orbit.stateVectors, geometryStartGps, geometryStopGps,
            orbit.osvStartGps, orbit.osvStopGps)) return false;
        orbit.source = "fine_state_vec";
        orbit.selectionReason = "fine_state_vec_valid_preferred_v1";
        orbit.timeScale = "GPS";
        return true;
    }

    bool readLookSide(const std::string& path, int& lookSide)
    {
        std::string value;
        if (readString(path, "lookside", value))
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
                return static_cast<char>(std::toupper(character));
            });
            if (value == "RIGHT") { lookSide = 1; return true; }
            if (value == "LEFT") { lookSide = -1; return true; }
            return false;
        }
        std::string sensor;
        if (!readString(path, "sensor", sensor)) return false;
        std::transform(sensor.begin(), sensor.end(), sensor.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        if (sensor == "sentinel" || sensor == "sentinel-1" || sensor == "sentinel1")
        {
            lookSide = 1;
            return true;
        }
        return false;
    }

    bool loadInputProducts(const Arguments& arguments, InputProduct& master, InputProduct& slave, std::string& error)
    {
        master = InputProduct();
        slave = InputProduct();
        if (!readTopsMetadata(arguments.masterPath, false, master.tops) ||
            !readTopsMetadata(arguments.slavePath, true, slave.tops) ||
            !readDoubleScalar(arguments.masterPath, "prf", master.prfHz) || master.prfHz <= 0.0 ||
            !readDoubleScalar(arguments.slavePath, "prf", slave.prfHz) || slave.prfHz <= 0.0 ||
            !readDoubleScalar(arguments.masterPath, "carrier_frequency", master.carrierFrequencyHz) || master.carrierFrequencyHz <= 0.0 ||
            !readIntScalar(arguments.masterPath, "offset_col", master.outputOffsetColumn) || master.outputOffsetColumn < 0 ||
            !readIntScalar(arguments.masterPath, "range_len", master.outputColumns) || master.outputColumns < 1 ||
            !readIntScalar(arguments.masterPath, "s1_tops_source_full_burst_row_count", master.sourceRowCount) || master.sourceRowCount < 1 ||
            !readIntScalar(arguments.slavePath, "s1_tops_source_burst_offset", slave.sourceBurstOffset) ||
            !readMat(arguments.masterPath, "s1_tops_output_source_row_map", master.sourceRowMap) ||
            !readMat(arguments.slavePath, "s1_tops_output_source_row_map", slave.sourceRowMap) ||
            !readLookSide(arguments.masterPath, master.lookSide))
        {
            error = "failed to read the strict registered TOPS input contract";
            return false;
        }
        master.tops.azimuthIntervalSeconds = 1.0 / master.prfHz;
        slave.tops.azimuthIntervalSeconds = 1.0 / slave.prfHz;
        if (master.sourceRowMap.type() != CV_32S || master.sourceRowMap.cols != 1 ||
            slave.sourceRowMap.type() != CV_32S || slave.sourceRowMap.size() != master.sourceRowMap.size() ||
            cv::countNonZero(master.sourceRowMap != slave.sourceRowMap) != 0)
        {
            error = "master/slave source-row maps are missing or differ";
            return false;
        }

        double masterStart = 0.0;
        double masterStop = 0.0;
        double slaveStart = 0.0;
        double slaveStop = 0.0;
        TopsFepV5Options options;
        if (!nativeTopsGeometryCoverage(master.tops, master.tops.linesPerBurst, master.sourceRowMap,
                0, false, 0.0, masterStart, masterStop) ||
            !nativeTopsGeometryCoverage(slave.tops, master.tops.linesPerBurst, master.sourceRowMap,
                slave.sourceBurstOffset, true, options.slaveSearchMaximumHalfWindowSeconds, slaveStart, slaveStop) ||
            !loadFineOrbit(arguments.masterPath, masterStart, masterStop, master.baseFineOrbit) ||
            !loadFineOrbit(arguments.slavePath, slaveStart, slaveStop, slave.baseFineOrbit))
        {
            error = "strict fine_state_vec or native geometry coverage contract is invalid";
            return false;
        }
        return true;
    }

    TopsBurstPhaseMetadata localizeSlaveMapping(const TopsBurstPhaseMetadata& original, int originalColumn)
    {
        TopsBurstPhaseMetadata localized = original;
        // cv::Mat assignment is shallow. The two strategy passes must start from
        // the identical registered mapping, so only this probe-local matrix may change.
        localized.registrationMappingCoefficients = original.registrationMappingCoefficients.clone();
        for (int row = 0; row < localized.registrationMappingCoefficients.rows; ++row)
        {
            const double a1Range = original.registrationMappingCoefficients.at<double>(row, 1);
            const double a1Azimuth = original.registrationMappingCoefficients.at<double>(row, 4);
            localized.registrationMappingCoefficients.at<double>(row, 0) =
                original.registrationMappingCoefficients.at<double>(row, 0) + originalColumn + a1Range * originalColumn;
            localized.registrationMappingCoefficients.at<double>(row, 3) =
                original.registrationMappingCoefficients.at<double>(row, 3) + a1Azimuth * originalColumn;
        }
        return localized;
    }

    bool sourceRowIsRetained(const cv::Mat& sourceRowMap, int sourceRow)
    {
        for (int row = 0; row < sourceRowMap.rows; ++row)
        {
            if (sourceRowMap.at<int>(row, 0) == sourceRow) return true;
        }
        return false;
    }

    bool runStrategy(const std::string& strategy, const InputProduct& master, const InputProduct& slave,
        const Arguments& arguments, StrategyResults& results, std::string& error)
    {
        results.name = strategy;
        results.points.clear();
        const double wavelength = kSpeedOfLightMetersPerSecond / master.carrierFrequencyHz;
        TopsFepV5Options options;
        options.masterLookSide = master.lookSide;

        TopsFepV5Orbit masterOrbit = master.baseFineOrbit;
        TopsFepV5Orbit slaveOrbit = slave.baseFineOrbit;
        if (strategy == "fine_v2")
        {
            masterOrbit.interpolationStrategy = "fine_state_vec_cubic_hermite_v2";
            slaveOrbit.interpolationStrategy = "fine_state_vec_cubic_hermite_v2";
        }
        else if (strategy == "fine_lagrange")
        {
            masterOrbit.interpolationStrategy = "orbit_state_vectors_apply_orbit_1s_lagrange_v1";
            slaveOrbit.interpolationStrategy = "orbit_state_vectors_apply_orbit_1s_lagrange_v1";
        }
        else
        {
            error = "unknown strategy";
            return false;
        }

        Deflat deflat;
        std::set<int> requestedBursts;
        for (const int sourceRow : arguments.sourceRows)
        {
            if (sourceRow < 0 || sourceRow >= master.sourceRowCount || !sourceRowIsRetained(master.sourceRowMap, sourceRow))
            {
                error = "requested source row is outside the retained full-burst source-row map";
                return false;
            }
            if (!requestedBursts.insert(sourceRow / master.tops.linesPerBurst).second)
            {
                error = "only one requested source row per master burst is supported by the fixed-point probe";
                return false;
            }
        }

        std::map<std::pair<int, int>, PointResult> pointByCoordinate;
        for (const int column : arguments.columns)
        {
            if (column < 0 || column >= master.outputColumns)
            {
                error = "requested column is outside the registered output grid";
                return false;
            }
            cv::Mat localSourceRowMap(static_cast<int>(arguments.sourceRows.size()), 1, CV_32S);
            for (size_t rowIndex = 0; rowIndex < arguments.sourceRows.size(); ++rowIndex)
            {
                localSourceRowMap.at<int>(static_cast<int>(rowIndex), 0) = arguments.sourceRows[rowIndex];
            }
            cv::Mat pairValidMask(localSourceRowMap.rows, 1, CV_8U, cv::Scalar(1));
            cv::Mat phase;
            TopsFepV5Provenance provenance;
            const TopsBurstPhaseMetadata localSlaveTops = localizeSlaveMapping(slave.tops, column);
            const int result = deflat.computeSentinel1FlatEarthPhaseV5(masterOrbit, slaveOrbit,
                master.tops, localSlaveTops, localSourceRowMap, pairValidMask, master.sourceRowCount,
                1, master.outputOffsetColumn + column, slave.sourceBurstOffset, 1, wavelength,
                options, phase, provenance, nullptr);
            for (const int sourceRow : arguments.sourceRows)
            {
                PointResult point;
                point.sourceRow = sourceRow;
                point.column = column;
                point.status = result;
                const int burst = sourceRow / master.tops.linesPerBurst;
                if (burst >= 0 && burst < static_cast<int>(provenance.fixedPointDiagnostics.size()))
                {
                    point.diagnostic = provenance.fixedPointDiagnostics[burst];
                }
                if (result != 0 || point.diagnostic.status != 1)
                {
                    std::ostringstream message;
                    message << "V5 probe failed for strategy=" << strategy << " source_row=" << sourceRow
                        << " column=" << column << " result=" << result << " diagnostic_status=" << point.diagnostic.status;
                    error = message.str();
                    return false;
                }
                pointByCoordinate[std::make_pair(sourceRow, column)] = point;
            }
        }
        for (const int sourceRow : arguments.sourceRows)
        {
            for (const int column : arguments.columns)
            {
                results.points.push_back(pointByCoordinate[std::make_pair(sourceRow, column)]);
            }
        }
        return true;
    }

    void jsonString(std::ostream& stream, const std::string& value)
    {
        stream << '"';
        for (const char character : value)
        {
            switch (character)
            {
            case '\\': stream << "\\\\"; break;
            case '"': stream << "\\\""; break;
            case '\n': stream << "\\n"; break;
            case '\r': stream << "\\r"; break;
            case '\t': stream << "\\t"; break;
            default: stream << character; break;
            }
        }
        stream << '"';
    }

    void writeDiagnostic(std::ostream& stream, const TopsFepV5FixedPointDiagnostic& value)
    {
        stream << std::setprecision(17) << "{"
            << "\"status\":" << value.status
            << ",\"master_burst\":" << value.masterBurst
            << ",\"slave_burst\":" << value.slaveBurst
            << ",\"master_time_gps\":" << value.masterTimeGps
            << ",\"master_position_ecef_m\":[" << value.masterSatelliteX << ',' << value.masterSatelliteY << ',' << value.masterSatelliteZ << ']'
            << ",\"master_velocity_ecef_mps\":[" << value.masterVelocityX << ',' << value.masterVelocityY << ',' << value.masterVelocityZ << ']'
            << ",\"master_rde_point_ecef_m\":[" << value.pointX << ',' << value.pointY << ',' << value.pointZ << ']'
            << ",\"rho_master_m\":" << value.rhoMaster
            << ",\"slave_native_line\":" << value.slaveNativeLine
            << ",\"slave_native_sample\":" << value.slaveNativeSample
            << ",\"slave_time_seed_gps\":" << value.slaveTimeSeedGps
            << ",\"slave_time_gps\":" << value.slaveTimeGps
            << ",\"slave_position_ecef_m\":[" << value.slaveSatelliteX << ',' << value.slaveSatelliteY << ',' << value.slaveSatelliteZ << ']'
            << ",\"slave_velocity_ecef_mps\":[" << value.slaveVelocityX << ',' << value.slaveVelocityY << ',' << value.slaveVelocityZ << ']'
            << ",\"rho_slave_m\":" << value.rhoSlave
            << ",\"rho_slave_minus_master_m\":" << value.rhoSlave - value.rhoMaster
            << ",\"fep_rad\":" << value.geometryPhase
            << ",\"differential_range_error_bound_m\":" << value.differentialRangeErrorBound
            << ",\"final_geometry_phase_change_rad\":" << value.finalGeometryPhaseChange
            << '}';
    }

    bool writeOutput(const Arguments& arguments, const InputProduct& master, const InputProduct& slave,
        const StrategyResults& fineV2, const StrategyResults& fineLagrange, std::string& error)
    {
        if (!arguments.overwrite && fileExists(arguments.outputPath))
        {
            error = "output JSON already exists; choose a new --out path or pass --overwrite";
            return false;
        }
        if (fineV2.points.size() != fineLagrange.points.size())
        {
            error = "strategy result cardinalities differ";
            return false;
        }
        std::ofstream stream(arguments.outputPath.c_str(), std::ios::out | std::ios::trunc);
        if (!stream)
        {
            error = "cannot create output JSON";
            return false;
        }
        stream << std::setprecision(17) << "{\n"
            << "  \"tool\": \"TopsEdgeProbe\",\n"
            << "  \"method\": \"read-only single-pixel V5 geometry probe; no SLC read and no H5 product write\",\n"
            << "  \"inputs\": {\n    \"master_h5\": ";
        jsonString(stream, arguments.masterPath);
        stream << ",\n    \"slave_h5\": ";
        jsonString(stream, arguments.slavePath);
        stream << ",\n    \"source_row_count\": " << master.sourceRowCount
            << ",\n    \"slave_source_burst_offset\": " << slave.sourceBurstOffset
            << ",\n    \"registered_output_columns\": " << master.outputColumns
            << ",\n    \"master_offset_column\": " << master.outputOffsetColumn
            << ",\n    \"fine_state_vec_only\": true\n  },\n"
            << "  \"points\": [\n";
        for (size_t index = 0; index < fineV2.points.size(); ++index)
        {
            const PointResult& v2 = fineV2.points[index];
            const PointResult& lagrange = fineLagrange.points[index];
            stream << "    {\n      \"source_row\": " << v2.sourceRow
                << ",\n      \"column\": " << v2.column
                << ",\n      \"fine_v2\": ";
            writeDiagnostic(stream, v2.diagnostic);
            stream << ",\n      \"fine_lagrange\": ";
            writeDiagnostic(stream, lagrange.diagnostic);
            stream << ",\n      \"fine_v2_minus_fine_lagrange\": {"
                << "\"master_time_gps\":" << v2.diagnostic.masterTimeGps - lagrange.diagnostic.masterTimeGps
                << ",\"rho_master_m\":" << v2.diagnostic.rhoMaster - lagrange.diagnostic.rhoMaster
                << ",\"slave_time_gps\":" << v2.diagnostic.slaveTimeGps - lagrange.diagnostic.slaveTimeGps
                << ",\"rho_slave_m\":" << v2.diagnostic.rhoSlave - lagrange.diagnostic.rhoSlave
                << ",\"fep_rad\":" << v2.diagnostic.geometryPhase - lagrange.diagnostic.geometryPhase
                << "}\n    }" << (index + 1 == fineV2.points.size() ? "\n" : ",\n");
        }
        stream << "  ],\n  \"consecutive_source_row_fep_deltas\": [\n";
        bool firstBoundaryDelta = true;
        const size_t columnCount = arguments.columns.size();
        for (size_t rowIndex = 1; rowIndex < arguments.sourceRows.size(); ++rowIndex)
        {
            for (size_t columnIndex = 0; columnIndex < columnCount; ++columnIndex)
            {
                const size_t previousIndex = (rowIndex - 1) * columnCount + columnIndex;
                const size_t currentIndex = rowIndex * columnCount + columnIndex;
                const TopsFepV5FixedPointDiagnostic& previousV2 = fineV2.points[previousIndex].diagnostic;
                const TopsFepV5FixedPointDiagnostic& currentV2 = fineV2.points[currentIndex].diagnostic;
                const TopsFepV5FixedPointDiagnostic& previousLagrange = fineLagrange.points[previousIndex].diagnostic;
                const TopsFepV5FixedPointDiagnostic& currentLagrange = fineLagrange.points[currentIndex].diagnostic;
                if (!firstBoundaryDelta) stream << ",\n";
                firstBoundaryDelta = false;
                stream << "    {\"from_source_row\":" << fineV2.points[previousIndex].sourceRow
                    << ",\"to_source_row\":" << fineV2.points[currentIndex].sourceRow
                    << ",\"column\":" << fineV2.points[currentIndex].column
                    << ",\"fine_v2_fep_delta_rad\":" << currentV2.geometryPhase - previousV2.geometryPhase
                    << ",\"fine_lagrange_fep_delta_rad\":" << currentLagrange.geometryPhase - previousLagrange.geometryPhase
                    << ",\"fine_v2_minus_fine_lagrange_delta_rad\":"
                    << (currentV2.geometryPhase - currentLagrange.geometryPhase) -
                       (previousV2.geometryPhase - previousLagrange.geometryPhase)
                    << '}';
            }
        }
        stream << "\n  ]\n}\n";
        if (!stream.good())
        {
            error = "failed while writing output JSON";
            return false;
        }
        return true;
    }
}

int main(int argc, char** argv)
{
    Arguments arguments;
    if (!parseArguments(argc, argv, arguments))
    {
        usage();
        return 2;
    }
    InputProduct master;
    InputProduct slave;
    std::string error;
    if (!loadInputProducts(arguments, master, slave, error))
    {
        std::fprintf(stderr, "Input contract error: %s\n", error.c_str());
        return 1;
    }
    StrategyResults fineV2;
    StrategyResults fineLagrange;
    if (!runStrategy("fine_v2", master, slave, arguments, fineV2, error) ||
        !runStrategy("fine_lagrange", master, slave, arguments, fineLagrange, error) ||
        !writeOutput(arguments, master, slave, fineV2, fineLagrange, error))
    {
        std::fprintf(stderr, "Probe failed: %s\n", error.c_str());
        return 1;
    }
    std::printf("Probe completed: %s\n", arguments.outputPath.c_str());
    return 0;
}
