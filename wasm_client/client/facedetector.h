#ifndef FACEDETECTOR_H
#define FACEDETECTOR_H

#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>
#include <memory>
#include <string>
#include <vector>

struct FaceInfo {
    cv::Rect bbox;
    float conf = 0.0f;
    std::vector<cv::Point2f> landmarks;
};

class FaceDetector {
public:
    explicit FaceDetector(const std::string& model_path);
    ~FaceDetector() = default;
    std::vector<FaceInfo> detect(const cv::Mat& img);

private:
    void generatePriors();
    std::vector<FaceInfo> parseOutputs(const std::vector<Ort::Value>& outputs, int img_h, int img_w);
    std::vector<FaceInfo> decodeThreeOutputs(const float* bbox_ptr, const float* conf_ptr,
                                                const float* landm_ptr, int num_priors,
                                                int img_h, int img_w);
    std::vector<FaceInfo> decodeMergedOutput(const float* data, int num, int cols,
                                                int img_h, int img_w);

    Ort::Env env_;
    Ort::SessionOptions session_options_;
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo memory_info_;
    std::vector<std::string> output_names_str_;
    std::vector<const char*> output_names_ptr_;
    std::vector<float> priors_;
    int input_w_ = 640;
    int input_h_ = 640;
    float conf_thresh_ = 0.5f;
    float nms_thresh_ = 0.4f;
};

#endif // FACEDETECTOR_H
