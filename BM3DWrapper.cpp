#include "BM3DWrapper.h"
#include "bm3d.h"
#include "bm3d_wiener.h"


cv::Mat BM3DWrapper::DenoiseGray(const cv::Mat& img8U, double sigma8)
{
    if (img8U.empty())
    {
        return cv::Mat();
    }

    if (img8U.type() != CV_8UC1)
    {
        return cv::Mat();
    }

    int width = img8U.cols;
    int height = img8U.rows;

    cv::Mat noisy = img8U.clone();
    cv::Mat basic(height, width, CV_8UC1);
    cv::Mat clean(height, width, CV_8UC1);

    int sigma = cvRound(sigma8);
    if (sigma < 1)
    {
        sigma = 1;
    }

    BM3D bm3d(
        width,
        height,
        16, // max_sim，对应 MATLAB 第一阶段 N2 = 16
        8,  // psize，对应 N1 = 8
        3,  // pstep，对应 Nstep = 3
        16, // swinrh，搜索半径，约对应 MATLAB Ns = 39 的一半
        1,  // ssteph
        16, // swinrv
        1   // sstepv
    );

    bm3d.load(
        noisy.ptr<ImageType>(),
        sigma,
        2500 // max_mdist，接近 MATLAB tau_match = 3000
    );
    bm3d.run(basic.ptr<ImageType>());

    BM3D_WIE bm3d_wie(
        width,
        height,
        32, // max_sim，对应 MATLAB 第二阶段 N2_wiener = 32
        8,  // psize，对应 N1_wiener = 8
        3,  // pstep，对应 Nstep_wiener = 3
        16, // swinrh
        1,  // ssteph
        16, // swinrv
        1   // sstepv
    );

    bm3d_wie.load(
        noisy.ptr<ImageType>(),
        basic.ptr<ImageType>(),
        sigma,
        400 // max_mdist，接近 MATLAB tau_match_wiener = 400
    );
    bm3d_wie.run(clean.ptr<ImageType>());

    return clean;
}
