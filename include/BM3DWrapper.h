#pragma once
#include <opencv2/opencv.hpp>

class BM3DWrapper
{
public:
    static cv::Mat DenoiseGray(const cv::Mat& img8U, double sigma8);
};
