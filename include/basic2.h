#ifndef BASIC2_H
#define BASIC2_H

#include <opencv2/opencv.hpp>
#include <vector>

// 定义一个结构体来存储提取到的 4 个基础特征
struct BasicFeatures {
    double fphr;         // peak_high_feature
    double correlation;  // 相关性
    double contrast;     // 对比度
    double asm_val;      // 能量 (ASM)
};


BasicFeatures extract_basic_features(const cv::Mat& img_gray);

#endif // BASIC2_H