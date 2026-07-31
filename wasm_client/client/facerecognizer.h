#ifndef FACERECOGNIZER_H
#define FACERECOGNIZER_H

#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>
#include <memory>
#include <string>
#include <vector>

class FaceRecognizer {
public:
    explicit FaceRecognizer(const std::string& model_path);
    ~FaceRecognizer() = default;
    std::vector<float> extract(const cv::Mat& aligned_face);

private:
    Ort::Env env_;
    Ort::SessionOptions session_options_;
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo memory_info_;
    std::vector<std::string> output_names_str_;
    std::vector<const char*> output_names_ptr_;
    int input_size_ = 112;
};

#endif // FACERECOGNIZER_H