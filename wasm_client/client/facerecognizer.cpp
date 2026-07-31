#include "facerecognizer.h"
#include <cmath>
#include <string>

FaceRecognizer::FaceRecognizer(const std::string& model_path)
    : env_(ORT_LOGGING_LEVEL_WARNING, "FaceRecognizer"),
      session_options_(),
      memory_info_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
    session_options_.SetIntraOpNumThreads(1);
    session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef _WIN32
    std::wstring wpath(model_path.begin(), model_path.end());
    session_ = std::make_unique<Ort::Session>(env_, wpath.c_str(), session_options_);
#else
    session_ = std::make_unique<Ort::Session>(env_, model_path.c_str(), session_options_);
#endif

    Ort::AllocatorWithDefaultOptions allocator;
    size_t num_outputs = session_->GetOutputCount();
    for (size_t i = 0; i < num_outputs; ++i) {
        auto name = session_->GetOutputNameAllocated(i, allocator);
        output_names_str_.emplace_back(name.get());
    }
    for (const auto& s : output_names_str_) {
        output_names_ptr_.push_back(s.c_str());
    }
}

std::vector<float> FaceRecognizer::extract(const cv::Mat& aligned_face) {
    cv::Mat rgb;
    if (aligned_face.channels() == 3) {
        cv::cvtColor(aligned_face, rgb, cv::COLOR_BGR2RGB);
    } else if (aligned_face.channels() == 4) {
        cv::cvtColor(aligned_face, rgb, cv::COLOR_BGRA2RGB);
    } else {
        cv::cvtColor(aligned_face, rgb, cv::COLOR_GRAY2RGB);
    }

    cv::Mat resized;
    cv::resize(rgb, resized, cv::Size(input_size_, input_size_));
    resized.convertTo(resized, CV_32F);

    std::vector<float> input_tensor_values;
    input_tensor_values.reserve(3 * input_size_ * input_size_);
    for (int c = 0; c < 3; ++c) {
        for (int i = 0; i < input_size_; ++i) {
            for (int j = 0; j < input_size_; ++j) {
                float v = (resized.at<cv::Vec3f>(i, j)[c] - 127.5f) / 127.5f;
                input_tensor_values.push_back(v);
            }
        }
    }

    std::vector<int64_t> input_shape = {1, 3, input_size_, input_size_};
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info_, input_tensor_values.data(),
        input_tensor_values.size(), input_shape.data(), input_shape.size());

    Ort::AllocatorWithDefaultOptions allocator;
    auto in_name_alloc = session_->GetInputNameAllocated(0, allocator);
    const char* input_name = in_name_alloc.get();

    std::vector<Ort::Value> outputs;
    try {
        outputs = session_->Run(Ort::RunOptions{nullptr},
                                &input_name, &input_tensor, 1,
                                output_names_ptr_.data(), output_names_ptr_.size());
    } catch (const Ort::Exception&) {
        return {};
    }

    const float* feat_ptr = outputs[0].GetTensorData<float>();
    auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    int feat_dim = 1;
    for (size_t i = 1; i < shape.size(); ++i) {
        feat_dim *= static_cast<int>(shape[i]);
    }

    std::vector<float> feature(feat_ptr, feat_ptr + feat_dim);

    float norm = 0.0f;
    for (float v : feature) norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 1e-6f) {
        for (float& v : feature) v /= norm;
    }
    return feature;
}