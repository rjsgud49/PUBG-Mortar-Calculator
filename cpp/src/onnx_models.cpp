#include "onnx_models.hpp"

#include <onnxruntime_c_api.h>

#include <opencv2/imgproc.hpp>

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <vector>

namespace {

const char* kClassNames[] = {
    "yellow_mark",
    "yellow_player_mark",
    "orange_mark",
    "orange_player_mark",
    "blue_mark",
    "blue_player_mark",
    "green_mark",
    "green_player_mark",
};

float iou_xywh(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
    const float ax1 = ax + aw;
    const float ay1 = ay + ah;
    const float bx1 = bx + bw;
    const float by1 = by + bh;
    const float ix0 = std::max(ax, bx);
    const float iy0 = std::max(ay, by);
    const float ix1 = std::min(ax1, bx1);
    const float iy1 = std::min(ay1, by1);
    const float iw = std::max(0.0f, ix1 - ix0);
    const float ih = std::max(0.0f, iy1 - iy0);
    const float inter = iw * ih;
    const float uni = aw * ah + bw * bh - inter;
    if (uni <= 0) {
        return 0;
    }
    return inter / uni;
}

std::vector<int> nms(const std::vector<cv::Vec4f>& boxes, const std::vector<float>& scores, float iou_threshold) {
    std::vector<int> order(scores.size());
    for (size_t i = 0; i < order.size(); ++i) {
        order[i] = static_cast<int>(i);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) { return scores[a] > scores[b]; });
    std::vector<int> kept;
    std::vector<char> removed(order.size(), 0);
    for (size_t i = 0; i < order.size(); ++i) {
        const int index = order[i];
        if (removed[index]) {
            continue;
        }
        kept.push_back(index);
        for (size_t j = i + 1; j < order.size(); ++j) {
            const int other = order[j];
            if (removed[other]) {
                continue;
            }
            if (iou_xywh(boxes[index][0], boxes[index][1], boxes[index][2], boxes[index][3], boxes[other][0], boxes[other][1], boxes[other][2], boxes[other][3]) > iou_threshold) {
                removed[other] = 1;
            }
        }
    }
    return kept;
}

}  // namespace

struct OnnxModels::Session {
    OrtSession* session = nullptr;
    std::string input_name;
    std::string output_name;
    int width = 640;
    int height = 640;
};

OnnxModels::~OnnxModels() {
    if (api_ != nullptr) {
        if (mark_ != nullptr && mark_->session != nullptr) {
            api_->ReleaseSession(mark_->session);
        }
        if (minimap_ != nullptr && minimap_->session != nullptr) {
            api_->ReleaseSession(minimap_->session);
        }
        if (env_ != nullptr) {
            api_->ReleaseEnv(env_);
        }
    }
    delete mark_;
    delete minimap_;
    if (dll_ != nullptr) {
        FreeLibrary(static_cast<HMODULE>(dll_));
    }
}

bool OnnxModels::has_mark() const {
    return mark_ != nullptr && mark_->session != nullptr;
}

bool OnnxModels::has_minimap() const {
    return minimap_ != nullptr && minimap_->session != nullptr;
}

const std::string& OnnxModels::last_error() const {
    return last_error_;
}

void OnnxModels::load(const std::wstring& dll_path, const std::wstring& mark_path, const std::wstring& minimap_path) {
    dll_ = LoadLibraryW(dll_path.c_str());
    if (dll_ == nullptr) {
        last_error_ = "onnxruntime.dll";
        return;
    }
    using GetApiBaseFn = const OrtApiBase*(ORT_API_CALL*)();
    GetApiBaseFn get_api_base = nullptr;
    FARPROC proc = GetProcAddress(static_cast<HMODULE>(dll_), "OrtGetApiBase");
    std::memcpy(&get_api_base, &proc, sizeof(proc));
    if (get_api_base == nullptr) {
        last_error_ = "OrtGetApiBase";
        return;
    }
    const OrtApiBase* base = get_api_base();
    api_ = base == nullptr ? nullptr : base->GetApi(ORT_API_VERSION);
    if (api_ == nullptr) {
        last_error_ = "OrtApi";
        return;
    }
    OrtStatus* status = api_->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "pubg-mortar", &env_);
    if (status != nullptr) {
        last_error_ = api_->GetErrorMessage(status);
        api_->ReleaseStatus(status);
        return;
    }
    mark_ = new Session();
    if (!open_session(*mark_, mark_path, 640)) {
        delete mark_;
        mark_ = nullptr;
    }
    minimap_ = new Session();
    if (!open_session(*minimap_, minimap_path, 224)) {
        delete minimap_;
        minimap_ = nullptr;
    }
}

bool OnnxModels::open_session(Session& session, const std::wstring& model_path, int fallback_size) {
    if (GetFileAttributesW(model_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    OrtSessionOptions* options = nullptr;
    OrtStatus* status = api_->CreateSessionOptions(&options);
    if (status != nullptr) {
        last_error_ = api_->GetErrorMessage(status);
        api_->ReleaseStatus(status);
        return false;
    }
    status = api_->CreateSession(env_, model_path.c_str(), options, &session.session);
    api_->ReleaseSessionOptions(options);
    if (status != nullptr) {
        last_error_ = api_->GetErrorMessage(status);
        api_->ReleaseStatus(status);
        session.session = nullptr;
        return false;
    }

    OrtAllocator* allocator = nullptr;
    api_->GetAllocatorWithDefaultOptions(&allocator);
    char* input_name = nullptr;
    char* output_name = nullptr;
    api_->SessionGetInputName(session.session, 0, allocator, &input_name);
    api_->SessionGetOutputName(session.session, 0, allocator, &output_name);
    if (input_name != nullptr) {
        session.input_name = input_name;
        api_->AllocatorFree(allocator, input_name);
    }
    if (output_name != nullptr) {
        session.output_name = output_name;
        api_->AllocatorFree(allocator, output_name);
    }

    OrtTypeInfo* type_info = nullptr;
    if (api_->SessionGetInputTypeInfo(session.session, 0, &type_info) == nullptr && type_info != nullptr) {
        const OrtTensorTypeAndShapeInfo* tensor = nullptr;
        api_->CastTypeInfoToTensorInfo(type_info, &tensor);
        if (tensor != nullptr) {
            size_t count = 0;
            api_->GetDimensionsCount(tensor, &count);
            std::vector<int64_t> dims(count);
            if (count > 0) {
                api_->GetDimensions(tensor, dims.data(), count);
            }
            if (dims.size() >= 2) {
                if (dims[dims.size() - 2] > 0) {
                    session.height = static_cast<int>(dims[dims.size() - 2]);
                }
                if (dims[dims.size() - 1] > 0) {
                    session.width = static_cast<int>(dims[dims.size() - 1]);
                }
            }
        }
        api_->ReleaseTypeInfo(type_info);
    }
    if (session.width <= 0) {
        session.width = fallback_size;
    }
    if (session.height <= 0) {
        session.height = fallback_size;
    }
    return true;
}

std::vector<float> OnnxModels::run(
    Session& session,
    const std::vector<float>& input,
    const std::vector<int64_t>& shape,
    std::vector<int64_t>& out_shape
) {
    out_shape.clear();
    if (api_ == nullptr || session.session == nullptr || input.empty()) {
        return {};
    }
    OrtMemoryInfo* memory = nullptr;
    OrtStatus* status = api_->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory);
    if (status != nullptr) {
        api_->ReleaseStatus(status);
        return {};
    }
    OrtValue* input_value = nullptr;
    status = api_->CreateTensorWithDataAsOrtValue(
        memory,
        const_cast<float*>(input.data()),
        input.size() * sizeof(float),
        shape.data(),
        shape.size(),
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        &input_value
    );
    api_->ReleaseMemoryInfo(memory);
    if (status != nullptr) {
        api_->ReleaseStatus(status);
        return {};
    }

    const char* input_names[] = {session.input_name.c_str()};
    const char* output_names[] = {session.output_name.c_str()};
    const OrtValue* inputs[] = {input_value};
    OrtValue* output = nullptr;
    status = api_->Run(session.session, nullptr, input_names, inputs, 1, output_names, 1, &output);
    api_->ReleaseValue(input_value);
    if (status != nullptr || output == nullptr) {
        if (status != nullptr) {
            api_->ReleaseStatus(status);
        }
        return {};
    }

    OrtTensorTypeAndShapeInfo* info = nullptr;
    api_->GetTensorTypeAndShape(output, &info);
    size_t count = 0;
    size_t dims = 0;
    if (info != nullptr) {
        api_->GetTensorShapeElementCount(info, &count);
        api_->GetDimensionsCount(info, &dims);
        out_shape.resize(dims);
        if (dims > 0) {
            api_->GetDimensions(info, out_shape.data(), dims);
        }
        api_->ReleaseTensorTypeAndShapeInfo(info);
    }
    float* data = nullptr;
    api_->GetTensorMutableData(output, reinterpret_cast<void**>(&data));
    std::vector<float> values;
    if (data != nullptr && count > 0) {
        values.assign(data, data + count);
    }
    api_->ReleaseValue(output);
    return values;
}

std::vector<YoloBox> OnnxModels::detect(const cv::Mat& bgr) {
    if (!has_mark() || bgr.empty()) {
        return {};
    }
    const int target_w = mark_->width;
    const int target_h = mark_->height;
    const float scale = std::min(static_cast<float>(target_w) / bgr.cols, static_cast<float>(target_h) / bgr.rows);
    const int new_w = std::max(1, static_cast<int>(std::lround(bgr.cols * scale)));
    const int new_h = std::max(1, static_cast<int>(std::lround(bgr.rows * scale)));
    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);
    cv::Mat padded(target_h, target_w, CV_8UC3, cv::Scalar(114, 114, 114));
    const int left = (target_w - new_w) / 2;
    const int top = (target_h - new_h) / 2;
    resized.copyTo(padded(cv::Rect(left, top, new_w, new_h)));

    cv::Mat rgb;
    cv::cvtColor(padded, rgb, cv::COLOR_BGR2RGB);
    std::vector<float> input(static_cast<size_t>(3 * target_w * target_h));
    for (int y = 0; y < target_h; ++y) {
        const cv::Vec3b* row = rgb.ptr<cv::Vec3b>(y);
        for (int x = 0; x < target_w; ++x) {
            const size_t pixel = static_cast<size_t>(y * target_w + x);
            input[pixel] = row[x][0] / 255.0f;
            input[static_cast<size_t>(target_w * target_h) + pixel] = row[x][1] / 255.0f;
            input[static_cast<size_t>(2 * target_w * target_h) + pixel] = row[x][2] / 255.0f;
        }
    }

    std::vector<int64_t> out_shape;
    const std::vector<float> output = run(*mark_, input, {1, 3, target_h, target_w}, out_shape);
    while (out_shape.size() > 2 && out_shape.front() == 1) {
        out_shape.erase(out_shape.begin());
    }
    if (output.empty() || out_shape.size() != 2) {
        return {};
    }

    int rows = static_cast<int>(out_shape[0]);
    int cols = static_cast<int>(out_shape[1]);
    bool transposed = false;
    if (rows < cols) {
        std::swap(rows, cols);
        transposed = true;
    }
    auto at = [&](int row, int col) {
        if (transposed) {
            return output[static_cast<size_t>(col) * rows + row];
        }
        return output[static_cast<size_t>(row) * cols + col];
    };
    if (rows <= 0 || cols < 5) {
        return {};
    }

    constexpr float kConfidence = 0.1f;
    std::vector<cv::Vec4f> boxes;
    std::vector<float> scores;
    std::vector<int> classes;
    bool xyxy = false;
    if (cols == 6) {
        float max_conf = 0;
        for (int row = 0; row < rows; ++row) {
            max_conf = std::max(max_conf, at(row, 4));
        }
        if (max_conf <= 1.0f) {
            xyxy = true;
            for (int row = 0; row < rows; ++row) {
                const float conf = at(row, 4);
                if (conf <= kConfidence) {
                    continue;
                }
                boxes.emplace_back(at(row, 0), at(row, 1), at(row, 2) - at(row, 0), at(row, 3) - at(row, 1));
                scores.push_back(conf);
                classes.push_back(static_cast<int>(at(row, 5)));
            }
        }
    }
    if (!xyxy && cols > 4) {
        float min_score = at(0, 4);
        float max_score = at(0, 4);
        for (int row = 0; row < rows; ++row) {
            for (int col = 4; col < cols; ++col) {
                min_score = std::min(min_score, at(row, col));
                max_score = std::max(max_score, at(row, col));
            }
        }
        const bool logits = max_score > 1.0f || min_score < 0.0f;
        for (int row = 0; row < rows; ++row) {
            int best = 4;
            float best_score = logits ? (1.0f / (1.0f + std::exp(-at(row, 4)))) : at(row, 4);
            for (int col = 5; col < cols; ++col) {
                float score = at(row, col);
                if (logits) {
                    score = 1.0f / (1.0f + std::exp(-score));
                }
                if (score > best_score) {
                    best_score = score;
                    best = col;
                }
            }
            if (best_score <= kConfidence) {
                continue;
            }
            const float cx = at(row, 0);
            const float cy = at(row, 1);
            const float w = at(row, 2);
            const float h = at(row, 3);
            boxes.emplace_back(cx - w / 2.0f, cy - h / 2.0f, w, h);
            scores.push_back(best_score);
            classes.push_back(best - 4);
        }
    }
    if (boxes.empty()) {
        return {};
    }

    std::vector<YoloBox> detections;
    for (int index : nms(boxes, scores, kConfidence)) {
        const float x = boxes[index][0];
        const float y = boxes[index][1];
        const float w = boxes[index][2];
        const float h = boxes[index][3];
        const float x0 = std::clamp((x - left) / scale, 0.0f, static_cast<float>(bgr.cols));
        const float y0 = std::clamp((y - top) / scale, 0.0f, static_cast<float>(bgr.rows));
        const float x1 = std::clamp((x + w - left) / scale, 0.0f, static_cast<float>(bgr.cols));
        const float y1 = std::clamp((y + h - top) / scale, 0.0f, static_cast<float>(bgr.rows));
        YoloBox box;
        box.x0 = static_cast<int>(x0);
        box.y0 = static_cast<int>(y0);
        box.x1 = static_cast<int>(x1);
        box.y1 = static_cast<int>(y1);
        box.class_id = classes[index];
        box.confidence = scores[index];
        if (box.class_id >= 0 && box.class_id < static_cast<int>(sizeof(kClassNames) / sizeof(kClassNames[0]))) {
            box.name = kClassNames[box.class_id];
        }
        detections.push_back(box);
    }
    return detections;
}

int OnnxModels::classify_minimap(const cv::Mat& bgr) {
    if (!has_minimap() || bgr.empty()) {
        return 0;
    }
    const int side = std::max(bgr.cols, bgr.rows);
    cv::Mat square(side, side, CV_8UC3, cv::Scalar(0, 0, 0));
    const int pad_left = (side - bgr.cols) / 2;
    const int pad_top = (side - bgr.rows) / 2;
    bgr.copyTo(square(cv::Rect(pad_left, pad_top, bgr.cols, bgr.rows)));
    cv::Mat sized;
    cv::resize(square, sized, cv::Size(224, 224), 0, 0, cv::INTER_AREA);
    cv::Mat rgb;
    cv::cvtColor(sized, rgb, cv::COLOR_BGR2RGB);

    constexpr float kMean[3] = {0.485f, 0.456f, 0.406f};
    constexpr float kStd[3] = {0.229f, 0.224f, 0.225f};
    std::vector<float> input(3 * 224 * 224);
    for (int y = 0; y < 224; ++y) {
        const cv::Vec3b* row = rgb.ptr<cv::Vec3b>(y);
        for (int x = 0; x < 224; ++x) {
            const size_t pixel = static_cast<size_t>(y * 224 + x);
            for (int channel = 0; channel < 3; ++channel) {
                input[static_cast<size_t>(channel) * 224 * 224 + pixel] = (row[x][channel] / 255.0f - kMean[channel]) / kStd[channel];
            }
        }
    }
    std::vector<int64_t> out_shape;
    const std::vector<float> logits = run(*minimap_, input, {1, 3, 224, 224}, out_shape);
    if (logits.empty()) {
        return 0;
    }
    float max_logit = *std::max_element(logits.begin(), logits.end());
    float sum = 0;
    std::vector<float> probs(logits.size());
    for (size_t i = 0; i < logits.size(); ++i) {
        probs[i] = std::exp(logits[i] - max_logit);
        sum += probs[i];
    }
    if (sum <= 0) {
        return 0;
    }
    const auto best = std::max_element(probs.begin(), probs.end());
    return static_cast<int>(std::distance(probs.begin(), best));
}
