#ifndef FACEALIGNER_H
#define FACEALIGNER_H

#include <opencv2/opencv.hpp>
#include <vector>

class FaceAligner {
public:
    FaceAligner();
    cv::Mat align(const cv::Mat& img, const std::vector<cv::Point2f>& landmarks);
    static constexpr int OUT_SIZE = 112;

private:
    std::vector<cv::Point2f> dst_pts_;
};

#endif // FACEALIGNER_H