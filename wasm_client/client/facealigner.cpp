#include "facealigner.h"

FaceAligner::FaceAligner() {
    dst_pts_ = {
        {38.2946f, 51.6963f},
        {73.5318f, 51.5014f},
        {56.0252f, 71.7366f},
        {41.5493f, 92.3655f},
        {70.7299f, 92.2041f}
    };
}

cv::Mat FaceAligner::align(const cv::Mat& img, const std::vector<cv::Point2f>& landmarks) {
    if (landmarks.size() < 5) return cv::Mat();

    std::vector<cv::Point2f> src_pts(landmarks.begin(), landmarks.begin() + 5);
    cv::Mat M = cv::estimateAffinePartial2D(src_pts, dst_pts_);
    if (M.empty()) return cv::Mat();

    cv::Mat aligned;
    cv::warpAffine(img, aligned, M, cv::Size(OUT_SIZE, OUT_SIZE),
                   cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
    return aligned;
}