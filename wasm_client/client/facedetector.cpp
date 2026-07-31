#include "facedetector.h"
#include <opencv2/dnn.hpp>
#include <algorithm>
#include <cmath>

FaceDetector::FaceDetector(const std::string& model_path)
    : env_(ORT_LOGGING_LEVEL_WARNING, "FaceDetector"),
      session_options_(),
      memory_info_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)),
      priors_() {
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

    generatePriors();
}

void FaceDetector::generatePriors() {
    const std::vector<std::vector<int>> min_sizes = {{16, 32}, {64, 128}, {256, 512}};
    const std::vector<int> steps = {8, 16, 32};
    priors_.clear();

    for (size_t k = 0; k < steps.size(); ++k) {
        int f = input_w_ / steps[k];
        for (int i = 0; i < f; ++i) {
            for (int j = 0; j < f; ++j) {
                for (int min_s : min_sizes[k]) {
                    float cx = (j + 0.5f) * steps[k] / static_cast<float>(input_w_);
                    float cy = (i + 0.5f) * steps[k] / static_cast<float>(input_h_);
                    float w = min_s / static_cast<float>(input_w_);
                    float h = min_s / static_cast<float>(input_h_);
                    priors_.push_back(cx);
                    priors_.push_back(cy);
                    priors_.push_back(w);
                    priors_.push_back(h);
                }
            }
        }
    }
}

std::vector<FaceInfo> FaceDetector::detect(const cv::Mat& img) {
    int orig_h = img.rows;
    int orig_w = img.cols;

    cv::Mat rgb;
    if (img.channels() == 3) {
        cv::cvtColor(img, rgb, cv::COLOR_BGR2RGB);
    } else if (img.channels() == 4) {
        cv::cvtColor(img, rgb, cv::COLOR_BGRA2RGB);
    } else {
        cv::cvtColor(img, rgb, cv::COLOR_GRAY2RGB);
    }

    cv::Mat resized;
    cv::resize(rgb, resized, cv::Size(input_w_, input_h_));
    resized.convertTo(resized, CV_32F);

    std::vector<float> input_tensor_values;
    input_tensor_values.reserve(3 * input_h_ * input_w_);
    for (int c = 0; c < 3; ++c) {
        for (int i = 0; i < input_h_; ++i) {
            for (int j = 0; j < input_w_; ++j) {
                input_tensor_values.push_back(resized.at<cv::Vec3f>(i, j)[c]);
            }
        }
    }

    std::vector<int64_t> input_shape = {1, 3, input_h_, input_w_};
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

    return parseOutputs(outputs, orig_h, orig_w);
}

std::vector<FaceInfo> FaceDetector::parseOutputs(const std::vector<Ort::Value>& outputs,
                                                  int img_h, int img_w) {
    if (outputs.size() == 3) {
        const float* bbox_ptr = outputs[0].GetTensorData<float>();
        const float* conf_ptr = outputs[1].GetTensorData<float>();
        const float* landm_ptr = outputs[2].GetTensorData<float>();
        auto bbox_shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        int num_priors = static_cast<int>(bbox_shape[1]);
        return decodeThreeOutputs(bbox_ptr, conf_ptr, landm_ptr, num_priors, img_h, img_w);
    } else if (outputs.size() == 1) {
        auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        int num = static_cast<int>(shape[1]);
        int cols = static_cast<int>(shape[2]);
        const float* data = outputs[0].GetTensorData<float>();
        return decodeMergedOutput(data, num, cols, img_h, img_w);
    }
    return {};
}

std::vector<FaceInfo> FaceDetector::decodeThreeOutputs(const float* bbox_ptr,
                                                        const float* conf_ptr,
                                                        const float* landm_ptr,
                                                        int num_priors,
                                                        int img_h, int img_w) {
    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<std::vector<cv::Point2f>> landmarks;

    for (int i = 0; i < num_priors; ++i) {
        float conf = conf_ptr[i * 2 + 1];
        if (conf < conf_thresh_) continue;

        float cx = priors_[i * 4 + 0];
        float cy = priors_[i * 4 + 1];
        float w = priors_[i * 4 + 2];
        float h = priors_[i * 4 + 3];

        float dx = bbox_ptr[i * 4 + 0];
        float dy = bbox_ptr[i * 4 + 1];
        float dw = bbox_ptr[i * 4 + 2];
        float dh = bbox_ptr[i * 4 + 3];

        float pred_cx = cx + dx * 0.1f * w;
        float pred_cy = cy + dy * 0.1f * h;
        float pred_w = w * std::exp(dw * 0.2f);
        float pred_h = h * std::exp(dh * 0.2f);

        float x1 = (pred_cx - pred_w * 0.5f) * img_w;
        float y1 = (pred_cy - pred_h * 0.5f) * img_h;
        float x2 = (pred_cx + pred_w * 0.5f) * img_w;
        float y2 = (pred_cy + pred_h * 0.5f) * img_h;

        boxes.emplace_back(
            static_cast<int>(std::max(0.0f, x1)),
            static_cast<int>(std::max(0.0f, y1)),
            static_cast<int>(std::min<float>(static_cast<float>(img_w), x2) - std::max(0.0f, x1)),
            static_cast<int>(std::min<float>(static_cast<float>(img_h), y2) - std::max(0.0f, y1))
        );
        confidences.push_back(conf);

        std::vector<cv::Point2f> lm;
        for (int p = 0; p < 5; ++p) {
            float lmx = cx + landm_ptr[i * 10 + p * 2 + 0] * 0.1f * w;
            float lmy = cy + landm_ptr[i * 10 + p * 2 + 1] * 0.1f * h;
            lm.emplace_back(lmx * img_w, lmy * img_h);
        }
        landmarks.push_back(std::move(lm));
    }

    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, conf_thresh_, nms_thresh_, indices);

    std::vector<FaceInfo> results;
    for (int idx : indices) {
        FaceInfo fi;
        fi.bbox = boxes[idx];
        fi.conf = confidences[idx];
        fi.landmarks = landmarks[idx];
        results.push_back(std::move(fi));
    }
    return results;
}

std::vector<FaceInfo> FaceDetector::decodeMergedOutput(const float* data,
                                                        int num, int cols,
                                                        int img_h, int img_w) {
    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<std::vector<cv::Point2f>> landmarks;

    for (int i = 0; i < num; ++i) {
        float conf = data[i * cols + 4];
        if (conf < conf_thresh_) continue;

        float cx = priors_[i * 4 + 0];
        float cy = priors_[i * 4 + 1];
        float w = priors_[i * 4 + 2];
        float h = priors_[i * 4 + 3];

        float dx = data[i * cols + 0];
        float dy = data[i * cols + 1];
        float dw = data[i * cols + 2];
        float dh = data[i * cols + 3];

        float pred_cx = cx + dx * 0.1f * w;
        float pred_cy = cy + dy * 0.1f * h;
        float pred_w = w * std::exp(dw * 0.2f);
        float pred_h = h * std::exp(dh * 0.2f);

        float x1 = (pred_cx - pred_w * 0.5f) * img_w;
        float y1 = (pred_cy - pred_h * 0.5f) * img_h;
        float x2 = (pred_cx + pred_w * 0.5f) * img_w;
        float y2 = (pred_cy + pred_h * 0.5f) * img_h;

        boxes.emplace_back(
            static_cast<int>(std::max(0.0f, x1)),
            static_cast<int>(std::max(0.0f, y1)),
            static_cast<int>(std::min<float>(static_cast<float>(img_w), x2) - std::max(0.0f, x1)),
            static_cast<int>(std::min<float>(static_cast<float>(img_h), y2) - std::max(0.0f, y1))
        );
        confidences.push_back(conf);

        std::vector<cv::Point2f> lm;
        for (int p = 0; p < 5; ++p) {
            float lmx = cx + data[i * cols + 6 + p * 2 + 0] * 0.1f * w;
            float lmy = cy + data[i * cols + 6 + p * 2 + 1] * 0.1f * h;
            lm.emplace_back(lmx * img_w, lmy * img_h);
        }
        landmarks.push_back(std::move(lm));
    }

    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, conf_thresh_, nms_thresh_, indices);

    std::vector<FaceInfo> results;
    for (int idx : indices) {
        FaceInfo fi;
        fi.bbox = boxes[idx];
        fi.conf = confidences[idx];
        fi.landmarks = landmarks[idx];
        results.push_back(std::move(fi));
    }
    return results;
}